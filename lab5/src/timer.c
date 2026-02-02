/* src/timer.c */
#include "timer.h"
#include "mm.h"
#include "sbi.h"
#include "uart.h"
#include "fdt.h"   /* For fdt_get_timebase */
#include "utils.h" /* For kmalloc/kfree/bswap32 */
#include "printk.h"
#include "task.h"
/* Global Timer Queue */
static struct list_head timer_queue;
static unsigned long timebase_freq; 

/* Helper: Read 'rdtime' register */
static inline unsigned long get_cycles(void) {
    unsigned long n;
    asm volatile("rdtime %0" : "=r"(n));
    return n;
}

/* Enable Supervisor Timer Interrupt (sie.STIE) */
void enable_timer_interrupt(void) {
    asm volatile("li t0, (1 << 5); csrs sie, t0;" ::: "t0");
}

/* Disable Supervisor Timer Interrupt (sie.STIE) */
void disable_timer_interrupt(void) {
    asm volatile("li t0, (1 << 5); csrc sie, t0;" ::: "t0");
}

/* 1. Initialization */
void timer_init(void *dtb) {
    INIT_LIST_HEAD(&timer_queue);

    // Try to get frequency from DTB first
    timebase_freq = fdt_get_timebase(dtb);

    // Fallback if not found in DTB
    if (timebase_freq == 0) {
        timebase_freq = TIME_FREQ; // Macro from timer.h
        uart_puts("[Timer] Warning: using default fallback frequency\n");
    } else {
        uart_puts("[Timer] Frequency loaded from DTB\n");
    }
    uart_puts("Timer base: ");
    uart_hex(timebase_freq);
    uart_puts("\n");
    // Schedule the first interrupt (1 second from now)
    sbi_set_timer(get_cycles() + timebase_freq);
    
    // Enable Interrupts to start the heartbeat
    enable_timer_interrupt();
}

/* 2. Uptime in Seconds */
unsigned long get_uptime(void) {
    return get_cycles() / timebase_freq;
}

/* 3. Add Timer (Thread-Safe / Critical Section) */
void timer_add(void (*callback)(void *), void *arg, int after) {
    struct timer *t = (struct timer *)kmalloc(sizeof(struct timer));
    if (!t) return;

    t->func = callback;
    t->arg = arg;
    t->time = get_uptime() + after; // Expiration in seconds
    INIT_LIST_HEAD(&t->list);

    // CRITICAL SECTION START
    disable_timer_interrupt();

    struct list_head *pos;
    struct timer *curr;
    int inserted = 0;
    int is_earliest = 0; // Flag to track if we need to reprogram hardware

    if (list_empty(&timer_queue)) {
        // Case 1: List was empty, this is definitely the earliest
        list_add(&t->list, &timer_queue);
        inserted = 1;
        is_earliest = 1;
    } else {
        // Case 2: Sorted Insertion
        list_for_each(pos, &timer_queue) {
            curr = list_entry(pos, struct timer, list);
            if (curr->time > t->time) {
                // We found a timer that expires LATER than the new one.
                // Insert 't' BEFORE 'curr'.
                
                // Check if 'curr' was the head. If so, 't' becomes the new head.
                if (pos == timer_queue.next) {
                    is_earliest = 1;
                }
                
                list_add_tail(&t->list, pos); // Insert before 'pos'
                inserted = 1;
                break;
            }
        }
        
        // Case 3: Insert at the end (not the earliest)
        if (!inserted) {
            list_add_tail(&t->list, &timer_queue);
        }
    }

    // [MULTIPLEXING LOGIC] 
    // If the new timer is the soonest one, we must update mtimecmp immediately.
    // Otherwise, we wait for the currently scheduled interrupt.
    if (is_earliest) {
        sbi_set_timer(t->time * timebase_freq);
    }

    // CRITICAL SECTION END
    enable_timer_interrupt();
}

/* 4. Public API: Set Timeout */
void set_timeout(const char *message, int after) {
    timer_add((void (*)(void *))uart_puts, (void *)message, after);
}

/* 5. Interrupt Handler with Dynamic Multiplexing */
void timer_irq_handler(void) {
    // uart_putc('.');
    struct timer *t;
    unsigned long current_time = get_uptime(); // In Seconds

    // A. Process ALL Expired Timers
    while (!list_empty(&timer_queue)) {
        t = list_first_entry(&timer_queue, struct timer, list);

        // Since the list is sorted, if the Head is in the future, 
        // then EVERYTHING else is also in the future. We can stop.
        if (t->time > current_time) {
            break;
        }

        // Execute Callback
        if (t->func) {
            // t->func(t->arg);
            task_add(t->func, t->arg, PRIORITY_NORMAL);
        }

        // Remove from list and Free Memory
        list_del_init(&t->list);
        kfree(t);
    }

    // B. Schedule NEXT Interrupt (Multiplexing)
    if (!list_empty(&timer_queue)) {
        // 1. Peek at the next timer
        struct timer *next_t = list_first_entry(&timer_queue, struct timer, list);
        
        // 2. Reprogram mtimecmp to wake up exactly at that time
        //    (Convert seconds back to ticks)
        sbi_set_timer(next_t->time * timebase_freq);
    } else {
        // 3. Queue is empty. No interrupts needed.
        //    Set timer to max (UINT64_MAX) to effectively sleep until timer_add is called.
        sbi_set_timer(-1ULL); 
    }
    
    // C. Ensure Interrupts are enabled for the next shot
    enable_timer_interrupt();
}

/* 6. Busy Wait Sleep (Polled, does not use interrupts) */
void sleep(int msec) {
    unsigned long t1 = get_cycles();
    unsigned long t2;
    unsigned long ticks_needed = msec * (timebase_freq / 1000);

    do {
        t2 = get_cycles();
    } while ((t2 - t1) < ticks_needed);
}