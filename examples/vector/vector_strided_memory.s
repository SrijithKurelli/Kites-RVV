# Demonstrate signed-stride vector loads and stores (byte stride is in a GPR).
.data
source_values:
    .word 11, 99, 22, 99, 33
destination_values:
    .word 0, 0, 0, 0, 0
.text
    addi x5, x0, 3
    addi x8, x0, 8
    la x7, source_values
    la x9, destination_values
    vsetvli x6, x5, 16
    vlse32.v v1, (x7), x8
    vsse32.v v1, (x9), x8
