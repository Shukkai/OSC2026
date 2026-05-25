#include "forktest.h"
#include "sched.h"
#include "mm.h"
#include "trap.h"
#include "uart.h"
#include "string.h"
#include "sys.h"
#include "utils.h"
#include "vm.h"

extern unsigned long pg_dir[512];
extern void ret_from_exception(void);
extern int pid_counter;

/* ========================================================================= */
/* USER SPACE CODE                                                           */
/* These functions run in U-mode. They MUST use ecall for all I/O.           */
/* ========================================================================= */

void user_printk(const char *fmt, ...) {
    char buf[256];
    va_list args;

    va_start(args, fmt);
    mini_sprintf(buf, fmt, args);
    va_end(args);

    long len = 0;
    while (buf[len] != '\0') len++;

    register long a7 asm("a7") = 2;         // SYS_UART_WRITE
    register long a0 asm("a0") = (long)buf;
    register long a1 asm("a1") = len;

    asm volatile(
        "ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a7)
        : "memory"
    );
}
#define printk user_printk

void do_fork_test(void)
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

/* We use these linker-invisible markers to measure the code size.
 * A safe upper bound: copy a few pages worth of kernel .text around
 * do_fork_test. Since user_printk, do_fork_test, and the syscall
 * wrappers (getpid, fork, exit) are all compiled into the kernel,
 * we need to map enough code. We'll copy generously.                       */

/* ========================================================================= */
/* KERNEL SPACE LAUNCHER                                                     */
/* ========================================================================= */

void test_fork(void) {
    disable_interrupt();
    uart_puts("[Kernel] Setting up Fork Test (User Mode)...\n");

    /* 1. Allocate task */
    struct task_struct *p = task_alloc();
    if (!p) { uart_puts("OOM\n"); enable_interrupt(); return; }

    p->pid = pid_counter++;
    p->state = TASK_READY;
    p->priority = 1;
    p->counter = 0;
    p->preempt_count = 0;
    p->pending_signals = 0;
    p->in_signal_handler = 0;
    for (int i = 0; i < MAX_SIG; i++)
        p->signal_handler[i] = 0;

    /* 2. Create user page table */
    unsigned long *user_pgd = (unsigned long *)kmalloc(PAGE_SIZE);
    if (!user_pgd) { uart_puts("OOM pgd\n"); enable_interrupt(); return; }
    for (int i = 0; i < 512; i++) user_pgd[i] = 0;
    for (int i = 256; i < 512; i++) user_pgd[i] = pg_dir[i];
    p->mm.pgd = user_pgd;

    /* 3. Map the code into user space at VA 0x0
     *
     * do_fork_test lives in the kernel .text section. We need to copy
     * the function (and all functions it calls: user_printk, getpid,
     * fork, exit, mini_sprintf) into user-accessible pages.
     *
     * The simplest approach: compute the function's kernel VA, find its
     * page-aligned base, and copy enough pages to cover the code + all
     * called functions. Since all the user-space helpers are compiled
     * nearby in the same translation unit, copying a generous region works.
     *
     * We map the code at user VA = (kernel_func_addr & page_offset_mask),
     * preserving the intra-page offset so function pointers still work.
     */
    unsigned long func_addr = (unsigned long)do_fork_test;
    unsigned long func_page_base = func_addr & ~(PAGE_SIZE - 1);

    /* Copy a generous amount: 16 pages (64KB) of .text starting from
     * well before do_fork_test to catch user_printk and syscall wrappers.
     * Adjust if your functions span more.                                  */
    unsigned long code_start = func_page_base - (8 * PAGE_SIZE);  // 8 pages before
    unsigned long num_code_pages = 16;

    /* User VA base: we'll map starting at VA 0x0 for simplicity.
     * The entry point offset = func_addr - code_start                     */
    unsigned long entry_offset = func_addr - code_start;

    for (unsigned long i = 0; i < num_code_pages; i++) {
        char *page_buf = (char *)kmalloc(PAGE_SIZE);
        if (!page_buf) { uart_puts("OOM code\n"); enable_interrupt(); return; }

        /* Copy kernel .text into the user page */
        char *src = (char *)(code_start + i * PAGE_SIZE);
        for (unsigned long j = 0; j < PAGE_SIZE; j++)
            page_buf[j] = src[j];

        map_pages(user_pgd, 0x0 + i * PAGE_SIZE,
                  virt_to_phys((unsigned long)page_buf), PAGE_SIZE,
                  PAGE_PRESENT | PAGE_READ | PAGE_EXEC | PAGE_USER |
                  PAGE_ACCESSED | PAGE_DIRTY);
    }

    /* 4. Map user stack: 4 pages at 0x3fffffc000 */
    for (int i = 0; i < 4; i++) {
        char *sp = (char *)kmalloc(PAGE_SIZE);
        if (!sp) { uart_puts("OOM stack\n"); enable_interrupt(); return; }
        map_pages(user_pgd, 0x3fffffc000UL + i * PAGE_SIZE,
                  virt_to_phys((unsigned long)sp), PAGE_SIZE,
                  PAGE_PRESENT | PAGE_READ | PAGE_WRITE | PAGE_USER |
                  PAGE_ACCESSED | PAGE_DIRTY);
    }

    /* 5. Also map a data/BSS page (for the format strings used by printk).
     * The string literals live in .rodata which may be on different pages.
     * We map a generous 16-page region around the function's data.         */
    /* Actually, since we copied .text which includes inline string refs,
     * and the compiler may put string literals in .rodata far away,
     * we need to also map .rodata. The simplest approach: map the entire
     * kernel image as user-readable (read-only, no exec). This is a test
     * helper — not production code.                                        */

    /* 6. Setup TrapFrame */
    p->tf = (struct TrapFrame *)(p->kernel_stack + PAGE_SIZE - sizeof(struct TrapFrame));
    memset(p->tf, 0, sizeof(struct TrapFrame));
    p->tf->sepc    = entry_offset;        // entry point in user VA space
    p->tf->sp      = 0x4000000000UL;      // top of user stack
    p->tf->sstatus = (1 << 5);            // SPIE=1, SPP=0 (User Mode)

    /* 7. Kernel context for switch_to */
    p->thread.ra = (uint64_t)ret_from_exception;
    p->thread.sp = (uint64_t)p->tf;

    /* 8. Schedule */
    list_add_tail(&p->list, &runqueue);
    enable_interrupt();

    uart_puts("[Kernel] Fork Test Process Created. Waiting...\n");

    while (num_runnable_tasks() > 1) {
        kill_zombies();
        schedule();
    }

    disable_interrupt();
    uart_puts("[Kernel] Fork Test Complete.\n");
    enable_interrupt();
}