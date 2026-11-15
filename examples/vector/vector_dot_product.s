# Dot product of two four-element int32 vectors.
# Expected result: 1*5 + 2*6 + 3*7 + 4*8 = 70.
.data
lhs:    .word 1, 2, 3, 4
rhs:    .word 5, 6, 7, 8
result: .word 0

.text
main:
    la x5, lhs
    la x6, rhs
    la x7, result
    addi x8, x0, 4
    vsetvli x9, x8, 16       # SEW=32, VL=4
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2       # Element-wise products: [5, 12, 21, 32]
    vredsum.vs v4, v3, v0    # Sum products; v0[0] is the zero seed.
    addi x8, x0, 1
    vsetvli x9, x8, 16       # One active lane for storing the scalar result.
    vse32.v v4, (x7)
    lw a0, 0(x7)
    li a7, 1
    ecall
    li a0, 0
    li a7, 10
    ecall
