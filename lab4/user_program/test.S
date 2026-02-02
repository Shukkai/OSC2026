.section ".text"
.global _start
_start:
    li a7, 5
1:
    ecall
    addi a7, a7, -1
    bne zero, a7, 1b
    li a7, 93    /* SYS_EXIT */
    li a0, 0
    ecall        /* Trigger Exit -> Kernel hangs */
    j .