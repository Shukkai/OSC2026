#include "uart.h"
#include "shell.h"
#include "cmd.h"

/* Global variables to store boot information */
unsigned long boot_cpu_hartid;
unsigned long DTB_BASE;

/* CRITICAL FIX: Add arguments (hartid, dtb) here! 
 * RISC-V bootloaders pass Hart ID in register a0 and DTB address in a1.
 * C functions receive the first two arguments in a0 and a1.
 */
int start_kernel(unsigned long hartid, unsigned long dtb)
{
    /* Now these variables exist as arguments, so we can save them */
    boot_cpu_hartid = hartid;
    DTB_BASE = dtb;

    uart_init();
    cmd_init();
    
    run_shell();
    return 0;
}