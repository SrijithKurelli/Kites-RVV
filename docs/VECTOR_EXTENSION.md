# Vector extension support in Kites (selected RVV subset)

This document describes the selected vector instruction subset implemented in
Kites. The functional subset is available in the single-cycle RVSS processor and
in all four five-stage pipeline variants. This is not a claim of complete
RISC-V Vector (RVV 1.0) compliance.

## Implemented state

- 32 vector registers (`v0`-`v31`), each 128 bits wide (`VLEN=128`).
- `LMUL=1` only.
- `SEW` values of 8, 16, 32, or 64 bits.
- Current `vl` and `sew` are maintained by RVSS and by each five-stage processor.
- Vector register values are visible in the GUI's Vector register section,
  in the CLI REPL `regs` output, and in `registers_dump.json` under `vec_registers`.
- The CLI REPL `info` command reports the active vector configuration
  (`VLEN`, `LMUL`, `SEW`, `VL`, and `VLMAX`) for RVSS and all four five-stage
  processor variants.

## Supported assembly instructions

### `vsetvli rd, rs1, zimm` and `vsetivli rd, uimm, zimm`

Both configuration instructions support only `zimm` values `0`, `8`, `16`, and `24`.
These select `SEW=8`, `16`, `32`, and `64` respectively, with `LMUL=1` and
undisturbed tail/mask policy bits. The vector length is
`vl = min(AVL, VLMAX)`, where `VLMAX = VLEN / SEW`.

For this subset, the architectural AVL special cases are implemented:

- `rs1=x0`, `rd!=x0`: AVL is the maximum unsigned value, so `vl=VLMAX`.
- `rs1=x0`, `rd=x0`: the current `vl` is reused and preserved only when
  the new configuration leaves `VLMAX` unchanged. Any `VLMAX` change—an
  increase or a reduction—is reserved and traps in this subset.
- Otherwise AVL is read from `x[rs1]`.

When `rd` is not `x0`, the selected `vl` is written to `x[rd]`.

`vsetivli` uses a zero-extended 5-bit immediate AVL (`uimm=0..31`) in the
`rs1` field rather than reading a GPR. It shares the same supported VTYPE
values (`zimm=0,8,16,24`), computes `vl=min(uimm,VLMAX)`, and writes the result
to `rd` unless `rd=x0`. An immediate AVL of zero means `vl=0`; it does not
invoke the `vsetvli rs1=x0` special cases. Its disassembly prints the immediate
AVL and the 10-bit VTYPE field.
The disassembler preserves the full 11-bit `zimm[10:0]` field when printing
`vsetvli` (the assembler intentionally accepts only the four supported
configurations above).

### `vadd.vv vd, vs2, vs1` and `vsub.vv vd, vs2, vs1`

These operations use the current `SEW` and operate on the first `vl` elements.
Addition and subtraction wrap modulo the element width. Each form also accepts
an optional `, v0.t` operand (for example, `vadd.vv vd, vs2, vs1, v0.t`).
When masked, only lanes with the corresponding `v0` predicate bit set execute;
masked-off destination lanes remain unchanged. Elements beyond `vl` are preserved
(tail-undisturbed).

For masked instructions, `v0` is an implicit 1-bit predicate source. This subset
enforces the RVV overlap constraints: an ordinary masked vector result cannot
target `v0`, and an ordinary vector data source cannot also be `v0` because
that would read one register with both EEW=1 (mask) and EEW=SEW (data). The
exceptions are mask-producing comparisons (which may write `v0`), scalar-result
reductions (which may write `v0`), and `vcpop.m` using `v0` as its mask-data
source. Masked vector loads to `v0` and masked vector stores from `v0` are
reserved and trap in this subset.

Vector instructions use the standard 32-bit instruction word layout for the
implemented encodings. The assembly operand order is `vd, vs2, vs1`.

### Selected integer vector multiplication

The subset implements two standard integer vector multiply forms in the RVSS
processor. Each operation returns only the low `SEW` bits of each product; it
does not produce widened or high-half results. Therefore the low-bit result is
the same for signed and unsigned interpretations of the input bit patterns.

- `vmul.vv vd, vs2, vs1` — multiply corresponding elements of two vector
  registers.
- `vmul.vx vd, vs2, rs1` — multiply each element of `vs2` by the scalar
  integer value in GPR `x[rs1]`, truncated to the configured `SEW`.

Both forms accept an optional trailing `, v0.t` predicate. Only active lanes
below `vl` are written; masked-off lanes and tail elements remain undisturbed.
The vector-vector form uses OPMVV (`funct6=0b100101`,
`funct3=0b010`); the vector-scalar form uses OPMVX
(`funct6=0b100101`, `funct3=0b110`). Both use vector opcode
`0x57`.

A dot product composes `vmul.vv` with the existing `vredsum.vs` reduction.
SAXPY composes `vmul.vx` with `vadd.vv`. These kernels use standard vector
instructions; no custom dot-product or multiply-accumulate opcode is introduced.

### Additional integer arithmetic, min/max, and shifts

The selected integer instruction set also includes the following standard forms:

- `vadd.vx vd, vs2, rs1` and `vsub.vx vd, vs2, rs1` add or subtract the
  scalar GPR value `x[rs1]` from each active element of `vs2`. The scalar is
  truncated to the low SEW bits and the result wraps modulo SEW.
- `vmin.vv` and `vmax.vv` select the signed minimum or maximum of each pair
  of elements.
- `vminu.vv` and `vmaxu.vv` select the unsigned minimum or maximum.
- `vsll.vv`, `vsrl.vv`, and `vsra.vv` perform logical left, logical right,
  and arithmetic right shifts respectively. Each lane's shift amount uses only
  the low `log2(SEW)` bits of the corresponding element in `vs1`; arithmetic
  right shift sign-extends within the configured SEW.

These forms use the standard OPIVX/OPIVV vector encodings and vector opcode
`0x57`. They operate on active lanes below `vl`, accept the optional
`, v0.t` predicate, and preserve masked-off and tail destination elements.
The test suite pins the nine new assembler/disassembler encodings and checks
their results at all four supported SEW values.

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

`vcpop.m rd, vs2` uses the standard OP-MVV encoding (`funct6=0b010000`, `funct3=0b010`, `vs1=0b10000`) and counts set predicate bits in the active portion (`0..vl-1`)
of vector register `vs2` and writes the count to scalar integer register `rd`.
An optional trailing `, v0.t` counts only elements enabled by `v0`. This lets an
assembly sequence count elements selected by a preceding comparison.

### Selected vector bit-manipulation operations

This increment adds a selected subset of Zvbb-style vector operations. Only
the vector-vector or per-element vector forms are supported; vector-scalar and
vector-immediate variants are not included.

Per-element bit counts (each accepts an optional trailing `, v0.t`):

- `vclz.v vd, vs2` — count leading zero bits in each SEW-sized element.
- `vctz.v vd, vs2` — count trailing zero bits in each element.
- `vcpop.v vd, vs2` — count set bits in each element.

For `vclz.v` and `vctz.v`, a zero-valued input element produces `SEW`.
Population count is performed independently for each active element.

Vector-vector bitwise and rotate operations (each accepts an optional
trailing `, v0.t`):

- `vandn.vv vd, vs2, vs1` — bitwise AND of `vs2` with the element-wise
  complement of `vs1`.
- `vrol.vv vd, vs2, vs1` — rotate each `vs2` element left by the
  corresponding `vs1` element's rotate amount.
- `vror.vv vd, vs2, vs1` — rotate each `vs2` element right by the
  corresponding `vs1` element's rotate amount.

Rotate amounts use only their low `log2(SEW)` bits. Masked-off and tail
destination elements remain undisturbed. This is a selected bit-manipulation
subset, not a complete Zvbb implementation.

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
and replay them. Masked forms use the `v0` predicate and skip inactive lanes.

### Strided vector memory instructions

The selected strided one-field forms are also supported:

- `vlse8.v vd, (rs1), rs2` and `vsse8.v vs3, (rs1), rs2`
- `vlse16.v vd, (rs1), rs2` and `vsse16.v vs3, (rs1), rs2`
- `vlse32.v vd, (rs1), rs2` and `vsse32.v vs3, (rs1), rs2`
- `vlse64.v vd, (rs1), rs2` and `vsse64.v vs3, (rs1), rs2`

`rs1` is the scalar base address. `rs2` supplies a signed byte stride, and
active lane `i` accesses `base + i * stride`; positive, zero, and negative
strides are supported. `EEW` must equal the configured `SEW`. An optional
`, v0.t` mask skips inactive memory accesses. Masked-off and tail destination
lanes remain undisturbed. Strided stores validate all active lane addresses and
capture undo data before writing, preventing a partial store if a later active
lane is out of range. Indexed and segment vector memory operations are not
supported.

### Cross-product and matrix-multiplication kernels

The examples demonstrate higher-level computations composed from the selected
vector instructions; no custom cross-product or matrix-multiply opcodes are
introduced.

- `vector_cross_product.s` computes
  `[Ay*Bz-Az*By, Az*Bx-Ax*Bz, Ax*By-Ay*Bx]`. It loads prearranged operand
  streams, uses two `vmul.vv` instructions and one `vsub.vv`, then stores
  the three results. Prearranging the operands avoids requiring a vector
  permutation instruction.
- `vector_matrix_multiply_2x2.s` multiplies two 2x2 matrices. The right-hand
  matrix is stored as columns. Each output element is a row/column dot product
  using `vmul.vv` followed by `vredsum.vs`; each scalar result is stored
  using the existing vector store path. The example output is
  `[[19,22],[43,50]]`.
- `vector_matrix_multiply_3x3.s` extends the same composition to 3x3 matrices:
  it loads three rows and three columns, computes nine dot products using
  `vmul.vv` + `vredsum.vs`, and stores the result in row-major order. Its
  example output is `[[30,24,18],[84,69,54],[138,114,90]]`.

These are SEW=32 integer demonstrations, not a general matrix library or an
overflow-safe arbitrary-precision implementation.

## Runnable example programs


The repository includes end-to-end assembly examples in `examples/vector/`:

- `vector_load_add_store.s` — unit-stride load, vector add, and store.
- `vector_compare_count.s` — compare corresponding elements and count matches.
- `vector_masked_add_store.s` — compare, masked arithmetic, and masked store.
- `vector_average_selected.s` — masked sum, population count, zero-count guard, and integer average.
- `vector_bitmanip.s` — selected vector AND-NOT, rotate, leading/trailing-zero, and population-count operations.
- `vector_strided_memory.s` — positive-stride SEW=32 load/store, selecting every second word.
- `vector_multiply_dot_product.s` — vector-vector multiplication and dot product using `vmul.vv` plus `vredsum.vs`.
- `vector_saxpy.s` — SAXPY (`alpha*x + y`) using `vmul.vx` and `vadd.vv`.
- `vector_immediate_config.s` — immediate AVL configuration using `vsetivli`.
- `vector_cross_product.s` — 3D cross product using two `vmul.vv` operations and `vsub.vv`.
- `vector_matrix_multiply_2x2.s` — 2x2 matrix multiplication using vector multiplication and sum reductions.
- `vector_matrix_multiply_3x3.s` — 3x3 matrix multiplication using nine vector dot products.

See `examples/vector/README.md` for commands and expected results. The separate abstract timing assumptions and CLI are documented in `docs/VECTOR_PERFORMANCE_MODEL.md`. A focused independent Spike comparison is described in `docs/SPIKE_REFERENCE.md`.

## Five-stage pipeline integration

All four five-stage processor variants use the shared vector semantic path in
`RV5StageVM_Base`. The pipeline subset covers the documented vector configuration,
integer arithmetic and logical operations (including the selected scalar add/
subtract, min/max, and shift forms), comparisons, reductions, selected
bit-manipulation and bit-count operations, mask population count, and the
supported unit-stride and strided one-field vector memory forms. Vector results
are carried through the pipeline writeback path; vector loads/stores use the
pipeline memory stage, and vector register/memory state participates in undo/redo.

The four pipeline configurations retain their scalar hazard/forwarding behavior:

- `H_F` (`rv5-h-f`): hazard detection and forwarding.
- `H_NF` (`rv5-h-nf`): hazard detection without forwarding; dependent operations
  are stalled until their operands are available.
- `NH_F` (`rv5-nh-f`): forwarding without hazard detection; software must schedule
  bubbles for hazards that forwarding cannot cover, notably load-use dependencies.
- `NH_NF` (`rv5-nh-nf`): neither hazard detection nor forwarding; programs must
  explicitly schedule the required bubbles between dependent operations.

The automated pipeline tests exercise dependency behavior, all supported SEWs
for arithmetic/reduction and unit-stride memory, and vector-memory undo/redo in
all four variants. These tests validate the selected subset, not full RVV support.

## Current limitations

- RVSS and all four five-stage variants implement the selected vector subset
  described here. The same instruction families are shared across the pipeline
  variants, but each model retains its distinct hazard-detection/forwarding
  behavior described above; software scheduling is required in the two models
  without hazard detection.
- Only `LMUL=1` and `SEW=8/16/32/64` are supported.
- The documented arithmetic, logical, comparison, reduction, bit-manipulation, and unit-stride/strided memory operations are supported in RVSS and all four pipeline variants. Masked forms use `v0.t`; masked-off and tail destination lanes remain undisturbed.
- Unit-stride and selected strided vector loads/stores require `EEW=SEW`; differing element widths, indexed/segment operations, and nonzero `nf` are not implemented.
- Only the selected bit-manipulation instructions `vandn.vv`, `vrol.vv`, `vror.vv`, `vclz.v`, `vctz.v`, and `vcpop.v` are implemented; the rest of Zvbb is not included.
- Average is composed from the supported masked reduction, mask population count, a scalar load of the reduction result, and the base ISA `div` instruction. The documented convention is signed integer division (round toward zero); programs must guard a zero count and choose an application-specific result. Indexed/segment memory, vector floating-point, widening/narrowing, permutation, and many other RVV operations remain unimplemented.

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

## Average of elements selected by a comparison

The subset does not add a non-standard average instruction. An average can be
constructed from the supported vector reduction and population count, then
finished with scalar integer division. This example averages the elements in
`v1` that equal the corresponding elements in `v2` (SEW=32):

```asm
addi x6, x0, 4
addi x10, x0, 256       # scratch address for the reduction result
vsetvli x7, x6, 16      # SEW=32, VL=4
vmseq.vv v0, v1, v2     # select matching lanes
vredsum.vs v5, v1, v4, v0.t  # v4[0] must be initialized to zero
vse32.v v5, (x10)
lw x11, 0(x10)          # scalar sum
vcpop.m x12, v0         # scalar count
beq x12, x0, no_matches
div x13, x11, x12       # signed integer quotient, rounded toward zero
jal x0, average_done
no_matches:
addi x13, x0, 0         # application-defined empty-set result
average_done:
```

The example assumes non-negative input values whose sum fits in the signed
32-bit range, and a valid scratch-memory address. The reduction itself wraps
modulo SEW, so a wider or signed-overflow-safe accumulation is not provided by
this subset. Integer division truncates toward zero. The empty-set result is
explicitly selected by the caller; this example uses zero. The reduction seed
in `v4[0]` must be initialized before execution.

## Tests

`test/vector_extension_test.cpp` covers 128-bit register storage/reset,
instruction encoding, arithmetic (including the additional scalar add/sub, signed/unsigned min/max, and shifts), logical and selected bit-manipulation operations at all supported SEWs, comparison masks, masked arithmetic/memory/reduction/bit-count operations, reductions, mask population count, positive and negative average-from-sum/count rounding (including `-5 / 2 = -2`) and zero-count handling,
`vsetvli` AVL special cases, unit-stride load/store encodings and execution,
tail preservation, memory undo/redo, unsupported vector memory forms, and
strided memory assembly/execution across SEWs, zero stride, negative stride
with masking, and active-address store preflight. Vector multiply tests cover OPMVV and OPMVX assembly encoding/disassembly,
low-bit wraparound at all four SEWs, scalar sign/boundary inputs, and
masked/tail preservation. The 3x3 matrix test checks all nine output elements.
The five-stage pipeline tests cover dependent vector arithmetic/logical,
comparison-mask, reduction and population-count sequences; the additional
scalar add/sub, signed/unsigned min/max, and shift operations at all SEWs;
vector memory in all four pipeline configurations; all-SEW arithmetic/reduction
and unit-stride memory; and memory undo/redo round trips. The tests explicitly
distinguish hazard-detection/forwarding behavior between `H_F`, `H_NF`, `NH_F`,
and `NH_NF`.
`test/register_model_test.cpp` checks vector display in signed decimal,
zero-padded hexadecimal, grouped binary, and each supported element width.
The latest full validation,
[CI #44031969692](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/44031969692),
passed the Linux build, RVSS vector-example/CLI smoke checks, and CTest:
417 passed, zero failed, one was skipped, and four were disabled (418 active
cases selected by CTest; four additional cases are disabled). This run includes
the all-SEW extended integer-operation tests in RVSS and all four pipeline
variants, as well as the all-SEW arithmetic/reduction and unit-stride memory
tests plus vector-memory undo/redo round trips in all four pipeline variants.
The earlier CI runs above remain historical checkpoints. Spike comparison is
optional, manually triggered, and is not part of the normal CI gate; it has not
yet produced a passing reference result.


## Selected FP32 vector arithmetic (development milestone)

The `rvv/vector-fp32` development branch adds the following OPFVV operations for
`SEW=32`, `LMUL=1`, and the existing 128-bit vector register configuration:

- `vfadd.vv` — lane-wise single-precision addition.
- `vfsub.vv` — lane-wise single-precision subtraction.
- `vfmul.vv` — lane-wise single-precision multiplication.
- `vfdiv.vv` — lane-wise single-precision division.

These operations use the existing scalar single-precision arithmetic helper,
respect the active vector length and mask, and preserve inactive/tail lanes.
Other element widths are rejected for these four instructions. This is a
selected FP32 subset, not complete vector floating-point or full ISA support.
Division-by-zero and floating-point exception behavior currently follow the
existing scalar helper and require a separate conformance review.

