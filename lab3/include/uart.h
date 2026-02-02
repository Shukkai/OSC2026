#pragma once

/* * We declare a global variable to hold the UART base address.
 * This allows the kernel to update the address at runtime based on 
 * the Device Tree (DTB), rather than being stuck with a hardcoded macro.
 */
extern unsigned long uart_base_addr;

#ifdef __QEMU__
    /* QEMU "virt" machine: 8-bit registers, Stride 1 */
    #define DEFAULT_UART_BASE 0x10000000UL
    
    #define UART_RBR  (volatile unsigned char *)(uart_base_addr + 0x0)
    #define UART_THR  (volatile unsigned char *)(uart_base_addr + 0x0)
    #define UART_IER  (volatile unsigned char *)(uart_base_addr + 0x1)
    #define UART_IIR  (volatile unsigned char *)(uart_base_addr + 0x2)
    #define UART_MCR  (volatile unsigned char *)(uart_base_addr + 0x4)
    #define UART_LSR  (volatile unsigned char *)(uart_base_addr + 0x5)
#else
    /* Orange Pi RV2 (D1 SoC): 32-bit registers, Stride 4 */
    #define DEFAULT_UART_BASE 0xd4017000UL
    
    #define UART_RBR  (volatile unsigned int *)(uart_base_addr + 0x0)
    #define UART_THR  (volatile unsigned int *)(uart_base_addr + 0x0)
    #define UART_IER  (volatile unsigned int *)(uart_base_addr + 0x4)
    #define UART_IIR  (volatile unsigned int *)(uart_base_addr + 0x8)
    #define UART_MCR  (volatile unsigned int *)(uart_base_addr + 0x10)
    #define UART_LSR  (volatile unsigned int *)(uart_base_addr + 0x14)
#endif

/* Function Prototypes */
void uart_set_base(unsigned long addr);
void uart_init();
char uart_getc();
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);

/* Helper for binary transfer (no \n -> \r\n translation) */
char uart_getc_raw();