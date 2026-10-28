# Reflection filtering and neural motion texture work

This pass filters reflected screen radiance in linear light. Each of four taps
must independently agree with the ray-hit depth; rejected taps use the physical
material fallback. This smooths moving reflected detail without blending a
foreground actor into an unrelated reflected surface. Visible sky uses the same
filtered coverage, jitter alignment and a screen-edge fade to retained weather.
The offscreen sky remains an observed average, not a full environment map.

Water shading fades over at least one projected pixel at distant shorelines,
while retaining the existing 96-unit minimum near the bank. Fully zero-coverage
water skips reflection work. This does not remesh the original terrain or remove
coarse silhouettes and raster/exported terrain LOD differences.

Native neural motion now passes only alpha textures referenced by accepted moving
geometry. It remaps their indices and retains borrowed pixel views. The previous
code passed all resident textures to the GPU array, including opaque and static
surfaces. This reduces unused array layers/uploads; it did not previously copy
all texture pixels on the CPU. Opaque models keep their existing motion vectors.

## Validation

Six focused renderer tests pass. The wave-motion reflection test passes on the
new renderer with maximum output step 0.001709 in display colour units; the
previous deployed renderer fails its 0.02 threshold. Depth
transition, alpha, offscreen geometry, moving blockers, native shadow lighting,
sky retention and level reset regressions also pass. A texture remapping test
uses 200 resident slots and checks that only the one referenced alpha slot is
passed, and that opaque moving geometry passes none.

Final candidate movement runs cover three Surdana routes: snowy riverbank, pond
and dragon camp. Independent model-motion pixel counts are 22667, 26382 and
31456. Native shadows remain active throughout 350/349/349 captured world frames;
none lose native lighting. SR/NR values are finite and all three final 1080p
presentation comparisons are byte-exact. Source, user and private saves remain
unchanged. These captures are correctness evidence, not timing benchmarks.

Evidence and exact build provenance are in `build/water-polish-20261019`.
The deployed runtime/menu link inputs are retained; only the water shader object
and backend object including the motion-texture change are replaced. Backend
source matches the earlier release. Concurrent experimental CPU/VU work is
excluded. Performance and installation receipts are recorded separately there.

## Remaining limitations

Original terrain facets and model polygon counts remain. Neural processing
improves rendered reconstruction rather than generating new model geometry.
Shoreline coverage still follows native submitted water geometry. The sky-owner
classification is still a conservative geometry heuristic, and offscreen sky
radiance is approximate. Coverage is three routes in one level, not a whole-game
quality certification. These improvements do not establish polished modern RTX
asset quality or a 60 FPS target.

## Performance and deployment

Clean pond samples at 1920x1080 output, native scale 3, Quality DLSS, neural
rendering and all ray effects enabled measured 21.04 FPS for the previous
deployment and 21.94 FPS for this candidate. These are last-five-window averages
from separate 30-second scripted movement runs, without validation readbacks.
The roughly 4.3% difference is a short scene sample, not a statistically proven
whole-game gain. An earlier baseline run overlapped another game and was excluded.

Installed executable SHA-256:
`92c7456df7ec083bd79512d40ea755b48944e7652fc73dcab7f0d2d1e7b6bce8`.
The prior executable and configuration/build receipt are in
`build/water-polish-20261019/deployment-backup`. Settings, saves and menu/runtime
components are preserved. `deployment.json` records installed smoke validation.
