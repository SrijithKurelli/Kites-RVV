# Demonstrate vsetivli with immediate AVL=4 and SEW=32 (zimm=16).
.data
input_values:  .word 3, 5, 7, 9
output_values: .word 0, 0, 0, 0

.text
main:
    la x5, input_values
    la x6, output_values
    vsetivli x7, 4, 16
    vle32.v v1, (x5)
    vadd.vv v2, v1, v1
    vse32.v v2, (x6)
