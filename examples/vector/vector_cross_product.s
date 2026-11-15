# Cross product of A=(1,2,3) and B=(4,5,6).
# The inputs are arranged as lane-wise products for the three cross-product terms.
# Expected result: (-3, 6, -3).
.data
ayz_ax: .word 2, 3, 1
bzx_by: .word 6, 4, 5
azx_ay: .word 3, 1, 2
byz_bx: .word 5, 6, 4
result: .word 0, 0, 0

.text
main:
    la x5, ayz_ax
    la x6, bzx_by
    la x7, azx_ay
    la x8, byz_bx
    la x9, result
    addi x10, x0, 3
    vsetvli x11, x10, 16    # SEW=32, VL=3

    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vv v3, v1, v2      # [Ay*Bz, Az*Bx, Ax*By]

    vle32.v v4, (x7)
    vle32.v v5, (x8)
    vmul.vv v6, v4, v5      # [Az*By, Ax*Bz, Ay*Bx]

    vsub.vv v7, v3, v6
    vse32.v v7, (x9)

    lw a0, 0(x9)
    li a7, 1
    ecall
    lw a0, 4(x9)
    li a7, 1
    ecall
    lw a0, 8(x9)
    li a7, 1
    ecall
    li a0, 0
    li a7, 10
    ecall
