# Add only lanes where lhs < rhs; masked-off output/memory lanes stay unchanged.
.data
lhs:    .word 3, 9, 1, 8
rhs:    .word 4, 7, 2, 9
result: .word 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF

.text
main:
    la x5, lhs
    la x6, rhs
    la x7, result
    addi x8, x0, 4
    vsetvli x9, x8, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmsltu.vv v0, v1, v2     # Predicate = 0b1101 (lanes 0, 2, 3)
    vadd.vv v3, v1, v2, v0.t
    vse32.v v3, (x7), v0.t
