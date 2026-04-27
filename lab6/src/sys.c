#include "sys.h"
#include "sched.h"
#include "mm.h"
#include "string.h"
#include "uart.h"
#include "initrd.h"
#include "trap.h"
#include "timer.h"
#include"fb.h"
#include "vm.h"

extern void ret_from_exception(void);
extern unsigned long pg_dir[512];

/* ========================================================================= */
/* USER-SPACE SYSCALL WRAPPERS                                               */
/* ========================================================================= */

// Existing 1-argument syscall helper
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

// [NEW] 2-argument syscall helper (needed for buffer + size)
long call_sys_2(long num, long a0, long a1) {
    long ret;
    asm volatile(
        "mv a7, %1\n\t"
        "mv a0, %2\n\t"
        "mv a1, %3\n\t" // Load the second argument into a1
        "ecall\n\t"
        "mv %0, a0"
        : "=r"(ret)
        : "r"(num), "r"(a0), "r"(a1)
        : "a0", "a1", "a7", "memory"
    );
    return ret;
}

// [NEW] 3-argument syscall helper (needed for display)
long call_sys_3(long num, long a0, long a1, long a2) {
    long ret;
    asm volatile(
        "mv a7, %1\n\t"
        "mv a0, %2\n\t"
        "mv a1, %3\n\t" 
        "mv a2, %4\n\t" // Load the third argument into a2
        "ecall\n\t"
        "mv %0, a0"
        : "=r"(ret)
        : "r"(num), "r"(a0), "r"(a1), "r"(a2)
        : "a0", "a1", "a2", "a7", "memory"
    );
    return ret;
}

// [NEW] 4-argument syscall helper (needed for mmap)
long call_sys_4(long num, long a0, long a1, long a2, long a3) {
    long ret;
    asm volatile(
        "mv a7, %1\n\t"
        "mv a0, %2\n\t"
        "mv a1, %3\n\t" 
        "mv a2, %4\n\t" 
        "mv a3, %5\n\t" 
        "ecall\n\t"
        "mv %0, a0"
        : "=r"(ret)
        : "r"(num), "r"(a0), "r"(a1), "r"(a2), "r"(a3)
        : "a0", "a1", "a2", "a3", "a7", "memory"
    );
    return ret;
}


int getpid() {
    return (int)call_sys(SYS_GETPID, 0);
}

int fork() {
    return (int)call_sys(SYS_FORK, 0);
}

long waitpid(long pid) {
    return call_sys(SYS_WAITPID, pid);
}

void exit(int status) {
    call_sys(SYS_EXIT, (long)status);
    while(1); 
}

int uart_read(char *buf, int size) {
    return (int)call_sys_2(SYS_UART_READ, (long)buf, (long)size);
}

int uart_write(const char *buf, int size) {
    return (int)call_sys_2(SYS_UART_WRITE, (long)buf, (long)size);
}

int exec(const char *path) {
    return (int)call_sys(SYS_EXEC, (long)path);
}

int usleep(unsigned int usec) {
    return (int)call_sys(SYS_USLEEP, (long)usec);
}

void display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    call_sys_3(SYS_DISPLAY, (long)bmp_image, (long)width, (long)height);
}

long signal(int signum, void (*handler)(void)) {
    return call_sys_2(SYS_SIGNAL, (long)signum, (long)handler);
}

long sigreturn(void) {
    return call_sys(SYS_SIGRETURN, 0);
}

int stop(int pid) {
    // SYS_KILL is the immediate/force-terminate command (7)
    return (int)call_sys(SYS_KILL, (long)pid);
}

int kill(int pid, int sig) {
    // SYS_SIG_KILL is the polite signal-delivery command (12)
    return (int)call_sys_2(SYS_SIG_KILL, (long)pid, (long)sig);
}

void *mmap(void *addr, unsigned long length, int prot, int flags) {
    return (void *)call_sys_4(SYS_MMAP, (long)addr, length, (long)prot, (long)flags);
}
/* ========================================================================= */
/* KERNEL-SPACE SYSCALL IMPLEMENTATIONS (do_*)                               */
/* ========================================================================= */

long do_uart_read(char *buf, unsigned long size) {
    if (size == 0) return 0;
    
    for (unsigned long i = 0; i < size; i++) {
        char c;
        
        // Keep checking both the Software Buffer AND the Hardware Register.
        // If both are empty, politely hand the CPU to the video player!
        while (!uart_getc_nonblocking(&c)) {
            schedule(); 
        }
        
        buf[i] = c; 
    }
    return size;
}

// =========================================================================
// UART WRITE (Safe Buffered Writer)
// =========================================================================
// long do_uart_write(const char *buf, unsigned long size) {
//     if (size == 0) return 0;

//     for (unsigned long i = 0; i < size; i++) {
//         // Safely use the interrupt-driven TX buffer for fast printing
//         uart_putc_buffered(buf[i]);
//     }
//     return size;
// }

long do_uart_write(const char *buf, unsigned long size) {
    if (size == 0) return 0;
    for (unsigned long i = 0; i < size; i++) {
        char c = buf[i];
        if (c == '\n') uart_putc('\r');
        uart_putc(c);
    }
    return size;
}

int do_fork() {
    disable_interrupt();

    struct task_struct *p = task_alloc();
    if (!p) { enable_interrupt(); return -1; }

    p->pid = pid_counter++;
    p->state = TASK_READY;
    p->priority = current->priority;
    p->counter = 0;
    p->preempt_count = 0;

    // [FIX] CoW means the child shares the parent's physical stack!
    // We must free the eagerly allocated user_stack from task_alloc() 
    // to prevent memory leaks.
    if (p->user_stack) {
        kfree((void *)p->user_stack);
        p->user_stack = 0;
    }

    // 1. Setup child PGD (Page Directory)
    unsigned long *child_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
    if (!child_pgd) { /* Handle error */ enable_interrupt(); return -1; }
    
    for (int i = 0; i < 512; i++) child_pgd[i] = 0;
    for (int i = 256; i < 512; i++) child_pgd[i] = pg_dir[i]; // Copy Kernel Space
    
    p->mm.pgd = child_pgd;

    // 2. [THE CoW MAGIC]: Copy VMAs and map physical pages as Read-Only!
    clone_vmas_and_page_tables(current, p);

    // 3. Setup TrapFrame (Deep Copy from Parent)
    p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    *p->tf = *current->tf;
    p->tf->a0 = 0; // Child returns 0 in a0 register

    // 4. Inherit Signal Handlers
    p->pending_signals = 0;
    p->in_signal_handler = 0;
    p->signal_stack_va = 0;
    p->signal_stack_phys = 0;
    for (int i = 0; i < MAX_SIG; i++) {
        p->signal_handler[i] = current->signal_handler[i];
    }

    // 5. Setup Kernel Context for switch_to
    p->thread.ra = (uint64_t)ret_from_exception;
    p->thread.sp = (uint64_t)p->tf; 

    list_add_tail(&p->list, &runqueue);
    enable_interrupt();

    return p->pid; 
}

void do_exit(int status) {
    // uart_puts("[exit] pid="); uart_hex(current->pid); uart_puts("\n");
    current->exit_code = status;
    kthread_exit();
}

int do_exec(const char *path, struct TrapFrame *tf) {
    unsigned long filesize;
    void *content_start = initrd_find_file(path, &filesize);

    if (!content_start) {
        return -1; 
    }

    // --- 1. Load Program Code (Still Eager) ---
    unsigned long prog_pages = (filesize + PAGE_SIZE - 1) / PAGE_SIZE;
    if (prog_pages == 0) prog_pages = 1;

    char *prog_buf = (char *)kmalloc(prog_pages * PAGE_SIZE);
    if (!prog_buf) return -1;

    char *src = (char *)content_start;
    for (unsigned long i = 0; i < filesize; i++) {
        prog_buf[i] = src[i];
    }

    // --- 2. Clean up old memory tracking ---
    release_signal_stack(current);

    struct list_head *pos, *n;
    list_for_each_safe(pos, n, &current->mm.mmap_list) {
        struct vm_area_struct *vma = list_entry(pos, struct vm_area_struct, list);
        list_del(pos);
        kfree(vma);
    }
    INIT_LIST_HEAD(&current->mm.mmap_list);

    // Free old pgd
    if (current->mm.pgd) {
        kfree((void *)current->mm.pgd);
    }

    // Create new user page table
    unsigned long *user_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
    for (int i = 0; i < 512; i++) user_pgd[i] = 0;
    for (int i = 256; i < 512; i++) user_pgd[i] = pg_dir[i];

    current->mm.pgd = user_pgd;

    // --- 3. Map Text Segment (Eager) ---
    unsigned long prog_pa = virt_to_phys((unsigned long)prog_buf);
    
    // [FIX 1]: Added PAGE_WRITE so the program can modify its .data segment
    unsigned long text_flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
    map_pages(user_pgd, 0x0, prog_pa, prog_pages * PAGE_SIZE, text_flags);

    // BUMP THE REF COUNT FOR THE ALLOCATED PROGRAM CODE!
    for (unsigned long i = 0; i < prog_pages; i++) {
        inc_page_ref(prog_pa + (i * PAGE_SIZE));
    }

    // ==========================================================
    // --- 4. CREATE THE TEXT VMA WITH BSS PADDING ---
    // ==========================================================
    // [FIX 2]: Create the Text VMA so CoW and Demand Paging know about the code and globals!
    struct vm_area_struct *text_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
    if (text_vma) {
        text_vma->vm_start = 0x0;
        text_vma->vm_end   = (prog_pages * PAGE_SIZE) + (4 * PAGE_SIZE); // Padding for .bss
        text_vma->vm_mm    = &current->mm;
        text_vma->vm_flags = VM_READ | VM_WRITE | VM_EXEC; 
        text_vma->vm_file  = 0;
        
        list_add_tail(&text_vma->list, &current->mm.mmap_list);
    }

    // ==========================================================
    // --- 5. CREATE THE STACK VMA (Lazy Promise) ---
    // ==========================================================
    struct vm_area_struct *stack_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
    if (stack_vma) {
        stack_vma->vm_start = 0x3fffffc000UL;
        stack_vma->vm_end   = 0x4000000000UL;
        stack_vma->vm_mm    = &current->mm;
        stack_vma->vm_flags = VM_READ | VM_WRITE | VM_EXEC;
        stack_vma->vm_file  = 0;
        
        list_add_tail(&stack_vma->list, &current->mm.mmap_list);
    }

    // --- 6. Context Switch ---
    tf->sepc = 0x0;
    tf->sp = 0x4000000000UL;

    unsigned long satp_val = SATP_MODE_SV39 | (virt_to_phys((unsigned long)user_pgd) >> 12);
    asm volatile("csrw satp, %0\n\tsfence.vma zero, zero" :: "r"(satp_val) : "memory");
    asm volatile("fence.i");

    return 0;
}
void do_usleep(unsigned int usec) {
    kernel_usleep(usec); // Call the yielding sleep function from timer.c
}

void do_display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    // Pass the request off to the actual hardware driver!
    fb_draw(bmp_image, width, height); 
}

long do_signal(int sig, void (*handler)(void)) {
    if (sig < 0 || sig >= MAX_SIG) return -1;
    void (*old_handler)(void) = current->signal_handler[sig];
    current->signal_handler[sig] = handler;
    return (long)old_handler;
}

long do_kill(int pid, int sig) {
    if (sig < 0 || sig >= MAX_SIG) return -1;
    
    struct task_struct *t = find_task_by_pid(pid);
    if (!t) return -1; // PID doesn't exist
    
    // Set the bit to indicate this signal is pending!
    t->pending_signals |= (1 << sig);
    return 0;
}

long do_sigreturn(struct TrapFrame *tf) {
    release_signal_stack(current);

    // Restore the exact CPU state from before the signal fired!
    *tf = current->saved_tf;
    
    return tf->a0; 
}

long do_mmap(unsigned long addr, unsigned long len, unsigned long prot, unsigned long flags) {
    
    // 1. Align length to PAGE_SIZE
    unsigned long aligned_len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // [THE FIX]: Ignore dangerous address hints!
    // If the user asks for a low address (like 0x1000) where our program code lives,
    // force it to 0 so we find a safe space instead.
    if (addr < MMAP_BASE) {
        addr = 0;
    }
    // 2. If addr is 0, find a free space
    if (addr == 0) {
        addr = MMAP_BASE;
        struct list_head *pos;
        list_for_each(pos, &current->mm.mmap_list) {
            struct vm_area_struct *vma = list_entry(pos, struct vm_area_struct, list);
            if (addr >= vma->vm_start && addr < vma->vm_end) {
                addr = (vma->vm_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            }
        }
    }

    // 3. Create the VMA promise
    struct vm_area_struct *new_vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct));
    if (!new_vma) return -1;

    new_vma->vm_start = addr;
    new_vma->vm_end   = addr + aligned_len;
    new_vma->vm_mm    = &current->mm;
    
    // 4. Map the POSIX PROT_* flags to your internal VM_* flags
    new_vma->vm_flags = VM_NONE;
    if (prot & PROT_READ)  new_vma->vm_flags |= VM_READ;
    if (prot & PROT_WRITE) new_vma->vm_flags |= VM_WRITE;
    if (prot & PROT_EXEC)  new_vma->vm_flags |= VM_EXEC;

    new_vma->vm_file  = 0; // No files in this lab

    // 5. Add to the list
    list_add_tail(&new_vma->list, &current->mm.mmap_list);

    return addr;
}


/* src/sys.c */
long do_waitpid(long pid) {
    while (1) {
        struct task_struct *target = find_task_by_pid((int)pid);

        if (!target) return -1; 

        if (target->state == TASK_ZOMBIE) {
            uart_puts("[Kernel] Reaping Zombie PID "); uart_hex(target->pid); uart_puts("\n");
            
            // [FIX]: The parent "reaps" the child here
            int child_pid = target->pid;
            
            // Remove from the global task list so it's gone for good
            list_del(&target->list);
            
            // Free the kernel resources
            if (target->mm.pgd) {
                release_signal_stack(target);
                kfree((void *)target->mm.pgd);
            }
            kfree((void *)target->kernel_stack);
            kfree(target);

            return (long)child_pid;
        }

        schedule();
    }
}
