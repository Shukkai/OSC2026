#include "plic.h"
#include <stdint.h>
#include "trap.h"
#include "uart.h"
#include "printk.h"

// Defined in kernel.c
extern unsigned long boot_cpu_hartid;

void plic_init(void)
{
    // 1. Set Priority for UART Interrupt
    // Set to 1 (lowest active priority)
    *(volatile uint32_t *)PLIC_PRIORITY(UART_IRQ_ID) = 1;

    // 2. Enable UART Interrupt for the Current Hart (S-Mode)
    int enable_reg_offset = (UART_IRQ_ID / 32);
    int enable_bit_offset = (UART_IRQ_ID % 32);

    // Use PLIC_S_ENABLE to target the S-Mode Enable register directly
    volatile uint32_t *enable_ptr = (volatile uint32_t *)(PLIC_S_ENABLE(boot_cpu_hartid) + (enable_reg_offset * 4));
    
    // Set the bit
    *enable_ptr |= (1 << enable_bit_offset);

    // 3. Set Priority Threshold to 0 (Allow all interrupts)
    // Use PLIC_S_THRESHOLD to target S-Mode threshold directly
    *(volatile uint32_t *)PLIC_S_THRESHOLD(boot_cpu_hartid) = 0;
}

int plic_claim(void)
{
    // Read Claim register for the current Hart's S-Mode
    return *(volatile uint32_t *)PLIC_S_CLAIM(boot_cpu_hartid);
}

void plic_complete(int irq)
{
    // Write the ID back to signal completion
    *(volatile uint32_t *)PLIC_S_CLAIM(boot_cpu_hartid) = irq;
}


void plic_irq_handler(void)
{
    int irq = plic_claim();

    if (irq == UART_IRQ_ID) {
        uart_isr(); 
    } 
    else if (irq) {
        printk("[IRQ] Unknown: %d\n", irq);
    }

    if (irq) {
        plic_complete(irq);
    }
}