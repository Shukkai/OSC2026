#include "uart.h"
#include "printk.h"
#include "trap.h"
#include "sched.h"
#include "vm.h"

unsigned long uart_base_addr = DEFAULT_UART_BASE + PAGE_OFFSET;

// ====================================================================
// Ring Buffers
// ====================================================================
static char rx_buffer[UART_BUF_SIZE];
static volatile int rx_head = 0;
static volatile int rx_tail = 0;

static char tx_buffer[UART_BUF_SIZE];
static volatile int tx_head = 0;
static volatile int tx_tail = 0;

#define BUF_NEXT(i)  ((i + 1) % UART_BUF_SIZE)
#define RX_EMPTY()   (rx_head == rx_tail)
#define TX_EMPTY()   (tx_head == tx_tail)

// ====================================================================
// Hardware Setup
// ====================================================================

void uart_set_base(unsigned long addr) {
    uart_base_addr = phys_to_virt(addr);
}

void uart_init(void) {
    *UART_IER &= ~(IER_RX_ENABLE | IER_TX_ENABLE);
    *UART_MCR &= ~(1 << 3);
}

void uart_enable_interrupt(void) {
    *UART_IER |= IER_RX_ENABLE;
    *UART_MCR |= (1 << 3);  // OUT2 — required for QEMU 16550
}

void uart_disable_interrupt(void) {
    *UART_IER &= ~(IER_RX_ENABLE | IER_TX_ENABLE);
    *UART_MCR &= ~(1 << 3);
}

// ====================================================================
// Polling I/O (safe in any context: early boot, panic, ISR)
// ====================================================================

void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    while ((*UART_LSR & 0x20) == 0);
    *UART_THR = c;
}

char uart_getc_sync(void) {
    while ((*UART_LSR & 0x01) == 0);
    char c = (char)*UART_RBR;
    return (c == '\r') ? '\n' : c;
}

char uart_getc_raw(void) {
    while ((*UART_LSR & 0x01) == 0);
    return (char)*UART_RBR;
}

// ====================================================================
// ISR Handlers (called from uart_isr → plic_irq_handler)
// ====================================================================

static void uart_handle_rx(void) {
    while (*UART_LSR & 0x01) {
        char c = (char)*UART_RBR;
        int next = BUF_NEXT(rx_head);
        if (next != rx_tail) {
            rx_buffer[rx_head] = c;
            rx_head = next;
        }
    }
}

static void uart_handle_tx(void) {
    if (TX_EMPTY()) {
        *UART_IER &= ~IER_TX_ENABLE;
        return;
    }
    while ((*UART_LSR & 0x20) && !TX_EMPTY()) {
        *UART_THR = tx_buffer[tx_tail];
        tx_tail = BUF_NEXT(tx_tail);
    }
}

void uart_isr(void) {
    while (1) {
        unsigned int iir = *UART_IIR;
        if (iir & IIR_NO_INT) break;

        int id = iir & IIR_ID_MASK;
        if (id == IIR_RX_RDY || id == IIR_RX_TIMEOUT)
            uart_handle_rx();
        else if (id == IIR_TX_EMPTY)
            uart_handle_tx();
    }
}

// ====================================================================
// Buffered TX — interrupt-driven, with polling fallback
// ====================================================================

void uart_putc_buffered(char c) {
    if (c == '\n')
        uart_putc_buffered('\r');

    unsigned long flags;
    asm volatile("csrr %0, sstatus" : "=r"(flags));
    disable_interrupt();

    int next = BUF_NEXT(tx_head);

    // Back-pressure: wait for space in the ring buffer
    while (next == tx_tail) {
        if (flags & SSTATUS_SIE) {
            // Interrupts were on — briefly re-enable so ISR can drain
            enable_interrupt();
            asm volatile("nop");
            disable_interrupt();
        } else {
            // Interrupts were off — poll-drain one byte manually
            if (*UART_LSR & 0x20) {
                *UART_THR = tx_buffer[tx_tail];
                tx_tail = BUF_NEXT(tx_tail);
            }
        }
    }

    // Enqueue
    tx_buffer[tx_head] = c;
    tx_head = next;

    // Kick-start: push first byte to HW + enable TX IRQ
    if (!(*UART_IER & IER_TX_ENABLE)) {
        if ((*UART_LSR & 0x20) && !TX_EMPTY()) {
            *UART_THR = tx_buffer[tx_tail];
            tx_tail = BUF_NEXT(tx_tail);
        }
        *UART_IER |= IER_TX_ENABLE;
    }

    if (flags & SSTATUS_SIE)
        enable_interrupt();
}

// ====================================================================
// TX Flush — poll-drain everything remaining in the buffer
// ====================================================================

void uart_flush_tx(void) {
    // Disable TX interrupt to avoid racing with ISR
    *UART_IER &= ~IER_TX_ENABLE;

    while (!TX_EMPTY()) {
        while ((*UART_LSR & 0x20) == 0);  // wait for HW ready
        *UART_THR = tx_buffer[tx_tail];
        tx_tail = BUF_NEXT(tx_tail);
    }

    // Wait for the last byte to leave the shift register
    while ((*UART_LSR & 0x40) == 0);
}

// ====================================================================
// Buffered RX
// ====================================================================

char uart_getc_buffered(void) {
    while (RX_EMPTY())
        asm volatile("wfi");

    char c = rx_buffer[rx_tail];
    rx_tail = BUF_NEXT(rx_tail);
    return (c == '\r') ? '\n' : c;
}

int uart_getc_nonblocking(char *out_char) {
    // 1. Software buffer first
    if (!RX_EMPTY()) {
        char c = rx_buffer[rx_tail];
        rx_tail = BUF_NEXT(rx_tail);
        *out_char = (c == '\r') ? '\n' : c;
        return 1;
    }
    // 2. Hardware register fallback
    if (*UART_LSR & 0x01) {
        char c = (char)*UART_RBR;
        *out_char = (c == '\r') ? '\n' : c;
        return 1;
    }
    return 0;
}

// ====================================================================
// Yielding RX — for kernel threads (shell)
// ====================================================================

char uart_getc(void) {
    char c;
    while (!uart_getc_nonblocking(&c))
        schedule();
    return c;
}

// ====================================================================
// High-level output — auto-selects polling vs buffered
// ====================================================================

void uart_puts_async(const char *s) {
    while (*s) uart_putc_buffered(*s++);
}

void uart_puts(const char *s) {
    unsigned long sstatus;
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));

    if (sstatus & SSTATUS_SIE)
        uart_puts_async(s);
    else
        while (*s) uart_putc(*s++);
}

void uart_hex(unsigned long h) {
    char buf[20];
    int idx = 0;

    buf[idx++] = '0';
    buf[idx++] = 'x';
    for (int i = 60; i >= 0; i -= 4) {
        int n = (h >> i) & 0xF;
        buf[idx++] = (n > 9) ? (n + 0x37) : (n + '0');
    }
    buf[idx] = '\0';
    uart_puts(buf);
}