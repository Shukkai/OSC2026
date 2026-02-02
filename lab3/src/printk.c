#include "printk.h"
#include "uart.h"
#include <stdarg.h>

static char digits[] = "0123456789abcdef";

static void printint(long long xx, int base, int sign)
{
    char buf[16] = { 0 };
    int i = 0;
    unsigned long long x;

    if (sign && (xx < 0))
        x = -xx;
    else
        x = xx;

    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);

    if (sign && (xx < 0))
        buf[i++] = '-';

    while (--i >= 0)
        uart_putc(buf[i]);
}

int printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            uart_putc(*fmt++);
            continue;
        }
        
        fmt++; /* skip % */
        
        if (*fmt == 'd') {
            printint(va_arg(args, int), 10, 1);
        } else if (*fmt == 'x') {
            printint(va_arg(args, int), 16, 0);
        } else if (*fmt == 's') {
            char *s = va_arg(args, char *);
            if (!s) s = "(null)";
            uart_puts(s);
        } else if (*fmt == 'c') {
            uart_putc(va_arg(args, int));
        } else if (*fmt == '%') {
            uart_putc('%');
        }
        
        fmt++;
    }
    
    va_end(args);
    return 0;
}