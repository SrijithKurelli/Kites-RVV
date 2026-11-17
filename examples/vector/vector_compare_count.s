# Compare corresponding lanes and count the matches.
.data
lhs: .word 2, 4, 6, 8
rhs: .word 2, 5, 6, 9

.text
main:
    la x5, lhs
    la x6, rhs
    addi x7, x0, 4
    vsetvli x8, x7, 16       # SEW=32, LMUL=1, VL=4
    vle32.v v1, (x5)
    vle32.v v2, (x6)
    vmseq.vv v0, v1, v2      # Packed predicate: lanes 0 and 2 are set
    vcpop.m x10, v0           # x10 = 2
