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