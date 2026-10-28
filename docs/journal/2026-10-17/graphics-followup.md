# GS alpha follow-up

Source baseline: outer `7a3625b`; private game baseline executable SHA256
`2AE7FC64D6AD9C87186925913F8D5F2C35D0AD0F5CA1FA89D994754DF34555FB`.
No runtime patch export, private assets, deployment or generated game rebuild
is included in this change. Main integrates the source and maintains the pinned
runtime/compiler patch manifest separately.

## Corrected defects

- Alpha-test FB_ONLY/RGB_ONLY failure passes retain RGB blending. AFAIL changes
  write masks/depth writes; it does not turn the ALPHA equation off.
- FBA sets stored framebuffer alpha without changing source alpha used by ALPHA
  or PABE. Both fixed-function and shader blending follow this rule.
- CT16 AEM suppresses an all-zero texel, preserving black with its alpha bit
  set (`0x8000 -> TA1`). This applies to texture/CLUT decode and RT sampling.

Primary implementation cross-checks (independent fixes, no copied code):
[PCSX2 alpha-failure pass policy](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSRendererHW.cpp),
[FBA versus blend alpha](https://github.com/PCSX2/pcsx2/blob/master/bin/resources/shaders/dx11/tfx.fx),
[16-bit TEXA expansion](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/GSLocalMemory.h).

## Evidence and limits

`gsr_test_alpha_pipeline` exhaustively checks both AEM settings over every
16-bit texel, CT16 CLUT conversion, alpha-write policy and all failure-pass
write-mask combinations. It compiles the actual VSMain/PSMain/VSMotion/PSMotion
for both blend paths without creating a device.

`gsr_test_synthetic --warp --scale 1 --alpha-only`: **16 checks, zero failures**
in eight GIF scenes. RGB and actual stored framebuffer alpha are checked for
FBA hardware/software paths, FB_ONLY hardware/software paths, RGB_ONLY, PABE,
and opaque/transparent CT16 black. No hardware GPU or game run used the fixes.

The private baseline run quit normally in 60.3 seconds, exit 0, hash unchanged.
Three GS frames at **34 seconds** produced a 5,694,088-byte capture; screenshot
triggers were guest GS frames 1900/2500. Late captures omit initial VRAM and are
not faithful standalone image replays. The state audit found 5,283 GIF packets
and 55,389 vertex kicks, but no observed FBA-blending, AFAIL-color-fallback or
CT16-AEM opportunities. These fixes are proven synthetic defects, **not yet
demonstrated causes of the photographed smoke, hair or striped shadows**.

The shadow screenshot shows long parallel dark streaks; old inventory captures
also contain malformed preview geometry. Neither is resolved by asserting an
alpha fix. RT ownership/filter seams and geometry/culling need separate evidence.
Main owns live before/after testing and all distribution changes.

## Bounded RT investigation (not a shadow fix)

The three-frame baseline packet audit finds an observed producer at FRAME
`0x201d1` (FBP465, FBW2, CT32), before a texture consumer at TBP14880:

- Clear: submitted rectangle 0..128 on both axes, RGBA0, ALPHA0xA.
- Geometry: 4,548 submitted vertex kicks, x15.0625..98.25,
  y9.6875..96.6875, RGBA(127,127,127,140), TEST0x34002,
  ZBUF0x100000100. Depth writes are masked, depth comparison ALWAYS;
  DATE is enabled. Fog and AA1 are enabled in PRIM0x2AC.
- Consumer: 128x128 CT32, CLAMP both axes, ALPHA0x44, TCC1/TFX0,
  vertex alpha40..42. Texture alpha140 modulates to approximately43..45;
  this is not an observed over-128 source blend coefficient.
- Although submitted consumer UVs extend outside the logical texture, current
  `sampleRT` clamps them to 0.5..127.5. The observed source is width128 and
  allocated at least512 rows. This does not demonstrate an extent overrun.
  This RT path uses hardware `SampleLevel`, not out-of-range `Texture.Load`.

Only two FRAME layouts occur in this late capture (0/8/CT32 and
465/2/CT32). Earlier cached RTs are unknown. Do not infer exclusive ownership,
actual covered pixels, or a match to the original photographed NPC shadow.
The other existing small capture contains no usable observed RT producer state.

New tools: `analysis/scripts/gs_rt_capture_audit.py` reports packed/REGLIST
UV/STQ, producer geometry, texture formats, clamp and transfer register state.
`python analysis/scripts/test_gs_rt_capture_audit.py`: five CPU tests pass.
Includes explicit numeric counterexamples for these pending sampler defects:

1. RT bilinear repeat wraps only the centre UV, not each tap. At u0 in a
   two-texel logical [red,black] region within a larger RT, correct red is127.5;
   current physical sampler clamp gives255.
2. RT CT24/16 TEXA/AEM conversion happens after filtering. Halfway from opaque
   red to AEM-transparent black with TA0=128, correct alpha is64; conversion
   after filtering produces128. Over blue200 this gives approximately
   (64,0,100) versus (128,0,0).

Opt-in GIF fixtures in `gsr_test_synthetic --warp --scale 1 --rt-filter-only`
cover these two contracts. They compile, but have **not been executed** while
main/Banach own the GPU. They are diagnostic fixtures outside the normal suite;
the current shader is expected to fail. No sampler/RT/depth production change
has been made. Neither counterexample is exercised by the observed CT32+CLAMP
RT consumer, so neither is claimed as the photographed shadow cause.

### Next actual NPC capture

Banach's documented source is `build/recomp_combat/encounter/fixture-hostile675`.
Its pre-hit snapshot places Rynn at (675796.19,159754.33,523345.63), NPC83 at
(675318.13,159439.25,523348.31), distance572.56. This is a repeatable nearby
enemy scene; matching the old gate-guard shadow photograph is still unverified.

Prepare independent cards and executable/dependencies; use the same equip Q,
target F and W65..80 approach as the passing melee recipe, then hold position
and quit at gameplay380. No RAM dumps or attacks are needed for the first
visual inspection. Use current integrated executable, DLSSoff/NR0/scale4.
Take three actual GSR output PNGs at guest GS frames1900/2200/2500, and just
three GIF frames with `PS2X_GS_CAPTURE_FROM=37` **seconds**. Startup timing can
shift: check screenshots/capture metadata against gameplay before interpreting.
One-frame `GSR_DRAWLOG=2200-2200`, `GSR_DRAWLOGV=3`, `GSR_RTLIST=1` helps inspect
live allocations and fallback candidates. Avoid all-frame RT dumps/TEXLOG.
If the PNG shows stripes, follow with a targeted source-RT dump/consumer draw
comparison. If it does not, report not reproduced at this pose; do not label
the original issue fixed. Keep source/original cards hashed and unchanged.

### Actual nearby-enemy capture: completed

Private evidence root: `tools/graphics-followup/build/graphics-shadow/`.
Main's stage was copied read-only, not modified. Integrated executable SHA256
`C27468C483C42639C6E8911AB956C1A39A47F64253847B8427CD522BEC67F3CB`;
private executable hash matched before/after. Normal script quit/exit0 in
54.316 seconds. Four source-fixture card file hashes matched before/after;
only private copied cards were supplied to the game. Original ISO-card hashes
were not measured by this run. GPU reservation released immediately afterward.

Three actual renderer PNGs are `render_1900.png`, `render_2200.png`,
`render_2500.png`, all3840x2160. They show Rynn and nearby hostile NPC(s),
including attacks/target effects and the ground shadows. Visual inspection
finds coherent actor silhouettes, not the long striped rectangular shadows in
the original `Screenshot 2026-10-17 065020.png`. Result: **not reproduced at
this fixture/pose**. This is a different actor/location and no paired old-build
run was performed, so the original gate-guard issue is **not proven fixed**.
Rynn's ponytail has a visible cutout edge without an obvious rectangular
background in these views; this is not a reference-quality hair-alpha verdict.
A torch flame is visible, but no controlled smoke effect was exercised.

Source capture: `captures/gscap_20261017_214445.bin`, 5,441,160 bytes,
SHA256 `0E6AEB5BB7EF5A3A91DC47FD00B9F5EE5BC3585A8A91521221EADA3E1C192E02`.
Three vsync records, 5,899 GIF packets, 50,364 submitted vertex kicks, no
split-tag report. Capture-relative markers are (0,0), (1,2222), (2,2223);
these must not be confused with input-script gameplay frame numbers.
Raw assets/capture/PNGs remain ignored local evidence, never tracked.

`rt-audit.json` finds CT32 shadow producers at FBP496/504 (FBW2), each clearing
128x128 then drawing bounded silhouette geometry. Consumers TBP15872/16128
use logical128x128, CLAMP1/1, TCC1/TFX0, ALPHA0x44. TBP16128 has both an exact
FBP504 candidate and an offset(0,128) FBP496 candidate: the existing exact-base
rule is relevant and this inspected scene does not show stale stripes.
No CT24/16 RT alpha-conversion/repeat-seam opportunity was demonstrated for
these shadows. Late-capture ownership history remains incomplete.

There is also a FRAME0 texture with logical1024x1024 and submitted UV0..512,
copied into FBP384/FBW4. This illustrates why rejecting an alias solely because
logical TW/TH exceed the RT dimensions would reject legitimate used regions.
The one-frame2200 draw log and RT inventory are in
`t/shadow-repro-01/stderr.log`; raw capture is near2222, not the exact same GS
frame as PNG2200. No new RT lookup, shader, depth or filtering fix was shipped.

Tracked receipt: `docs/journal/2026-10-17/data/graphics-shadow-repro-01.json`. Diagnostic
tools/opt-in fixtures are commit `e839ff7`; they do not change renderer behavior.

#### Detached blade qualification

PNG2200 contains a separate bright blade at approximately x3368..3389,
y951..1421 (3840x2160); that bright component is absent from PNG1900/2500.
Do not describe this capture as globally graphically clean. CPU pixel-component
inspection and the near2222 GIF state support an actively rendered right-side
3D UI preview, rather than simply a retained inventory-page image:

- Current-stream depth-only sprites cover native x429..493, y218..410.
- Subsequent FRAME0 colour-writing geometry, PRIM0x23C, TEST0x5064C,
  TEX0 `0x2007bd6555307de7`, has101 submitted vertices in the inspected
  x440..462/y215..345 region. Their bounds are x442.5..460.3125,
  y219.4375..344.8125, consistent with the separate blade's screen region.
- The recipe never opens inventory GUI21. The native source has a 3D preview
  route `FUN_001ab530 -> FUN_001e97a0` (caller return0x1AB7D8), matching the
  runtime's explicit preview-camera hook. This is source inference, not a
  recorded live PC stack proving this exact pixel's caller.

Classification: active native gameplay weapon/selector preview is supported;
normal intended scale/placement is **not established** without a same-pose
native4:3 reference or a targeted draw-isolation capture. No HUD/preview fix
was made, and no further GPU reservation was taken for this CPU check.
