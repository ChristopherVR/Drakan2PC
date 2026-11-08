# OotF waterfalls do not flow (2026-11-08)

User report: at New Game of Order of the Flame the waterfall in the Intro landscape shot (perched owl) draws as a still
image. The same applies to every converted model with an animated texture.

## How the PC data animates the waterfall

The waterfalls are plain scenery classes (`Waterfall Tall #2`, `Village Waterfall 04`: rfl 17 "Ground Object / Building",
no scroll or animation field among their 20 class fields). The flow is a texture animation:

* `Intro Objects.txd` 5024 is the first frame (32x32, 16 bpp, **fps byte 40**, flags 0x02), 5025..5053 follow with flags
  0x22 (0x20 = "next frame"): 30 frames at 40 fps.
* The models (`Fall02#2` 3787, `villagefalls04` 3930, shading 6) list **all 30 frames** in their 0x0206 texture list
  (entries 0..29 = 5024..5053) and every polygon uses entry 0.

No PC terrain cell uses an animated texture in any of the 23 levels (census over every `.lvl`), so rivers, lakes and lava
on the terrain are still in Drakan 1 too; only objects animate. Animated object textures in the install: the waterfalls of
Intro, Ruined Village, Wartok Canyons, Alwarren and the Islands (29 to 30 frames at 30 or 40 fps), the torch, sconce and
banner flames (24 frames), the Surreal logo of the Intro (8 frames at 20 fps), the Temple / Support flames, power crystals,
the Runeblade and its glow.

## How the PS2 disc does it

The same mechanism, byte for byte. TXD: first frame carries the rate in +36 (WORLDCON 591: 15, 652: 30; RESOURES 6317:
8), the following frames have +36 = 10 and +37 = 0x22. Models list every frame after the first and draw with the first:
`SurdanaFalls01` (WORLDCON 1430: 591..620), `SurdanaFalls02` and `SurdanaFalls_05LU` (652..681), `D2 flame breath`
(6317..6321), `troglightning`, `sanddragonlightning`, `iceblast`, `Mulgak_Head(B)` (6525..6530, polygons on entry 0 only).
No scroll class or shading bit is involved (`Mulgak_Head` has shading 9, `troglightning` 2).

## Root cause

The converter already wrote the frames into the TXD with the right +36 / +37 bytes (consecutive ids, `ootfassets.ts`
`addTexture`), but `pcToNeutral` builds the PS2 texture list from the textures the polygons use, so a converted waterfall
listed frame 0 only (`Fall02#2`: `tex 6668/0`). With nothing after frame 0 in the list the engine has no further frame to
step to and draws the first one.

## Fix (formats submodule)

`PcToPs2Options.textureFrames` (`ootfmodel.ts`): when a mapped texture is the first of `n` frames, the `n - 1` following
PS2 ids are listed right after it; polygons keep naming the first frame. `OotfAssetBuilder` answers it from its frame table
(`texFrames`). The regenerated Intro pack lists `6668..6697` for `Fall02#2` and `villagefalls04`, and `6651..6658` for the
Surreal logo models. Applies to every converted model (waterfalls, flames, logo, crystals); terrain needs nothing.

Tests: `ootfmodel.test.ts` (frames listed after the first, polygons and later textures re-indexed), `ootftex.test.ts`
(asset builder lists all three frames of an animated texture).

## Verification

Two campaign runs of the Intro (`ootf_level_run.py --campaign --no-convert`, same recipe: screenshot pairs 150 ms apart every
2 s from launch + 30 s, no input), one with the pack of the old converter (`build/ootf-run/wf-base`) and one with the fixed
pack (`wf-fix`); both PASS (Intro loaded, script quit). In the owl and landscape shot the waterfall (window x 560..780,
y 350..540 at 960x540) changes between the two shots of a pair:

| pack | pixels changed by > 30 in the waterfall (of 41800), five pairs | elsewhere |
| --- | --- | --- |
| old | 30, 31, 43, 64, 42 (mean diff 1.1) | 2272 to 19672 |
| fixed | 3657, 4260, 3868, 3676, 4203 (mean diff 11 to 13) | 3384 to 6870 |

The crops (`build/ootf-run/wf-fix/waterfall_pairs.png`, top row old, bottom row fixed) show the streaks moving down in the
fixed pack and an identical picture in the old one. Ruined Village, Wartok Canyons, Alwarren and Islands waterfalls and the
torch flames use the same path (same models, same list rule) but were not photographed.

## Open

* The fps of the PC first frame is written as is (40 for the Intro / Ruined Village falls, 30 elsewhere); the disc uses 8
  to 30. The pace was not compared with the PC game.
* Terrain cells never animate (none in the PC data either), so nothing to do there.
