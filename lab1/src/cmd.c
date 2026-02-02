#include "cmd.h"
#include "uart.h"
#include "string.h"
#include "sbi.h"
#include "fdt.h" // Include the new header

/* Access global variables defined in kernel.c */
extern unsigned long boot_cpu_hartid;
extern unsigned long DTB_BASE;

void cmd_help()
{
    uart_puts("Available commands:\n");
    uart_puts("  help   - Show this message\n");
    uart_puts("  hello  - Print Hello World\n");
    uart_puts("  reboot - Reboot system (SBI Shutdown)\n");
    uart_puts("  info   - Show System Info\n");
}

void cmd_clear(){
    uart_puts("\033[2J\033[1;1H");
}

void cmd_init()
{
    /* Clear Screen using ANSI escape code */
    cmd_clear();
    cmd_help();
}

void exec_command(char *buf)
{
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == ' ')) {
        buf[--len] = '\0';
    }
    if (len == 0) return;

    if (!strcmp(buf, "help")) {
        cmd_help();
    } 
    else if (!strcmp(buf, "hello")) {
        uart_puts("Hello World!\n");
    } 
    else if (!strcmp(buf, "reboot")) {
        uart_puts("Rebooting system...\n");
        
        /* Attempt Reset/Shutdown */
        struct sbiret ret = sbi_system_reset(SBI_RESET_TYPE_COLD_REBOOT, SBI_RESET_REASON_NONE);
        
        /* If we are still here, it failed completely */
        uart_puts("Software reset failed (Error: ");
        uart_hex(ret.error);
        uart_puts(").\n");
        uart_puts("Please press the RESET button on the board manually.\n");
    } 
    else if (!strcmp(buf, "info")) {
        struct sbiret ret;

        uart_puts("\n--- System Info ---\n");
        uart_puts("Hart ID:          "); uart_hex(boot_cpu_hartid); uart_puts("\n");
        uart_puts("DTB Address:      "); uart_hex(DTB_BASE); uart_puts("\n");

        ret = sbi_get_spec_version();
        uart_puts("SBI Spec Version: "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_id();
        uart_puts("SBI Impl ID:      "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_version();
        uart_puts("SBI Impl Version: "); uart_hex(ret.value); uart_puts("\n");

        /* FDT Information */
        fdt_print_info(); // Prints Model
        
        uint64_t uart_addr = fdt_get_uart_base();
        if (uart_addr != 0) {
            uart_puts("UART Base (FDT):  "); uart_hex(uart_addr); uart_puts("\n");
            
            /* Re-initialize with found address */
            uart_set_base((unsigned long)uart_addr);
            uart_init();
            uart_puts("UART re-initialized from DTB.\n");
        } else {
            uart_puts("UART Base (FDT):  Not Found\n");
        }
        
        uart_puts("-------------------\n");
    }
    else if(!strcmp(buf, "clear")){
        cmd_clear();
    }
    else {
        uart_puts("Unknown command: ");
        uart_puts(buf);
        uart_puts("\n");
    }
}
