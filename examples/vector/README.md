# RVSS vector examples

These files target the initial RVSS vector subset. Run them from the repository
root after building `kites-cli`:

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
The average example uses a signed scalar `div`, which truncates toward zero.
When no elements match, its branch sets the result to zero. The first four examples use SEW=32 and VL=4; the bit-manipulation example uses SEW=8 and VL=4; the strided-memory example uses SEW=32 and VL=3. The multiply/dot-product and SAXPY examples use SEW=32 and VL=4; the cross-product uses VL=3; the 2x2 matrix kernel uses VL=2 for dot products and VL=1 for scalar-element stores.
These programs exercise the implemented subset, not general RVV 1.0 programs. The separate abstract execution timing model is documented in `docs/VECTOR_PERFORMANCE_MODEL.md` and is available as `kites-vector-timing`.
