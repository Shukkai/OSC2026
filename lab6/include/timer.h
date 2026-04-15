/* include/timer.h */
#pragma once

#include "list.h"
#include <stdint.h>

/* Default Fallbacks */
#ifdef __QEMU__
    #define TIME_FREQ 10000000
#else
    #define TIME_FREQ 24000000
#endif

struct timer {
    struct list_head list;       /* Linked list node */
    void (*func)(void *arg);     /* Callback function */
    void *arg;                   /* Argument for callback */
    unsigned long time;          /* Expiration time (in seconds) */
};

/* Core Functions */
void timer_init(void *dtb);
void timer_irq_handler(void);

/* Control API */
void enable_timer_interrupt(void);
void disable_timer_interrupt(void);

/* Time Queries */
unsigned long get_uptime(void);

/* User API */
void timer_add(void (*callback)(void *), void *arg, int after);
void set_timeout(const char *message, int after);
void sleep(int msec);
void kernel_usleep(unsigned int usec);