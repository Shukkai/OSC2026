.section ".text.boot"

.global _start
_start:
    /* 1. Disable Interrupts */
    /* Ensure interrupts are off until the kernel is ready to handle them */
    csrw sie, zero

    /* 2. Setup Stack Pointer */
    /* Defines the top of the stack (from linker script) */
    la sp, __stack_top

    /* 3. Set Trap Vector (Optional but Recommended) */
    /* Point stvec to your C handler immediately so early crashes are caught */
    la t0, handle_exception
    csrw stvec, t0

    /* 4. Preserve Boot Arguments */
    /* a0 = hartid, a1 = dtb */
    /* We MUST save these to preserved registers (s0/s1) because 
       'start_kernel' needs them, and we might touch memory next. */
    mv s0, a0
    mv s1, a1

    /* 5. Clear BSS Section */
    /* This loop ensures all uninitialized global variables are zeroed. */
    la t0, __bss_start
    la t1, __bss_end
    bge t0, t1, run_kernel  /* If BSS size is 0, skip */

clear_bss_loop:
    sd zero, 0(t0)          /* Write 0 to address in t0 */
    addi t0, t0, 8          /* Advance by 8 bytes */
    blt t0, t1, clear_bss_loop

run_kernel:
    /* 6. Restore Arguments */
    /* Move saved values back to argument registers for the C call */
    mv a0, s0               /* Restore hartid */
    mv a1, s1               /* Restore dtb */

    /* 7. Enter C Kernel */
    /* calls start_kernel(hartid, dtb) */
    call start_kernel

halt:
    wfi
    j halt