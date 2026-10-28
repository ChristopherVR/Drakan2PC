# Inventory orb: proven shifted render-target row alias

Production/test commit `bb244c8`, integrated by main as `76d8e18`.
This is the cause verified for the photographed inventory red rectangle.
The earlier alpha-only DATE mapping patch fixes a separate synthetic defect;
7F game captures still showed this rectangle after that patch.

## Actual failing scene and packet evidence

Private fixture: `build/recomp_combat/encounter/fixture-bow-item`, copied cards,
7F executable copied into `tools/graphics-followup/build/graphics-orb/bin`.
No original or source saves were supplied for writes. Normal script quit,
exit0,56.385s; source/original card hashes unchanged. This worker released the
GPU immediately after the one game capture and used only WARP for regression.

Private evidence under `tools/graphics-followup/build/graphics-orb/`:

- `render_2500.png`: same health rectangle and pale overhead shape as main.
- `captures/gscap_20261017_222753.bin`:9,471,248 bytes, three GIF frames,
  87,701 submitted vertex kicks. Capture starts at41.6 **seconds**, not GS frame.
- `t/equipment-orb-01/stderr.log`: only frame2500 with up to768 vertices per
  draw. It contains batch#77796, a tiled depth clear. RGB draws are in adjacent
  GS frames; the three-frame GIF recording supplies their complete state.
- `rt-audit.json`, `raw-groups.json`, `upload-order.json`: local CPU analyses.

Capture-relative kicks establish the relevant producer/consumer order:

| Event | Captured state |
|---|---|
|32180..32181|FRAME0x201CC: FBP460/FBW2/CT32 clear128x128|
|32182 onward|Same target: silhouette geometry, DATE, RGBA127/127/127/140; bounds29.875..113.0625 by12.375..98.125|
|After38046|Host upload DBP15008/DBW2/CT32, TRXREG128x128; BITBLTBUF0x00023AA000000000|
|38047..38048|Gameplay orb fill at48,432..108,492 using the uploaded texture|
|40540..40541|Equipment health fill at14,433..74,493, RGBA72/0/0/128, PRIM0x25E|
|81903 /84397..84398|Next recorded frame repeats upload and Equipment fill|

Health fill TEX0=`0x00075405dc00baa0`: TBP15008,TBW2,CT32,128x128,TCC1,TFX0.
TEST0x3002D, ALPHA0x44, CLAMP1/1, TEX1 point sampling. Exact endpoint texture
coordinates are **(28.5,127.5) -> (88.5,67.5)**. Runtime logs explicitly select
`sample render target fbp=460 fbw=2 ... tbp=15008 ...128x128`.

FBP460 begins at block14720. Offset288 blocks is nine GS pages: source origin
**x64,y128** in the128-wide parent RT. The fill's maximum translated x is
**64+88.5=152.5**, beyond width128. `sampleRT` applies a linear UV offset and
hardware sampler clamp, so the right portion repeats the parent's last column.
GS page addressing instead carries to the next page/row. An SRV rectangle
cannot represent that shifted texture. This explains the right-side stretched
red fill; it is not a circular depth-mask transform problem in this recording.
The depth-only batches inspected are page-sized tiled clears, not a proved
orb-shaped mask. That finding must not be generalized to every UI depth draw.

## Targeted fix and WARP proof

Reject a candidate only when its horizontal origin is nonzero and its logical
width crosses the parent's row. Continue through the existing VRAM flush/decode
path, which uses GS addressing. Exact-base, aligned-row, zero-origin aliases
remain eligible. In particular, a larger logical texture sampling a valid part
of a zero-origin RT retains its prior fast path. No shader, world/camera,
depth-mask, settings, SDK or presentation changes are in this fix.

`gsr_test_synthetic --warp --scale 1 --rt-shifted-row-only`:

- Before: **16 checks,3 failed,exit1**.
- After: **16 checks,0 failed,exit0**.
- Uses the captured60x60 red fill, exact UV pair, source pitch and page9 offset.
  An uploaded circular alpha atlas reproduces the top-right red rectangle.
- Exterior pixel68,438: before(72,0,0), after expected(0,0,200); stored alpha
  now remains0. Interior44,463 remains red72.
- Controls preserve the aligned-row subview, zero-origin larger logical texture
  and the existing exact-base shadow-overlap regression. Tests join the normal
  synthetic suite; the focused flag is suitable for CI.

Fallback can incur readback for overlapping dirty GPU data and CPU decode;
it is not claimed cost-free. Its cache/coherence path is the existing decoder.
Main owns actual native/SRNR and pacing measurements; no unmeasured FPS claim.

## Actual native result, limits and integration

Main's rebuilt executable is
`77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`.
Main and this worker visually inspected
`build/t/followup-release-orb-native/render_2500.png`: **the red rectangle
outside the health orb is gone; mana has its curved swirl again**.
This is actual native-renderer image evidence in addition to synthetic proof.
Full native-run completion was not yet recorded when this receipt was authored.
**SR/NR validation and performance measurement remain pending** at this point.

The pale overhead shape is unchanged and may be the nearby vendor-booth awning
underside; it is **unverified world/preview placement**, not an established
renderer defect without a matching native reference. Hair was visually coherent;
smoke was not exercised. These results do not claim every graphical issue fixed.

Main already integrated the two production/test files. Cherry-pick this separate
documentation/metadata commit without rebuilding for its prose. Raw private
PNGs, game captures, executable, cards and downloaded artifacts are untracked.
Hashes and exact test results are in
`docs/journal/2026-10-17/data/inventory-rt-row-alias.json`.
