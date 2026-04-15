#include "sys.h"
#include "sched.h"
#include "mm.h"
#include "string.h"
#include "uart.h"
#include "initrd.h"
#include "trap.h"
#include "timer.h"
#include"fb.h"

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

// [NEW] User-space UART Read
int uart_read(char *buf, int size) {
    return call_sys_2(1, (long)buf, (long)size); // SYS_UART_READ = 1
}

// [NEW] User-space UART Write
int uart_write(const char *buf, int size) {
    return call_sys_2(2, (long)buf, (long)size); // SYS_UART_WRITE = 2
}

int exec(const char *path) {
    return call_sys(3, (long)path); // SYS_EXEC = 3
}

int usleep(unsigned int usec) {
    return call_sys(8, (long)usec); // SYS_USLEEP = 8
}

void display(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    call_sys_3(7, (long)bmp_image, (long)width, (long)height); // SYS_DISPLAY = 7
}

// [NEW] User-space Signal API
long signal(int signum, void (*handler)(void)) {
    // SYS_SIGNAL = 9
    return call_sys_2(9, (long)signum, (long)handler); 
}

// [NEW] User-space Sigreturn API (Called automatically by the Trampoline)
long sigreturn(void) {
    // SYS_SIGRETURN = 10
    return call_sys(10, 0); 
}

// [NEW] User-space Stop API (Force Quit)
int stop(int pid) {
    // SYS_KILL = 6 (Force stop/terminate)
    return call_sys(6, (long)pid); 
}

// [NEW] User-space Kill API (Sends a Polite Signal)
int kill(int pid, int sig) {
    // SYS_SIG_KILL = 11 (Send signal)
    return call_sys_2(11, (long)pid, (long)sig); 
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
    // 1. Critical Section Start
    // We disable interrupts immediately to protect the shared runqueue and PID counter.
    disable_interrupt();

    // 2. Allocate Memory (using our new helper)
    struct task_struct *p = task_alloc();
    if (!p) {
        enable_interrupt(); // Failed, unlock and return
        return -1;
    }

    // 3. Setup Identity (PID & State)
    p->pid = pid_counter++; // Accessing global shared variable
    p->state = TASK_READY;
    p->priority = current->priority;
    p->counter = 0;
    p->preempt_count = 0;

    // 3.1 Create a separate child page table
    if (current->mm.pgd) {
        unsigned long *child_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
        if (!child_pgd) {
            kfree((void *)p->kernel_stack);
            kfree((void *)p->user_stack);
            kfree(p);
            enable_interrupt();
            return -1;
        }

        for (int i = 0; i < 512; i++)
            child_pgd[i] = 0;

        // Copy kernel mappings upper half
        for (int i = 256; i < 512; i++)
            child_pgd[i] = pg_dir[i];

        // Deep copy user page table levels to avoid L1/L2 sharing
        for (int i = 0; i < 256; i++) {
            unsigned long ent = current->mm.pgd[i];
            if (!ent) continue;

            if (ent & (PAGE_READ | PAGE_WRITE | PAGE_EXEC)) {
                // leaf mapping can be shared for fork (no COW for now)
                child_pgd[i] = ent;
            } else {
                // intermediate page table entry; clone it
                unsigned long parent_l1_pa = (ent >> 10) << 12;
                unsigned long *parent_l1 = (unsigned long *)phys_to_virt(parent_l1_pa);
                unsigned long *child_l1 = (unsigned long *)kmalloc(PAGE_SIZE);
                if (!child_l1) {
                    // cleanup failures (free all allocated levels)
                    for (int j = 0; j < i; j++) {
                        unsigned long e = child_pgd[j];
                        if (e && !(e & (PAGE_READ | PAGE_WRITE | PAGE_EXEC))) {
                            kfree((void *)phys_to_virt((e >> 10) << 12));
                        }
                    }
                    kfree(child_pgd);
                    kfree((void *)p->kernel_stack);
                    kfree((void *)p->user_stack);
                    kfree(p);
                    enable_interrupt();
                    return -1;
                }
                for (int j = 0; j < 512; j++)
                    child_l1[j] = parent_l1[j];

                unsigned long child_l1_pa = virt_to_phys((unsigned long)child_l1);
                unsigned long flags = ent & 0x3FF;
                child_pgd[i] = (child_l1_pa >> 12) << 10 | flags;
            }
        }

        p->mm.pgd = child_pgd;

        // Map child stack physical buffer at 0x3fffffc000 (replace parent stack mapping)
        unsigned long stack_pa = virt_to_phys((unsigned long)p->user_stack);
        unsigned long stack_flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | 
                            PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
        map_pages(p->mm.pgd, 0x3fffffc000UL, stack_pa, PAGE_SIZE, stack_flags);
    } else {
        p->mm.pgd = 0;
    }

    // 4. Copy User Stack (Deep Copy from Parent)
    memcpy((void *)p->user_stack, (void *)current->user_stack, PAGE_SIZE);

    // 5. Setup TrapFrame (Deep Copy from Parent)
    p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    *p->tf = *current->tf;
    // [CRITICAL FIX] Inherit the Parent's Signal Handlers!
    p->pending_signals = 0;
    p->in_signal_handler = 0;
    for (int i = 0; i < MAX_SIG; i++) {
        p->signal_handler[i] = current->signal_handler[i];
    }

    // 6. Child Process Tweaks
    p->tf->a0 = 0; // Child returns 0

    const unsigned long USER_STACK_BASE = 0x3fffffc000UL;
    const unsigned long USER_STACK_TOP = 0x4000000000UL;

    // Relocate SP in user virtual space (not kernel buffer pointer)
    if (current->tf->sp >= USER_STACK_BASE && current->tf->sp <= USER_STACK_TOP) {
        unsigned long sp_offset = current->tf->sp - USER_STACK_BASE;
        p->tf->sp = USER_STACK_BASE + sp_offset;
    } else {
        p->tf->sp = current->tf->sp;
    }

    // [FIX] Relocate s0/fp in user virtual space
    if (current->tf->s0 >= USER_STACK_BASE && current->tf->s0 < USER_STACK_TOP) {
        unsigned long fp_offset = current->tf->s0 - USER_STACK_BASE;
        p->tf->s0 = USER_STACK_BASE + fp_offset;
    } else {
        p->tf->s0 = current->tf->s0;
    }

    // 7. Setup Kernel Context (for switch_to)
    p->thread.ra = (uint64_t)ret_from_exception;
    p->thread.sp = (uint64_t)p->tf; 

    // 8. Add to Scheduler
    list_add_tail(&p->list, &runqueue);

    // 9. Critical Section End
    enable_interrupt();

    return p->pid; // Return Child PID to Parent
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
        return -1; // File not found in initramfs
    }

    unsigned long prog_pages = (filesize + PAGE_SIZE - 1) / PAGE_SIZE;
    if (prog_pages == 0) prog_pages = 1;

    char *prog_buf = (char *)kmalloc(prog_pages * PAGE_SIZE);
    char *stack_buf = (char *)kmalloc(PAGE_SIZE);
    if (!prog_buf || !stack_buf) {
        if (prog_buf) kfree(prog_buf);
        if (stack_buf) kfree(stack_buf);
        return -1; // Out of memory
    }

    char *src = (char *)content_start;
    for (unsigned long i = 0; i < filesize; i++) {
        prog_buf[i] = src[i];
    }

    // Free old pgd if present (avoid leak on repeated exec)
    if (current->mm.pgd) {
        kfree((void *)current->mm.pgd);
        current->mm.pgd = 0;
    }

    // Create user page table (PGD)
    unsigned long *user_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
    if (!user_pgd) {
        kfree(prog_buf);
        kfree(stack_buf);
        return -1;
    }

    for (int i = 0; i < 512; i++)
        user_pgd[i] = 0;

    for (int i = 256; i < 512; i++)
        user_pgd[i] = pg_dir[i];

    current->mm.pgd = user_pgd;

    unsigned long prog_pa = virt_to_phys((unsigned long)prog_buf);
    unsigned long text_flags = PAGE_PRESENT | PAGE_READ | PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
    map_pages(user_pgd, 0x0, prog_pa, prog_pages * PAGE_SIZE, text_flags);

    unsigned long stack_pa = virt_to_phys((unsigned long)stack_buf);
    // unsigned long stack_flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY;
    unsigned long stack_flags = PAGE_PRESENT | PAGE_READ | PAGE_WRITE | 
                            PAGE_EXEC | PAGE_USER | PAGE_ACCESSED | PAGE_DIRTY; // <-- Added PAGE_EXEC
    map_pages(user_pgd, 0x3fffffc000UL, stack_pa, PAGE_SIZE, stack_flags);

    if (current->user_stack) {
        kfree((void *)current->user_stack);
    }
    current->user_stack = (uint64_t)stack_buf;

    tf->sepc = 0x0;
    tf->sp = 0x4000000000UL;

    unsigned long satp_val = SATP_MODE_SV39 | (virt_to_phys((unsigned long)user_pgd) >> 12);
    asm volatile("csrw satp, %0\n\tsfence.vma zero, zero" :: "r"(satp_val) : "memory");

    asm volatile("fence.i");

    return 0; // Success!
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
    current->signal_handler[sig] = handler;
    return 0;
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
    // Restore the exact CPU state from before the signal fired!
    *tf = current->saved_tf;
    current->in_signal_handler = 0;
    
    return tf->a0; 
}