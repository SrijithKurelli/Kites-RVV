# KITES Vector Extension — Design, Validation, and Limitations

**Course:** CS2323 Computer Architecture  
**Project:** KITES Simulator Extension for Standard Vector Extension Support  
**Team:** Srijith Sai Kurelli (ES24BTECH11034), Srihith Sai Kurelli (ES24BTECH11033)

## 1. Objective and implementation boundary

This project extends KITES with an initial, deliberately selected vector
instruction subset. The implementation target is the single-cycle RVSS
processor. It uses 32 vector registers, each 128 bits wide (VLEN=128), LMUL=1,
and supports SEW values of 8, 16, 32, and 64 bits. This is not full RISC-V
Vector (RVV 1.0) compliance.

## 2. Design overview

The vector register file is maintained alongside the existing general-purpose
and floating-point registers. Each vector register stores 16 bytes. The RVSS
processor tracks the active SEW and VL. The vector execution path dispatches
opcode 0x57 before scalar operand decoding so that vector register fields are
not mistaken for GPR indices.

The implementation reuses the existing assembler, decoder/disassembler,
memory controller, and RVSS undo/redo history. Vector register writes are
captured as 128-bit deltas; vector memory stores record prior and resulting
bytes so that undo and redo can restore or replay the operation. Masked stores
stage their active lanes and check active-lane address bounds before writing,
so a masked-off lane is not accessed and an invalid later active lane does not
leave an earlier lane partially stored.

## 3. Supported state and instructions

### Configuration

The supported `vsetvli rd, rs1, zimm` and `vsetivli rd, uimm, zimm` subset accepts zimm values 0, 8, 16,
and 24, selecting SEW=8, 16, 32, and 64 respectively. LMUL is fixed to 1.
VL is limited by VLMAX=VLEN/SEW and the AVL operand (a GPR for `vsetvli`, or a zero-extended immediate for `vsetivli`). The implemented
`rs1=x0` special forms are described in [VECTOR_EXTENSION.md](VECTOR_EXTENSION.md). In particular, `rs1=x0, rd=x0` preserves the current VL only when VLMAX remains unchanged; an increase or reduction is reserved. The disassembler prints all 11 bits of the encoded `zimm` field, while the assembler accepts only the four supported vtype values.

### Arithmetic, logic, and comparisons

Selected integer multiplication is implemented with `vmul.vv` (OPMVV) and
`vmul.vx` (OPMVX). Both produce the low SEW bits, accept the implemented
`v0.t` mask form, and preserve masked-off and tail elements. High-half and
widening products are not implemented.


Supported vector-vector arithmetic is `vadd.vv` and `vsub.vv`, with
element-width wraparound. Supported logical operations are `vand.vv`,
`vor.vv`, and `vxor.vv`. Equality, inequality, signed less-than, and
unsigned less-than comparisons produce packed predicate bits:
`vmseq.vv`, `vmsne.vv`, `vmslt.vv`, and `vmsltu.vv`.

### Aggregates and masks

The subset includes `vredsum.vs`, `vredmin.vs`, `vredmax.vs`,
`vredand.vs`, `vredor.vs`, and `vredxor.vs`. Reductions use the seed in
`vs1[0]`, fold active `vs2` lanes in lane order, and write `vd[0]`.
`vcpop.m` counts set predicate bits in the active vector length and writes
the count to a GPR. Supported vector-vector operations, reductions, and
population count accept the optional `, v0.t` mask form.

Average is composed from a masked sum, population count, and scalar signed
integer division. The documented convention is truncation toward zero. The
caller handles a zero count explicitly; the example chooses zero. The
reduction wraps modulo SEW, so callers must ensure their sum is in range for
the intended scalar division.

### Selected bit manipulation

A selected Zvbb-style subset is implemented and covered by CI tests:

- `vclz.v`, `vctz.v`, and `vcpop.v` compute leading-zero, trailing-zero, and set-bit counts independently for each active SEW-sized element. A zero input to either zero-count operation returns the configured SEW.
- `vandn.vv` computes `vs2 & ~vs1` element-wise.
- `vrol.vv` and `vror.vv` rotate each element left or right; only the low `log2(SEW)` bits of the corresponding rotate amount are used.

These operations accept the optional `, v0.t` mask form. Masked-off and tail destination elements remain undisturbed. This is only the selected subset listed above, not complete Zvbb support; vector-scalar and vector-immediate forms and other Zvbb instructions are not implemented.

### Memory

The supported vector memory forms are unit-stride one-field loads and stores
for EEW 8, 16, 32, and 64 bits. EEW must equal the configured SEW. The active
lanes transfer contiguously through the existing memory controller. Masked
loads preserve inactive destination lanes; masked stores skip inactive
addresses. Indexed, segment, whole-register, and fault-only-first forms are
outside this initial subset.

The selected strided forms `vlse8/16/32/64.v` and `vsse8/16/32/64.v` are
also implemented. Their `rs2` GPR supplies a signed byte stride, so lane `i`
accesses `base + i * stride`; the stride may be positive, zero, or negative.
The implementation preserves inactive/tail destination elements, skips
masked-off memory accesses, validates each active element's full address range,
and stages stores before writing so a later invalid active lane cannot leave a
partial store. Strided memory undo/redo is integrated with the existing history.

## 4. Tooling and state inspection

Assembler, decoder/disassembler, and RVSS execution are connected for the
supported mnemonics. The GUI's vector register view and the CLI's `regs`
command expose the 32 vector registers. The CLI's `info` command reports
VLEN, LMUL, SEW, VL, and VLMAX for RVSS; the JSON register dump contains
`vec_registers`.

## 5. Validation strategy and evidence

Higher-level assembly kernels demonstrate the selected primitives:
`vector_multiply_dot_product.s` uses `vmul.vv` plus `vredsum.vs`;
`vector_saxpy.s` uses `vmul.vx` plus `vadd.vv`;
`vector_cross_product.s` uses two `vmul.vv` operations and `vsub.vv`;
and `vector_matrix_multiply_2x2.s` evaluates four row/column dot products
using vector multiply and reduction. These are composed kernels, not custom
ISA instructions.


Validation uses hand-calculated expected values and the existing CMake/CTest
suite. The vector-specific tests cover register storage/reset, instruction
encoding/disassembly, all supported SEWs, arithmetic wraparound, logical
operations, comparison boundaries and packed masks, reduction results,
population counts, VL/VLMAX configuration, masking and tail preservation,
unit-stride memory operations, store bounds preflight, undo/redo, and invalid
forms.

The eleven vector example programs under `examples/vector/` provide runnable
load-compute-store, comparison/count, masked-memory, average,
selected bit-manipulation, strided-memory, multiplication/dot-product, SAXPY,
cross-product, and 2x2 matrix-multiplication workflows.
The test suite also compares a four-element scalar sum with the vector
reduction equivalent using dynamic instruction counts: the scalar sequence
uses 8 instructions and the vector sequence uses 4, with both producing 10.
This count comparison is a simple architectural metric; it does not claim a
wall-clock speedup or a cycle-accurate timing model. The average tests cover
positive `5 / 2 = 2`, negative `-5 / 2 = -2`, truncation toward zero, and an
explicit zero-count branch. The selected bit-manipulation tests cover assembler
encoding/disassembly, per-element bit counts and AND-NOT/rotates for every
supported SEW, plus masked bit-count preservation of inactive and tail elements.
VMUL assembly tests also include literal golden machine words for masked
and unmasked OPMVV/OPMVX encodings. The field definitions were cross-checked
against [riscv-opcodes](https://github.com/riscv/riscv-opcodes/blob/master/extensions/rv_v)
and the [LLVM RISC-V vector MC tests](https://github.com/llvm/llvm-project/blob/main/llvm/test/MC/RISCV/rvv/mul.s).
Strided-memory tests cover encoding/disassembly, all SEWs, zero stride, negative
stride with masking, and active-lane store preflight; all five passed in CI run #73.
CI run #73 (commit `aa963b9`) reports 100% tests passed with 0 failures out of
343 CTest tests in 19.77 seconds; four tests were disabled and one was skipped.
Subsequent CI #119 passed 346 tests, with one skipped and four disabled,
validating `vmul.vv` and the dot-product example. CI #122 passed 349 tests,
with one skipped and four disabled, validating `vmul.vx` and the SAXPY smoke
test. CI #123 passed 351 tests, with one skipped and four disabled; the
cross-product and 2x2 matrix-kernel tests passed and both examples smoke-tested. See [CI #119](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/43742024859)
and [CI #122](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/43743377319). See [CI #123](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/43744532237) and [CI #125](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/43748923884). CI #125 passed 359 tests, skipped one, disabled four, and had zero failures (32.16 seconds); it validated the initial timing-model tests and CLI summary/help checks. CI #126 passed 364 tests, skipped one, disabled four, and had zero failures (18.15 seconds); it validated vector configuration/disassembly edge cases, timing input/overflow hardening and strict CLI argument checks. CI #127 passed 369 tests, skipped one and disabled four with zero failures (20.53 seconds); all five masked-`v0` overlap, exception and unmasked-use regression tests passed. The clean build and ten-example/timing-CLI smoke checks also passed.
The workflow built the CLI and test suite under Linux with Qt 6.9.2 and GCC,
executed all ten runnable assembly examples on RVSS (including bit-manipulation,
strided-memory, multiplication and matrix/cross-product kernels), asserted that
the average example prints `2`, and
exercised the REPL `info` and `regs` commands by checking vector configuration
and register output. See [CI run #73](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/42468840913).

## 6. Abstract performance model

The project adds a separate `VectorTimingModel` and the
`kites-vector-timing` command-line tool. This model estimates operation
schedules for scalar and vector streams representing four-element addition,
dot product, 2x2 matrix multiplication, and vector load-add-store.

Its configurable assumptions include lane count, issue interval, scalar ALU
latency, vector startup and lane-group latency, reduction overhead, base
memory latency, and extra memory delay. It tracks register readiness for
read-after-write and write-after-write dependencies and assumes one serialized
memory resource. Its trace reports issue, start, and completion cycles, active
elements, lane groups and slots, dependency stalls, and memory stalls. Summary
statistics include estimated cycles, lane utilization, active elements per
cycle, and scalar/vector estimated speedup.

This is an abstract scheduling model, **not cycle-accurate hardware or pipeline
simulation**. It does not model caches, memory banks, vector register ports,
precise pipeline stages, out-of-order issue, or full processor microarchitecture.
Its scalar/vector streams are hand-authored representatives, not extracted
dynamically from RVSS execution. The assumptions and limitations are documented
in [VECTOR_PERFORMANCE_MODEL.md](VECTOR_PERFORMANCE_MODEL.md).

## 7. Known limitations

- Vector execution is implemented only in the single-cycle RVSS processor.
- VLEN is fixed at 128 bits; LMUL is fixed at 1; supported SEW values are
  8, 16, 32, and 64 bits.
- Only the selected integer arithmetic, multiplication, logical, comparison, reduction,
  population-count, unit-stride memory, and strided memory operations listed
  above are implemented.
- `vsetvl`, other vector configuration encodings, and official vector CSRs
  are not implemented. `vsetivli` supports the 5-bit immediate AVL 0–31.
- Tail and masked-off destination elements follow the undisturbed behavior
  implemented by this subset. Masked operations treat `v0` as the implicit
  EEW=1 predicate source: ordinary data sources and ordinary vector destinations
  cannot overlap it. Mask-producing comparisons and scalar-result reductions may
  write `v0`; `vcpop.m` may use `v0` as its mask-data source. Masked vector loads
  into and stores from `v0` are reserved. Full RVV agnostic policies, register
  groups, and other overlap constraints outside this subset remain unimplemented.
- Vector floating-point, fixed-point, widening/narrowing, and permutation
  operations are not implemented. Only the selected bit-manipulation subset
  described above is present; the remaining Zvbb operations are not implemented.
- Indexed/segment/fault-only-first/whole-register memory operations are not
  implemented. Strided memory is limited to the eight one-field EEW=SEW forms
  listed above.
- The abstract timing model does not integrate vector instructions into the
  five-stage pipeline variants and is not a cycle-accurate processor model.
  The five-stage pipeline path currently tracks GPR/FPR state and hazards only;
  safe vector support needs coordinated vector state, decode, execution,
  writeback and dependency logic.
  An architecture audit found that pipeline registers and hazard detection
  currently track GPR/FPR state only; a correct vector pipeline extension needs
  coordinated vector state, decode, execution, writeback and hazard changes.

## 8. Stretch work and deferred items

The project scope identified bit-manipulation operations, strided memory,
performance modeling, pipeline integration, and independent reference
comparisons as stretch work. The selected bit-count, AND-NOT, rotate, strided-
memory, multiplication and composed-kernel subsets have been implemented and
CI-verified. The abstract vector timing model is implemented and CI-verified.
Remaining Zvbb operations, indexed/segment memory, integration into the
five-stage pipeline, and independent reference comparison remain deferred.
The limitations above make those boundaries explicit.
