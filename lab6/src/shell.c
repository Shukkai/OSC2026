#include "shell.h"
#include "uart.h"
#include "cmd.h"

#define SHELL_BUF_SIZE 256

void read_user_input(char *buf)
{
    int i = 0;
    while (1) {

        char c = uart_getc_buffered();
        // char c = uart_getc_sync(); // Use the non-buffered version for better UX (no buffering)
        if (c == '\n' || c == '\r') {
            // Just send newline. The driver adds the \r automatically.
            uart_puts("\n");
            buf[i] = '\0';
            break;
        }
        /* Handle Backspace (127 = DEL, 8 = BS) */
        else if (c == 127 || c == '\b') {
            if (i > 0) {
                i--;
                /* Move back, print space to overwrite, move back again */
                uart_puts("\b \b");
            }
        } 
        /* Handle Regular Character */
        else {
            if (i < SHELL_BUF_SIZE - 1) {
                buf[i] = c;
                i++;
                uart_putc_buffered(c);
                // uart_putc(c); // Echo immediately for better UX (no buffering)
            }
        }
    }
}

void run_shell()
{
    char buf[SHELL_BUF_SIZE];
    while (1) {
        uart_puts("# ");
        read_user_input(buf);
        exec_command(buf);
    }
}