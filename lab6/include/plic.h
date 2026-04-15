#ifndef _PLIC_H_
#define _PLIC_H_
#include "vm.h"
// 1. Base Addresses
// =========================================================
#ifdef __QEMU__
    // #define PLIC_BASE      0x0c000000UL
    #define PLIC_BASE      phys_to_virt(0x0c000000UL)
    #define UART_IRQ_ID    10
#else
    #define PLIC_BASE      phys_to_virt(0xe0000000UL)
    #define UART_IRQ_ID    42
#endif

// 2. S-Mode Specific Macros 
// =========================================================
// These macros automatically add the offsets for S-Mode (Context 1),
// so you don't need to manually calculate "2 * hart + 1".

// Priority is global (0x000000)
#define PLIC_PRIORITY(id)      (PLIC_BASE + (id) * 4)

// Enable Bits: 
// - Standard Base: 0x2000
// - S-Mode Offset: +0x80
// - Stride: 0x100 (Jumps over M-mode to get to the next S-mode)
#define PLIC_S_ENABLE(hart)    (PLIC_BASE + 0x002080 + (hart) * 0x100)

// Threshold & Claim:
// - Standard Base: 0x200000
// - S-Mode Offset: +0x1000
// - Stride: 0x2000 (Jumps over M-mode to get to the next S-mode)
#define PLIC_S_THRESHOLD(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_S_CLAIM(hart)     (PLIC_BASE + 0x201004 + (hart) * 0x2000)

// 3. Prototypes
// =========================================================
void plic_init(void);
int plic_claim(void);
void plic_complete(int irq);
void plic_irq_handler(void);

#endif