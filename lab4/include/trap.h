#ifndef _TRAP_H_
#define _TRAP_H_

// This logic ensures 'stdint.h' and structs are ONLY seen by C files (kernel.c, trap.c)
// Assembly files (entry.S) will only see the #defines below.
#ifndef __ASSEMBLER__
#include <stdint.h>
#endif

/* ========================================================================= */
/* RISC-V STANDARD CAUSE CODES (Available to both C and ASM)                 */
/* ========================================================================= */

// MSB distinguishes Interrupts (1) from Exceptions (0)
#define SCAUSE_IRQ_FLAG       (1UL << 63)
#define SCAUSE_CODE_MASK      (~SCAUSE_IRQ_FLAG)

// Interrupt Codes (Standard RISC-V)
#define IRQ_S_SOFT            1
#define IRQ_S_TIMER           5
#define IRQ_S_EXT             9

// Exception Codes (Standard RISC-V)
#define EXC_INST_MISALIGNED   0
#define EXC_INST_ACCESS       1
#define EXC_ILLEGAL_INST      2
#define EXC_BREAKPOINT        3
#define EXC_LOAD_MISALIGNED   4
#define EXC_LOAD_ACCESS       5
#define EXC_STORE_MISALIGNED  6
#define EXC_STORE_ACCESS      7
#define EXC_U_ECALL           8
#define EXC_S_ECALL           9
#define EXC_INST_PAGE_FAULT   12
#define EXC_LOAD_PAGE_FAULT   13
#define EXC_STORE_PAGE_FAULT  15

/* ========================================================================= */
/* SYSCALL NUMBERS                                                           */
/* ========================================================================= */
#define SYS_WRITE             64
#define SYS_EXIT              93
#define SYS_GETPID            172

/* ========================================================================= */
/* C-ONLY DEFINITIONS (Hidden from Assembly)                                 */
/* ========================================================================= */
#ifndef __ASSEMBLER__

struct TrapFrame {
    uint64_t ra;      // 0
    uint64_t sp;      // 1
    uint64_t gp;      // 2
    uint64_t tp;      // 3
    uint64_t t0;      // 4
    uint64_t t1;      // 5
    uint64_t t2;      // 6
    uint64_t s0;      // 7
    uint64_t s1;      // 8
    uint64_t a0;      // 9
    uint64_t a1;      // 10
    uint64_t a2;      // 11
    uint64_t a3;      // 12
    uint64_t a4;      // 13
    uint64_t a5;      // 14
    uint64_t a6;      // 15
    uint64_t a7;      // 16
    uint64_t s2;      // 17
    uint64_t s3;      // 18
    uint64_t s4;      // 19
    uint64_t s5;      // 20
    uint64_t s6;      // 21
    uint64_t s7;      // 22
    uint64_t s8;      // 23
    uint64_t s9;      // 24
    uint64_t s10;     // 25
    uint64_t s11;     // 26
    uint64_t t3;      // 27
    uint64_t t4;      // 28
    uint64_t t5;      // 29
    uint64_t t6;      // 30
    
    // CSRs
    uint64_t sepc;    // 31
    uint64_t sstatus; // 32
    uint64_t scause;  // 33
    uint64_t stval;   // 34
};

#define SSTATUS_SIE (1 << 1)

/* ========================================================================= */
/* PLATFORM INTERRUPT IDs (SpacemiT K1 Specific)                             */
/* ========================================================================= */
// These are the "Source IDs" used by the PLIC (Platform Level Interrupt Controller)
// Ref: SpacemiT K1 Manual Ch 7.2

#ifdef __QEMU__
    #define UART0_IRQ   10
#else
    #define UART0_IRQ   42 
#endif
#define TIMER_IRQ   23
#define GPIO_IRQ    58

/* ========================================================================= */
/* INTERRUPT CONTROL FUNCTIONS                                               */
/* ========================================================================= */

void enable_interrupt();

void disable_interrupt();


void enable_external_interrupt();


void verify_trap_frame();
void do_trap(struct TrapFrame *tf);

#endif // __ASSEMBLER__

#endif // _TRAP_H_