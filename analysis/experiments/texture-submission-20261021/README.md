# CPU submission prototypes

These source-only patches are private experiments, not promoted runtime changes.
The exact-block patch applies to the renderer snapshot containing the texture
hash changes. It narrows texture RAM stamp checks and keeps render-target
coherence flushing unchanged. `test-footprint.cpp` validates storage footprints
against exhaustive pixel enumeration. The geometry patch skips diagnostic
clock reads when statistics are disabled; arithmetic and packet handling are
unchanged. The native-upload patch moves exact nearest-neighbour pixel expansion
to a compute shader and restores compute state; its hardware test compares
every output texel against the CPU rule, including untouched rows.
Gameplay evidence is in ../../../docs/journal/2026-10-21/data/texture-submission-20261021.json.
