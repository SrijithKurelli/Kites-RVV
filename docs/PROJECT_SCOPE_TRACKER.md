# Project Scope Tracker

This tracker follows the approved KITES Simulator Extension for Standard Vector Extension Support project-scope document. It separates required core work from stretch work and identifies optional improvements added during implementation.

Last updated: 2026-11-08 (CI run #64 baseline; latest assertions pending run #66)

## Must-have core scope

| Scope item | Status | Evidence / remaining work |
|---|---|---|
| 32 vector registers, VLEN=128 | Implemented; CI coverage present | Register file and Vector GUI section; verify in current CI run. |
| SEW 8/16/32/64, LMUL=1 | Implemented; CI coverage present | `vsetvli` subset; `vtype` is restricted to four supported encodings. |
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
| Assembly examples and automated tests | Implemented and CI-verified for current subset | All four runnable examples completed in CI run #64; 334 CTest tests passed. The negative signed-average regression and strengthened CLI output assertions are included in the latest branch commit and await CI run #66. |
| Scalar regression | Verified | CI run #64 passed the full CTest suite: 334 passed, 0 failed (20.72 seconds), including existing scalar regressions. The newly added negative-average test awaits run #66. |
| Scalar-vs-vector instruction-count comparison | Implemented and CI-verified | Verified by CI run #64: a four-element sum test checks equal results and compares 8 scalar instructions with 4 vector instructions; this is an instruction-count metric, not a cycle-time claim. |
| Short design/validation/limitations report | Drafted; validation evidence refreshed | `docs/PROJECT_REPORT.md` summarizes architecture, supported instructions, validation approach, and known/deferred limitations; it records CI run #64 as the latest completed green baseline; newer regression/CLI assertions await run #66. Final editorial review remains. |

## Stretch scope

| Stretch item | Status | Planned validation |
|---|---|---|
| Bit counts | Not started | Lane-wise results and boundary tests for each SEW. |
| AND-NOT and rotate operations | Not started | Verify bit semantics and element-width masking. |
| Strided vector loads/stores | Not started | Positive/negative stride, zero stride if supported, and bounds cases. |
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
- Current vector arithmetic includes `vadd.vv` and `vsub.vv`; logical operations include `vand.vv`, `vor.vv`, and `vxor.vv`. These support optional `v0.t` predication.
- Current vector comparisons are `vmseq.vv`, `vmsne.vv`, `vmslt.vv`, and `vmsltu.vv`; they produce packed predicate bits and support optional masking.
- Current vector memory support is unit-stride, one-field load/store with EEW=SEW; optional `v0.t` masks skip inactive lanes.
- This is an incremental RVV subset, not a claim of full RVV 1.0 compliance.

## Working rules

1. Keep implementation work on `rvv/milestone-1`; do not merge to `main` until review and regression checks pass.
2. Add each newly supported vector mnemonic and its exact operand syntax to `docs/VECTOR_EXTENSION.md` and the project update notes.
3. Add tests for encoding, disassembly, execution, SEW boundaries, VL/tail/mask behavior, undo/redo where applicable, and scalar regressions.
4. Mark stretch work separately from must-have scope. Any omitted scope item must be explicitly identified in the final report rather than silently treated as complete.