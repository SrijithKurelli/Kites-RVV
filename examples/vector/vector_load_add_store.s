# Load two four-element arrays, add them lane-wise, and store the result.
.data
lhs:    .word 1, 2, 3, 4
rhs:    .word 10, 20, 30, 40
result: .word 0, 0, 0, 0

.text
main:
    la x5, lhs
    la x6, rhs
    la x7, result
    addi x8, x0, 4
    vsetvli x9, x8, 16       # SEW=32, LMUL=1, VL=4
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vadd.vv v3, v1, v2
    vse32.v v3, (x7)
