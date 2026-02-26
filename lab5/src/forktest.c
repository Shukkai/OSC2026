#include "forktest.h"
#include "sched.h"    
#include "mm.h"      
#include "trap.h"    
#include "uart.h"    
#include "string.h"  // For memset
#include "sys.h"     // For fork(), getpid(), exit()
#include "utils.h"   // [NEW] For mini_sprintf

/* ========================================================================= */
/* USER SPACE CODE (Running in User Mode)                                    */
/* ========================================================================= */

// Wrapper to format string and call SYS_UART_WRITE
void user_printk(const char *fmt, ...) {
    char buf[256]; // Temporary buffer
    va_list args;
    
    va_start(args, fmt);
    mini_sprintf(buf, fmt, args); 
    va_end(args);

    // [NEW] Calculate the actual string length!
    long len = 0;
    while(buf[len] != '\0') len++;

    // Bind variables directly to RISC-V registers for the ecall
    register long a7 asm("a7") = 2;         // SYS_UART_WRITE
    register long a0 asm("a0") = (long)buf; // Buffer address
    register long a1 asm("a1") = len;       // [NEW] Pass the real length

    // Execute system call safely
    asm volatile(
        "ecall"
        : "+r"(a0)           // Output: a0
        : "r"(a1), "r"(a7)   // Inputs: a1, a7
        : "memory"           // Tell GCC memory was modified
    );
}
// Locally map printk to our user-mode wrapper
#define printk user_printk

void do_fork_test()
{
    printk("Fork test (pid = %d)\n", getpid());
    int cnt = 1;
    int ret = 0;
    if ((ret = fork()) == 0) {
        long cur_sp;
        asm("mv %0, sp" : "=r"(cur_sp));
        printk("child1: pid = %d, cnt = %d, ptr = %p, sp = %p\n", getpid(), cnt,
               &cnt, cur_sp);
        cnt++;

        if ((ret = fork()) != 0) {
            asm("mv %0, sp" : "=r"(cur_sp));
            printk("child1: pid = %d, cnt = %d, ptr = %p, sp = %p\n", getpid(),
                   cnt, &cnt, cur_sp);
            cnt++;
        } else {
            while (cnt < 5) {
                asm("mv %0, sp" : "=r"(cur_sp));
                printk("child2: pid = %d, cnt = %d, ptr = %p, sp = %p\n",
                       getpid(), cnt, &cnt, cur_sp);
                for (int i = 0; i < 1000000000; i++)
                    ;
                cnt++;
            }
        }
    } else {
        printk("parent: pid = %d, child pid = %d\n", getpid(), ret);
    }
    exit(0);
}
#undef printk 

/* ========================================================================= */
/* KERNEL SPACE LAUNCHER                                                     */
/* ========================================================================= */

void test_fork(void) {
    disable_interrupt();
    uart_puts("[Kernel] Setting up Fork Test...\n");
    struct task_struct *p = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    if (!p) return;

    p->kernel_stack = (uint64_t)kmalloc(PAGE_SIZE);
    p->user_stack   = (uint64_t)kmalloc(PAGE_SIZE);

    if (!p->kernel_stack || !p->user_stack) {
        uart_puts("OOM: Failed to allocate stacks\n");
        return;
    }

    // Use global PID counter from sched.c (Requires 'extern' in sched.h)
    extern int pid_counter; 
    p->pid = pid_counter++; 
    
    p->state = TASK_READY;
    p->priority = 1;
    p->counter = 0;
    p->preempt_count = 0;

    // Reset TrapFrame
    p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    memset(p->tf, 0, sizeof(struct TrapFrame));

    p->tf->sp = p->user_stack + PAGE_SIZE;     // Top of User Stack
    p->tf->sepc = (uint64_t)do_fork_test;      // Entry Point
    p->tf->sstatus = (1 << 5);                 // SPIE=1, SPP=0 (User Mode)

    extern void ret_from_exception(void);
    p->thread.ra = (uint64_t)ret_from_exception;
    p->thread.sp = (uint64_t)p->tf; 

    // Add to Scheduler
    list_add_tail(&p->list, &runqueue);
    uart_puts("[Kernel] Fork Test Process Created. Switching to scheduler...\n");
    // Wait until test processes finish
    while (num_runnable_tasks() > 1) {
        kill_zombies();
        schedule();
    }
    disable_interrupt();
    uart_puts("[Kernel] Fork Test Complete.\n");
    enable_interrupt();
}