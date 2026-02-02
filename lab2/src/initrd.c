#include "initrd.h"
#include "fdt.h"
#include "uart.h"
#include "string.h"
#include "utils.h" /* Include the new utils */

extern unsigned long DTB_BASE;

/* Remove local parse_hex8 and align_up implementations */

void *get_initrd_base() {
    int chosen_offset = fdt_path_offset((void *)DTB_BASE, "/chosen");
    if (chosen_offset < 0) return NULL;

    int len;
    const void *prop = fdt_getprop((void *)DTB_BASE, chosen_offset, "linux,initrd-start", &len);
    if (!prop) return NULL;

    if (len == 8) {
        const uint32_t *val = (const uint32_t *)prop;
        /* USE bswap32 from utils.h instead of __builtin */
        uint64_t addr = ((uint64_t)bswap32(val[0]) << 32) | bswap32(val[1]);
        return (void *)addr;
    } 
    else if (len == 4) {
        /* USE bswap32 from utils.h */
        uint32_t val = bswap32(*(uint32_t *)prop);
        return (void *)(uintptr_t)val;
    }
    return NULL;
}

void initrd_list() {
    char *archive = (char *)get_initrd_base();
    if (!archive) {
        uart_puts("Error: Initrd not found in DTB.\n");
        return;
    }

    char *p = archive;
    
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
        if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
            uart_puts("Error: Invalid CPIO Magic\n");
            return;
        }

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);
        
        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uart_puts(filename);
        uart_puts("\n");

        uintptr_t next = (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize;
        next = align_up(next, 4); /* Align to 4 bytes */
        next += filesize;
        next = align_up(next, 4); /* Align to 4 bytes */
        p = (char *)next;
    }
}

void initrd_cat(const char *target_filename) {
    char *archive = (char *)get_initrd_base();
    if (!archive) {
        uart_puts("Error: Initrd not found.\n");
        return;
    }

    char *p = archive;
    
    while (1) {
        struct cpio_newc_header *header = (struct cpio_newc_header *)p;
        
        if (strncmp(header->c_magic, CPIO_NEWC_MAGIC, 6) != 0) return;

        unsigned long namesize = parse_hex8(header->c_namesize);
        unsigned long filesize = parse_hex8(header->c_filesize);
        char *filename = p + sizeof(struct cpio_newc_header);

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uintptr_t content_start = (uintptr_t)p + sizeof(struct cpio_newc_header) + namesize;
        content_start = align_up(content_start, 4);
        
        if (strcmp(filename, target_filename) == 0) {
            char *content = (char *)content_start;
            for (unsigned long i = 0; i < filesize; i++) {
                uart_putc(content[i]);
            }
            uart_puts("\n");
            return;
        }

        uintptr_t next = content_start + filesize;
        next = align_up(next, 4);
        p = (char *)next;
    }
    uart_puts("File not found.\n");
}