# 3D cross product A x B for A=[1,2,3], B=[4,5,6].
# Reordered input streams encode the six scalar products without a custom
# permutation instruction. vmul.vv and vsub.vv perform all calculations.
.data
left_first:    .word 2, 3, 1
right_first:   .word 6, 4, 5
left_second:   .word 3, 1, 2
right_second:  .word 5, 6, 4
cross_result:  .word 0, 0, 0

.text
main:
    la x5, left_first
    la x6, right_first
    la x7, left_second
    la x8, right_second
    la x9, cross_result
    addi x10, x0, 3
    vsetvli x11, x10, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vle32.v v3, (x7)
    vle32.v v4, (x8)
    vmul.vv v5, v1, v2
    vmul.vv v6, v3, v4
    vsub.vv v7, v5, v6
    vse32.v v7, (x9)       # [-3, 6, -3]
