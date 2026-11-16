.data
pipeline_seed: .word 0

.text
main:
    addi x7, x0, 4
    vsetvli x5, x7, 16
    addi x8, x5, 1
    addi x6, x0, 3
    vmul.vx v3, v1, x6
    vadd.vv v4, v3, v2
