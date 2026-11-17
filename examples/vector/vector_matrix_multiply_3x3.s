# 3x3 matrix multiplication using RVSS vector instructions.
# A = [[1,2,3],[4,5,6],[7,8,9]]
# B = [[9,8,7],[6,5,4],[3,2,1]]
# C = A*B = [[30,24,18],[84,69,54],[138,114,90]]
# B is stored as columns so each output element is a vector dot product.

.data
a_row0: .word 1, 2, 3
a_row1: .word 4, 5, 6
a_row2: .word 7, 8, 9
b_col0: .word 9, 6, 3
b_col1: .word 8, 5, 2
b_col2: .word 7, 4, 1
c_out:  .word 0, 0, 0, 0, 0, 0, 0, 0, 0

.text
main:
    la x5, a_row0
    la x6, a_row1
    la x7, a_row2
    la x8, b_col0
    la x9, b_col1
    la x10, b_col2
    la x11, c_out
    addi x12, x0, 3
    addi x13, x0, 1

    vsetvli x14, x12, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vle32.v v3, (x7)
    vle32.v v4, (x8)
    vle32.v v5, (x9)
    vle32.v v6, (x10)

    # Row 0
    vmul.vv v7, v1, v4
    vredsum.vs v8, v7, v0
    vmul.vv v7, v1, v5
    vredsum.vs v9, v7, v0
    vmul.vv v7, v1, v6
    vredsum.vs v10, v7, v0

    # Row 1
    vmul.vv v7, v2, v4
    vredsum.vs v11, v7, v0
    vmul.vv v7, v2, v5
    vredsum.vs v12, v7, v0
    vmul.vv v7, v2, v6
    vredsum.vs v13, v7, v0

    # Row 2
    vmul.vv v7, v3, v4
    vredsum.vs v14, v7, v0
    vmul.vv v7, v3, v5
    vredsum.vs v15, v7, v0
    vmul.vv v7, v3, v6
    vredsum.vs v16, v7, v0

    # Store the nine scalar reduction results in row-major order.
    vsetvli x14, x13, 16
    vse32.v v8, (x11)
    addi x11, x11, 4
    vse32.v v9, (x11)
    addi x11, x11, 4
    vse32.v v10, (x11)
    addi x11, x11, 4
    vse32.v v11, (x11)
    addi x11, x11, 4
    vse32.v v12, (x11)
    addi x11, x11, 4
    vse32.v v13, (x11)
    addi x11, x11, 4
    vse32.v v14, (x11)
    addi x11, x11, 4
    vse32.v v15, (x11)
    addi x11, x11, 4
    vse32.v v16, (x11)
