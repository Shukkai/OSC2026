.section ".text.boot"

.global _start
_start:
    /* Disable Interrupts globally */
    csrw sie, zero
    
    /* Setup Stack Pointer */
    la sp, __stack_top

    /* Basic Exercise 1: Clear BSS Section */
    la t0, __bss_start
    la t1, __bss_end
    bge t0, t1, run_kernel

clear_bss_loop:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, clear_bss_loop

run_kernel:
    /* Jump to C code */
    call start_kernel

halt:
    wfi
    j halt