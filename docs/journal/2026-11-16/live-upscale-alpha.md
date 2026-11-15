# Live texture upscale: black ground under low alpha (2026-11-16)

Report: with HD textures off, ground drawn with Proc Textures 621 (brown rock) and 535 (rocky snow paths) shows black
patches: OotF Ruined Village (`2026-11-15/ootf-final-check.md`, 9b) and Drakan 2's Inquisitor's Lair
(`2026-11-15/ootf-ground-textures.md`, where `GSR_TEXUP=1`, live upscale off, made them disappear). Scratch, tools and
pictures: `build/review/upscale-alpha/` (private, not tracked). Nothing from either game is quoted here.

## Cause

`texup::smoothUpscale` (`runtime/gs/src/gs_texup.cpp`), the live filter `auto` picks for photographic textures, ran
Catmull-Rom on alpha-premultiplied colour and divided by the filtered alpha, with `inv = 0` below 1e-4. 621 is 32 bpp
with 43 % of its texels at alpha 0 (TXD decode); the splat pass draws its colour without texture alpha, so every
upscaled texel under alpha 0 was drawn black. Offline, the old filter on the decoded 621 (`tools/texup_before.exe`,
`--algo auto` picks smooth, 2x / 4x / 8x, wrap): 93 % of the output pixels over alpha-0 texels are more than 40 levels
darker than the source texel, mean luminance error -93 (`tools/cmp.py`). `ev/621_rgb_native_before_after.png`: native
RGB (nearest), old 4x, new 4x, colour only (as the splat pass draws it).

535 has no texel at alpha 0 in the TXD decode (14 % below 32 of 255): offline the old filter darkened only 0.4 % of
its low-alpha pixels by more than 40 levels. Whether the game edits its alpha at load (the runtime decode can differ
from the TXD decode) was not checked.

## Fix (renderer)

See `docs/reference/hd-textures.md` section 8b. In short: a plain (unweighted) bicubic colour is filtered alongside;
below 6/128 of the largest alpha the colour blends from alpha-weighted to plain; the un-premultiply divides by the
unclamped alpha and is clamped to the footprint's alpha-bearing colour range (widened by the plain colour); the unsharp
mask is done per colour, the alpha-weighted one only over neighbours that carry alpha; xBRZ's `gradient` keeps the
colour of two transparent texels. Alpha output is unchanged.

The converter workaround (OotF packs ship opaque copies of 621 / 535, `TerrainTextureBuilder` `nativeOpaque`, dev
knob `DRAKAN_TERRAIN_NATIVE_OPAQUE=0`) stays. With this renderer fix it is optional for the live filter; it still
covers an older exe and keeps the OotF ground independent of the filter.

## Evidence

* Unit tests (`gsr_test_texup`), all synthetic: colour under alpha 0 versus the same image made opaque at 2x / 4x / 8x
  for a texel checker, noisy mostly-low alpha (45 % zero) and a solid transparent block (mean / max channel
  difference new 0.2 to 3.7 / 10 to 22; old 4.4 to 21.5 / 157 to 199; inside the block at most 1 level); no halo at a
  cut-out disc over white, green or black colour (max 0 levels on visible pixels; old 27 to 33, from the clamped
  divide brightening the inside of the edge); xBRZ keeps colour under alpha 0 (mostly alpha-0 texels over a gradient with
  15 % scattered opaque ones). The new tests fail on the old filter (checked by building the tests against the old
  `gs_texup.cpp`).
* Offline on the TXD decodes (`tools/dump.ts`, `tools/topng.py`, the test CLI with `--file`):
  * 621 at 2x / 4x / 8x: over alpha-0 texels mean error -93 -> 0.0, |d| 94 -> 2.0 to 2.4, the same as 621 made opaque
    and filtered (2.4) and as the other alpha bins.
  * Opaque textures (537, 621 made opaque): output bit-identical to the old filter.
  * 16 textures with alpha (cut-outs and foliage from WORLDCON / RESOURES / MOUNTAON, FX particles, PROCTEES ground
    with holes; `tools/reg.py`): alpha bit-identical in all; the xBRZ-classified ones unchanged except colour under
    alpha 0. On visible pixels (alpha >= 64) within two source texels of transparency (`tools/edge.py`) the signed
    luminance error versus the source shrinks (WORLDCON 320: +2.3 -> +0.6, 322: +3.3 -> +0.2; the old unsharp mask
    sharpened edges against black) and the share of pixels more than 30 levels darker stays the same (+-0.2 points).
    In the band alpha 16 to 63 (of 255; visible on blended foliage, above the ramp) the error against the nearest
    source texel shrinks for all four cut-outs tried (|d| 320: 69.9 -> 64.2, 322: 75.6 -> 66.2, 5909: 36.2 -> 32.3,
    6544: 40.6 -> 35.9; dark by more than 30 levels: 8.1 -> 6.5 %, 7.1 -> 3.0 %, 5.5 -> 3.9 %, 6.3 -> 3.8 %; the
    error is large in both because the nearest texel is often a transparent one).
  * xBRZ (WORLDCON 314 / 315, classified xBRZ): the only pixels that change are 648 and 431 alpha-0 pixels, all black
    before and none black after.
* Runtime tests: the first full `ctest --preset runtime-tests` run with the new filter passed all 200 (6 skip for
  missing data). A second full run had 199 of 200: `vertex_records_webgpu_d3d12` ended with 0xc0000409 (also alone);
  that test runs with `textureUpscale = 1` (the live filter off), so it does not reach this code, and other sessions
  had uncommitted renderer changes and a game on the GPU at the time. `gsr_test_texup`: 52 checks pass.

## Not verified

* In game. No exe was built with the change: a private game tree is a full build of about 75 minutes, and the shared
  `build/final-game` tree and `dist/DrakanPC` belong to the main session's build. Inquisitor's Lair and
  Ruined Village with HD textures off are the views to check, with the OotF pack converted with
  `DRAKAN_TERRAIN_NATIVE_OPAQUE=0` to see the renderer alone.
* The offline input is the TXD decode, not the renderer's decode (the game edits some palette alpha at load).
* The WebGPU backend shares the same filter (`gs_texture_cache.inl`) and was not run.
