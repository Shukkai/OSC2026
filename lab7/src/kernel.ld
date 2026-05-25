SECTIONS
{
    . = KERNEL_BASE;                          /* higher-half VMA */
    PROVIDE(_start = .);

    .text : AT(PHYS_BASE) {                    /* physical load address (LMA) */
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) }

    . = ALIGN(4096);
    .data : { *(.data .data.*) }

    . = ALIGN(4096);
    .bss : {
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /* Stack lives outside any named section — boot.S sets sp here.       */
    . = ALIGN(16);
    . = . + 0x400000;
    __stack_top = .;

    . = ALIGN(4096);
    PROVIDE(_end = .);
}