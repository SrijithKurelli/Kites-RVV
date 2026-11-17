# SAXPY: result[i] = alpha * x[i] + y[i].
# Uses standard OPMVX vmul.vx followed by vector-vector addition.
.data
x_values: .word 1, -2, 3, 4
y_values: .word 10, 20, 30, 40
result:   .word 0, 0, 0, 0

.text
main:
    la x5, x_values
    la x6, y_values
    la x7, result
    addi x8, x0, 3        # alpha
    addi x9, x0, 4
    vsetvli x10, x9, 16   # SEW=32, VL=4
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmul.vx v3, v1, x8    # [3, -6, 9, 12]
    vadd.vv v4, v3, v2   # [13, 14, 39, 52]
    vse32.v v4, (x7)
