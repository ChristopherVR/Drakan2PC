# Grouped GS write stamps and Surdana diagnostics

Private experiment; not promoted. `stamp-groups.patch` stores exact maxima for
64-block groups and keeps exact scalar checks at range boundaries. Every stamping
path updates both indexes. `test-stampgroups.cpp` compares queries against scalar
block stamps after interleaved writes, including wraparound and partial groups.
Compile as C++20 with runtime/gs/src on the include path.

The separate world-interval profiler patch is based on the block-color-decode
experiment. Both scene candidates use it. It is disabled with GSR_PROFILE=0.
Counts refer to completed world epochs, not physical presentations. Per-window
percentiles are not aggregate percentiles. Recipes are synthetic motion/combat
in existing Surdana fixtures; they are not a verified reproduction of the user's
exact city approach. No RAM dump or screenshot commands run during measurement.

Patch application was checked in an isolated source fixture.
Results: ../../../docs/journal/2026-10-21/data/surdana-consistency-20261021.json.

`exact-mip-resource-reuse.patch` applies after the preceding experiments. It retains
an invalidated immutable texture until every decoded mip equals the retained CPU
copy. It reuses only GPU texture/SRV allocation; coherence checks, decode, callbacks,
volatility tracking and replacement/upscale selection still run. CPU pixel bytes
are included in cache budget accounting. The profiler textureCreate bucket includes
comparison/reuse; subtract textureResourceReuse calls to count actual creation
attempts. This candidate needs broader visual and clean performance validation.
