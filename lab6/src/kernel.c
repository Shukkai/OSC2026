#include "uart.h"
#include "shell.h"
#include "cmd.h"
#include "printk.h"
#include <stdint.h>
#include "fdt.h"
#include "mm.h"
#include "trap.h"
#include "timer.h"
#include "plic.h"
#include "task.h"
#include "sched.h"
#include "fb.h"
#include "vm.h"
/* Global variables to store boot information */
unsigned long boot_cpu_hartid;
unsigned long DTB_BASE;
extern unsigned long pg_dir[];
int start_kernel(unsigned long hartid, unsigned long dtb)
{
    /* Now these variables exist as arguments, so we can save them */
    boot_cpu_hartid = hartid;
    DTB_BASE = dtb;
    // 1. Parse UART Address from DTB *First*
    uint64_t dtb_uart_addr = fdt_get_uart_base((void *)dtb);
    
    // 2. Update the driver if a valid address was found
    if (dtb_uart_addr != 0) {
        uart_set_base(dtb_uart_addr);
    }

    // 3. NOW initialize UART (it will use the new base address)
    uart_init();
    
    // // 4. Verify
    uart_puts("\n");
    if (dtb_uart_addr != 0) {
        uart_puts("UART Base set to 0x"); uart_hex(dtb_uart_addr); uart_puts(" from DTB\n");
    } else {
        uart_puts("UART Base using default value (DTB lookup failed)\n");
    }
    // mm_init((void *)dtb);
    mm_init((void *)phys_to_virt(dtb));
    task_init();
    sched_init();
    fb_init();
    plic_init();
    enable_external_interrupt();
    uart_enable_interrupt();
    timer_init((void *)dtb);
    enable_interrupt();

    // uart_puts_async("Interrupts Enabled. Buffered I/O check.\n");
    // uart_puts("Start at ");
    // uart_hex((unsigned long)_start);
    // uart_puts("\npg_dir: ");
    // uart_hex((unsigned long)pg_dir);
    // uart_puts("\nsatp: ");

    // unsigned long val;
    // asm volatile("csrr %0, satp" : "=r"(val));
    // uart_hex(val);
    cmd_init();
    run_shell();
    return 0;
}