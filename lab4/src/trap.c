#include "trap.h"
#include "uart.h"
#include "printk.h"
#include <stddef.h>
#include "timer.h" 
#include "plic.h"
#include "task.h"
/* ========================================================================= */
/* VERIFICATION (Moved from kernel.c)                                        */
/* ========================================================================= */

void verify_trap_frame() {
    uart_puts("\n=== Verifying TrapFrame Layout ===\n");
    
    // 1. Check Total Size
    // Assembly: addi sp, sp, -8 * 35 -> 280 bytes
    uint64_t size = sizeof(struct TrapFrame);
    printk("Sizeof(TrapFrame): %d bytes (Expected: 280)\n", size);
    
    if (size != 280) {
        uart_puts("!!! ERROR: Struct size mismatch! Check trap.h types !!!\n");
    }

    // 2. Check Critical Offsets
    // These must match the 'sd' offsets in entry.S
    
    uint64_t off_ra = offsetof(struct TrapFrame, ra);
    printk("Offset ra:         %d (Expected: 0)\n", off_ra);
    
    uint64_t off_sp = offsetof(struct TrapFrame, sp);
    printk("Offset sp:         %d (Expected: 8)\n", off_sp);

    uint64_t off_a0 = offsetof(struct TrapFrame, a0);
    printk("Offset a0:         %d (Expected: 72)\n", off_a0); // 8 * 9

    uint64_t off_sepc = offsetof(struct TrapFrame, sepc);
    printk("Offset sepc:       %d (Expected: 248)\n", off_sepc); // 8 * 31
    
    uint64_t off_sstatus = offsetof(struct TrapFrame, sstatus);
    printk("Offset sstatus:    %d (Expected: 256)\n", off_sstatus); // 8 * 32

    if (off_sepc != 248 || off_sstatus != 256) {
         uart_puts("!!! ERROR: CSR Offsets mismatch! Assembly will save to wrong location !!!\n");
    } else {
         uart_puts("-> Layout matches entry.S expectations.\n");
    }
    uart_puts("==================================\n");
}


void enable_interrupt() {
    asm volatile("csrrs zero, sstatus, %0" : : "r" (SSTATUS_SIE));
}

void disable_interrupt() {
    asm volatile("csrrc zero, sstatus, %0" : : "r" (SSTATUS_SIE));
}

void enable_external_interrupt()
{
    // Enable Supervisor External Interrupts (Bit 9)
    asm volatile("li t0, (1 << 9); csrs sie, t0" : : : "t0");
}


/* ========================================================================= */
/* TRAP DISPATCHER (Called from entry.S)                                     */
/* ========================================================================= */

void do_trap(struct TrapFrame *tf) {
    // 1. Separate Interrupt Bit (MSB) from Exception Code
    unsigned long scause = tf->scause;
    unsigned long exception_code = scause & SCAUSE_CODE_MASK;
    int is_interrupt = (scause & SCAUSE_IRQ_FLAG) ? 1 : 0;

    if (is_interrupt) {
        switch (exception_code) {
            case IRQ_S_TIMER: 
                // 1. TOP HALF: Fast. Acknowledge and Schedule Next Tick.
                timer_irq_handler(); 
                
                // 2. BOTTOM HALF: Slow. Run queued tasks with interrupts enabled.
                task_run(); 
                break;
            
            case IRQ_S_EXT:   
                // 1. TOP HALF: Fast. Buffer data from UART.
                plic_irq_handler(); 
                
                // 2. BOTTOM HALF: Process any high-priority tasks if needed.
                task_run(); 
                break;

            default:
                printk("[Trap] Unhandled Interrupt! Code: %d\n", exception_code);
                break;
        }
    }
    else {
        // Handle Synchronous Exceptions
        switch (exception_code) {
            case EXC_U_ECALL:
                /* * SYSCALL HANDLER 
                 * 1. Advance sepc by 4 bytes (instruction size) 
                 * Otherwise, 'sret' returns to the 'ecall' instruction,
                 * causing an infinite loop!
                 */
                tf->sepc += 4;

                // 2. Handle Syscall Number (passed in a7)
                if (tf->a7 == SYS_WRITE) {
                }
                else if (tf->a7 == SYS_EXIT) {
                    // printk("[Syscall] Program exited with code %d.\n", tf->a0);
                    // printk("System Halt. Reboot to restart.\n");
                    // while(1); // <--- Just hang here!
                }
                else {
                    printk("[System Call] Unknown Syscall ID: %d\n", tf->a7);
                    tf->a0 = -1; // Return Error Code
                }
                break;

            case EXC_INST_PAGE_FAULT:
            case EXC_LOAD_PAGE_FAULT:
            case EXC_STORE_PAGE_FAULT:
                printk("!!! Page Fault at 0x%lx !!!\n", tf->stval);
                while(1);

            default:
                printk("!!! PANIC: Unhandled Exception. Code: %d !!!\n", exception_code);
                printk("scause: 0x%lx, sepc: 0x%lx, stval: 0x%lx\n", 
                        tf->scause, tf->sepc, tf->stval);
                while(1); 
        }
    }
}