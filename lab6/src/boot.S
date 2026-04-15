.section ".text.boot"

.global _start
_start:
    /* a0: hartid, a1: dtb */

    /* 1. Preserve boot args */
    mv s0, a0
    mv s1, a1

    /* 2. Set stvec to VA of label 1 */
    la a2, 1f
    li a3, 0xffffffc000000000
    add a2, a2, a3
    csrw stvec, a2
    mv s3, a2

    /* 3. Early boot stack */
    la sp, _start

    /* 4. Build page table + enable MMU */
    call setup_vm

    /* 5. Jump to virtual address space */
    jr s3

.align 2
1:
    /* --- Virtual address space --- */

    /* 6. Set real trap handler */
    la t0, handle_exception
    csrw stvec, t0

    /* 7. Virtual stack */
    la sp, __stack_top

    /* 8. Clear BSS */
    la t0, __bss_start
    la t1, __bss_end
    bge t0, t1, clear_bss_done
clear_bss:
    sd zero, 0(t0)
    addi t0, t0, 8
    blt t0, t1, clear_bss
clear_bss_done:

    /* 9. Enter C kernel */
    mv a0, s0
    mv a1, s1
    call start_kernel

halt:
    wfi
    j halt