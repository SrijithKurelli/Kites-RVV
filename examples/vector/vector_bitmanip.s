# Demonstrate selected Zvbb-style vector bit manipulation operations.
# Supported forms here are vector-vector and per-element vector forms only.
.data
data:                .byte 129, 66, 0, 255
rotate_amounts:      .byte 1, 2, 3, 4
andnot_result:       .byte 0, 0, 0, 0
rotate_left_result:  .byte 0, 0, 0, 0
rotate_right_result: .byte 0, 0, 0, 0
leading_zeros:       .byte 0, 0, 0, 0
trailing_zeros:      .byte 0, 0, 0, 0
population_counts:   .byte 0, 0, 0, 0

.text
main:
    la x5, data
    la x6, rotate_amounts
    la x7, andnot_result
    la x8, rotate_left_result
    la x9, rotate_right_result
    la x11, leading_zeros
    la x12, trailing_zeros
    la x13, population_counts
    addi x10, x0, 4
    vsetvli x14, x10, 0       # SEW=8, LMUL=1, VL=4
    vle8.v v1, (x5)
    vle8.v v2, (x6)
    vandn.vv v3, v1, v2
    vrol.vv v4, v1, v2
    vror.vv v5, v1, v2
    vclz.v v6, v1
    vctz.v v7, v1
    vcpop.v v8, v1
    vse8.v v3, (x7)
    vse8.v v4, (x8)
    vse8.v v5, (x9)
    vse8.v v6, (x11)
    vse8.v v7, (x12)
    vse8.v v8, (x13)
