# Vector extension support in Kites (initial RVSS milestone)

This document describes the deliberately limited first implementation of vector
instructions in Kites. It is an RVSS single-cycle simulator extension, not a
claim of complete RISC-V Vector (RVV 1.0) compliance.

## Implemented state

- 32 vector registers (`v0`-`v31`), each 128 bits wide (`VLEN=128`).
- `LMUL=1` only.
- `SEW` values of 8, 16, 32, or 64 bits.
- Current `vl` and `sew` are maintained by the RVSS processor.
- Vector register values are visible in the GUI's Vector register section and
  in `registers_dump.json` under `vec_registers`.

## Supported assembly instructions

### `vsetvli rd, rs1, zimm`

The first milestone supports only `zimm` values `0`, `8`, `16`, and `24`.
These select `SEW=8`, `16`, `32`, and `64` respectively, with `LMUL=1` and
undisturbed tail/mask policy bits. The vector length is
`vl = min(AVL, VLMAX)`, where `VLMAX = VLEN / SEW`.

For this subset, the architectural AVL special cases are implemented:

- `rs1=x0`, `rd!=x0`: AVL is the maximum unsigned value, so `vl=VLMAX`.
- `rs1=x0`, `rd=x0`: the current `vl` is reused and preserved. The subset
  rejects this reserved form whenever the new configuration reduces the previous
  `VLMAX`, even if the old `vl` would still fit in the smaller vector length.
- Otherwise AVL is read from `x[rs1]`.

When `rd` is not `x0`, the selected `vl` is written to `x[rd]`.

### `vadd.vv vd, vs2, vs1` and `vsub.vv vd, vs2, vs1`

These operations use the current `SEW` and operate on the first `vl` elements.
Addition and subtraction wrap modulo the element width. The implementation
supports unmasked operations (`vm=1`) only. Elements beyond `vl` are preserved
(tail-undisturbed).

Vector instructions use the standard 32-bit instruction word layout for the
implemented encodings. The assembly operand order is `vd, vs2, vs1`.

## Current limitations

- Vector instructions are implemented only in the RVSS single-cycle processor.
  The five-stage pipeline processors do not support them.
- Only `vsetvli`, `vadd.vv`, and `vsub.vv` are currently implemented.
- Only `LMUL=1` and `SEW=8/16/32/64` are supported.
- Masked arithmetic and other mask operations are not implemented.
- Vector loads/stores, comparisons, reductions, and other RVV instructions are
  not implemented in this milestone.
- There is not yet an assembly-level mechanism to initialize vector registers
  from data memory; the current arithmetic tests seed register contents through
  the simulator's register-file API. Vector memory instructions are a planned
  follow-on milestone.

## Example configuration and arithmetic sequence

```asm
.text
addi x5, x0, 4
vsetvli x6, x5, 0       # SEW=8, LMUL=1, VL=4
vadd.vv v1, v2, v3
vsub.vv v4, v2, v3
```

This shows instruction syntax and configuration. To produce non-zero arithmetic
results, `v2` and `v3` must first be populated; vector memory support is not yet
part of this milestone.

## Tests

`test/vector_extension_test.cpp` contains focused tests for 128-bit register
storage/reset, instruction encoding, configured vector addition, subtraction
wraparound, `vsetvli` AVL special cases, and rejection of unsupported `vtype`
values. The repository's full CMake test suite still requires its documented
Qt 6.9.2 build dependencies.
