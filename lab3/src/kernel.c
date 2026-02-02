#include "uart.h"
#include "shell.h"
#include "cmd.h"
#include "printk.h"
#include <stdint.h>
#include "fdt.h"
#include "mm.h"

/* Global variables to store boot information */
unsigned long boot_cpu_hartid;
unsigned long DTB_BASE;

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
    // 4. Verify
    uart_puts("\n");
    if (dtb_uart_addr != 0) {
        printk("UART Base set to 0x%x from DTB\n", dtb_uart_addr);
    } else {
        printk("UART Base using default value (DTB lookup failed)\n");
    }
    mm_init((void *)dtb);
    cmd_init();
    run_shell();
    return 0;
}