.section ".text"
.global _start
_start:
    li a7, 5
1:
    ecall
    addi a7, a7, -1
    bne zero, a7, 1b
    j .
