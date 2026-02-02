/* src/kernel.ld for BOOTLOADER */
SECTIONS
{
    . = KERNEL_BASE;
    PROVIDE(_start = .);

    .text : { KEEP(*(.text.boot)) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .sbss)
        __bss_end = .;
    }
    
    /* 1. Allocate Stack Space */
    . = ALIGN(16);
    . = . + 0x4000;    /* 16KB Stack */
    __stack_top = .;

    /* 2. Define _end AFTER the stack */
    /* This ensures 'image_size' includes the stack, keeping SP inside RAM! */
    . = ALIGN(4096); 
    PROVIDE(_end = .); 
}