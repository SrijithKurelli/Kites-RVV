# Abstract vector execution timing model

## Purpose and boundary

The functional vector instruction implementation executes in the RVSS processor.
This separate model estimates how a selected instruction stream could use a
configurable abstract vector unit. It is intended for architectural comparison
and sensitivity experiments; it is **not cycle-accurate hardware simulation**,
does not replace functional execution, and does not claim measured silicon
performance.

The CLI constructs representative scalar and vector operation streams for:
four-element addition, a four-element dot product, 2x2 matrix multiplication,
and a vector load-add-store sequence. These streams express the same algorithmic
work as the assembly examples, but are authored as timing-model inputs rather
than extracted dynamically from the assembler or RVSS execution trace.

## Configurable assumptions

Default parameters are:

| Parameter | Default | Meaning |
|---|---:|---|
| Vector lanes | 2 | Elements processed by one vector group |
| Issue interval | 1 cycle | Minimum spacing between instruction issues |
| Scalar ALU latency | 3 cycles | Completion latency of a scalar ALU operation |
| Vector startup latency | 2 cycles | Delay between issue and vector execution start |
| Vector group latency | 1 cycle | Latency per group of active vector elements |
| Reduction extra latency | 1 cycle | Additional reduction overhead |
| Memory latency | 4 cycles | Base delay added to each vector load/store |
| Extra memory delay | 0 cycles | Per-memory-operation delay for sensitivity experiments |

Every parameter can be changed from the command line. Run
`./build/ci/kites-vector-timing --help` for the accepted options (or invoke the
tool with no options to use the defaults).

## Scheduling model

Instructions issue in program order through one abstract issue slot. The model
tracks when each named register becomes ready.

- A source operand waits for its previous writer (read-after-write dependency).
- A destination waits for its previous writer (write-after-write dependency).
- Operands are assumed to be read at issue. Consequently, separate
  write-after-read tracking is unnecessary in this abstraction.
- Vector operations use a startup delay plus one latency unit for each lane
  group. The number of groups is
  `ceil(active_elements / lanes)`.
- A reduction adds the configured reduction overhead.
- Vector loads and stores add the base memory latency and the selected
  per-operation extra memory delay.
- One serialized memory resource is assumed: a later vector memory operation
  cannot issue until the previous one has completed. Arithmetic can proceed
  when its operands are ready.

For a vector operation with nonzero active elements:

\[
groups = \left\lceil \frac{active\_elements}{lanes} \right\rceil
\]

\[
completion = issue + startup + groups \times group\_latency
             + reduction\_overhead + memory\_delay
\]

Only the overheads relevant to an operation kind are included. Scalar ALU
operations complete at `issue + scalar_latency`. The estimated program cycle
count is the maximum completion cycle of its operations.

The model does not simulate cache hierarchy, memory banking, vector register
ports, out-of-order issue, precise pipeline stages, branch prediction,
interrupts, bus contention, or the internal microarchitecture of a processor.
It also does not model WAR hazards, because operands are treated as read at
issue. These simplifications are intentional and are part of the documented
assumptions.

## Reported statistics

Each run reports:

- Dynamic instruction count and vector instruction count.
- Total active vector elements and allocated lane slots.
- Lane utilisation: total active elements divided by total allocated lane slots.
- Estimated completion cycles.
- Dependency stall cycles and memory stall cycles.
- Active vector elements per estimated cycle.
- Optional per-instruction trace with issue, start, completion, active elements,
  lane groups, lane slots, dependency stalls and memory stalls.
- Scalar-to-vector estimated speedup for the three arithmetic kernels.

Lane utilisation is aggregated across all vector operations in a kernel. It is
not a measure of physical hardware occupancy.

## Build and run

```bash
cmake -S . -B build/ci -G Ninja \\
  -DCMAKE_BUILD_TYPE=Debug \\
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \\
  -DENABLE_TESTS=ON -DDISABLE_GUI=ON
cmake --build build/ci --target kites_vector_timing kites_test --parallel 2
./build/ci/kites-vector-timing
```

Useful options:

| Option | Meaning |
|---|---|
| `--lanes N` | Number of abstract execution lanes |
| `--issue-interval N` | Minimum cycles between instruction issues |
| `--startup N` | Vector startup latency |
| `--vector-group N` | Latency for each vector lane group |
| `--scalar-latency N` | Scalar ALU latency |
| `--reduction-extra N` | Additional reduction latency (zero is allowed) |
| `--memory-latency N` | Base latency for each vector memory operation |
| `--memory-delay N` | Additional delay applied to each vector load/store |
| `--no-trace` | Print summary counters without instruction traces |

Run multiple configurations to compare lane counts or memory assumptions, for
example:

```bash
./build/ci/kites-vector-timing --lanes 1 --no-trace
./build/ci/kites-vector-timing --lanes 4 --no-trace
./build/ci/kites-vector-timing --lanes 4 --memory-delay 8 --no-trace
```

## Validation

Unit tests cover lane-group rounding and utilisation, read-after-write stalls,
serialized memory stalls, lane-count sensitivity, invalid zero-lane
configurations, and empty programs. The CI workflow builds the timing tool,
runs its summary-mode smoke test, and runs the full CTest suite.

The scalar/vector comparisons are estimates from this model's stated
assumptions. They are not cycle measurements from the functional RVSS
processor and should be presented as such in the project evaluation.
