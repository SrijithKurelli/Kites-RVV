# RVSS vector examples

These general assembly examples target the documented selected vector subset
on the single-cycle RVSS processor. Run them from the repository root after
building `kites-cli`:

```bash
./build/kites-cli examples/vector/vector_load_add_store.s --vm rvss
./build/kites-cli examples/vector/vector_compare_count.s --vm rvss
./build/kites-cli examples/vector/vector_masked_add_store.s --vm rvss
./build/kites-cli examples/vector/vector_average_selected.s --vm rvss
./build/kites-cli examples/vector/vector_bitmanip.s --vm rvss
./build/kites-cli examples/vector/vector_strided_memory.s --vm rvss
./build/kites-cli examples/vector/vector_multiply_dot_product.s --vm rvss
./build/kites-cli examples/vector/vector_immediate_config.s --vm rvss
./build/kites-cli examples/vector/vector_saxpy.s --vm rvss
./build/kites-cli examples/vector/vector_cross_product.s --vm rvss
./build/kites-cli examples/vector/vector_matrix_multiply_2x2.s --vm rvss
./build/kites-cli examples/vector/vector_matrix_multiply_3x3.s --vm rvss
```

Expected results:

| Program | Expected result |
|---|---|
| `vector_load_add_store.s` | `v3` contains [11, 22, 33, 44]; those values are stored at `result`. |
| `vector_compare_count.s` | `v0` has predicate bits 0 and 2 set; `x10 = 2`. |
| `vector_masked_add_store.s` | Predicate bits 0, 2, and 3 are set; active result lanes are 7, 3, and 17. Inactive result lane 1 remains -1 in memory. |
| `vector_average_selected.s` | The selected values are 1 and 4; sum=5, count=2, integer average=2. The program prints `2`. |
| `vector_bitmanip.s` | On 8-bit lanes: AND-NOT [128, 64, 0, 251], rotate-left [3, 9, 0, 255], rotate-right [192, 144, 0, 255], leading/trailing zeros [0, 1, 8, 0], and population counts [2, 2, 0, 8]. |
| `vector_strided_memory.s` | Strided SEW=32 load selects source words 11, 22, and 33 (skipping padding words) and strided store writes those three values to destination word positions 0, 2, and 4. |
| `vector_immediate_config.s` | Uses `vsetivli` with AVL=4 and SEW=32, doubles four input values, and stores `[6,10,14,18]`. |
| `vector_multiply_dot_product.s` | `vmul.vv` produces [12, -21, -8, 15]; `vredsum.vs` computes the dot product -2. |
| `vector_saxpy.s` | With alpha=3, computes `3*x + y` and stores [13, 14, 39, 52]. |
| `vector_cross_product.s` | Computes `[1,2,3] x [4,5,6]` and stores `[-3,6,-3]`. |
| `vector_matrix_multiply_2x2.s` | Computes `[[1,2],[3,4]] x [[5,6],[7,8]]` and stores `[[19,22],[43,50]]` in row-major order. |
| `vector_matrix_multiply_3x3.s` | Computes `[[1,2,3],[4,5,6],[7,8,9]] x [[9,8,7],[6,5,4],[3,2,1]]` and stores `[[30,24,18],[84,69,54],[138,114,90]]` in row-major order. |
The average example uses a signed scalar `div`, which truncates toward zero.
When no elements match, its branch sets the result to zero. The first four examples use SEW=32 and VL=4; the bit-manipulation example uses SEW=8 and VL=4; the strided-memory example uses SEW=32 and VL=3. The multiply/dot-product and SAXPY examples use SEW=32 and VL=4; the cross-product uses VL=3; the 2x2 matrix kernel uses VL=2 for dot products and VL=1 for scalar-element stores.
## H_F pipeline test fixtures

The following additional assembly files are used by the H_F pipeline unit tests.
The tests seed the vector registers before execution; these files are not
standalone result demonstrations. Separate programmatically assembled tests in
`test/rv5s_vector_pipeline_test.cpp` also exercise the shared selected-vector
subset in all four five-stage variants, including all-SEW arithmetic/memory and
vector-memory undo/redo.

| Program | Pipeline behavior verified |
|---|---|
| `vector_pipeline_hf_subset.s` | Masked `vadd.vv`, dependent `vmul.vv` / `vsub.vv`, and vector dependency stalls. |
| `vector_pipeline_hf_undo.s` | Vector writeback through MEM/WB and vector-register undo/redo. |
| `vector_pipeline_hf_gpr_forwarding.s` | GPR AVL forwarding into `vsetvli`, forwarding of the returned VL to an integer instruction, and scalar GPR forwarding into `vmul.vx`, followed by a vector dependency. |
| `vector_pipeline_hf_all_sew.s` | H_F vector addition at SEW=8/16/32/64 with tail-undisturbed destination checks. |
| `vector_pipeline_hf_load_forwarding.s` | Load-use forwarding of AVL into `vsetvli` and scalar operand into `vmul.vx`. |

These programs are included in the automated example assembly/smoke checks.
The regular RVSS examples exercise the broader documented subset; H_F pipeline
execution is limited to the instructions exercised by its unit tests.

These programs exercise the implemented subset, not general RVV 1.0 programs. The separate abstract execution timing model is documented in `docs/VECTOR_PERFORMANCE_MODEL.md` and is available as `kites-vector-timing`.
