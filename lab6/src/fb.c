#include "fb.h"
#include "uart.h"
#include "string.h"
#include "vm.h"
// =========================================================================
// GLOBAL MEMORY ALLOCATION
// =========================================================================
#ifdef __QEMU__
    // ALLOCATE the actual physical RAM here. Notice we removed 'static' 
    // so it matches the 'extern' declaration in the header!
    unsigned int qemu_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT] __attribute__((aligned(64)));
#endif

// =========================================================================
// FRAME BUFFER IMPLEMENTATION
// =========================================================================

void fb_init() {
#ifdef __QEMU__
    uart_puts("Initializing QEMU ramfb...\n");
    
    *FW_CFG_SELECTOR = __builtin_bswap16(FW_CFG_FILE_DIR);
    
    uint32_t count = 0;
    uint8_t *c_ptr = (uint8_t *)&count;
    for (int i = 0; i < 4; i++) c_ptr[i] = *FW_CFG_DATA;
    count = __builtin_bswap32(count);
    
    uint16_t ramfb_select = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        struct FWCfgFile file;
        uint8_t *p = (uint8_t *)&file;
        for (unsigned int j = 0; j < sizeof(struct FWCfgFile); j++) {
            p[j] = *FW_CFG_DATA;
        }
        
        if (strcmp(file.name, "etc/ramfb") == 0) {
            ramfb_select = __builtin_bswap16(file.select);
            break;
        }
    }
    
    if (ramfb_select == 0) {
        uart_puts("Error: ramfb not found in fw_cfg!\n");
        return;
    }

    // Notice we can still use the structs because they are defined in fb.h!
    struct QemuRamFBCfg cfg __attribute__((aligned(64)));
    cfg.addr   = __builtin_bswap64(virt_to_phys(FB_ADDR));
    cfg.fourcc = __builtin_bswap32(0x34325258); 
    cfg.flags  = __builtin_bswap32(0);
    cfg.width  = __builtin_bswap32(SCREEN_WIDTH);
    cfg.height = __builtin_bswap32(SCREEN_HEIGHT);
    cfg.stride = __builtin_bswap32(SCREEN_WIDTH * 4);

    struct FWCfgDmaAccess dma __attribute__((aligned(64)));
    dma.control = __builtin_bswap32((ramfb_select << 16) | 0x0018); 
    dma.length  = __builtin_bswap32(sizeof(struct QemuRamFBCfg));
    dma.address = __builtin_bswap64(virt_to_phys((unsigned long)&cfg));


    asm volatile("cbo.clean 0(%0)" :: "r"(&cfg) : "memory");
    asm volatile("cbo.clean 0(%0)" :: "r"(&dma) : "memory");
    __sync_synchronize();
    *FW_CFG_DMA_ADDR = __builtin_bswap64(virt_to_phys((unsigned long)&dma));
    __sync_synchronize();
    uart_puts("QEMU ramfb ready!\n");
#else
    uart_puts("Orange Pi RV2 Hardware FB Ready at 0x7f700000.\n");
#endif
    
}

void fb_draw(unsigned int *bmp_image, unsigned int width, unsigned int height) {
    unsigned int *fb_addr = (unsigned int *)FB_ADDR;
    if (!bmp_image) return;

    unsigned int start_x = (SCREEN_WIDTH > width) ? (SCREEN_WIDTH - width) / 2 : 0;
    unsigned int start_y = (SCREEN_HEIGHT > height) ? (SCREEN_HEIGHT - height) / 2 : 0;

    for (unsigned int y = 0; y < height && (start_y + y) < SCREEN_HEIGHT; y++) {
        
        unsigned int *dst_row = fb_addr + ((start_y + y) * SCREEN_WIDTH) + start_x;

        // 1. Write the pixels to the L1 Cache
        for (unsigned int x = 0; x < width && (start_x + x) < SCREEN_WIDTH; x++) {
            dst_row[x] = bmp_image[y * width + x];
        }

        // 2. CRITICAL MEMORY BARRIER
        __sync_synchronize();

        // 3. Flush the Cache to Physical RAM
        unsigned long start_addr = (unsigned long)dst_row & ~(64UL - 1);
        unsigned long end_addr = (unsigned long)dst_row + (width * 4);

        for (unsigned long addr = start_addr; addr <= end_addr; addr += 64) {
            asm volatile("cbo.clean 0(%0)" :: "r"(addr) : "memory");
            __sync_synchronize(); 
        }
    }
}