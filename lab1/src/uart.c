#include "uart.h"
#include "printk.h"

unsigned long uart_base_addr = UART_BASE;

void uart_set_base(unsigned long addr) {
    uart_base_addr = addr;
}

void uart_init()
{
    // *UART_IER |= (1 << 1); // Enable TX interrupt
    *UART_IER |= (1 << 0); // Enable RX interrupt
    *UART_MCR |= (1 << 3); // Enable uart interrupt
    // printk("UART_IER = 0x%x\n", *UART_IER);
    // printk("UART_IIR = 0x%x\n", *UART_IIR);
    // printk("UART_MCR = 0x%x\n", *UART_MCR);
}

char uart_getc()
{
    while ((*UART_LSR & 0x01) == 0)
        ;
    char c = (char)*UART_RBR;
    return c == '\r' ? '\n' : c;
}

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');

    while ((*UART_LSR & 0x20) == 0)
        ;
    *UART_THR = c;
}

void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

void uart_hex(unsigned long h)
{
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        int n = (h >> i) & 0xF;
        uart_putc(n > 9 ? n + 0x37 : n + '0');
    }
}