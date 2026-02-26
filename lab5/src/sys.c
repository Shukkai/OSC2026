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
long do_uart_write(const char *buf, unsigned long size) {
    if (size == 0) return 0;

    for (unsigned long i = 0; i < size; i++) {
        // Safely use the interrupt-driven TX buffer for fast printing
        uart_putc_buffered(buf[i]);
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
    
    // Relocate SP to point to the new stack
    unsigned long sp_offset = current->tf->sp - current->user_stack;
    p->tf->sp = p->user_stack + sp_offset;

    // [FIX] Only relocate S0/FP if it is actually pointing inside the stack!
    if (current->tf->s0 >= current->user_stack && current->tf->s0 < current->user_stack + PAGE_SIZE) {
        unsigned long fp_offset = current->tf->s0 - current->user_stack;
        p->tf->s0 = p->user_stack + fp_offset;
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
    // // 1. Save the exit status so parent can read it later (if wait() is implemented)
    current->exit_code = status;
    kthread_exit();
}


int do_exec(const char *path, struct TrapFrame *tf) {
    unsigned long filesize;
    void *content_start = initrd_find_file(path, &filesize);

    if (!content_start) {
        return -1; // File not found in initramfs
    }

    // 1. Allocate memory for the new program
    char *prog_buf = (char *)kmalloc(0x20000); 
    char *stack_buf = (char *)kmalloc(0x1000);

    if (!prog_buf || !stack_buf) {
        return -1; // Out of memory
    }

    // 2. Copy the new program binary into memory
    char *src = (char *)content_start;
    for (unsigned long i = 0; i < filesize; i++) {
        prog_buf[i] = src[i];
    }

    // 3. Critical: Flush Instruction Cache so CPU sees the new code!
    asm volatile("fence.i");
    // [CRITICAL FIX] Update the process's stack pointer!
    // This ensures that when fork() is called later, it copies the REAL stack.
    if (current->user_stack) {
        kfree((void *)current->user_stack);
    }
    current->user_stack = (uint64_t)stack_buf;
    // 4. Overwrite the TrapFrame passed from do_trap
    // We safely use the 'tf' pointer instead of 'current->tf'
    tf->sepc = (uint64_t)prog_buf;
    tf->sp   = (uint64_t)stack_buf + 0x1000;

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