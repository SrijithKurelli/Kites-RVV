# Independent RVV reference check with Spike

## Purpose

This optional check executes a small bare-metal RVV program in Spike, an
independent RISC-V ISA simulator, and checks its scalar-register results against
the golden values already asserted by the Kites RVSS tests.

It covers a narrow common subset:
- vsetivli with immediate AVL 4, SEW=32, LMUL=1;
- vle32.v and vse32.v;
- vmul.vv and vmul.vx;
- vredsum.vs.

The inputs are lhs=[2,-3,4,5], rhs=[6,7,-2,3], and scalar multiplier -3.
Expected results are:
- vector-vector product: [12,-21,-8,15];
- vector-scalar product: [-6,9,-12,-15];
- sum reduction of the vector-vector product: -2.

The reference program is assembled independently by the RISC-V GNU toolchain.
It is not assembled by Kites and does not use Kites' instruction encoder. Spike
executes it with the standard RVV 1.0 V extension. The workflow pins Spike to
release v1.1.0 and uses the rv64gcv profile. Spike's V extension implies
Zve64d and Zvl128b, giving ELEN=64 and VLEN=128 for this reference
configuration. This matches the vector register length and element-width
capabilities configured by the Kites RVSS subset.

## Run locally on Ubuntu 24.04

Install the RISC-V cross-toolchain and Spike build dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  device-tree-compiler \
  libboost-regex-dev \
  libboost-system-dev \
  gcc-riscv64-unknown-elf \
  binutils-riscv64-unknown-elf
```

Build Spike 1.1.0:

```bash
git clone --depth 1 --branch v1.1.0 \
  https://github.com/riscv-software-src/riscv-isa-sim.git
cd riscv-isa-sim
git submodule update --init --recursive
mkdir build
cd build
../configure --prefix="$HOME/.local/spike"
make -j2
make install
cd ../..
export PATH="$HOME/.local/spike/bin:$PATH"
```

Run the reference check from the Kites repository root:

```bash
tests/spike/run_spike_reference.sh
```

The script compiles vector_subset_reference.S into a bare-metal ELF with a
linker script at 0x80000000, starts Spike in debug mode, runs until the program
reaches its _done label, reads the result GPRs, and compares all nine values
to the expected 64-bit representations.

## CI

The separate GitHub Actions workflow
.github/workflows/spike-reference.yml builds Spike 1.1.0 and runs the same
script on Ubuntu 24.04. This keeps the normal Qt/CMake/CTest workflow free of
the additional simulator build dependencies.

## Scope and limitations

This validates a focused set of standard RVV operations against a second
functional implementation. It is not a complete ISA conformance suite, it
does not compare every implemented mnemonic, and it does not validate Kites'
pipeline variants. The timing model remains a separate abstract scheduler,
not a cycle-accurate model. Spike is used only for functional ISA comparison,
not performance measurement.
