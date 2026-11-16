.text
# Execute on RV5StageProcessorHF. The vector registers are seeded by the
# automated test before the pipeline is stepped.
vsetivli x5, 4, e32, m1, tu, mu
vadd.vv v3, v1, v2, v0.t
vmul.vv v4, v3, v2, v0.t
vsub.vv v5, v4, v1
