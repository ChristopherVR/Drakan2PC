# Moving-model guide allocation overhead

The DLSS motion-guide path recreated its alpha texture array whenever the active
material count changed. Visibility changes could therefore allocate a new GPU
resource and invalidate all cached slots on successive frames. This work belongs
to the game's DLSS input preparation, not the NVIDIA reconstruction network.

The array now retains capacity when the count shrinks and grows geometrically
when necessary. The existing 768-layer input limit still applies, bounding this
128x128 R8 array to 12 MiB of texel storage. Device destruction releases it.
Slot identity checks still upload changed/reordered materials. The CPU alpha
conversion scratch buffer is created only when an upload is actually needed.

A regression alternates one, two and three active materials while swapping
opaque and transparent slots. It checks resulting motion through the cutouts
and asserts that no new array allocation occurs after warmup. The old allocation
policy fails this assertion; the new policy passes. The six focused renderer
tests also pass, including the D3D11 debug checks in the guide suite.

Only the nr_guides object is replaced in the validated installed renderer library.
The library member listing confirms one nr_guides object; duplicate-object link
attempts were discarded before any game testing. Runtime, water and shadow code
remain from the installed build.

Evidence and build hashes: `build/dlss-overhead-20261021`.

Deployed SHA-256:
`79a25529583daa872f3b726feab816acb961ddf2648fe150ca9bb0bc7dd26aa1`.
River, pond and camp movement checks produced 31478, 24419 and 35408 independent
model-motion pixels. Each retained native shadows over 349 logged world frames,
with finite SR/NR output, exact presentation and unchanged save hashes. The prior
executable and settings are in `deployment-backup` inside the evidence folder.

A competing compiler was observed throughout the movement checks. Subsequent
fixed-view runs disabled validation readbacks and found a window without detected
game/compiler overlap. The accepted baseline retry measured 20.786 FPS and the
candidate 20.729 FPS at 1080p, Quality DLSS, neural rendering and ray effects on.
The -0.28% difference is small relative to the variation within either run; this
single pair shows no meaningful FPS gain or regression. Earlier contended runs
are retained but excluded. The benchmark now refuses to start if competing work
persists after its bounded wait; it still records overlap that begins later.

An exploratory same-build run with optional neural rendering disabled measured
32.05 FPS, but another process overlapped it. It is excluded from quantitative
neural-stage cost attribution. SR-only versus SR-plus-neural profiling remains
necessary before attributing the remaining frame time to either stage.

The installed smoke passed with finite neural output, exact presentation, native
shadows and unchanged saves. Settings and launchers are preserved.

This removes a known source of resource churn, not every possible DLSS cost.
Neural evaluation, guide preparation, interop synchronization, raster rendering
and ray effects still consume time. Whole-game FPS and CPU contention must be
measured separately; this change alone is not a claim of higher FPS or zero
performance issues. Terrain topology and original model polygon counts remain
separate visual limitations.
