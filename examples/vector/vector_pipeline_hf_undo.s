.data
pipeline_seed: .word 0

.text
main:
# Minimal sequence used to verify vector-register WB undo/redo in H_F.
vsetivli x5, 4, 16
vadd.vv v3, v1, v2
