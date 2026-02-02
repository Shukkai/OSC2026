#ifndef RELOCATE_H
#define RELOCATE_H

#include <stdint.h>

/* These symbols are defined in the linker script */
extern char _start[];
extern char _end[];

/**
 * Moves the currently running bootloader to the top of RAM.
 * Returns the address of the new location.
 */
void *relocate_bootloader(unsigned long dtb_addr);

#endif