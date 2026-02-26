#include "uart.h"
#include "printk.h"
#include "trap.h"
#include "sched.h"
unsigned long uart_base_addr = DEFAULT_UART_BASE;

// ====================================================================
// RING BUFFERS
// ====================================================================
static char rx_buffer[UART_BUF_SIZE];
static volatile int rx_head = 0; // Write (ISR)
static volatile int rx_tail = 0; // Read (User)

static char tx_buffer[UART_BUF_SIZE];
static volatile int tx_head = 0; // Write (User)
static volatile int tx_tail = 0; // Read (ISR)

#define BUF_NEXT(i)   ((i + 1) % UART_BUF_SIZE)
#define RX_EMPTY()    (rx_head == rx_tail)
#define TX_EMPTY()    (tx_head == tx_tail)

// ====================================================================
// EXISTING BASIC FUNCTIONS
// ====================================================================

void uart_set_base(unsigned long addr) {
    uart_base_addr = addr;
}

void uart_enable_interrupt() {
    // Enable RX Interrupts ONLY at start
    // We do NOT enable TX interrupts until we actually have data to send.
    *UART_IER |= IER_RX_ENABLE; 
    
    // Enable OUT2 (Required for some 16550 implementations like QEMU)
    *UART_MCR |= (1 << 3); 
}

void uart_disable_interrupt() {
    *UART_IER &= ~(IER_RX_ENABLE | IER_TX_ENABLE);
    *UART_MCR &= ~(1 << 3);
}

void uart_init() {
    uart_disable_interrupt();
}

// char uart_getc() {
//     while ((*UART_LSR & 0x01) == 0);
//     char c = (char)*UART_RBR;
//     return c == '\r' ? '\n' : c;
// }

char uart_getc() {
    char c;
    // Keep yielding the CPU until the smart reader finds a character
    while (!uart_getc_nonblocking(&c)) {
        schedule();
    }
    return c;
}

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    while ((*UART_LSR & 0x20) == 0);
    *UART_THR = c;
}

// void uart_puts(const char *s) {
//     while (*s) uart_putc(*s++);
// }

void uart_puts(const char *s) {
    // Check if we are in a context where we can use interrupts
    // (sstatus.SIE is bit 1)
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    if (sstatus & SSTATUS_SIE) {
        // Safe to use the buffered async version
        uart_puts_async(s);
    } else {
        // Interrupts are off (Early boot or Panic), use polling
        while (*s) {
             uart_putc(*s++); // The raw blocking version
        }
    }
}

void uart_puts_async(const char *s) {
    while (*s) uart_putc_buffered(*s++);
}


void uart_hex(unsigned long h) {
    char buf[20]; // Buffer for "0x" + 16 digits + null terminator
    int idx = 0;

    // 1. Build the string locally (This is fast and doesn't need locking)
    buf[idx++] = '0';
    buf[idx++] = 'x';
    
    for (int i = 60; i >= 0; i -= 4) {
        int n = (h >> i) & 0xF;
        buf[idx++] = (n > 9) ? (n + 0x37) : (n + '0');
    }
    buf[idx] = '\0'; // Null terminate

    uart_puts(buf);
}

char uart_getc_raw() {
    while ((*UART_LSR & 0x01) == 0);
    char c = (char)*UART_RBR;
    return c;
}


// --- Internal ISR Handlers ---

static void uart_handle_rx() {
    // Read while data is ready
    while ((*UART_LSR & 0x01)) {
        char c = (char)*UART_RBR; // Read HW (clears IRQ)

        int next = BUF_NEXT(rx_head);
        if (next != rx_tail) { // Store if not full
            rx_buffer[rx_head] = c;
            rx_head = next;
        }
        // else: Overflow, drop character
    }
}

static void uart_handle_tx() {
    // If buffer is empty, we MUST disable TX interrupt
    if (TX_EMPTY()) {
        *UART_IER &= ~IER_TX_ENABLE;
        return;
    }

    // Fill hardware FIFO while allowed (LSR Bit 5 = Empty)
    while ((*UART_LSR & 0x20) && !TX_EMPTY()) {
        *UART_THR = tx_buffer[tx_tail];
        tx_tail = BUF_NEXT(tx_tail);
        // --- DEBUG: VISUAL PROOF ---
        // while ((*UART_LSR & 0x20) == 0); 
        // *UART_THR = '~'; 
        // ---------------------------
    }
}

// --- Top Level ISR (Call this from trap.c) ---

void uart_isr() {
    while (1) {
        unsigned int iir = *UART_IIR;
        
        // If Bit 0 is 1, no interrupt is pending
        if (iir & IIR_NO_INT) break;

        // Check Interrupt ID
        int id = iir & IIR_ID_MASK;

        if (id == IIR_RX_RDY || id == IIR_RX_TIMEOUT) {
            uart_handle_rx();
        }
        else if (id == IIR_TX_EMPTY) {
            uart_handle_tx();
        }
    }
}

// --- User Facing Buffered Functions ---

// REPLACES: uart_getc() for Shell
char uart_getc_buffered() {
    // 1. Wait if buffer is empty
    while (RX_EMPTY()) {
        asm volatile("wfi"); // Optional: Wait For Interrupt
    }

    // 2. Read from buffer
    char c = rx_buffer[rx_tail];
    rx_tail = BUF_NEXT(rx_tail);
    
    return (c == '\r') ? '\n' : c;
}

void uart_putc_buffered(char c) {
    if (c == '\n') {
        uart_putc_buffered('\r');
    }

    // 1. SAVE current interrupt state
    // We need to know if interrupts were ON or OFF before we started.
    unsigned long flags;
    asm volatile("csrr %0, sstatus" : "=r"(flags));

    // 2. DISABLE Interrupts (Critical Section Start)
    // This prevents:
    //  a) Race conditions between Main Thread and Task Thread writing to tx_buffer.
    //  b) Race conditions with the ISR modifying IER.
    disable_interrupt(); 

    int next = BUF_NEXT(tx_head);

    // 3. Wait if buffer is full
    while (next == tx_tail) {
        // If interrupts were originally ON, we MUST enable them here
        // to allow the ISR to fire and drain the buffer.
        if (flags & SSTATUS_SIE) {
            enable_interrupt();
            asm volatile("nop"); // Wait for ISR
            disable_interrupt(); // Lock again immediately
        } else {
            // DEADLOCK PREVENTION:
            // If we are here, interrupts were OFF globally (e.g., inside an ISR or Early Boot).
            // We cannot wait for an interrupt that will never come.
            // We must poll the hardware manually to drain space.
            if (*UART_LSR & 0x20) { 
                 *UART_THR = tx_buffer[tx_tail]; // Force write to HW
                 tx_tail = BUF_NEXT(tx_tail);    // Make space
            }
        }
    }

    // 4. Write to buffer (Safe now)
    tx_buffer[tx_head] = c;
    tx_head = next;

    // 5. Ensure TX Interrupt is ON
    if (!(*UART_IER & IER_TX_ENABLE)) {
        *UART_IER |= IER_TX_ENABLE;
    }

    // 6. RESTORE Interrupt State (Critical Section End)
    // Only re-enable if they were enabled when we entered.
    if (flags & SSTATUS_SIE) {
        enable_interrupt();
    }
}

// Returns 1 if data was read, 0 if completely empty
int uart_getc_nonblocking(char *out_char) {
    // 1. Check the software buffer (The ISR grabbed the key)
    if (!RX_EMPTY()) {
        char c = rx_buffer[rx_tail];
        rx_tail = BUF_NEXT(rx_tail);
        *out_char = (c == '\r') ? '\n' : c;
        return 1; // Success
    }
    
    // 2. Check the hardware directly (Interrupts are currently disabled)
    if (*UART_LSR & 0x01) {
        char c = (char)*UART_RBR;
        *out_char = (c == '\r') ? '\n' : c;
        return 1; // Success
    }
    
    return 0; // Nothing available anywhere
}