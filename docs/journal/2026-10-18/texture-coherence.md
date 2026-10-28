# Decoded texture cache coherence — 18 October 2026

Overlapping GPU writes could leave a cached decoded texture stale when the GS
RAM write clock had not advanced. A focused software-renderer regression fails
before the cache fix and passes afterward. This defect is **not established as
the cause of smoke or sky artifacts**.

## Cause and focused change

`Backend::Impl::resolveTexture` previously called `flushToMemory` for a cached
texture's mip spans only when `e.checkedClock != mem.clock()`. GPU colour writes
mark render-target rows dirty without advancing that RAM clock. A cached texture
could therefore return its old pixels without the readback that would expose the
new writes and advance the block stamps. The front end's `TEXFLUSH` handling did
not invalidate that decoded entry.

The fix moves the existing overlapping-RT flush ahead of the clock comparison.
Existing block-stamp validation and re-decode behavior follow it. Nonoverlapping
GPU writes retain the cached decoded texture. The separate shifted-row alias
rejection remains enabled; its fallback makes this cache path relevant.

This worker changed only the cache-validation hunk in `d3d11_backend.cpp`.
The parent independently changed `syncRT` to use `rt_upload.h`; both private
before/after validation builds include that helper. Each decoded-cache hit now
scans dirty RT overlap. Performance effects require the parent's measurements.

## Reproduction and results

`rtCacheCoherenceDiagnostic` seeds a 128-wide CT32 target at FBP256/FBW2 red,
then samples a logical 128x128 texture at TBP8480. Its parent origin is (64,128),
so the shifted-row rule forces GS-memory decode. Two sample regions exercise
the right-hand page and the page carrying into the next row.

The fixture repaints overlapping parent rows 128..288 blue entirely on the GPU,
emits `TEXFLUSH`, then samples the same texture key again. No capture, VRAM read
or vsync intervenes between samples. Before the fix, both second samples remain
red (200,0,0) instead of blue (0,0,200), and the decode count remains one instead
of two. An unrelated one-pixel RAM upload makes the old implementation refresh;
a GPU repaint outside the texture's rows correctly retains red and one decode.

| Software D3D11 WARP invocation | Checks | Failures | Exit |
|---|---:|---:|---:|
| Before, `--scale 1 --rt-cache-coherence-only` | 18 | 3 | 1 |
| After, `--scale 1 --rt-cache-coherence-only` | 18 | 0 | 0 |
| Final default full suite, `--scale 1` | 118 | 0 | 0 |
| Final default full suite, `--scale 4` | 118 | 0 | 0 |

The regression enters the default full suite and the focused CI command in
`.github/workflows/build-tools.yml`. Remote CI was not run for this change.
The full scale-4 run completed at **03:05:47.574 Australia/Brisbane**; the worker
then released the reservation. No hardware GPU, game or benchmark was launched.

Private logs are under `build/graphics-audit-20261018/`:
`coherence-before.log`, `coherence-after.log`, `full-scale1.log` and
`full-scale4.log`. The validation build is Release/MSVC19.44.35217.0 with
`GSR_DLSS=OFF`, `GSR_NEURAL=OFF`, and forced WARP. Its final executable SHA256 is
`2F5B8883D4770EB526439226760365C9A19C8721DACFEB9F903587368F41211C`.
That executable ran both final full suites; the earlier focused binaries were
relinked and were not separately hashed.

## Source identity and edit timing

Hashes below cover raw file bytes, including CRLF. **The before backend hash was
reconstructed after the experiment by reversing only the coherence hunk from
the frozen after source. No contemporaneous pre-run source hash was captured.**
The parent row-replication helper is retained in both snapshots; the hashes are
source-comparison receipts, not a complete immutable build manifest. Private
copies are in `build/graphics-audit-20261018/source/`.

| Source state | SHA256 |
|---|---|
| Backend before, reconstructed | `AAB63914302C3F677E0E65422DFE48A16C02E44313FC4427401FBCA16A4F596A` |
| Backend after, observed | `48325E681A68133CBAF63AA718F26659970F10B39DA743BFB383CED6E8567714` |
| Final synthetic source | `05E7E7EE4C013BB6E3C833CC40582AE468F55C583B903DA0951ACA018B180B56` |
| Final CI workflow, including parent edits | `A0A896A17EC920C3C64B04C877F2172CC38EDACE30199A974FB2679531431F58` |
| Parent `rt_upload.h` | `E35244C8ED126CAD76F3D5E88DB23D2E58C8ADAB303277A8AAD53BF6EF8A28B9` |

The worker's backend hunk was last edited at **03:01:08.450 Brisbane**. The test
and focused CI registration were finalized at **03:02:34.879 Brisbane**. No
shared production file was temporarily reverted for comparison. The receipt's
[JSON](data/texture-coherence-20261018.json) includes reconstructed
focused-fixture identity, log hashes, exact commands and controls.

## Visual evidence and limits

The actual inventory orb row-addressing defect was established separately in
[inventory-rt-row-alias.md](../2026-10-17/inventory-rt-row-alias.md). This synthetic cache
regression does not establish another observed orb defect or a cause for the
photographed smoke, sky, hair or original gate-guard shadows.

The historical `build/screenshots/r5_g_mouseup_3s.png` shows a hard-edged pale
sky wedge. It does not establish that the latest build still shows it or identify
its responsible draw. Existing smoke evidence is inconclusive. No awning or
preview-geometry fix is asserted. WARP pixel checks do not establish native or
SR/NR live-game parity.

### Existing 77AF combat views inspected read-only

The parent supplied three 3840x2160 PNGs in
`build/combat_audio_20261018/t/audio77-melee01/`. Its manifest and normal-exit
receipt both identify executable
`77AF378BC98F14757262C902B995C115D37B2DB025DC78CBD565D5A9BE52298E`,
with SR and NR disabled. The source is `fixture-hostile675`, the same encounter
fixture as the earlier shadow receipt: these add combat poses, not another map
or the original gate-guard scene.

| View | Observation |
|---|---|
| [1900](../../../build/combat_audio_20261018/t/audio77-melee01/render_1900.png) | Rynn and nearby enemy have actor-shaped ground shadows. A torch flame and soft yellow targeting marks are visible. Separate bright blade/grip components appear on the right. |
| [2200](../../../build/combat_audio_20261018/t/audio77-melee01/render_2200.png) | Two enemies, Rynn and their weapons have coherent silhouettes and ground shadows. Yellow targeting arcs have soft edges. No obvious opaque rectangular hair/effect backing is visible. |
| [2500](../../../build/combat_audio_20261018/t/audio77-melee01/render_2500.png) | Actor shadows remain coherent. The lower-right mana outline is enlarged and displaced left/up from its blue fill, unlike the aligned ring/fill in the other two images. |

The original long striped shadow rectangles are not visible in these views.
Hair has a cutout edge, with a jagged/fanned ponytail silhouette in some poses;
these screenshots do not establish reference-quality hair alpha or a new hair
defect. No open sky or controlled smoke effect is visible, so neither receives
additional coverage from this inspection.

The detached blade and displaced mana outline are concrete visible anomalies.
Their intended placement/animation and responsible draws remain unverified.
The prior receipt supports an actively submitted native weapon preview, but
does not prove the cause of these particular pixels. There is no paired
same-pose combined-candidate comparison. Neither anomaly is attributed to the
new cache fix, and no frozen production source was changed for this inspection.
PNG hashes and per-view qualifications are included in the JSON receipt.

### Final combined native views inspected read-only

The parent supplied `build/t/followup-20261018-final-native/` images from combined
candidate `54F082B55C3A768391EC5B39DB48033F2AA77DEA6B51CDFBBE1E81A61D92D557`.
Its manifest enables audio, sets internal scale4, and disables SR and NR. All
four inspected PNGs are 3840x2160. The worker inspected existing files without
launching the game or changing frozen production source.

| View | Observation |
|---|---|
| [1900](../../../build/t/followup-20261018-final-native/render_1900.png) | Gameplay beneath the booth canopy. Rynn's ponytail and the canopy have coherent silhouettes in this pose. Health and mana fills align with their bottom-corner rings. |
| [2500](../../../build/t/followup-20261018-final-native/render_2500.png) | Equipment page with dagger and bow. The red health fill is contained by its ring with no exterior red rectangle; the curved mana swirl is visible. Both fills align with their page-positioned rings. The character is on the right and the dagger preview is within its panel. |
| [4000](../../../build/t/followup-20261018-final-native/render_4000.png) | Journal page with readable quest text. Character and orb placement remain coherent with the inventory layout. |
| [4500](../../../build/t/followup-20261018-final-native/render_4500.png) | Bow selected and held by the character; bow preview is within its panel and the right orb displays ammo200. A jagged/fanned ponytail edge is visible during the pose. |

Thin white L-shaped lines remain outside the health/mana artwork borders in
these views, including Equipment2500. Their responsible draws and cause are
unverified; they remain a visible qualification. The photographed red orb
rectangle is absent in the inspected Equipment2500 image, but this does not
resolve those thin lines or establish all HUD animations and placements.

The pale overhead canopy is still present on the inventory pages. Its appearance
does not independently establish a renderer defect or its intended relationship
to the preview camera. Hair has no obvious opaque rectangular backing in the
inspected poses, but its cutout edges and native reference parity remain
unverified. No controlled smoke effect or matching original sky-wedge pose is
shown. These views do not establish that all graphics are fixed, and do not
attribute any actual game pixel change to the new cache-coherence fix. The
corresponding SR/NR views are qualified below.

### Final combined SR and NR views and unresolved sky-region artifact

The four corresponding 3840x2160 images in
`build/t/followup-20261018-final-srnr/` use the same combined candidate, scale4,
audio enabled, SR Quality, and an NR toggle sequence. The manifest requests
NRoff at1900, on2000, off2200 and on2300. Its log records successful SR and NR
evaluations at GSframe2500; this establishes execution, not visual correctness.

| View | Observation |
|---|---|
| [1900](../../../build/t/followup-20261018-final-srnr/render_1900.png) | Canopy, ponytail and corner orb placement are coherent in the gameplay pose. Thin white orb-border lines remain visible. |
| [2500](../../../build/t/followup-20261018-final-srnr/render_2500.png) | Equipment health fill remains inside its ring, mana swirl is visible, and both fills align with their rings. Flat gray jagged lobes appear beneath the canopy above the mountains. |
| [4000](../../../build/t/followup-20261018-final-srnr/render_4000.png) | Journal layout and orb placement remain coherent; the gray lobes persist above the mountains. |
| [4500](../../../build/t/followup-20261018-final-srnr/render_4500.png) | Bow and ammo200 are visible. Gray jagged/outlined shapes persist beneath the canopy; thin white orb-border lines remain. |

The latest SR/NR sky-region artifact is visible at approximately
x2520..3320/y300..550 in the 3840-wide images, with its shape varying by pose.
Direct inspection of native2500/4000/4500 does not show the same prominent gray
lobes. These are matching script stages and executable identities, but character
and camera poses differ slightly. SR and NR also change together relative to
native. Consequently this comparison does not isolate an NR-only cause, prove
a new regression relative to older SR/NR builds, or identify the responsible
GS draw. The artifact remains unresolved; no sky fix is claimed.

**Next bounded root-cause action:** obtain one artifact frame at a locked
inventory camera/pose while holding SR Quality constant and comparing NRoff/on.
Preserve that frame's scene camera, projection/viewport and GS state. Take
bounded color snapshots before SR, after SR/before NR, and after NR/before HUD,
plus matching depth and alpha. If the gray shapes already exist before SR,
inspect the contributing sky/canopy/backdrop draws and their texture state; if
they first appear in a resolve, isolate that stage. A late GIF capture without
initial VRAM is insufficient as a faithful standalone image replay. Avoid
geometry or decoder changes until the failing stage and draw are established.

Both modes retain the orb addressing correction in the inspected views while
thin white border lines remain. Smoke, original gate-guard shadows and a
reference-quality hair-alpha comparison remain outside these images' proof.
No extra GPU work or production-code change was performed for this inspection.

The parent reports combined game executable SHA256
`54F082B55C3A768391EC5B39DB48033F2AA77DEA6B51CDFBBE1E81A61D92D557`.
The worker did not run that executable. Live before/after evidence and longer
performance comparisons belong to the parent and
`docs/journal/2026-10-18/followup.md`; that document was not modified by this worker.
No distribution update, push or patch regeneration accompanies this receipt.
