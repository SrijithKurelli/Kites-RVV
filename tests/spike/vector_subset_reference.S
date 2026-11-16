    .section .text.start
    .globl _start
    .type _start, @function
_start:
    la      t0, lhs
    la      t1, rhs

    # AVL=4, SEW=32, LMUL=1, tail-undisturbed, mask-undisturbed.
    vsetivli t3, 4, e32, m1, tu, mu
    vle32.v  v1, (t0)
    vle32.v  v2, (t1)

    # [2,-3,4,5] * [6,7,-2,3] = [12,-21,-8,15].
    vmul.vv  v3, v1, v2
    la       t4, result_vv
    vse32.v  v3, (t4)

    # [2,-3,4,5] * -3 = [-6,9,-12,-15].
    li       t2, -3
    vmul.vx  v4, v1, t2
    la       t5, result_vx
    vse32.v  v4, (t5)

    # Explicitly initialize the reduction seed. Do not rely on reset values.
    la       t6, reduction_seed
    vle32.v  v0, (t6)
    vredsum.vs v5, v3, v0

    # Store only the reduced element.
    vsetivli x0, 1, e32, m1, tu, mu
    la       t6, result_sum
    vse32.v  v5, (t6)

    # Independently assert every vector-vector product lane.
    la       t0, result_vv
    lw       t2, 0(t0)
    li       t1, 12
    bne      t2, t1, _fail
    lw       t2, 4(t0)
    li       t1, -21
    bne      t2, t1, _fail
    lw       t2, 8(t0)
    li       t1, -8
    bne      t2, t1, _fail
    lw       t2, 12(t0)
    li       t1, 15
    bne      t2, t1, _fail

    # Independently assert every vector-scalar product lane.
    la       t0, result_vx
    lw       t2, 0(t0)
    li       t1, -6
    bne      t2, t1, _fail
    lw       t2, 4(t0)
    li       t1, 9
    bne      t2, t1, _fail
    lw       t2, 8(t0)
    li       t1, -12
    bne      t2, t1, _fail
    lw       t2, 12(t0)
    li       t1, -15
    bne      t2, t1, _fail

    # Assert the reduction result.
    la       t0, result_sum
    lw       t2, 0(t0)
    li       t1, -2
    bne      t2, t1, _fail

_pass:
    la       t0, tohost
    li       t1, 1              # HTIF test-pass code.
    sd       t1, 0(t0)
1:
    j        1b

_fail:
    la       t0, tohost
    li       t1, 3              # HTIF test-fail code (exit status 1).
    sd       t1, 0(t0)
2:
    j        2b

    .section .data
    .balign 16
lhs:
    .word 2, -3, 4, 5
rhs:
    .word 6, 7, -2, 3
reduction_seed:
    .word 0, 0, 0, 0
result_vv:
    .word 0, 0, 0, 0
result_vx:
    .word 0, 0, 0, 0
result_sum:
    .word 0

    .balign 8
    .globl tohost
tohost:
    .dword 0
    .globl fromhost
fromhost:
    .dword 0
