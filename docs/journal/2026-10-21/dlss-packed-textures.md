# Packed ray textures — 21 October 2026

Target: stable 120 FPS at 1920x1080 with reflections, shadows and neural
rendering enabled, including movement. This target is not achieved.

Private candidate: `359dcc1bf3f6fbf887ea73fafe70e4a9a32ffaf919a4e8c679779f9fb889c5d4`.
Not deployed: three-area gameplay validation passed; an uncontended performance
comparison is still required. Evidence is in `build/dlss-packed-textures-20261021`.

The renderer previously expanded RGBA8 ray textures to four 32-bit floats per
texel. The candidate keeps the original packed bytes and converts only fetched
texels in the shader, before the existing bilinear interpolation and alpha
tests. Triangle attributes, geometry, resolution, effect settings and fences
are unchanged. `GSR_RAY_PACKED_TEXTURES=0` selects the original representation
for comparisons; the default is packed storage.

Texture storage and changed-texture uploads drop from 16 to 4 bytes per texel.
At the existing limit of 768 textures of 128x128 pixels, the texture portion
drops from 192 MiB to 48 MiB (144 MiB saved). The fixed triangle-attribute
allocation is unchanged. This is a resource-size calculation, not a measured
whole-process VRAM reduction or FPS improvement.

All six focused tests passed. Paired hardware readbacks cover 150 frames and
2,457,600 half-float channels. Only two color channels differ, each by one
half-float ULP (maximum absolute difference 0.0001220703125); alpha is exact
and every output is finite. Existing tests include cutout shadows, transparent
models, offscreen reflections, shoreline transitions, camera motion and
resource lifetime. `pixel-equivalence.json` retains the comparison and hashes.

The private executable replaces only the ray renderer object in the frozen
installed archive; concurrent backend and texture-hashing edits are excluded.
The inherited ShowCursor duplicate-symbol and /FORCE link warnings remain.

The first movement benchmark overlapped compilation, another gameplay run,
and substantial external CPU activity. With user authorization, the other
optimization chat held launches from 09:00 to 09:10 Brisbane time. It confirmed
that further compiler and Node activity was not from that chat. The rerun still
observed external CPU load averaging 2.85 cores for the baseline and 4.30 cores
for the candidate, with peaks near 8 and 9 cores. These runs are not valid
evidence of a gain or regression. `performance.json` marks them non-comparable;
loading-spanning pacing windows over three seconds are excluded.

Three 350-frame movement routes passed in river, pond and camp. Each recorded
349 frames with native-light shadows, finite SR/neural output, exact swapchain
presentation, and unchanged original/source/private saves. Independent model
motion affected 22,054, 24,371 and 38,332 guide pixels respectively. The CPU
packing equivalence check was enabled. `visual-metrics.json` retains the logs
and run manifests. The pond movement capture faces away from most of the water;
it is not sufficient reflection evidence by itself. The river capture includes
the swimming model and water, with the original coarse bank geometry still
visible. Whole-game coverage and temporal visual quality remain unproven.

An additional stationary pond run passed finite neural output, exact
presentation and save checks. Its inspected capture shows the reflective pond
and bank with the player and nearby foliage. Validation readbacks and CPU cache
comparison are enabled in these image runs, so their FPS overlays are not
performance results. `validation-status.json` records the completed checks.

The installed world-cache executable remains unchanged. Deployment is gated
on a clean paired result without a measured regression; no 120 FPS claim is
supported by these results.
