#include "trap.h"
#include "uart.h"
#include "printk.h"
#include <stddef.h>
#include "timer.h" 
#include "plic.h"
#include "task.h"
#include "sys.h"
#include "sched.h"
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

                // 2. Dispatch Syscall based on a7
                // Macros defined in sys.h
                switch (tf->a7) {
                    case SYS_GETPID: // 0
                        tf->a0 = current->pid;
                        break;

                    case SYS_UART_READ: // 1
                        // Basic wrapper for uart_getc (blocking for now)
                        // In future labs, this should handle 'size' (a1)
                        if (tf->a1 > 0) {
                             char *buf = (char *)tf->a0;
                             // Just read one char for simple shell support
                             // (Or implement a loop to read a1 bytes)
                             *buf = uart_getc(); 
                             tf->a0 = 1; // Return 1 byte read
                        } else {
                             tf->a0 = 0;
                        }
                        break;

                    case SYS_UART_WRITE: // 2
                        // Used by user-space printk
                        // a0 = buffer, a1 = size
                        uart_puts((char *)tf->a0);
                        tf->a0 = tf->a1; // Return count
                        break;

                    case SYS_EXEC: // 3
                        // Lab 5: Load a new program
                        printk("[Syscall] exec('%s') not implemented yet\n", (char *)tf->a0);
                        tf->a0 = -1;
                        break;

                    case SYS_FORK: // 4
                        tf->a0 = do_fork();
                        break;

                    case SYS_EXIT: // 5
                        do_exit(tf->a0);
                        break;

                    case SYS_STOP: // 6
                        // Often mapped to 'mbox_call' in some lab variations, 
                        // or used to stop a specific process.
                        printk("[Syscall] stop/mbox_call not implemented yet\n");
                        tf->a0 = 0;
                        break;

                    case SYS_DISPLAY: // 7
                        // Lab 8: Framebuffer display
                        printk("[Syscall] display() not implemented yet\n");
                        tf->a0 = 0;
                        break;
                    
                    case SYS_USLEEP: // 8
                        // Lab 6: Sleep for microseconds
                         printk("[Syscall] usleep() not implemented yet\n");
                        tf->a0 = 0;
                        break;

                    case SYS_SIGNAL: // 9
                        // Lab 6: Register signal handler
                        printk("[Syscall] signal() not implemented yet\n");
                        tf->a0 = -1;
                        break;

                    case SYS_SIGRETURN: // 10
                        // Lab 6: Return from signal handler
                        printk("[Syscall] sigreturn() not implemented yet\n");
                        tf->a0 = 0;
                        break;

                    case SYS_KILL: // 11
                        // You need to implement do_kill(pid) in sched.c
                        // tf->a0 = do_kill((int)tf->a0); 
                        printk("[Syscall] kill(%d) stub - implement do_kill in sched.c!\n", (int)tf->a0);
                        tf->a0 = -1; 
                        break;

                    case SYS_MMAP: // 12
                         printk("[Syscall] mmap() not implemented yet\n");
                         tf->a0 = 0;
                         break;

                    // --- File System Calls (Lab 7/8) ---
                    case SYS_OPEN:  // 13
                    case SYS_CLOSE: // 14
                    case SYS_READ:  // 15
                    case SYS_WRITE: // 16 (File write, distinct from UART)
                    case SYS_MKDIR: // 17
                    case SYS_MOUNT: // 18
                    case SYS_CHDIR: // 19
                    case SYS_LSEEK: // 20
                    case SYS_IOCTL: // 21
                        printk("[Syscall] FileSystem call #%d not implemented yet\n", tf->a7);
                        tf->a0 = -1;
                        break;

                    default:
                        printk("[Trap] Unknown Syscall ID: %d\n", tf->a7);
                        tf->a0 = -1; // Return Error Code
                        break;
                }
                break;
            }
    }
}