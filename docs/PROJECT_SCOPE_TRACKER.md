# Project Scope Tracker

This tracker follows the approved KITES Simulator Extension for Standard Vector Extension Support project-scope document. It separates required core work from stretch work and identifies optional improvements added during implementation.

Last updated: 2026-11-07

## Must-have core scope

| Scope item | Status | Evidence / remaining work |
|---|---|---|
| 32 vector registers, VLEN=128 | Implemented; CI coverage present | Register file and Vector GUI section; verify in current CI run. |
| SEW 8/16/32/64, LMUL=1 | Implemented; CI coverage present | `vsetvli` subset; `vtype` is restricted to four supported encodings. |
| Vector configuration / AVL rules | Implemented for selected `vsetvli` forms | Includes `rs1=x0` and `rd=x0` special handling and regression tests. |
| Selected integer arithmetic | Partial | `vadd.vv`, `vsub.vv`; add additional integer operations only after defining exact scope. |
| Selected logical operations | Not started | Implement selected vector logical mnemonics and test lane-wise results. |
| Equality / inequality comparisons | Not started | Produce predicate bits in vector mask state. |
| Signed / unsigned comparisons | Not started | Cover signedness and boundary values for each SEW. |
| Comparison masks | Not started | Define/verify `v0` mask-bit representation and mask destination behavior. |
| Reductions: sum, min, max | Not started | Reduction over active elements; destination placement and signedness tests. |
| Reductions: AND, OR, XOR | Not started | Reduction over active elements. |
| Count elements matching a condition | Not started | Consume comparison mask; write count to an integer register. |
| Average from sum and count | Not started | Define rounding behavior in tests; handle zero count explicitly. |
| Masked execution | Not started | Masked arithmetic, memory and inactive-element policy. |
| Inactive and tail element handling | Partial | Current operations preserve inactive/tail elements; masked-off behavior remains. |
| Unit-stride vector loads/stores | Implemented subset; CI coverage present | `vle8/16/32/64.v`, `vse8/16/32/64.v`; currently unmasked and requires EEW=SEW. |
| Assembler, decoder and execution integration | Partial | Current subset is wired end-to-end; expand as instructions are added. |
| CLI view of vector registers | Partial | `registers_dump.json` includes all vector registers. |
| CLI view of vector configuration | Not started | Add `vl`, `vtype`/SEW and LMUL to a user-facing CLI/JSON state view. |
| Assembly examples and automated tests | Partial | Basic load-compute-store example and tests exist; build out examples for every core category. |
| Scalar regression | In progress | Full test suite runs; current CI results are being investigated and repaired. |
| Short design/validation/limitations report | Not started | Prepare after core behavior and regression results stabilize. |

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
| Double-precision FMA instruction classification fix | Implemented; CI verification pending | Upstream classifier omitted D-format FMA opcodes, causing existing double-FMA tests to fail. |
| Continuous Qt 6.9.2 Linux build/test workflow | Implemented | Gives repeatable compile and regression feedback. |

## Current implementation boundaries

- Vector instruction execution is currently RVSS single-cycle only; the five-stage pipeline is not yet extended.
- Supported state is VLEN=128, LMUL=1, SEW in {8,16,32,64}.
- Current vector arithmetic is unmasked `vadd.vv` and `vsub.vv`.
- Current vector memory support is unmasked unit-stride, one-field load/store with EEW=SEW.
- This is an incremental RVV subset, not a claim of full RVV 1.0 compliance.

## Working rules

1. Keep implementation work on `rvv/milestone-1`; do not merge to `main` until review and regression checks pass.
2. Add each newly supported vector mnemonic and its exact operand syntax to `docs/VECTOR_EXTENSION.md` and the project update notes.
3. Add tests for encoding, disassembly, execution, SEW boundaries, VL/tail/mask behavior, undo/redo where applicable, and scalar regressions.
4. Mark stretch work separately from must-have scope. Any omitted scope item must be explicitly identified in the final report rather than silently treated as complete.