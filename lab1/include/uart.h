#pragma once

#ifdef __QEMU__
    #define UART_BASE 0x10000000UL
    #define UART_RBR  (volatile unsigned char *)(UART_BASE + 0x0)
    #define UART_THR  (volatile unsigned char *)(UART_BASE + 0x0)
    #define UART_IER  (volatile unsigned char *)(UART_BASE + 0x1)
    #define UART_IIR  (volatile unsigned char *)(UART_BASE + 0x2)
    #define UART_MCR  (volatile unsigned char *)(UART_BASE + 0x4)
    #define UART_LSR  (volatile unsigned char *)(UART_BASE + 0x5)
#else
    /* Orange Pi RV2 (32-bit registers, Stride 4) */
    #define UART_BASE 0xd4017000UL
    #define UART_RBR  (volatile unsigned int *)(UART_BASE + 0x0)
    #define UART_THR  (volatile unsigned int *)(UART_BASE + 0x0)
    #define UART_IER  (volatile unsigned int *)(UART_BASE + 0x4)
    #define UART_IIR  (volatile unsigned int *)(UART_BASE + 0x8)
    #define UART_MCR  (volatile unsigned int *)(UART_BASE + 0x10)
    #define UART_LSR  (volatile unsigned int *)(UART_BASE + 0x14)
#endif

void uart_set_base(unsigned long addr);
void uart_init();
char uart_getc();
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);