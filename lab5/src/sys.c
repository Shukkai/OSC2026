#include "sys.h"
#include "sched.h"
#include "mm.h"
#include "string.h"
// extern void switch_to(struct thread_struct *prev, struct thread_struct *next);
extern void ret_from_exception(void);
long call_sys(long num, long a0) {
    long ret;
    asm volatile(
        "mv a7, %1\n\t"
        "mv a0, %2\n\t"
        "ecall\n\t"
        "mv %0, a0"
        : "=r"(ret)
        : "r"(num), "r"(a0)
        : "a0", "a7", "memory"
    );
    return ret;
}

int getpid() {
    return call_sys(0, 0); // SYS_GETPID = 0
}

int fork() {
    return call_sys(4, 0); // SYS_FORK = 4
}

void exit(int status) {
    call_sys(5, status);   // SYS_EXIT = 5
    while(1);
}

int do_fork() {
    // 1. Allocate new task struct
    struct task_struct *p = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    if (!p) return -1;

    // 2. Allocate Stacks (Physical pages)
    // Kernel Stack: Used for trap handling / context switching
    p->kernel_stack = (uint64_t)kmalloc(PAGE_SIZE);
    // User Stack: Used for the actual program execution
    p->user_stack   = (uint64_t)kmalloc(PAGE_SIZE);

    if (!p->kernel_stack || !p->user_stack) {
        // Simple error handling: free what we alloc'd and fail
        if (p->kernel_stack) kfree((void*)p->kernel_stack);
        if (p) kfree(p);
        return -1;
    }

    // 3. Clone Task Properties
    p->pid = pid_counter++;
    p->state = TASK_READY;
    p->priority = current->priority;
    p->counter = 0;
    p->preempt_count = 0;

    // 4. COPY USER STACK (Deep Copy)
    // This ensures child has its own stack data (variables, return addresses)
    memcpy((void *)p->user_stack, (void *)current->user_stack, PAGE_SIZE);

    // 5. SETUP TRAPFRAME
    // The TrapFrame is stored at the top of the Kernel Stack
    p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    
    // Copy the Parent's TrapFrame state (registers at the moment of syscall)
    *p->tf = *current->tf;

    // 6. ADJUST CHILD TRAPFRAME
    // A. Return value 0 for child
    p->tf->a0 = 0; 
    
    // B. Relocate User Stack Pointer
    // Calculate offset of current SP relative to the stack base
    unsigned long sp_offset = current->tf->sp - current->user_stack;
    // Apply that offset to the new stack base
    p->tf->sp = p->user_stack + sp_offset;
    
    // C. Frame Pointer Adjustment (Critical if not using -fomit-frame-pointer)
    // If your compiler uses s0/fp, it might still point to parent stack.
    // If you use -fomit-frame-pointer, we assume standard SP-relative addressing.
    unsigned long fp_offset = current->tf->s0 - current->user_stack;
    p->tf->s0 = p->user_stack + fp_offset;


    // 7. SETUP KERNEL CONTEXT (for switch_to)
    // When the scheduler switches to this task, it starts at ret_from_exception
    p->thread.ra = (uint64_t)ret_from_exception;
    p->thread.sp = (uint64_t)p->tf; // Point SP to the TrapFrame

    // 8. Add to Runqueue
    list_add_tail(&p->list, &runqueue);

    // Return Child PID to Parent
    return p->pid;
}

void do_exit(int status) {
    // 1. Save the exit status so parent can read it later (if wait() is implemented)
    current->exit_code = status;
    
    // Debug print to verify it works (optional)
    // printk("PID %d exited with code %d\n", current->pid, status);

    // 2. Mark as ZOMBIE
    // The memory (stacks/task_struct) will be freed by the idle task later,
    // but the exit_code remains valid until then.
    current->state = TASK_ZOMBIE;
    
    // 3. Yield CPU forever
    schedule();
    while (1); 
}