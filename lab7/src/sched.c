#include "sched.h"
#include "mm.h"
#include "list.h"
#include "printk.h"
#include "string.h"
#include "signal.h"
#include "uart.h"

extern unsigned long pg_dir[512];

struct list_head runqueue;   
struct task_struct *current;     
int pid_counter = 1;            

// Defined in entry.S
// extern void switch_to(struct thread_struct *prev, struct thread_struct *next);
extern void switch_to(struct thread_struct *prev, struct thread_struct *next, unsigned long satp);

extern void ret_from_exception(void);
// ====================================================================
// Scheduler Core
// ====================================================================

struct task_struct *get_current() {
    return current;
}

int num_runnable_tasks() {
    struct list_head *pos;
    struct task_struct *t;
    int count = 0;

    list_for_each(pos, &runqueue) {
        t = list_entry(pos, struct task_struct, list);
        // Count only tasks that are actually able to run
        if (t->state == TASK_RUNNING || t->state == TASK_READY) {
            count++;
        }
    }
    return count;
}

void sched_init() {
    INIT_LIST_HEAD(&runqueue);

    // 1. Manually create the 'Idle' task (PID 0)
    // This represents the code currently running (boot/kernel_main).
    // We need this so the first schedule() has somewhere to save the old context.
    struct task_struct *idle = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    memset(idle, 0, sizeof(struct task_struct));
    
    idle->pid = 0;
    idle->state = TASK_RUNNING;
    idle->priority = 0;
    idle->counter = 0;
    idle->mm.pgd = 0;
    idle->signal_stack_va = 0;
    idle->signal_stack_phys = 0;
    idle->cwd[0] = '/'; idle->cwd[1] = '\0';
    // We don't need to set idle->thread.sp/ra because switch_to 
    // will overwrite them with the actual CPU state when we switch OUT.
    
    current = idle;
    
    // Add idle to runqueue so the loop is circular
    list_add_tail(&idle->list, &runqueue);
    
    uart_puts("Scheduler Initialized. Current PID: ");
    uart_hex(current->pid);
    uart_puts("\n");
}

void schedule() {
    struct task_struct *prev = current;
    struct task_struct *next = NULL;

    // 1. Round Robin: Find next READY task
    struct list_head *ptr = prev->list.next;

    // Loop until we find a runnable task or come back to start
    while (ptr != &prev->list) {
        // Skip the list head (runqueue sentinel)
        if (ptr == &runqueue) {
            ptr = ptr->next;
            continue;
        }

        struct task_struct *t = list_entry(ptr, struct task_struct, list);

        // Found a candidate?
        if (t->state == TASK_READY || t->state == TASK_RUNNING) {
            next = t;
            break;
        }

        ptr = ptr->next;
    }

    // 2. If no new task found, just return (keep running current)
    if (!next || next == prev) {
        return;
    }

    // Debug
    // if (prev->pid != next->pid) {
        // printk("[Sched] Switch: %d -> %d\n", prev->pid, next->pid);
    // }
    // 3. Update States
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    next->state = TASK_RUNNING;
    current = next;

    // 4. Context Switch
    // This saves 'prev' registers and loads 'next' registers.
    // When this returns, we will be in the context of 'next'.
    unsigned long satp_val;
    if (next->mm.pgd) {
        unsigned long pgd_pa = virt_to_phys((unsigned long)next->mm.pgd);
        satp_val = SATP_MODE_SV39 | (pgd_pa >> 12);
    } else {
        unsigned long pgd_pa = virt_to_phys((unsigned long)pg_dir);
        satp_val = SATP_MODE_SV39 | (pgd_pa >> 12);
    }
    switch_to(&prev->thread, &next->thread, satp_val);
}

// ====================================================================
// Thread Management
// ====================================================================

struct task_struct *thread_create(void (*start_routine)(void *), void *arg) {
    struct task_struct *p = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    if (!p) return NULL;
    memset(p, 0, sizeof(struct task_struct));

    // [FIX] Use 'kernel_stack' to match struct definition in sched.h
    p->kernel_stack = (uint64_t)kmalloc(0x1000);
    if (!p->kernel_stack) {
        kfree(p);
        return NULL;
    }

    p->pid = pid_counter++;
    p->state = TASK_READY;
    p->priority = 1;
    p->counter = 0;
    p->pending_signals = 0;
    p->in_signal_handler = 0;
    p->signal_stack_va = 0;
    p->signal_stack_phys = 0;
    p->mm.pgd = 0; // No user page table for kernel threads
    for (int i = 0; i < MAX_SIG; i++) {
        p->signal_handler[i] = 0;
    }
    // [FIX] Use 'kernel_stack' here too
    p->thread.sp = p->kernel_stack + 0x1000;
    
    p->thread.ra = (uint64_t)start_routine;

    // Handle unused arg warning (optional)
    (void)arg; 

    list_add_tail(&p->list, &runqueue);

    return p;
}

void kthread_exit() {
    current->state = TASK_ZOMBIE;
    schedule();
    while(1);
}

void kill_zombies() {
    struct list_head *pos, *tmp;
    struct task_struct *task;

    // Use _safe because we are deleting nodes while iterating
    list_for_each_safe(pos, tmp, &runqueue) {
        task = list_entry(pos, struct task_struct, list);
        
        if (task->state == TASK_ZOMBIE) {
            // 1. Remove from Runqueue
            list_del(&task->list); 

            // 2. Free Memory (Uncommented!)
            // Note: Verify your struct member names match exactly (kernel_stack vs stack)
            if (task->mm.pgd) {
                release_signal_stack(task);
                kfree((void *)task->mm.pgd);
            }
            kfree((void *)task->kernel_stack); 
            kfree((void *)task->user_stack); // If you allocated user stack, free it too
            kfree(task);
            
            // printk("[Zombie] Killed PID %d\n", task->pid);
        }
    }
}

void idle() {
    while (1) {
        // 1. Clean up any dead threads
        kill_zombies();

        // 2. Yield CPU to let real work happen
        schedule();
        
        // Optional: If implementing a breakable demo
        if (num_runnable_tasks() == 1) break; 
    }
}

// [Helper] Allocates a new task and its stacks. Returns NULL on failure.
struct task_struct *task_alloc() {
    struct task_struct *p = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    if (!p) return NULL;
    memset(p, 0, sizeof(struct task_struct));

    p->kernel_stack = (uint64_t)kmalloc(PAGE_SIZE);
    p->user_stack   = (uint64_t)kmalloc(PAGE_SIZE);
    p->mm.pgd = 0;
    p->signal_stack_va = 0;
    p->signal_stack_phys = 0;
    p->cwd[0] = '/'; p->cwd[1] = '\0';

    if (!p->kernel_stack || !p->user_stack) {
        if (p->kernel_stack) kfree((void*)p->kernel_stack);
        if (p->user_stack)   kfree((void*)p->user_stack);
        kfree(p);
        return NULL;
    }
    return p;
}


struct task_struct *find_task_by_pid(int pid) {
    struct list_head *pos;
    list_for_each(pos, &runqueue) {
        struct task_struct *t = list_entry(pos, struct task_struct, list);
        if (t->pid == pid) return t;
    }
    return NULL; // Process not found
}
