.section ".text.boot"

.global _start
_start:
    /* Disable Interrupts */
    csrw sie, zero
    
    /* Setup Stack Pointer */
    la sp, __stack_top

    /* SAVE BOOT ARGUMENTS: a0 (hartid) and a1 (dtb) */
    mv s0, a0
    mv s1, a1

    /* Clear BSS Section */
    la t0, __bss_start
    la t1, __bss_end
    bge t0, t1, run_kernel

clear_bss_loop:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, clear_bss_loop

run_kernel:
    /* RESTORE ARGUMENTS before C call */
    mv a0, s0
    mv a1, s1
    
    /* Jump to C code: start_kernel(hartid, dtb) */
    call start_kernel