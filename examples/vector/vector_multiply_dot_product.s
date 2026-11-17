# Element-wise products and a four-element dot product.
# The dot product is composed from vmul.vv and vredsum.vs.
.data
lhs:        .word 2, -3, 4, 5
rhs:        .word 6, 7, -2, 3
products:   .word 0, 0, 0, 0
dot_result: .word 0, 0, 0, 0

.text
main:
    la x5, lhs
    la x6, rhs
    la x7, products
    la x10, dot_result
    addi x8, x0, 4
    vsetvli x9, x8, 16       # SEW=32, LMUL=1, VL=4
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2      # [12, -21, -8, 15]
    vse32.v v3, (x7)
    vredsum.vs v4, v3, v0   # v0[0] is zero on reset; dot product = -2
    vse32.v v4, (x10)
