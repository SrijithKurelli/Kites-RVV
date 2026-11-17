.data
pipeline_seed: .word 0

.text
main:
    vsetivli x5, 16, 0
    vadd.vv v3, v1, v2
    vsetivli x5, 8, 8
    vadd.vv v4, v1, v2
    vsetivli x5, 4, 16
    vadd.vv v5, v1, v2
    vsetivli x5, 2, 24
    vadd.vv v6, v1, v2
