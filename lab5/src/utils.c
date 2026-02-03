#include "utils.h"
#include "uart.h"
#include "fdt.h"
/* Swap 32-bit integer (Big Endian <-> Little Endian) */
uint32_t bswap32(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | 
           ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
}

/* Swap 64-bit integer */
uint64_t bswap64(uint64_t x) {
    uint64_t low = bswap32(x & 0xFFFFFFFF);
    uint64_t high = bswap32(x >> 32);
    return (low << 32) | high;
}

/* Parse 8-char Hex String (e.g., "0000001A" -> 26) */
unsigned long parse_hex8(const char *s) {
    unsigned long r = 0;
    for (int i = 0; i < 8; i++) {
        char c = s[i];
        r <<= 4;
        if (c >= '0' && c <= '9') r |= c - '0';
        else if (c >= 'a' && c <= 'f') r |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') r |= c - 'A' + 10;
    }
    return r;
}

/* Align pointer up to the nearest 'align' byte boundary */
uintptr_t align_up(uintptr_t ptr, size_t align) {
    return (ptr + align - 1) & ~(align - 1);
}


int atoi(char *str)
{
    int res = 0;

    for (int i = 0; str[i] != '\0'; ++i)
    {
        if (str[i] > '9' || str[i] < '0')
            return res;
        res = res * 10 + str[i] - '0';
    }

    return res;
}

void print_dtb_info(unsigned long dtb_addr)
{
    uart_puts("DTB Address:      "); uart_hex(dtb_addr); uart_puts("\n");

    // Verify DTB is readable by fetching the Model name
    int root_offset = fdt_path_offset((void *)dtb_addr, "/");
    if (root_offset >= 0) {
        int len;
        char *model = (char *)fdt_getprop((void *)dtb_addr, root_offset, "model", &len);
        if (model) {
            uart_puts("Model:            "); uart_puts(model); uart_puts("\n");
        }
    } else {
        uart_puts("Model:            [Invalid DTB Magic or Offset]\n");
    }
}

uint64_t get_pc() {
    uint64_t pc;
    asm volatile("auipc %0, 0" : "=r"(pc));
    return pc;
}

// Simple function to convert int/long to string
void simple_itoa(long value, char *str, int base, int width) {
    char temp[32];
    int i = 0;
    int is_neg = 0;
    unsigned long uval = value;

    if (value == 0) {
        temp[i++] = '0';
    } else {
        if (base == 10 && value < 0) {
            is_neg = 1;
            uval = -value;
        }
        
        while (uval != 0) {
            int rem = uval % base;
            temp[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
            uval /= base;
        }
    }

    if (is_neg) {
        temp[i++] = '-';
    }

    // Pad with zeros or spaces if width is specified
    while (i < width) {
        temp[i++] = '0'; // padding with 0
    }

    temp[i] = '\0';

    // Reverse the string
    int start = 0; 
    int end = i - 1;
    while (start < end) {
        char t = temp[start];
        temp[start] = temp[end];
        temp[end] = t;
        start++;
        end--;
    }
    
    // Copy to output
    int j = 0;
    while (temp[j]) {
        *str++ = temp[j++];
    }
    *str = '\0';
}

// Minimal sprintf implementation
// Supports: %d (int), %p (hex ptr), %s (string), %x (hex)
void mini_sprintf(char *buf, const char *fmt, va_list args) {
    char *str = buf;
    const char *p;
    
    for (p = fmt; *p; p++) {
        if (*p != '%') {
            *str++ = *p;
            continue;
        }
        
        p++; // Skip '%'
        
        switch (*p) {
            case 'd': {
                int val = va_arg(args, int);
                simple_itoa(val, str, 10, 0);
                while (*str) str++;
                break;
            }
            case 'p': // Fallthrough for pointer/hex
            case 'x': {
                long val = va_arg(args, long);
                *str++ = '0'; *str++ = 'x'; // Add 0x prefix
                simple_itoa(val, str, 16, 8); // Simple hex
                while (*str) str++;
                break;
            }
            case 's': {
                char *s = va_arg(args, char*);
                while (*s) {
                    *str++ = *s++;
                }
                break;
            }
            default: // Unknown, just print literal
                *str++ = '%';
                *str++ = *p;
                break;
        }
    }
    *str = '\0';
}