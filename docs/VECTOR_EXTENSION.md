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

### Unit-stride vector memory instructions

The assembler and RVSS processor support these unmasked forms:

- `vle8.v vd, (rs1)` and `vse8.v vs3, (rs1)`
- `vle16.v vd, (rs1)` and `vse16.v vs3, (rs1)`
- `vle32.v vd, (rs1)` and `vse32.v vs3, (rs1)`
- `vle64.v vd, (rs1)` and `vse64.v vs3, (rs1)`

The base address is read from the scalar GPR `rs1`. Active elements are
transferred contiguously at addresses `base + lane * (EEW / 8)`. For this
initial subset, `EEW` must equal the configured `SEW`, `vl` controls the
number of transferred elements, and load tail elements are preserved. Stores
record their memory changes so the simulator's undo/redo mechanism can reverse
and replay them. Masked, strided, indexed, and segment memory operations are
not supported.

## Current limitations

- Vector instructions are implemented only in the RVSS single-cycle processor.
  The five-stage pipeline processors do not support them.
- Only `LMUL=1` and `SEW=8/16/32/64` are supported.
- Vector arithmetic, loads, and stores support unmasked operations only.
- Unit-stride vector loads/stores require `EEW=SEW`; differing element widths,
  strided/indexed/segment operations, and nonzero `nf` are not implemented.
- Mask operations, vector comparisons, reductions, and other RVV instructions
  are not implemented in this milestone.

## Example load–compute–store sequence

```asm
.text
# x5 points to initialized source bytes; x6 points to destination memory.
addi x7, x0, 4
vsetvli x8, x7, 0       # SEW=8, LMUL=1, VL=4
vle8.v  v1, (x5)        # Load four bytes into v1.
vadd.vv v2, v1, v1      # Double each active element.
vse8.v  v2, (x6)        # Store four result bytes.
```

The source and destination addresses must refer to valid, initialized memory.
The example uses matching EEW and SEW (8 bits).

## Tests

`test/vector_extension_test.cpp` covers 128-bit register storage/reset,
instruction encoding, vector addition/subtraction at all supported SEWs,
`vsetvli` AVL special cases, unit-stride load/store encodings and execution,
tail preservation, memory undo/redo, and unsupported vector memory forms.
The full CMake test suite requires Qt 6.9.2 and GoogleTest.
