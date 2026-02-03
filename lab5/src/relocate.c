#include "relocate.h"
#include "fdt.h"
#include "uart.h"
#include "string.h" 
#include "utils.h" 

extern unsigned long DTB_BASE;

/* Helper to read integer values based on cell count */
static uint64_t read_cells(const uint32_t *ptr, int cells) {
    if (cells == 1) {
        return bswap32(ptr[0]);
    } else if (cells == 2) {
        uint64_t hi = bswap32(ptr[0]);
        uint64_t lo = bswap32(ptr[1]);
        return (hi << 32) | lo;
    }
    return 0; // Should not happen for valid DTBs
}

uint64_t get_ram_top(void *dtb) {
    // 1. Establish Defaults per Spec (Section 2.3.5)
    // "If missing... default value of 2 for #address-cells, and 1 for #size-cells"
    int root_ac = 2; 
    int root_sc = 1;
    
    // 2. Get Root Node Properties (#address-cells / #size-cells)
    // The root node is always the first node (offset usually 0, or after struct header)
    int root = fdt_next_node(dtb, -1, NULL); 
    if (root >= 0) {
        int len;
        const uint32_t *prop;
        
        prop = (const uint32_t *)fdt_getprop(dtb, root, "#address-cells", &len);
        if (prop) root_ac = bswap32(*prop);
        
        prop = (const uint32_t *)fdt_getprop(dtb, root, "#size-cells", &len);
        if (prop) root_sc = bswap32(*prop);
    }

    uint64_t max_ram_end = 0;
    int node = -1;
    int depth = 0;

    // 3. Search for /memory node
    for (node = fdt_next_node(dtb, -1, &depth); 
         node >= 0 && depth >= 0; 
         node = fdt_next_node(dtb, node, &depth)) {
             
        // Optimization: Memory is usually at root level
        if (depth != 1) continue; 
        
        int len;
        const char *type = (const char *)fdt_getprop(dtb, node, "device_type", &len);
        
        // Spec Section 3.4: "The device_type property ... Value shall be 'memory'"
        if (type && !strcmp(type, "memory")) {
            
            const uint32_t *reg = (const uint32_t *)fdt_getprop(dtb, node, "reg", &len);
            if (!reg) continue;

            // Calculate size of one (address, size) tuple
            int tuple_len_bytes = (root_ac + root_sc) * sizeof(uint32_t);
            int num_tuples = len / tuple_len_bytes;
            
            const uint32_t *ptr = reg;
            
            // 4. Iterate over ALL memory ranges in this node
            for (int i = 0; i < num_tuples; i++) {
                uint64_t base = read_cells(ptr, root_ac);
                ptr += root_ac;
                
                uint64_t size = read_cells(ptr, root_sc);
                ptr += root_sc;
                
                uint64_t end = base + size;
                uart_puts("[Reloc] Found RAM: "); uart_hex(base); uart_puts(" - "); uart_hex(end); uart_puts("\n");
                // Track the highest memory address found
                if (end > max_ram_end) {
                    max_ram_end = end;
                }
            }
        }
    }

    if (max_ram_end == 0) {
        uart_puts("[Reloc] Critical: No valid /memory node found in DTB.\n");
        // Fallback or Panic - effectively halting is safer than guessing 0x0
        while(1); 
    }

    return max_ram_end;
}

void *relocate_bootloader(unsigned long dtb_addr) {
    uint64_t image_size = (uint64_t)_end - (uint64_t)_start;
    
    // Dynamically get top of RAM from DTB
    uint64_t ram_top = get_ram_top((void *)dtb_addr);
    
    // Calculate Safe Destination: Top of RAM - Image Size - 64KB Buffer
    // Align down to 4KB (0xFFF)
    uint64_t dest_addr = (ram_top - image_size - 0x10000) & ~0xFFF;
    uint64_t current_pc = get_pc();
    
    // Safety Check: Are we already running at the destination?
    if (current_pc >= dest_addr && current_pc < (dest_addr + 0x100000)) {
        return (void *)dest_addr;
    }

    uart_puts("Relocating info:\n");
    uart_puts("  Source: "); uart_hex((uint64_t)_start); uart_puts("\n");
    uart_puts("  Dest:   "); uart_hex(dest_addr); uart_puts("\n");
    
    char *src = (char *)_start;
    char *dst = (char *)dest_addr;
    memcpy(dst, src, image_size);

    // Sync Instruction Cache since we wrote executable code
    asm volatile("fence.i");

    return (void *)dest_addr;
}