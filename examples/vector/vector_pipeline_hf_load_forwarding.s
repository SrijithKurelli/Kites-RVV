.data
pipeline_seed: .word 0

.text
main:
    addi x7, x0, 100
    addi x8, x0, 4
    sw x8, 0(x7)
    lw x9, 0(x7)
    vsetvli x5, x9, 16
    addi x7, x0, 104
    addi x6, x0, 3
    sw x6, 0(x7)
    lw x6, 0(x7)
    vmul.vx v3, v1, x6
    vadd.vv v4, v3, v2
