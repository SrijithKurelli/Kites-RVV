# Average the values in lhs whose corresponding rhs value is equal.
# Integer division truncates toward zero. An empty selection returns zero.
.data
lhs:   .word 1, 2, 4, 4
rhs:   .word 1, 0, 4, 0
seed:  .word 0, 0, 0, 0
scratch: .word 0

.text
main:
    la x5, lhs
    la x6, rhs
    la x7, seed
    la x8, scratch
    addi x9, x0, 4
    vsetvli x10, x9, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vle32.v v4, (x7)
    vmseq.vv v0, v1, v2
    vredsum.vs v5, v1, v4, v0.t
    vse32.v v5, (x8)
    lw x11, 0(x8)            # Sum = 5
    vcpop.m x12, v0           # Count = 2
    beq x12, x0, no_matches
    div x13, x11, x12         # Average = 2 (5 / 2, trunc toward zero)
    jal x0, average_done
no_matches:
    addi x13, x0, 0
average_done:
    mv a0, x13
    li a7, 1
    ecall
    li a0, 0
    li a7, 10
    ecall
