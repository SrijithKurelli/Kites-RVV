# 2x2 matrix multiplication using four vector dot products.
# A = [[1,2],[3,4]], B = [[5,6],[7,8]]
# Expected C = [[19,22],[43,50]].
.data
a_row0: .word 1, 2
a_row1: .word 3, 4
b_col0: .word 5, 7
b_col1: .word 6, 8
result: .word 0, 0, 0, 0

.text
main:
    la x10, result

    # C[0,0] = dot(A row 0, B column 0) = 19
    la x5, a_row0
    la x6, b_col0
    addi x8, x0, 2
    vsetvli x9, x8, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2
    vredsum.vs v4, v3, v0
    addi x8, x0, 1
    vsetvli x9, x8, 16
    vse32.v v4, (x10)
    addi x10, x10, 4

    # C[0,1] = dot(A row 0, B column 1) = 22
    la x5, a_row0
    la x6, b_col1
    addi x8, x0, 2
    vsetvli x9, x8, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2
    vredsum.vs v4, v3, v0
    addi x8, x0, 1
    vsetvli x9, x8, 16
    vse32.v v4, (x10)
    addi x10, x10, 4

    # C[1,0] = dot(A row 1, B column 0) = 43
    la x5, a_row1
    la x6, b_col0
    addi x8, x0, 2
    vsetvli x9, x8, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2
    vredsum.vs v4, v3, v0
    addi x8, x0, 1
    vsetvli x9, x8, 16
    vse32.v v4, (x10)
    addi x10, x10, 4

    # C[1,1] = dot(A row 1, B column 1) = 50
    la x5, a_row1
    la x6, b_col1
    addi x8, x0, 2
    vsetvli x9, x8, 16
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2
    vredsum.vs v4, v3, v0
    addi x8, x0, 1
    vsetvli x9, x8, 16
    vse32.v v4, (x10)

    la x11, result
    lw a0, 0(x11)
    li a7, 1
    ecall
    lw a0, 4(x11)
    li a7, 1
    ecall
    lw a0, 8(x11)
    li a7, 1
    ecall
    lw a0, 12(x11)
    li a7, 1
    ecall
    li a0, 0
    li a7, 10
    ecall
