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
Addition and subtraction wrap modulo the element width. Each form also accepts
an optional `, v0.t` operand (for example, `vadd.vv vd, vs2, vs1, v0.t`).
When masked, only lanes with the corresponding `v0` predicate bit set execute;
masked-off destination lanes remain unchanged. Elements beyond `vl` are preserved
(tail-undisturbed).

Vector instructions use the standard 32-bit instruction word layout for the
implemented encodings. The assembly operand order is `vd, vs2, vs1`.

### Logical vector-vector instructions

The following operations apply bitwise to each active SEW-sized lane and accept an optional trailing `, v0.t` mask operand:

- `vand.vv vd, vs2, vs1`
- `vor.vv vd, vs2, vs1`
- `vxor.vv vd, vs2, vs1`

### Integer vector comparisons

The following vector-vector comparisons compare corresponding signed or unsigned
integer lanes and write one predicate bit per active lane into `vd`. They accept an optional
trailing `, v0.t` mask operand; masked-off predicate bits are left unchanged. Predicate bit
`i` is stored at bit `i` of the destination vector register (packed across bytes):

- `vmseq.vv vd, vs2, vs1` — equal
- `vmsne.vv vd, vs2, vs1` — not equal
- `vmslt.vv vd, vs2, vs1` — signed less-than
- `vmsltu.vv vd, vs2, vs1` — unsigned less-than

The comparisons use the configured SEW. Active predicate bits are updated; other bits
remain unchanged. A comparison result can be used as the predicate register `v0` for
subsequent masked instructions, or counted with `vcpop.m`.
### Vector-wide integer reductions

The following reductions accept an optional trailing `, v0.t` mask operand. They use `vs1[0]` as the initial accumulator, fold
the active elements of `vs2` in lane order, and write the result to `vd[0]`.
Other destination elements are preserved. Sum wraps modulo SEW; min and max use
signed comparisons:

- `vredsum.vs vd, vs2, vs1`
- `vredmin.vs vd, vs2, vs1`
- `vredmax.vs vd, vs2, vs1`
- `vredand.vs vd, vs2, vs1`
- `vredor.vs vd, vs2, vs1`
- `vredxor.vs vd, vs2, vs1`

### Mask population count

`vcpop.m rd, vs2` counts set predicate bits in the active portion (`0..vl-1`)
of vector register `vs2` and writes the count to scalar integer register `rd`.
An optional trailing `, v0.t` counts only elements enabled by `v0`. This lets an
assembly sequence count elements selected by a preceding comparison.

### Unit-stride vector memory instructions

The assembler and RVSS processor support these unit-stride forms. Each accepts an optional trailing `, v0.t` operand:

- `vle8.v vd, (rs1)` and `vse8.v vs3, (rs1)`
- `vle16.v vd, (rs1)` and `vse16.v vs3, (rs1)`
- `vle32.v vd, (rs1)` and `vse32.v vs3, (rs1)`
- `vle64.v vd, (rs1)` and `vse64.v vs3, (rs1)`

The base address is read from the scalar GPR `rs1`. Active elements are
transferred contiguously at addresses `base + lane * (EEW / 8)`. For this
initial subset, `EEW` must equal the configured `SEW`, `vl` controls the
number of transferred elements, and load tail elements are preserved. Stores
record their memory changes so the simulator's undo/redo mechanism can reverse
and replay them. Masked forms use the `v0` predicate and skip inactive lanes; strided, indexed, and segment memory operations are
not supported.

## Current limitations

- Vector instructions are implemented only in the RVSS single-cycle processor.
  The five-stage pipeline processors do not support them.
- Only `LMUL=1` and `SEW=8/16/32/64` are supported.
- Arithmetic, logical, comparison, reduction, and unit-stride memory operations support unmasked and `v0.t` masked forms in RVSS.
- Unit-stride vector loads/stores require `EEW=SEW`; differing element widths,
  strided/indexed/segment operations, and nonzero `nf` are not implemented.
- An end-to-end average example and its rounding/zero-count behavior remain to be completed. Strided memory, pipeline support, and several RVV operations remain unimplemented.

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
The example uses matching EEW and SEW (8 bits). A comparison-based example is:

```asm
vsetvli x8, x7, 0       # SEW=8, VL from x7
vle8.v  v1, (x5)
vle8.v  v2, (x6)
vmsltu.vv v3, v1, v2   # v3 contains packed predicate bits
```

The comparison result is a bit-packed mask vector. To use it for predication, write the
comparison result to `v0` and append `, v0.t` to a supported vector operation.

## Tests

`test/vector_extension_test.cpp` covers 128-bit register storage/reset,
instruction encoding, arithmetic and logical operations at all supported SEWs, comparison masks, masked arithmetic/memory/reduction, reductions and mask population count,
`vsetvli` AVL special cases, unit-stride load/store encodings and execution,
tail preservation, memory undo/redo, and unsupported vector memory forms.
The full CMake test suite requires Qt 6.9.2 and GoogleTest. Masked tests cover `v0.t` lane skipping, inactive/tail preservation, and memory undo/redo.
