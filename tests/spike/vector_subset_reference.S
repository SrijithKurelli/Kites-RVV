    .section .text.start
    .globl _start
    .globl _done
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

    # 12 - 21 - 8 + 15 = -2; v0 is zero-initialized by Spike.
    vredsum.vs v5, v3, v0
    vsetivli x0, 1, e32, m1, tu, mu
    la       t6, result_sum
    vse32.v  v5, (t6)

    # Bring outputs into GPRs so the Spike debugger can report them.
    la       t0, result_vv
    lw       a0, 0(t0)
    lw       a1, 4(t0)
    lw       a2, 8(t0)
    lw       a3, 12(t0)

    la       t0, result_vx
    lw       a4, 0(t0)
    lw       a5, 4(t0)
    lw       a6, 8(t0)
    lw       a7, 12(t0)

    la       t0, result_sum
    lw       s0, 0(t0)

_done:
    j _done

    .section .data
    .balign 16
lhs:
    .word 2, -3, 4, 5
rhs:
    .word 6, 7, -2, 3
result_vv:
    .word 0, 0, 0, 0
result_vx:
    .word 0, 0, 0, 0
result_sum:
    .word 0
