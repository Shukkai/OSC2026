#include "cmd.h"
#include "uart.h"
#include "string.h"
#include "sbi.h"
#include "fdt.h" 
#include "initrd.h"
#include "bootload.h"
#include "utils.h"

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
    uart_puts("  clear  - Clear Terminal\n");
    uart_puts("  ls     - List initial ramdisk (initrd) files\n");
    uart_puts("  cat    - Output initrd file content\n");
    uart_puts("  load   - Load kernel over UART\n");
}

void cmd_clear(){
    uart_puts("\033[2J\033[1;1H");
}

void cmd_init()
{
    uart_puts("\n");
    uart_puts(" ==================================\n");
    uart_puts("      Welcome to  NYCU OSC         \n");
    uart_puts("      Type 'help' for commands     \n");
    uart_puts(" ==================================\n");
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
        struct sbiret ret = sbi_system_reset(SBI_RESET_TYPE_COLD_REBOOT, SBI_RESET_REASON_NONE);
        uart_puts("Software reset failed (Error: ");
        uart_hex(ret.error);
        uart_puts(").\n");
    } 
    else if (!strcmp(buf, "info")) {
        struct sbiret ret;

        uart_puts("\n--- System Info ---\n");
        uart_puts("Hart ID:          "); uart_hex(boot_cpu_hartid); uart_puts("\n");
        
        // 1. Print DTB Info using the new helper
        print_dtb_info(DTB_BASE);

        // 2. SBI Version Info
        ret = sbi_get_spec_version();
        uart_puts("SBI Spec Version: "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_id();
        uart_puts("SBI Impl ID:      "); uart_hex(ret.value); uart_puts("\n");
        ret = sbi_get_impl_version();
        uart_puts("SBI Impl Version: "); uart_hex(ret.value); uart_puts("\n");

        // 3. Get UART info
        uint64_t uart_addr = fdt_get_uart_base((void *)DTB_BASE);
        if (uart_addr) {
            uart_puts("UART Base:        "); uart_hex(uart_addr); uart_puts("\n");
        } else {
            uart_puts("UART Base:        Not Found\n");
        }
        
        uart_puts("-------------------\n");
    }
    else if(!strcmp(buf, "clear")){
        cmd_clear();
    }
    else if (!strcmp(buf, "ls")) {
        initrd_list();
    }
    else if (strncmp(buf, "cat ", 4) == 0) {
        char *filename = buf + 4;
        initrd_cat(filename);
    }
    else if (!strcmp(buf, "load")) {
        // You can now check info before loading inside cmd_load_kernel if you wish
        cmd_load_kernel();
    }
    else {
        uart_puts("Unknown command: ");
        uart_puts(buf);
        uart_puts("\n");
    }
}