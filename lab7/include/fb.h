#ifndef _FB_H_
#define _FB_H_

#include <stdint.h>

// =========================================================================
// FW_CFG DEFINITIONS (QEMU Virtual Hardware)
// =========================================================================
// #define FW_CFG_BASE         0x10100000
#define FW_CFG_BASE  (0x10100000UL + 0xffffffc000000000UL)
#define FW_CFG_DATA         (volatile uint8_t  *)(FW_CFG_BASE + 0x00)
#define FW_CFG_SELECTOR     (volatile uint16_t *)(FW_CFG_BASE + 0x08)
#define FW_CFG_DMA_ADDR     (volatile uint64_t *)(FW_CFG_BASE + 0x10)
#define FW_CFG_FILE_DIR     0x0019

struct FWCfgFile {
    uint32_t size;
    uint16_t select;
    uint16_t reserved;
    char name[56];
};

struct QemuRamFBCfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
}__attribute__((packed));

struct FWCfgDmaAccess {
    uint32_t control;
    uint32_t length;
    uint64_t address;
};

// =========================================================================
// SCREEN & MEMORY DEFINITIONS
// =========================================================================
#ifdef __QEMU__
    #define SCREEN_WIDTH 800
    #define SCREEN_HEIGHT 600
    extern unsigned int qemu_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
    #define FB_ADDR      ((unsigned long)qemu_framebuffer)
    #define FW_CFG_BASE  (0x10100000UL + 0xffffffc000000000UL)
#else
    #define SCREEN_WIDTH 1920
    #define SCREEN_HEIGHT 1080
    #define FB_ADDR      (0x7f700000UL + 0xffffffc000000000UL)
#endif

// =========================================================================
// FUNCTION PROTOTYPES
// =========================================================================
void fb_init(void);
void fb_draw(unsigned int *bmp_image, unsigned int width, unsigned int height);

#endif // _FB_H_