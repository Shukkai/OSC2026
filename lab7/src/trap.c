#include "trap.h"
#include "uart.h"
#include "printk.h"
#include <stddef.h>
#include "timer.h" 
#include "plic.h"
#include "task.h"
#include "sys.h"
#include "sched.h"
#include "signal.h"
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
                // 1. Advance the Program Counter to the next instruction
                // Otherwise, 'sret' returns to 'ecall', causing an infinite loop.
                tf->sepc += 4;

                // 2. Dispatch Syscall based on the a7 register
                switch (tf->a7) {
                    
                    // --- CATEGORY 1: PROCESS MANAGEMENT ---
                    case SYS_GETPID:
                        tf->a0 = current->pid;
                        break;

                    case SYS_FORK:
                        tf->a0 = do_fork();
                        break;

                    case SYS_WAITPID:
                        tf->a0 = do_waitpid(tf->a0);
                        break;

                    case SYS_EXEC:
                        tf->a0 = do_exec((const char *)tf->a0, tf);
                        break;

                    case SYS_EXIT:
                        do_exit((int)tf->a0);
                        break;

                    case SYS_KILL: 
                        // Immediate, forced termination (The 'stop' command)
                        {
                            struct task_struct *target = find_task_by_pid((int)tf->a0);
                            if (target) {
                                target->state = TASK_ZOMBIE; 
                                tf->a0 = 0; 
                            } else {
                                tf->a0 = -1; 
                            }
                        }
                        break;

                    // --- CATEGORY 2: I/O & DISPLAY ---
                    case SYS_UART_READ:
                        tf->a0 = do_uart_read((char *)tf->a0, tf->a1);
                        break;

                    case SYS_UART_WRITE:
                        tf->a0 = do_uart_write((const char *)tf->a0, tf->a1);
                        break;

                    case SYS_DISPLAY:
                        // Temporarily allow Kernel to access User-space memory (SUM bit)
                        asm("li t0, (1 << 18); csrs sstatus, t0;");
                        do_display((unsigned int *)tf->a0, (unsigned int)tf->a1, (unsigned int)tf->a2);
                        asm("li t0, (1 << 18); csrc sstatus, t0;");
                        tf->a0 = 0;
                        break;

                    case SYS_USLEEP:
                        do_usleep((unsigned int)tf->a0);
                        tf->a0 = 0;
                        break;

                    // --- CATEGORY 3: SIGNALS ---
                    case SYS_SIGNAL: 
                        tf->a0 = do_signal((int)tf->a0, (void (*)(void))tf->a1);
                        break;

                    case SYS_SIG_KILL:
                        // Polite signal delivery (The 'kill' command)
                        tf->a0 = do_kill((int)tf->a0, (int)tf->a1);
                        break;

                    case SYS_SIGRETURN: 
                        // Restore previous context; tf->a0 is set inside do_sigreturn
                        tf->a0 = do_sigreturn(tf);
                        break;

                    // --- CATEGORY 4: MEMORY MANAGEMENT ---
                    case SYS_MMAP:
                        tf->a0 = do_mmap(tf->a0, tf->a1, tf->a2, tf->a3);
                        break;

                    // --- CATEGORY 5: FILESYSTEM ---
                    case SYS_OPEN:
                        tf->a0 = do_open((const char *)tf->a0, (int)tf->a1);
                        break;
                    case SYS_CLOSE:
                        tf->a0 = do_close((int)tf->a0);
                        break;
                    case SYS_READ:
                        tf->a0 = do_read((int)tf->a0, (void *)tf->a1, (unsigned long)tf->a2);
                        break;
                    case SYS_WRITE:
                        tf->a0 = do_write((int)tf->a0, (const void *)tf->a1, (unsigned long)tf->a2);
                        break;
                    case SYS_MKDIR:
                        tf->a0 = do_mkdir((const char *)tf->a0, (unsigned)tf->a1);
                        break;
                    case SYS_MOUNT:
                        tf->a0 = do_mount((const char *)tf->a1, (const char *)tf->a2);
                        break;
                    case SYS_CHDIR:
                        tf->a0 = do_chdir((const char *)tf->a0);
                        break;
                    case SYS_LSEEK:
                        tf->a0 = do_lseek((int)tf->a0, (long)tf->a1, (int)tf->a2);
                        break;
                    case SYS_IOCTL:
                        // ioctl arg is a userspace pointer; allow kernel access
                        asm("li t0, (1 << 18); csrs sstatus, t0;");
                        tf->a0 = do_ioctl((int)tf->a0, (unsigned long)tf->a1, (void *)tf->a2);
                        asm("li t0, (1 << 18); csrc sstatus, t0;");
                        break;

                    default:
                        uart_puts("Unknown syscall: "); uart_hex(tf->a7); uart_puts("\n");
                        tf->a0 = -1;
                        break;
                }
                break;
            // ----------------------------------------------------
            // 2. HARDWARE PAGE FAULTS (The CPU tripped an alarm)
            // ----------------------------------------------------
            case 12: // Instruction Page Fault
            case 13: // Load Page Fault
            case 15: // Store Page Fault
                handle_page_fault(tf, exception_code);
                break;
            default:
                // Use polling UART functions so it prints even with interrupts off
                uart_puts("\n[Kernel Panic] Unhandled Synchronous Exception!\n");
                uart_puts("Exception Code: "); uart_hex(exception_code); uart_puts("\n");
                uart_puts("sepc: 0x"); uart_hex(tf->sepc); uart_puts("\n");
                uart_puts("stval: 0x"); uart_hex(tf->stval); uart_puts("\n");
                disable_interrupt();
                while(1); // Halt the system
                break;
        }
    }
    handle_signal(tf);
}
