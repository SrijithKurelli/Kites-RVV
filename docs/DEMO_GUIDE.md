# KITES RVV project demo and final verification

This guide is intended for the CS2323 project demonstration. Run commands from
the repository root. The supported ISA boundary is documented in
[VECTOR_EXTENSION.md](VECTOR_EXTENSION.md); this project implements a selected
vector subset, not a complete vector ISA.

## 1. Configure and build

Install Qt 6.9+ and Ninja, and set QT_ROOT_DIR to the Qt kit directory for
your compiler. In WSL/Ubuntu:

~~~bash
cmake -S . -B build/ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
  -DENABLE_TESTS=ON -DDISABLE_GUI=ON
cmake --build build/ci --target kites_cli kites_vector_timing kites_test --parallel 2
~~~

The commands above build the headless CLI, abstract timing-model executable,
and test suite. This CI-style configuration disables the Qt GUI.

## 2. Run the complete regression suite

~~~bash
ctest --test-dir build/ci --output-on-failure --parallel 2
~~~

The latest verified PR run (#203) passed the Linux build, smoke tests, and CTest:
428 passed, zero failed, one skipped, and four disabled. See
[the CI run](https://github.com/SrijithKurelli/Kites-RVV/actions/runs/44286591465).

## 3. Demonstrate the RVSS vector subset

Run selected examples:

~~~bash
./build/ci/kites-cli examples/vector/vector_load_add_store.s --vm rvss
./build/ci/kites-cli examples/vector/vector_compare_count.s --vm rvss
./build/ci/kites-cli examples/vector/vector_strided_memory.s --vm rvss
./build/ci/kites-cli examples/vector/vector_multiply_dot_product.s --vm rvss
./build/ci/kites-cli examples/vector/vector_saxpy.s --vm rvss
./build/ci/kites-cli examples/vector/vector_cross_product.s --vm rvss
./build/ci/kites-cli examples/vector/vector_matrix_multiply_2x2.s --vm rvss
./build/ci/kites-cli examples/vector/vector_matrix_multiply_3x3.s --vm rvss
~~~

Expected results include:

- Load/add/store: [11, 22, 33, 44].
- Dot product: products [12, -21, -8, 15], sum -2.
- SAXPY: [13, 14, 39, 52].
- Cross product: [-3, 6, -3].
- 2x2 matrix product: [[19, 22], [43, 50]].
- 3x3 matrix product: [[30, 24, 18], [84, 69, 54], [138, 114, 90]].

The full example list and expected results are in
[examples/vector/README.md](../examples/vector/README.md).

## 4. Demonstrate a five-stage processor

Use the hazard-detection/forwarding model for a straightforward run:

~~~bash
./build/ci/kites-cli examples/vector/vector_load_add_store.s --vm rv5-h-f
~~~

The four supported five-stage configurations are rv5-h-f, rv5-h-nf,
rv5-nh-f, and rv5-nh-nf. Their hazard detection and forwarding behavior
differs; programs running without hazard detection must schedule required
bubbles themselves. See the pipeline notes in VECTOR_EXTENSION.md.

## 5. Inspect registers and vector configuration

Start the REPL:

~~~bash
./build/ci/kites-cli --repl
~~~

Useful commands:

~~~text
load examples/vector/vector_load_add_store.s --vm rvss
run
info
regs
disas
~~~

info reports VLEN, LMUL, SEW, VL, and VLMAX. regs displays the scalar,
floating-point, CSR, and 128-bit vector registers. The register JSON dump also
contains the vec_registers section.

## 6. Demonstrate the abstract timing model

~~~bash
./build/ci/kites-vector-timing --lanes 4 --memory-delay 2
~~~

The tool reports estimated schedule cycles, dependencies, memory stalls, lane
utilisation, and instruction traces. This is an abstract scheduling model,
not a cycle-accurate simulation or a measurement of physical hardware.

## 7. GUI verification before the live demo

The normal CI job uses DISABLE_GUI=ON, so CI does not execute the full Qt GUI.
In a GUI-enabled Qt build, manually verify the Vector register panel:
hexadecimal, binary, and signed-decimal display; 8/16/32/64-bit lane width;
comma-separated lane values; and full 128-bit values without clipping. Keep
this as a live-demo check rather than describing it as CI-verified.

## 8. Scope and limitations to state clearly

- VLEN=128, LMUL=1, SEW=8/16/32/64.
- Integer vector operations, selected reductions, masks, bit operations,
  unit-stride/strided memory and selected FP32 operations are implemented.
- FP32 vfadd.vv, vfsub.vv, vfmul.vv, and vfdiv.vv require SEW=32.
- The timing tool is abstract, not cycle-accurate.
- Indexed/segment memory, most other vector floating-point operations, full
  Zvbb support, and full RVV conformance are outside the implementation.
- Spike comparison was deferred; no Spike conformance is claimed.
