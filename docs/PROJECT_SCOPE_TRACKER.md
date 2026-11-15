# Project Scope Tracker

This tracker follows the approved KITES Simulator Extension for Standard Vector Extension Support project-scope document. It separates required core work from stretch work and identifies optional improvements added during implementation.

Last updated: 2026-11-15 (after successful CI run #122; cross/matrix kernel CI pending)

## Must-have core scope

| Scope item | Status | Evidence / remaining work |
|---|---|---|
| 32 vector registers, VLEN=128 | Implemented | Register file and Vector GUI section; CLI `regs` output was exercised by the REPL smoke check in CI run #70. |
| SEW 8/16/32/64, LMUL=1 | Implemented and tested | `vsetvli` subset; `vtype` is restricted to four supported encodings; all four SEWs are covered by vector tests. |
| Vector configuration / AVL rules | Implemented for selected `vsetvli` forms | Includes `rs1=x0` and `rd=x0` special handling and regression tests. |
| Selected integer arithmetic | Implemented subset | `vadd.vv`, `vsub.vv`; wraparound and all supported SEWs tested. |
| Selected logical operations | Implemented subset | `vand.vv`, `vor.vv`, `vxor.vv`; unmasked lane-wise behavior tested for every supported SEW. |
| Equality / inequality comparisons | Implemented | `vmseq.vv`, `vmsne.vv`; results are packed predicate bits. |
| Signed / unsigned comparisons | Implemented subset | `vmslt.vv`, `vmsltu.vv`; signed boundary and unsigned comparison tests span all supported SEWs. |
| Comparison masks | Implemented subset | Predicate bits are packed by lane index; comparison results can feed `v0.t` predication and `vcpop.m`. |
| Reductions: sum, min, max | Implemented subset | `vredsum.vs`, `vredmin.vs`, `vredmax.vs`; `vs1[0]` seed and `vd[0]` result; signed min/max. |
| Reductions: AND, OR, XOR | Implemented | `vredand.vs`, `vredor.vs`, `vredxor.vs`; all supported SEWs covered by tests. |
| Count elements matching a condition | Implemented subset | `vcpop.m rd, vs2` counts active set predicate bits and writes a GPR; comparison-to-count sequence tested. |
| Average from sum and count | Implemented as a documented assembly sequence | Masked `vredsum.vs` + `vcpop.m` + scalar `div`; tests cover truncation toward zero and an explicit zero-count branch (the example chooses zero). |
| Masked execution | Implemented subset | Optional `, v0.t` on vector-vector ops, reductions, `vcpop.m`, and unit-stride loads/stores; inactive destination lanes are undisturbed. |
| Inactive and tail element handling | Implemented for current subset | `vta=vma=0` behavior: masked-off and tail destination elements remain unchanged; masked stores skip inactive lanes. |
| Unit-stride vector loads/stores | Implemented subset; CI coverage present | `vle8/16/32/64.v`, `vse8/16/32/64.v`; masked and unmasked forms; requires EEW=SEW. |
| Assembler, decoder and execution integration | Implemented for the selected subset | Every mnemonic listed in `VECTOR_EXTENSION.md` is assembled, decoded/disassembled, and executed on RVSS; this does not imply full RVV support. |
| CLI view of vector registers | Implemented | REPL `regs` prints all 32 128-bit vector registers; JSON dump remains available. |
| CLI view of vector configuration | Implemented for RVSS | REPL `info` reports VLEN, LMUL, SEW, VL and VLMAX; other processor models explicitly report that vector configuration is unavailable. |
| Assembly examples and automated tests | Implemented; latest kernels pending CI | CI #119 ran the dot-product example; CI #122 smoke-tested SAXPY. Cross-product and 2x2 matrix examples and independent result tests are staged for CI #123. |
| Scalar regression | Verified | CI #122 reports 100% tests passed with zero failures out of 350 non-disabled test cases: 349 passed, one skipped; four disabled tests (19.59 seconds). This includes existing scalar regressions. |
| Scalar-vs-vector instruction-count comparison | Implemented and CI-verified | CI run #70 verifies a four-element sum with equal result and compares 8 scalar instructions with 4 vector instructions; this is an instruction-count metric, not a cycle-time claim. |
| Short design/validation/limitations report | Updated and CI-verified | `docs/PROJECT_REPORT.md` summarizes architecture, the core subset plus selected bit-manipulation and strided-memory stretch work, and validation evidence from CI #73. Final team review before submission remains advisable. |

## Stretch scope

| Stretch item | Status | Planned validation |
|---|---|---|
| Integer vector multiplication | Implemented and CI-verified | Added standard `vmul.vv` (OPMVV) and `vmul.vx` (OPMVX), low-SEW-bit products, `v0.t` masking, and tail preservation. CI #119 passed four `vmul.vv`/dot-product tests; CI #122 passed three `vmul.vx` tests. |
| Dot-product and SAXPY kernels | Implemented and CI-verified | Dot product is `vmul.vv` followed by `vredsum.vs`; SAXPY is `vmul.vx` followed by `vadd.vv`. CI #119 and #122 smoke-tested these examples. |
| Bit counts | Partially implemented; CI-verified | Added per-element `vclz.v`, `vctz.v`, and `vcpop.v`; CI #70 passed tests for zero input, all supported SEWs, masking, and tail preservation. |
| AND-NOT and rotate operations | Partially implemented; CI-verified | Added `vandn.vv`, `vrol.vv`, and `vror.vv`; CI #70 passed tests across all supported SEWs and rotate-amount masking. Scalar/immediate variants and other Zvbb operations remain unimplemented. |
| Strided vector loads/stores | Partially implemented; CI-verified | Added `vlse8/16/32/64.v` and `vsse8/16/32/64.v` with signed byte strides, optional `v0.t`, bounds/overflow checks, and undo/redo support. CI #73 passed tests for encoding/disassembly, all SEWs, zero and negative stride, masking, and store preflight. Indexed and segment forms remain unsupported. |
| Cross-product and 2x2 matrix kernels | Implemented on branch; CI pending | Cross product composes two `vmul.vv` operations and `vsub.vv`; 2x2 multiplication composes four multiply/reduction pairs. Independent tests calculate expected results `[-3,6,-3]` and `[[19,22],[43,50]]`. |
| Cycle-stepped vector-unit model | Not started | Explicit operation latency, lanes, dependencies and memory delays. |
| Execution traces and performance statistics | Not started | Stable trace format, cycle/instruction counters, scalar-vs-vector kernel comparison. |
| Selected pipelined vector instructions | Not started | Begin with a deliberately small subset; add hazard and dependency tests. |
| Independent reference / Spike comparison | Not started | Compare supported assembled programs where reference tool/configuration is available. |

## Additional hardening beyond the stated scope

| Extra | Status | Reason |
|---|---|---|
| Cross-platform test runner (`windows.h` guard) | Implemented | Allows Linux CI to compile the existing test suite. |
| Double-precision FMA instruction classification fix | Implemented; latest CI run passed the existing FMA tests | Upstream classifier omitted D-format FMA opcodes, causing existing double-FMA tests to fail. |
| Continuous Qt 6.9.2 Linux build/test workflow | Implemented | Gives repeatable compile and regression feedback. |

## Current implementation boundaries

- Vector instruction execution is currently RVSS single-cycle only; the five-stage pipeline is not yet extended.
- Supported state is VLEN=128, LMUL=1, SEW in {8,16,32,64}.
- Current vector arithmetic includes `vadd.vv`, `vsub.vv`, `vmul.vv`, and `vmul.vx`; multiplication is low-SEW-bit only and supports optional `v0.t` predication. `vmul.vv` passed CI #119; `vmul.vx` passed CI #122.
- Current vector comparisons are `vmseq.vv`, `vmsne.vv`, `vmslt.vv`, and `vmsltu.vv`; they produce packed predicate bits and support optional masking.
- Current vector memory support includes unit-stride and strided one-field loads/stores with EEW=SEW; strided forms use a signed byte stride from a GPR. Optional `v0.t` masks skip inactive lanes. Indexed and segment forms are not implemented.
- Selected Zvbb-style instructions now cover element bit counts (`vclz.v`, `vctz.v`, `vcpop.v`), vector AND-NOT (`vandn.vv`), and vector rotates (`vrol.vv`, `vror.vv`). These have encoding/disassembly and execution coverage in CI #73; they do not constitute full Zvbb support.
- This is an incremental RVV subset, not a claim of full RVV 1.0 compliance.

## Working rules

1. Keep implementation work on `rvv/milestone-1`; do not merge to `main` until review and regression checks pass.
2. Add each newly supported vector mnemonic and its exact operand syntax to `docs/VECTOR_EXTENSION.md` and the project update notes.
3. Add tests for encoding, disassembly, execution, SEW boundaries, VL/tail/mask behavior, undo/redo where applicable, and scalar regressions.
4. Mark stretch work separately from must-have scope. Any omitted scope item must be explicitly identified in the final report rather than silently treated as complete.