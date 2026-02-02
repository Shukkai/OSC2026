#include "fdt.h"
#include "uart.h"
#include "string.h"
#include "printk.h"

extern unsigned long DTB_BASE;

static uint32_t bswap(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | 
           ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
}

static uintptr_t fdt_align(uintptr_t addr) {
    return (addr + 3) & ~(uintptr_t)3;
}

void fdt_parse(int search_type, void *result_ptr) {
    if (!DTB_BASE) return;

    struct fdt_header *header = (struct fdt_header *)DTB_BASE;
    if (bswap(header->magic) != FDT_MAGIC) {
        uart_puts("FDT: Invalid Magic\n");
        return;
    }

    uint8_t *p = (uint8_t *)DTB_BASE + bswap(header->off_dt_struct);
    char *strings = (char *)DTB_BASE + bswap(header->off_dt_strings);
    uint8_t *end = p + bswap(header->size_dt_struct);

    int node_compatible_match = 0;
    uint64_t node_reg_addr = 0;

    while (p < end) {
        uint32_t token = bswap(*(uint32_t *)p);
        p += 4;

        switch (token) {
            case FDT_BEGIN_NODE: {
                node_compatible_match = 0;
                node_reg_addr = 0;
                char *name = (char *)p;
                p = (uint8_t *)fdt_align((uintptr_t)(name + strlen(name) + 1));
                break;
            }

            case FDT_PROP: {
                uint32_t len = bswap(*(uint32_t *)p);
                uint32_t nameoff = bswap(*(uint32_t *)(p + 4));
                p += 8;

                char *prop_name = strings + nameoff;
                void *prop_val = (void *)p;

                if (search_type == 1) { 
                    if (strcmp(prop_name, "model") == 0) {
                        uart_puts("Model:            ");
                        uart_puts((char *)prop_val);
                        uart_puts("\n");
                        return; 
                    }
                }
                else if (search_type == 2) {
                    if (strcmp(prop_name, "compatible") == 0) {
                        char *compat = (char *)prop_val;
                        uint32_t scanned = 0;
                        while (scanned < len) {
                            /* Added "ky,pxa-uart" for Orange Pi RV2 */
                            if (strcmp(compat, "ns16550a") == 0 ||
                                strcmp(compat, "snps,dw-apb-uart") == 0 ||
                                strcmp(compat, "ky,pxa-uart") == 0) {
                                node_compatible_match = 1;
                                break;
                            }
                            int slen = strlen(compat) + 1;
                            compat += slen;
                            scanned += slen;
                        }
                    } 
                    else if (strcmp(prop_name, "reg") == 0) {
                        uint32_t *regs = (uint32_t *)prop_val;
                        uint64_t hi = bswap(regs[0]);
                        uint64_t lo = bswap(regs[1]);
                        node_reg_addr = (hi << 32) | lo;
                    }

                    if (node_compatible_match && node_reg_addr != 0) {
                        *(uint64_t *)result_ptr = node_reg_addr;
                        return; 
                    }
                }
                p = (uint8_t *)fdt_align((uintptr_t)p + len);
                break;
            }
            case FDT_END_NODE:
            case FDT_NOP:
                break;
            case FDT_END:
                return;
        }
    }
}

void fdt_print_info() {
    fdt_parse(1, 0);
}

uint64_t fdt_get_uart_base() {
    uint64_t addr = 0;
    fdt_parse(2, &addr);
    return addr;
}