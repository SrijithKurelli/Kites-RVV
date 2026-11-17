# Project Scope Tracker

This tracker follows the approved KITES Simulator Extension for Standard Vector Extension Support project-scope document. It separates required core work from stretch work and identifies optional improvements added during implementation.

Last updated: 2026-11-17 (CI #44044433781 passed: 417 passed, zero failed, one skipped, four disabled; adjacent vector load-use regression coverage added in commit ec5beed; CI #44070164962 is the validation run for that addition; optional Spike comparison remains unvalidated)

## Must-have core scope

| Scope item | Status | Evidence / remaining work |
|---|---|---|
| 32 vector registers, VLEN=128 | Implemented | Register file and Vector GUI section; CLI `regs` output was exercised by the REPL smoke check in CI run #70. |
| SEW 8/16/32/64, LMUL=1 | Implemented and tested | `vsetvli` subset; `vtype` is restricted to four supported encodings; all four SEWs are covered by vector tests. |
| Vector configuration / AVL rules | `vsetvli` and `vsetivli` CI-verified | `vsetivli` adds a 5-bit immediate AVL (0–31), with the supported SEW/VTYPE choices, and uses normal `min(AVL,VLMAX)` semantics. CI #129 passed encoding/disassembly and all-SEW AVL tests; additional parser-boundary, `rd=x0`, and load-compute-store cases are added for the next run. The `vsetvli x0,x0` unchanged-VLMAX rule remains separately tested by CI #126. |
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
| CLI view of vector configuration | Implemented for RVSS and all four RV5 models | REPL `info` reports VLEN, LMUL, SEW, VL and VLMAX for the single-cycle and all four five-stage processors. |
| Assembly examples and automated tests | Implemented and CI-verified | The RVSS workflow smoke-tests the vector examples and CLI inspection. CI #44044433781 passed 417 CTest cases, with zero failures, one skipped case, and four disabled cases; the run includes the new extended integer-operation tests. |
| Scalar regression | Verified | CI #44031969692 reported zero failures: 417 passed, one skipped, and four disabled. The selected CTest set contained 418 cases, including the skipped case. This includes the existing scalar regressions. |
| Scalar-vs-vector comparison | Implemented and CI-verified | CI #70 checks equal four-element-sum results and compares 8 scalar instructions with 4 vector instructions. The separate abstract timing tool compares addition, dot product and 2x2 matrix kernels under configurable scheduling assumptions; CI #125/#126 validated its build, tests and CLI input checks. |
| Short design/validation/limitations report | Updated; final team review pending | `docs/PROJECT_REPORT.md` now describes RVSS and all four five-stage pipeline variants, the abstract timing model, implementation boundaries, and CI #44031969692 evidence. Final team review before submission remains advisable. |

## Stretch scope

| Stretch item | Status | Planned validation |
|---|---|---|
| Integer vector multiplication | Implemented and CI-verified | Added standard `vmul.vv` (OPMVV) and `vmul.vx` (OPMVX), low-SEW-bit products, `v0.t` masking, and tail preservation. CI #119 passed four `vmul.vv`/dot-product tests; CI #122 passed three `vmul.vx` tests; CI #123 passed the cross-product and matrix kernel tests. |
| Dot-product and SAXPY kernels | Implemented and CI-verified | Dot product is `vmul.vv` followed by `vredsum.vs`; SAXPY is `vmul.vx` followed by `vadd.vv`. CI #119 and #122 smoke-tested these examples. |
| Bit counts | Partially implemented; CI-verified | Added per-element `vclz.v`, `vctz.v`, and `vcpop.v`; CI #70 passed tests for zero input, all supported SEWs, masking, and tail preservation. |
| AND-NOT and rotate operations | Partially implemented; CI-verified | Added `vandn.vv`, `vrol.vv`, and `vror.vv`; CI #70 passed tests across all supported SEWs and rotate-amount masking. Scalar/immediate variants and other Zvbb operations remain unimplemented. |
| Strided vector loads/stores | Partially implemented; CI-verified | Added `vlse8/16/32/64.v` and `vsse8/16/32/64.v` with signed byte strides, optional `v0.t`, bounds/overflow checks, and undo/redo support. CI #73 passed tests for encoding/disassembly, all SEWs, zero and negative stride, masking, and store preflight. Indexed and segment forms remain unsupported. |
| Cross-product and matrix kernels | Implemented and CI-verified for 2x2 and 3x3 | Cross product composes two `vmul.vv` operations and `vsub.vv`; 2x2 multiplication composes four multiply/reduction pairs. The 3x3 example composes nine `vmul.vv` + `vredsum.vs` dot products and stores row-major output. The 3x3 result test passed CI #43842617996. |
| Abstract vector-unit timing model | Implemented and CI-verified | Separate `VectorTimingModel` with configurable lanes, startup/group/scalar/reduction/memory latencies, dependencies, serialized memory delay, checked counters/cycles, estimated cycles and lane utilisation. CI #44044433781 rebuilt the model and passed its regression suite. It is an abstract scheduler, not a cycle-accurate processor model. |
| Execution traces and performance statistics | Implemented and CI-verified | Per-instruction issue/start/completion trace, active elements, lane groups/slots, dependency and memory stalls; CLI compares scalar/vector addition, dot product and 2x2 matrix kernels and models memory-delay sensitivity. CI #125/#126 built the tool and passed summary, help and strict-input smoke checks. |
| Selected pipelined vector instructions | Implemented selected subset in all four RV5 variants; CI-verified | A shared vector semantic helper executes the selected configuration, arithmetic/logical, comparison, reduction, bit-count/popcount, extended scalar add/sub, signed/unsigned min/max, vector shifts, and unit-stride/strided memory operations. Commit `ec5beed` adds adjacent vector load-use regression programs for the hazard-detection/forwarding variants and a scheduled-bubble control for NH_NF; its CI result is tracked separately. All four models carry vector results through pipeline writeback and support vector memory undo/redo. CI #44044433781 passed the dependency-chain tests, all-SEW arithmetic/reduction tests, all-SEW extended integer-operation tests, all-SEW unit-stride-memory tests, and memory undo/redo round trips in `H_F`, `H_NF`, `NH_F`, and `NH_NF`. Hazard detection and forwarding remain model-specific; see `VECTOR_EXTENSION.md`. This is a selected subset, not full RVV support. |
| Independent reference / Spike comparison | Optional; not yet validated | A separate manual workflow builds pinned Spike 1.1.1-dev and compares a small bare-metal subset (`vmul.vv`, `vmul.vx`, `vredsum.vs`) using HTIF pass/fail. The comparison has not passed and is not part of the required normal CI gate or core completion criteria. |

## Additional hardening beyond the stated scope

| Extra | Status | Reason |
|---|---|---|
| Cross-platform test runner (`windows.h` guard) | Implemented | Allows Linux CI to compile the existing test suite. |
| Double-precision FMA instruction classification fix | Implemented; latest CI run passed the existing FMA tests | Upstream classifier omitted D-format FMA opcodes, causing existing double-FMA tests to fail. |
| Continuous Qt 6.9.2 Linux build/test workflow | Implemented | Gives repeatable compile and regression feedback. |
| Masked `v0` overlap constraints | Implemented and CI-verified | Masked ordinary vector results/data cannot overlap the `v0` predicate register at a different EEW. Mask-producing comparisons, scalar-result reductions and `vcpop.m` mask-source use are explicit exceptions. Masked vector memory data/destination register `v0` is rejected. All five reserved/permitted/unmasked regression tests passed CI #127. |
| VMUL independent golden encodings | Implemented and CI-verified | Literal machine words for masked and unmasked `vmul.vv` (OPMVV) and `vmul.vx` (OPMVX) supplement the formula-based assembler checks; values were cross-checked against upstream `riscv-opcodes` and LLVM MC tests and passed CI #128. |
| Additional integer vector operations | Implemented and CI-verified | Added `vadd.vx`, `vsub.vx`, `vmin.vv`, `vminu.vv`, `vmax.vv`, `vmaxu.vv`, `vsll.vv`, `vsrl.vv`, and `vsra.vv`. Literal expected encodings/disassembly plus all-SEW execution tests pass in RVSS and all four pipeline configurations in CI #44031969692. |

## Current implementation boundaries

- The documented selected vector subset is implemented in RVSS and in all four five-stage pipeline variants through a shared semantic path. Pipeline hazard/forwarding behavior differs by model as described in `VECTOR_EXTENSION.md`.
- Supported state is VLEN=128, LMUL=1, SEW in {8,16,32,64}.
- Current vector arithmetic includes `vadd.vv`, `vsub.vv`, `vadd.vx`, `vsub.vx`, `vmul.vv`, and `vmul.vx`; multiplication is low-SEW-bit only and supports optional `v0.t` predication. The additional scalar add/sub, min/max, and shift instructions are listed in `VECTOR_EXTENSION.md` and passed all-SEW tests in CI #44031969692.
- Current vector comparisons are `vmseq.vv`, `vmsne.vv`, `vmslt.vv`, and `vmsltu.vv`; they produce packed predicate bits and support optional masking.
- Current vector memory support includes unit-stride and strided one-field loads/stores with EEW=SEW; strided forms use a signed byte stride from a GPR. Optional `v0.t` masks skip inactive lanes. Indexed and segment forms are not implemented.
- Selected Zvbb-style instructions now cover element bit counts (`vclz.v`, `vctz.v`, `vcpop.v`), vector AND-NOT (`vandn.vv`), and vector rotates (`vrol.vv`, `vror.vv`). These have encoding/disassembly and execution coverage in CI #73; they do not constitute full Zvbb support.
- This is an incremental RVV subset, not a claim of full RVV 1.0 compliance.

## Working rules

1. Keep implementation work on `rvv/milestone-1`; do not merge to `main` until review and regression checks pass.
2. Add each newly supported vector mnemonic and its exact operand syntax to `docs/VECTOR_EXTENSION.md`, the assembler tests, and the project update notes.
3. Add tests for encoding, disassembly, execution, SEW boundaries, VL/tail/mask behavior, undo/redo where applicable, and scalar regressions.
4. Mark stretch work separately from must-have scope. Any omitted scope item must be explicitly identified in the final report rather than silently treated as complete.