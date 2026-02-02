#include "fdt.h"
#include "string.h"
#include "uart.h"
#include "utils.h"
/* Access the DTB_BASE set in kernel.c */
extern unsigned long DTB_BASE;

/* * fdt_totalsize:
 * Returns the total size of the DTB blob from the header.
 * Required for parsing the structure and reserving the DTB memory region.
 */
uint32_t fdt_totalsize(const void *fdt) {
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    
    // Safety check: Validate Magic Number
    if (bswap32(header->magic) != FDT_MAGIC) {
        return 0;
    }
    
    return bswap32(header->totalsize);
}

/* * fdt_path_offset:
 * Traverses the device tree to find a node matching the path.
 * SIMPLIFICATION: We scan for the *node name* (the last part of the path).
 * Returns: The offset of the node, or -1 if not found.
 */
int fdt_path_offset(const void *fdt, const char *path) {
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    if (bswap32(header->magic) != FDT_MAGIC) return -1;

    const uint8_t *struct_base = (const uint8_t *)fdt + bswap32(header->off_dt_struct);
    const uint8_t *p = struct_base;

    /* Extract just the node name from the path (e.g., "/cpus/cpu@0" -> "cpu@0") */
    const char *target_name = path;
    const char *slash = path;
    while (*slash) {
        if (*slash == '/') target_name = slash + 1;
        slash++;
    }
    /* Special case: Root path "/" */
    if (path[0] == '/' && path[1] == '\0') target_name = "";

    while (1) {
        uint32_t token = bswap32(*(uint32_t *)p);
        
        if (token == FDT_END) break;

        if (token == FDT_BEGIN_NODE) {
            int offset = (int)(p - struct_base);
            
            p += 4;
            char *current_name = (char *)p;
            
            /* Align pointer after the name */
            p = (const uint8_t *)align_up((uintptr_t)(current_name + strlen(current_name) + 1), 4);
            
            /* Check if this is the node we are looking for */
            if (strcmp(current_name, target_name) == 0) {
                return offset;
            }
        }
        else if (token == FDT_PROP) {
            p += 4;
            uint32_t len = bswap32(*(uint32_t *)p);
            p += 8; // skip len (4) and nameoff (4)
            p = (const uint8_t *)align_up((uintptr_t)(p + len), 4);
        }
        else if (token == FDT_END_NODE || token == FDT_NOP) {
            p += 4;
        }
    }

    return -1; // Not found
}

/* * fdt_getprop:
 * Retrieves a property from a node at a specific offset.
 * Returns: Pointer to the data, and sets *lenp to the size.
 */
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name, int *lenp) {
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    const char *strings = (const char *)fdt + bswap32(header->off_dt_strings);
    const uint8_t *struct_base = (const uint8_t *)fdt + bswap32(header->off_dt_struct);
    
    /* Jump directly to the node */
    const uint8_t *p = struct_base + nodeoffset;

    /* Sanity check: Ensure we are at a node start */
    if (bswap32(*(uint32_t *)p) != FDT_BEGIN_NODE) return NULL;
    
    p += 4;
    /* Skip node name */
    p = (const uint8_t *)align_up((uintptr_t)(p + strlen((char *)p) + 1), 4);

    while (1) {
        uint32_t token = bswap32(*(uint32_t *)p);
        p += 4;

        if (token == FDT_PROP) {
            uint32_t len = bswap32(*(uint32_t *)p);
            uint32_t nameoff = bswap32(*(uint32_t *)(p + 4));
            p += 8;

            const char *prop_name = strings + nameoff;
            if (strcmp(prop_name, name) == 0) {
                if (lenp) *lenp = len;
                return (const void *)p;
            }

            /* Skip value and align */
            p = (const uint8_t *)align_up((uintptr_t)(p + len), 4);
        }
        else if (token == FDT_NOP) {
            continue;
        }
        else {
            /* FDT_BEGIN_NODE or FDT_END_NODE means we left the properties of this node */
            break; 
        }
    }
    return NULL;
}
/* * fdt_next_node:
 * Sequential iterator for the device tree.
 * * fdt:    Pointer to DTB blob
 * offset: Current node offset (-1 to start at the root)
 * depth:  Pointer to an integer tracking nesting level (updated by this function)
 * * Returns: Offset of the NEXT node, or -FDT_ERR_NOTFOUND if end of tree.
 */
int fdt_next_node(const void *fdt, int offset, int *depth) {
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    if (bswap32(header->magic) != FDT_MAGIC) return -1;

    const uint8_t *struct_base = (const uint8_t *)fdt + bswap32(header->off_dt_struct);
    const uint8_t *p;

    // --- Case 1: Start from Root ---
    if (offset < 0) {
        p = struct_base;
        if (depth) *depth = -1; // Initial depth before entering root
    } 
    // --- Case 2: Continue from specific Node ---
    else {
        p = struct_base + offset;
        /* Sanity check: Should be at a node start */
        if (bswap32(*(uint32_t *)p) != FDT_BEGIN_NODE) return -1;

        /* Advance p PAST the current node's name tag to see what's inside/next */
        p += 4; // Skip FDT_BEGIN_NODE tag
        p = (const uint8_t *)align_up((uintptr_t)(p + strlen((char *)p) + 1), 4);
    }

    // --- Loop to find the NEXT node ---
    while (1) {
        uint32_t token = bswap32(*(uint32_t *)p);
        
        if (token == FDT_BEGIN_NODE) {
            /* Found a node! */
            if (depth) *depth = *depth + 1;
            return (int)(p - struct_base);
        }
        else if (token == FDT_END_NODE) {
            /* Leaving a node */
            if (depth) *depth = *depth - 1;
            /* If we exited the root, we are done */
            if (depth && *depth < 0) return -1; 
            p += 4;
        }
        else if (token == FDT_PROP) {
            /* Skip Property: Tag(4) + Len(4) + NameOff(4) + Data(Len) + Align */
            p += 4;
            uint32_t len = bswap32(*(uint32_t *)p);
            p += 8; // Skip Len and NameOff
            p = (const uint8_t *)align_up((uintptr_t)(p + len), 4);
        }
        else if (token == FDT_NOP) {
            p += 4;
        }
        else if (token == FDT_END) {
            return -1; // End of DTB
        }
        else {
            return -1; // Unknown token/Corrupt
        }
    }
}

/* * fdt_get_uart_base:
 * Scans the tree to find the UART compatible node and returns its address.
 * Matches: "ns16550a" (QEMU) and "snps,dw-apb-uart" (Orange Pi).
 */
uint64_t fdt_get_uart_base(const void *fdt) {
    if (!fdt) return 0;
    
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    if (bswap32(header->magic) != FDT_MAGIC) return 0;

    const uint8_t *struct_base = (const uint8_t *)fdt + bswap32(header->off_dt_struct);
    const uint8_t *p = struct_base;

    while (1) {
        uint32_t token = bswap32(*(uint32_t *)p);
        
        if (token == FDT_END) break;

        if (token == FDT_BEGIN_NODE) {
            int offset = (int)(p - struct_base);
            
            p += 4;
            p = (const uint8_t *)align_up((uintptr_t)(p + strlen((char *)p) + 1), 4);

            int len;
            /* Use our new API to check compatibility */
            const char *compat = (const char *)fdt_getprop(fdt, offset, "compatible", &len);
            
            if (compat) {
                /* Check for QEMU or Orange Pi UART */
                if (strstr(compat, "ns16550a") || 
                    strstr(compat, "snps,dw-apb-uart") ||
                    strstr(compat, "ky,pxa-uart")) {
                    
                    const uint32_t *reg = (const uint32_t *)fdt_getprop(fdt, offset, "reg", &len);
                    if (reg) {
                        /* Read 64-bit address (Big Endian -> Little Endian) */
                        /* Note: 'reg' is usually address (uint64) + size (uint64) */
                        uint64_t val_hi = bswap32(reg[0]);
                        uint64_t val_lo = bswap32(reg[1]);
                        return (val_hi << 32) | val_lo;
                    }
                }
            }
        }
        else if (token == FDT_PROP) {
            p += 4;
            uint32_t len = bswap32(*(uint32_t *)p);
            p += 8;
            p = (const uint8_t *)align_up((uintptr_t)(p + len), 4);
        }
        else if (token == FDT_END_NODE || token == FDT_NOP) {
            p += 4;
        }
    }
    return 0;
}