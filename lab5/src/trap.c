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

void check_signals(struct TrapFrame *tf) {
    // Don't process signals if there is no current task, 
    // or if we are already inside a signal handler!
    if (!current || current->in_signal_handler) return;

    if (current->pending_signals) {
        for (int i = 0; i < MAX_SIG; i++) {
            if (current->pending_signals & (1 << i)) {
                // We found a pending signal! Clear the flag immediately.
                current->pending_signals &= ~(1 << i); 

                if (current->signal_handler[i]) {
                    // --- A CUSTOM HANDLER EXISTS ---
                    
                    // 1. Save original context
                    current->saved_tf = *tf;
                    current->in_signal_handler = 1;

                    // 2. Inject the Trampoline onto the User Stack
                    tf->sp = (tf->sp - 8) & ~15UL; 
                    uint32_t *trampoline = (uint32_t *)tf->sp;
                    
                    asm volatile("li t0, (1 << 18); csrs sstatus, t0;");
                    trampoline[0] = 0x00a00893; // li a7, 10 (SYS_SIGRETURN)
                    trampoline[1] = 0x00000073; // ecall
                    asm volatile("li t0, (1 << 18); csrc sstatus, t0;");

                    asm volatile("fence.i");

                    // 3. Force CPU to jump to handler
                    tf->sepc = (uint64_t)current->signal_handler[i];
                    tf->ra = tf->sp;
                    
                } else {
                    // --- NO HANDLER EXISTS ---
                    // Make it conditional: Only kill the process if the signal is SIGTERM (15)!
                    if (i == 15) {
                        do_exit(0);
                    }
                }
                break; // Process one signal per trap
            }
        }
    }
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
                        tf->a0 = do_uart_read((char *)tf->a0, tf->a1);
                        break;

                    case SYS_UART_WRITE: // 2
                        tf->a0 = do_uart_write((const char *)tf->a0, tf->a1);
                        break;

                    case SYS_EXEC: // 3
                        // tf->a0 contains the string pointer passed from user space
                        tf->a0 = do_exec((const char *)tf->a0, tf);
                        break;

                    case SYS_FORK: // 4
                        // uart_puts("do fork() called from syscall handler\n");
                        tf->a0 = do_fork();
                        break;

                    case SYS_EXIT: // 5
                        // uart_puts("\n>>> SUCCESS: User Program called SYS_EXIT! <<<\n");
                        do_exit(tf->a0);
                        break;


                    case SYS_KILL: // 6
                        // The 'stop' command: Forcefully terminate the process immediately!
                        {
                            struct task_struct *target = find_task_by_pid((int)tf->a0);
                            if (target) {
                                // Bypass signals entirely and turn it into a Zombie!
                                target->state = TASK_ZOMBIE; 
                                tf->a0 = 0; // Success
                            } else {
                                tf->a0 = -1; // Process not found
                            }
                        }
                        break;

                    case SYS_DISPLAY: // 7
                        // uart_puts("[Syscall] display() called\n");
                        
                        // Allow Kernel to read User memory buffer
                        asm("li t0, (1 << 18); csrs sstatus, t0;");
                        
                        do_display((unsigned int *)tf->a0, (unsigned int)tf->a1, (unsigned int)tf->a2);
                        
                        // Disable User memory access for security
                        asm("li t0, (1 << 18); csrc sstatus, t0;");
                        
                        tf->a0 = 0;
                        break;
                    
                    case SYS_USLEEP: // 8
                        // tf->a0 = microseconds
                        do_usleep((unsigned int)tf->a0);
                        // uart_puts("[Syscall] usleep() called with "); uart_hex(tf->a0); uart_puts(" usec\n");
                        tf->a0 = 0; // Return success
                        break;

                    case SYS_SIGNAL: 
                        // ACTUALLY SAVE THE HANDLER:
                        current->signal_handler[tf->a0] = (void (*)(void))tf->a1;
                        tf->a0 = 0;
                        break;

                    case SYS_SIGRETURN: 
                        // ACTUALLY RESTORE THE STATE:
                        *tf = current->saved_tf;
                        current->in_signal_handler = 0;
                        break;

                    case SYS_SIG_KILL: // 11
                         // The 'kill' command: Send a polite signal to the target process
                        {
                            // tf->a0 = pid, tf->a1 = signal number
                            struct task_struct *target = find_task_by_pid((int)tf->a0);
                            if (target) {
                                // Just set the flag! The process will handle it later.
                                target->pending_signals |= (1 << tf->a1);
                                tf->a0 = 0; // Success
                            } else {
                                tf->a0 = -1; // Process not found
                            }
                        }
                        break;
                    case SYS_MMAP: // 12

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
                        uart_puts("Unknown syscall: "); uart_hex(tf->a7); uart_puts("\n");
                        tf->a0 = -1; // Return Error Code
                        break;
                }
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
    check_signals(tf);
}