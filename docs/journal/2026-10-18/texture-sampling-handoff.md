# Texture sampling and pixel-validation handoff

Scope: shader fragment, main-shader integration, CPU tests and assigned WARP pixel tests.
No backend, CMake, runtime configuration, asset, dist or publication changes.
WARP used only during explicit device slots; no hardware GPU or game launch by
this worker. No commit or push. Parent messaging rejected
the native-ancestor destination. User subsequently explicitly assigned shader
integration to this worker; it is now applied.

## Implemented candidate and integration

`runtime/gs/src/gs_rt_sampling.inc` replaces the contiguous `wrapF` and
`sampleRT` functions in `gs_shaders.h`. It wraps each physical bilinear tap
in logical GS coordinates and applies CT24/CT16 TEXA/AEM to point-fetched
texels before interpolating. CT32 alpha remains unchanged. Physical scale
comes from actual texture dimensions times `cb_src.zw`, preserving the
existing origin and reduced-world mapping rather than assuming native scale.

Applied shader integration: replaced from `float wrapF(` up to (but
excluding) `float4 sampleMip(` with:

```cpp
)HLSL"
#include "gs_rt_sampling.inc"
R"HLSL(
```

This concatenates the fragment into the existing literal initializer. No
backend constants or backend hunks are required. `gs_rt_sampling.h` gives
the CPU compilation test standalone access to that same fragment.
**The current production shader now contains the fragment.** Point sampling
retains its original continuous address calculation, including fractional
reduced scales. CT32 CLAMP/CLAMP linear sampling retains the original single
hardware-filter fetch and native-edge clamp policy. Other linear RT paths
use the corrected tap addressing and alpha expansion. This RT change did not alter ordinary texture
sampling, shader bindings, constant layout, vertex layout or depth policy
changed. Both colour and motion consume the same texture/coverage functions.

## Validation

`runtime/gs/tests/run_rt_sampling_cpu.ps1`: **24,919 checks PASS**, MSVC
`/O2 /W4 /WX`, including eight D3DCompile invocations for both blend paths
and VSMain/PSMain/VSMotion/PSMotion. No D3D device is created. Outputs use a
unique temporary directory outside source/dist.

The existing alpha pipeline also passes **263,692 CPU checks and eight shader
compilations** against the integrated shader. The eight main variants are
VSMain/PSMain/VSMotion/PSMotion with HW_BLEND=0 and1; these cover every backend
entry/macro combination, plus the unused motion/vertex HW_BLEND=1 combinations.

The address checks compare a scalar transcription of the candidate against
an independent discrete grid oracle at scales1/2/4/8, dimensions2/8/128,
all four clamp modes and negative/positive coordinates. Numeric witnesses
cover the repeat seam (red127.5 versus255), CT24 AEM before filtering
(alpha64 versus128), CT16 alpha transitions, alpha-set black and CT32 alpha
preservation. Shader compilation uses the integrated production initializer
directly. These CPU arithmetic and syntax/optimization checks do not execute
HLSL pixels; the separate WARP results below do. Runtime cost and scene changes
remain outside these synthetic sampler checks. The implementation
uses four point fetches in the corrected linear paths, replacing one hardware
linear fetch. The preserved CT32 CLAMP/CLAMP path avoids that cost. A fractional
scale counterexample also protects the decision to preserve point addressing.

Existing tests also pass: sprite convention12, HUD packet/atlas12,
RT audit5. No new depth defect was established; no depth policy was changed.

### Final WARP pixel results

`runtime/gs/tests/run_rt_sampling_pixels.ps1 -RunWarp`: **368 draws,
23,552 channel checks, zero failures**. This supersedes the earlier144-draw,
9,216-check RT-only result; it comprises those144 RT draws plus224 ordinary
CT32 footprint draws. WARP is selected explicitly with no hardware fallback.
The harness compiles all eight main/motion shader variants, reflects the
192-byte CB including `spriteBounds` at offset176, and renders actual integrated
VSMain/PSMain pixels into a float target from byte-format source textures.
The runner builds only unless `-RunWarp` is supplied.

RT cases keep flag24 off and cover CT24 AEM expansion before bilinear filtering,
horizontal/vertical repeat seams, positive/negative repeat periods, four-tap
corners, point controls and alpha64 failing GEQUAL96. Scales are1,4,1.5,.75
and mixed X/Y1.5/.75. These fixtures use aligned physical alias origins/periods;
they do not establish arbitrary fractional-boundary behavior. Both blend shader
variants run with blending disabled to isolate sampled colour and stored alpha.
Numerical expectations use fixed half/quarter weights, with one GS byte-unit
tolerance for floating/filter quantization, rather than the CPU sampler oracle.

The224 flag24 probes cover FST/STQ, both axes and texture factors1/2/4/8.
They demonstrate flag-off legacy sampling, exclusion of adjacent unsampled
white/blue cells, preservation of an intentionally included white outline and
its alpha127, and distinct first/last HD subtexels inside a retained cell.
Bounds come from original endpoint quads through the parent's actual helper.
Constant-UV probes isolate helper/shader sampling from raster interpolation.
They do **not** execute backend batch splitting, HUD transforms, native-origin
conversion, varying-UV sprite interpolation or complete scene composition.

### Parent footprint review and live evidence

Read-only review of `sprite_footprint.h`, backend split policy and shader flag24
found no confirmed correctness defect in the guarded path. Every six-vertex
member must validate before a batch splits; sprite order is retained. Bounds
are computed before HUD mapping, only for eligible post-world, non-RT,
nonmipped point sprites. Bounds retain physical subtexels of each native cell;
neither white colour nor alpha127 is globally suppressed. Broader default-on
coverage and splitting/placement remain parent integration responsibilities.

Parent reported21:9 on/off evidence removing the mana edge; health's12-vertex
batch was outside the original single-sprite path. After parent added splitting,
the32:9 run showed both health/mana edges gone and coherent gap scenery.
Existing private evidence paths:

- [21:9 crop/footprint-on log](../../../build/t/inventory-crop21-probe01/stderr.log)
  and [Equipment image](../../../build/t/inventory-crop21-probe01/input_ingame_aspect_equipment.bmp).
- [21:9 footprint-off comparison log](../../../build/t/inventory-crop21-footprintoff01/stderr.log)
  and [Equipment image](../../../build/t/inventory-crop21-footprintoff01/input_ingame_aspect_equipment.bmp).
- [32:9 split-path log](../../../build/t/inventory-crop32-probe02/stderr.log)
  and [Equipment image](../../../build/t/inventory-crop32-probe02/input_ingame_aspect_equipment.bmp).

These scene verdicts are parent observations, not new image validation or a
game run by this worker. Final23-native/23-combined checks are not asserted by
this handoff without their separate parent receipts.

## Requested HP/mana screenshot

Inspected `build/t/native-inventory-final/input_ingame_category_equipment.bmp`
(3840x2160), SHA256
`6775b63103cc74f87fc1acb4880d6ae033cb93a117a89e1acfd1e2697a396a8b`.
The visible straight bottom/right L edges agree with the earlier highlight
atlas footprint finding. CPU pixel probes find the health horizontal segment
x73..409 inclusive on y2082 and2083, all channels>=220. Mana has a segment
x2587..2704 on y2074, with other portions interrupted by underlying artwork.

Rechecked the existing complete raw atlas upload: SHA256
`c891e7a6fa92a5790bddd4b6c892356c746f8639d701ccc29b60ecb864ae8956`;
every perimeter texel x27/88, y2/61 is RGBA(255,255,255,127). Earlier packet
evidence identifies a CT32 uploaded texture, CLAMP/CLAMP, point filtering,
UV(28.5,60.5)..(88.5,2.5). This is ordinary `sampleLvl`, not `sampleRT`.
The RT candidate therefore does not fix these L edges. TEXA/AEM conversion
also cannot explain that CT32 atlas's authored high-alpha white perimeter.

At native scale the documented integer GS raster origin excludes that border;
continuous enlargement can still sample it at scale4. The native origin
discrepancy and enlarged HUD footprint require coordinated draw/raster policy,
not global alpha suppression or blanket UV shifts. No atlas masking, sampler
clamp change or transform change is justified by this screenshot alone.
Matching this new screenshot to the old capture's exact draw state remains an
inference; no current packet capture was produced here.

Follow-up bounded CPU investigation adds five checks in
`analysis/scripts/test_gs_orb_footprint_scope.py` (all pass): atlas clamp cannot
exclude an internal perimeter; AEM cannot remove CT32 white texels; identical
local UV/derivatives occur in both unwanted enlarged border samples and an
intentionally included native outline; an endpoint-derived native bound can
distinguish them; a colour/alpha mask cannot. At the time of that initial audit,
the shader received neither original endpoint bounds nor a HUD/sprite classification bit.
Those cannot be reconstructed reliably from local UV derivatives after HUD
mapping. A shader-only correction therefore lacks the draw evidence/metadata
needed to preserve authored outlines. This worker applied no ordinary-CT32
perimeter production fix; parent subsequently added endpoint transport and
flag24, reviewed and tested above. That draw-scoped policy needs original native
XY/UV endpoints transported before HUD transforms, guards for point filtering
and actual sprite draws, and tests preserving intentional perimeter inclusion,
reversed axes, fractional endpoints, HD textures and nonunit slopes. Parent's
backend investigation remains separate.

## Scene status

- Smoke: no controlled emitter in inspected evidence; torch flame does not
  establish smoke correctness.
- Hair: existing views show plausible cutout silhouettes; full alpha parity
  and reference comparisons remain unverified.
- Shadow: prior nearby-enemy evidence did not reproduce gate-guard stripes.
  Its CT32+CLAMP consumers do not exercise either identified RT defect.
- Sky/canopy: inspected corrected depth-stage and native screenshots show
  coherent canopy/scenery in those poses; broad sky/geometry parity remains
  unverified. Prior depth correction has its own independent evidence.
- Inventory gap: separate viewport correction has480 CPU policy checks,
  2,489 existing widescreen checks and parent live21:9/32:9 evidence. See
  [viewport handoff](inventory-viewport-handoff.md). The RT sampler fix
  is not the cause of that scene improvement.
