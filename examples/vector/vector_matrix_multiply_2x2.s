# 2x2 matrix multiplication:
# [1 2] [5 6]   [19 22]
# [3 4] [7 8] = [43 50]
# B is stored as its two columns so each output is a two-element dot product.
.data
a_row0: .word 1, 2
a_row1: .word 3, 4
b_col0: .word 5, 7
b_col1: .word 6, 8
c_out:  .word 0, 0, 0, 0

.text
main:
    la x5, a_row0
    la x6, a_row1
    la x7, b_col0
    la x8, b_col1
    la x11, c_out
    addi x9, x0, 2
    addi x10, x0, 1

    vsetvli x12, x9, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vle32.v v3, (x7)
    vle32.v v4, (x8)

    # C[0,0] = dot(A row 0, B column 0) = 1*5 + 2*7 = 19
    vmul.vv v5, v1, v3
    vredsum.vs v6, v5, v0
    vsetvli x12, x10, 16
    vse32.v v6, (x11)
    addi x11, x11, 4

    # C[0,1] = dot(A row 0, B column 1) = 1*6 + 2*8 = 22
    vsetvli x12, x9, 16
    vmul.vv v5, v1, v4
    vredsum.vs v6, v5, v0
    vsetvli x12, x10, 16
    vse32.v v6, (x11)
    addi x11, x11, 4

    # C[1,0] = dot(A row 1, B column 0) = 3*5 + 4*7 = 43
    vsetvli x12, x9, 16
    vmul.vv v5, v2, v3
    vredsum.vs v6, v5, v0
    vsetvli x12, x10, 16
    vse32.v v6, (x11)
    addi x11, x11, 4

    # C[1,1] = dot(A row 1, B column 1) = 3*6 + 4*8 = 50
    vsetvli x12, x9, 16
    vmul.vv v5, v2, v4
    vredsum.vs v6, v5, v0
    vsetvli x12, x10, 16
    vse32.v v6, (x11)
