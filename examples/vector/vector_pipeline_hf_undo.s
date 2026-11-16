.text
# Minimal sequence used to verify vector-register WB undo/redo in H_F.
vsetivli x5, 4, e32, m1, tu, mu
vadd.vv v3, v1, v2
