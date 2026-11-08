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

The supported `vsetvli rd, rs1, zimm` subset accepts zimm values 0, 8, 16,
and 24, selecting SEW=8, 16, 32, and 64 respectively. LMUL is fixed to 1.
VL is limited by VLMAX=VLEN/SEW and the scalar AVL value. The implemented
`rs1=x0` special forms are described in [VECTOR_EXTENSION.md](VECTOR_EXTENSION.md).

### Arithmetic, logic, and comparisons

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

Validation uses hand-calculated expected values and the existing CMake/CTest
suite. The vector-specific tests cover register storage/reset, instruction
encoding/disassembly, all supported SEWs, arithmetic wraparound, logical
operations, comparison boundaries and packed masks, reduction results,
population counts, VL/VLMAX configuration, masking and tail preservation,
unit-stride memory operations, store bounds preflight, undo/redo, and invalid
forms.

The six example programs under `examples/vector/` provide runnable
load-compute-store, comparison/count, masked-memory, average,
selected bit-manipulation, and strided-memory workflows.
The test suite also compares a four-element scalar sum with the vector
reduction equivalent using dynamic instruction counts: the scalar sequence
uses 8 instructions and the vector sequence uses 4, with both producing 10.
This count comparison is a simple architectural metric; it does not claim a
wall-clock speedup or a cycle-accurate timing model. The average tests cover
positive `5 / 2 = 2`, negative `-5 / 2 = -2`, truncation toward zero, and an
explicit zero-count branch. The selected bit-manipulation tests cover assembler
encoding/disassembly, per-element bit counts and AND-NOT/rotates for every
supported SEW, plus masked bit-count preservation of inactive and tail elements.
Strided-memory tests cover encoding/disassembly, all SEWs, zero stride, negative
stride with masking, and active-lane store preflight; all five passed in CI run #73.
CI run #73 (commit `aa963b9`) reports 100% tests passed with 0 failures out of
343 CTest tests in 19.77 seconds; four tests were disabled and one was skipped.
The workflow built the CLI and test suite under Linux with Qt 6.9.2 and GCC,
executed all six runnable assembly examples on RVSS (including bit-manipulation
and strided-memory examples), asserted that the average example prints `2`, and
exercised the REPL `info` and `regs` commands by checking vector configuration
and register output. See [CI run #73](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/42468840913).

## 6. Known limitations

- Vector execution is implemented only in the single-cycle RVSS processor.
- VLEN is fixed at 128 bits; LMUL is fixed at 1; supported SEW values are
  8, 16, 32, and 64 bits.
- Only the selected integer arithmetic, logical, comparison, reduction,
  population-count, unit-stride memory, and strided memory operations listed
  above are implemented.
- `vsetivli`, `vsetvl`, other vector configuration encodings, and official
  vector CSRs are not implemented.
- Tail and masked-off destination elements follow the undisturbed behavior
  implemented by this subset. Full RVV agnostic policies, register groups,
  and illegal group-overlap rules are outside the implementation boundary.
- Vector floating-point, fixed-point, widening/narrowing, and permutation
  operations are not implemented. Only the selected bit-manipulation subset
  described above is present; the remaining Zvbb operations are not implemented.
- Indexed/segment/fault-only-first/whole-register memory operations are not
  implemented. Strided memory is limited to the eight one-field EEW=SEW forms
  listed above.
- The implementation does not add a cycle-stepped vector unit, vector
  pipeline hazard handling, a complete RVV reference comparison, or the
  associated performance timing model.

## 7. Stretch work and deferred items

The project scope identifies bit-manipulation operations, strided memory,
cycle-stepped execution/performance modeling, pipeline integration, and
independent reference comparisons as stretch work. The selected bit-count,
AND-NOT, and rotate subset has been implemented and CI-verified. The selected
strided-memory subset has been implemented and CI-verified in run #73. Remaining
bit-manipulation operations, indexed/segment memory, the cycle-stepped model,
pipeline integration, and independent reference comparison remain deferred. The limitations above make those boundaries explicit.
