# Using the Kites CLI

`kites-cli` provides both a batch runner and an interactive REPL for the Kites
simulator. It is a headless build: it assembles the selected RISC-V source,
runs it on the requested processor model, and writes register, disassembly,
error, and optional memory dumps under `vm_state/`.

## Building

The CLI target is built with the regular CMake project:

```bash
cmake -B build -DCMAKE_PREFIX_PATH="path/to/Qt/6.9.2/<compiler>" .
cmake --build build --target kites_cli
```

The resulting executable is `build/kites-cli` (or
`build/<config>/kites-cli.exe` on multi-config generators).

## Batch mode

```text
kites-cli <assembly-file> [--vm <name>] [--mem-dump <hex_addr> <row_count>]...
```

An assembly file without `--repl` is assembled and run to completion. The
`--mem-dump` option is repeatable and writes the selected 8-byte memory rows
after execution.

### Processor models

| Name | Processor model | Vector support |
|---|---|---|
| `rvss` | Single-cycle RVSS | Supports the initial vector subset described in [VECTOR_EXTENSION.md](VECTOR_EXTENSION.md). |
| `rv5-nh-nf` | Five-stage, no hazard detection, no forwarding | Vector instructions are not implemented. |
| `rv5-h-nf` | Five-stage, hazard detection, no forwarding | Vector instructions are not implemented. |
| `rv5-nh-f` | Five-stage, no hazard detection, forwarding | Vector instructions are not implemented. |
| `rv5-h-f` | Five-stage, hazard detection, forwarding | Vector instructions are not implemented. |

### Example

```bash
./build/kites-cli examples/vector/vector_load_add_store.s --vm rvss
```

The batch run prints program output to stdout and writes state files below
`vm_state/`. Vector register values are included in
`registers_dump.json` under `vec_registers`.

## Interactive REPL

Use `--repl` (or `-i`) to enter the REPL. With no assembly file, the CLI
starts an empty REPL; with a file, it loads that program before showing the
prompt.

```bash
./build/kites-cli --repl
./build/kites-cli examples/vector/vector_load_add_store.s --repl
```

The REPL supports the following commands:

| Command | Purpose |
|---|---|
| `load <file> [--vm <name>]` | Assemble and load a program, optionally changing processor model. |
| `run` | Run the loaded program to completion. |
| `step [n]` / `s [n]` | Execute one instruction or the requested positive number of instructions. |
| `run_debug` / `rd` | Request the debug-paused execution state. Use `step` to advance manually. |
| `undo` / `u` | Reverse the last executed instruction(s) on processor models that implement undo. |
| `redo` / `r` | Replay an undone instruction. |
| `reset` | Reset the selected VM. |
| `add_breakpoint <line>` | Add a source-line breakpoint. |
| `remove_breakpoint <line>` | Remove a source-line breakpoint. |
| `breakpoints` | List configured source-line breakpoints. |
| `modify_register <reg> <value>` / `mreg` | Modify a scalar register or CSR. 128-bit vector registers cannot be changed through this 64-bit command. |
| `regs` | Print GPRs, FPRs, CSRs, and all 32 128-bit vector registers. |
| `info` | Print PC and execution counters; on RVSS, also print VLEN, LMUL, SEW, VL, and VLMAX. |
| `disas` | Print the loaded program's disassembly. |
| `print_mem <hex_addr> <rows>` / `pmem` | Print memory rows. |
| `dump_mem <addr> <rows> [<addr> <rows>]...` / `dmem` | Dump one or more memory ranges to the memory dump file. |
| `vm_stdin <text>` / `vmsin` | Feed text to a program blocked on a supported read syscall. |
| `stop` | Request a stop of the current run. |
| `help` | Show command help. |
| `exit` / `quit` / `q` | Leave the REPL. |

The `info` command reports vector configuration only for RVSS because the
five-stage pipeline processors do not currently implement this vector subset.
The `modify_register` command is scalar-width only; vector register inspection
is supported through `regs` and `registers_dump.json`.

## State output files

All state files are written under the platform's Kites `vm_state/` directory.

| File | Contents |
|---|---|
| `registers_dump.json` | CSRs, all GPRs, all FPRs, and all 32 128-bit vector registers. |
| `disassembly.txt` | Human-readable disassembly, labels, instruction addresses, and raw words. |
| `errors_dump.json` | Empty error list on successful assembly, or parse diagnostics. |
| `memory_dump.json` | Only written when `--mem-dump` or `dump_mem` is used. |

The JSON vector-register section is named `vec_registers`; each register is
represented as a 32-hex-digit value.

## I/O and known limitations

Batch programs can use the supported print and exit syscalls. A program that
reaches a blocking stdin read in batch mode can wait indefinitely because the
batch runner does not supply interactive input. Use the REPL's `vm_stdin`
command for supported interactive reads.

This CLI and vector subset are still an incremental simulator extension, not
a claim of full RVV 1.0 or full pipeline vector support. See
[VECTOR_EXTENSION.md](VECTOR_EXTENSION.md) for exact supported instruction
forms and limitations.
