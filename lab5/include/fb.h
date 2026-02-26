#ifndef _FB_H_
#define _FB_H_

#include <stdint.h>

// Initialize the Frame Buffer hardware (via QEMU fw_cfg or hardware)
void fb_init(void);

// Draw a pixel buffer to the screen and flush the L1 cache
void fb_draw(unsigned int *bmp_image, unsigned int width, unsigned int height);

#endif // _FB_H_