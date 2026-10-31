# OotF texture quality: what was lost and what was lifted (2026-11-01)

Code: `editor/packages/formats/src/ootftex.ts` (`encodePs2Texture`, mode `truecolor`), `ootfterrain.ts` (terrain default size and
mode). Runs: `build/ootf-run/q0..q2` (960x540, texture upscale off, as the harness), `uo` / `un` (1280x720, GS scale 3,
`GSR_TEXUP=auto`, old vs new converter); helper scripts `build/ootf-run/q/t.sh`, `u.sh`, offline probes `err.ts`, `lr.ts`.

## Source data

OotF Ruined Village + Mountain World Common TXDs: sizes 16..256 only, mostly 64x64 and 128x128; 8 bpp (palette) or 16 bpp.
No PC texture is larger than 256. The PC game itself therefore has the same texel density; a converter cannot add detail.

## Where the converter lost quality (all removed)

| Place | Before | Now |
| --- | --- | --- |
| Terrain size cap `maxSize` (`DRAKAN_TERRAIN_TEX_MAX`) | 128: nearest-neighbour halving of every 256 texture (54 of the Ruined Village set) | 256 (every PC source size kept) |
| Terrain mode | always `indexed`: median cut to 256 colours | `truecolor` |
| Model/creature/object textures | 16 bpp 1:5:5:5 above 256 colours (565 -> 555, 1 bit of alpha), median cut to 8 bpp when alpha is graded | `truecolor`: 8 bpp when at most 256 colours (exact), else 32 bpp RGBA (disc flags word 7, alpha 0..0x80), no quantising |
| Layer texture count | cap 0 (unlimited), chunk split at 10 textures (engine limit, see ootf-texture-limit.md) | unchanged: nothing else reduces it (`DRAKAN_TERRAIN_LAYERTEX` and `maxTextures` are opt-in) |

Offline error against the PC source (mean abs RGB error at source resolution, 448 textures): old 128 indexed 0.59 (4.06 over the
50 textures of 256 px); 256 indexed 0.18 (0.38); the 16 bpp route 0.25. Truecolor is exact (up to the 0x80 alpha scale).
`DRAKAN_TERRAIN_TEX_MODE=0|1|2` (indexed / auto / truecolor) restores the old encodings for experiments.

The engine accepts 32 bpp and 256-pixel terrain textures in Proc Textures: `q1` (256 / 16 bpp) and `q2` (256 / 32 bpp) both load
Ruined Village and walk (437 entities), like the 128 / 8 bpp baseline `q0`. (Earlier note "16 bpp terrain only at 64x64 on the
disc" was only an observation, not an engine limit.)

## What was not the cause

* Runtime: the GS backend already upscales every decoded texture by content (xBRZ for pixel art, smooth for photographic) when
  `[video] texture_upscale` is not off and the internal scale is 2+, applies mips and 16x anisotropy, and replaces by content hash
  from `textures/` (HD pack). That works for converted textures too (no hash needed for the live upscale). The harness turns it
  off (`GSR_TEXUP=off`, `PS2X_GS_SCALE=1`) so harness screenshots are the worst case. No runtime change was needed.
* UV scale: PC and PS2 cells both carry 0..0xffff UVs per cell, copied unchanged; one 64 or 128 px texture covers a cell, the same
  density as on PC. Ground close to the camera is a magnified 64 px tile: that is the source data.
* One real structural loss remains: PC has a texture per triangle (left/right), PS2 one per cell. Only 1.2 % of the Ruined Village
  cells (328 of 28514) differ; the left triangle wins.

## Before / after

At 960x540 with upscale off the screenshots of 128/8 bpp, 256/16 bpp and 256/32 bpp are visually the same on the ground (Laplacian
variance 72.6 / 72.7 / 72.7): the ground textures are 64-128 px sources. The gain is on the 256 px cliff textures and in colour
fidelity (no banding from the 256-colour palettes). A user's "extremely blurry" mostly comes from low source density plus
`texture_upscale = off` or scale 1; with the default settings the runtime upscaler smooths it. A real sharpening would need
new content (Real-ESRGAN of the converted textures through the HD pack: `analysis/scripts/hd_pack.py` hashes decoded RGBA, so
a pack of the converted TXD could be generated the same way); not done.

## HD pack for converted mods (added later the same day)

`hd_pack.py modextract --zip <mod.zip> --work build/hdpack-mod` walks every SRSC member of a converted mod pack (level splat TXD,
PROCTEES.TXD, RESOURES.TXD ...), decodes type 0x40 textures like gsr and hashes them (same `hash_content`); then
`hd_pack.py upscale --work build/hdpack-mod` (default `--out dist/DrakanPC/textures`, skips hashes already present) runs Real-ESRGAN.
Ruined Village: 1207 textures, 1113 unique, 498 not yet in the disc pack, upscaled in 49 s. `upscale` now merges the existing
`alias.txt` instead of overwriting it, and makes `--work/--out` absolute (esrgan runs in its own cwd). Pack stays uncommitted.

Default settings: `dist/DrakanPC/drakan.ini` has `texture_upscale = auto`, `texture_dir = textures`, so mod textures are upscaled
live and replaced by hash with no change needed. Harness runs (1280x720, `PS2X_GS_SCALE=3`, `build/ootf-run/hd-before` with
`GSR_TEXUP=off`, `hd-after` with `auto`) both PASS on Ruined Village; the two runs ended in different game states
(the after run had the NPC cut scene in view), so the shots are not pixel-comparable. Laplacian variance dropped (168 -> 110-133)
because the upscaled ground is smoother, not sharper per texel: the content still has no detail beyond 64-128 px sources.
