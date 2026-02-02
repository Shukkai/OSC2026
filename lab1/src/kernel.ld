SECTIONS
{
    /* * Use KERNEL_BASE macro defined in Makefile via -D flag.
     * QEMU  = 0x80200000
     * OPI   = 0x00200000
     */
    . = KERNEL_BASE;

    .text : { 
        KEEP(*(.text.boot)) 
        *(.text) 
    }
    
    .rodata : { *(.rodata) }
    
    .data : { *(.data) }
    
    .bss : {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .sbss)
        __bss_end = .;
    }
    
    . = ALIGN(16);
    . = . + 0x4000; /* Stack size */
    __stack_top = .;
    _end = .;
}