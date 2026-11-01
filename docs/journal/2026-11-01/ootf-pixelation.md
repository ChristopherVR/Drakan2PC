# OotF converted levels: why they look blocky next to the disc (2026-11-01)

Code: `editor/packages/formats/src/ootftex.ts` (`resampleSmooth`, `Ps2TextureOptions.minSize`), `ootfterrain.ts` (`minSize`, env
`DRAKAN_TERRAIN_TEX_MIN`). Probe: `build/ootf-run/tq/uv.ts`; runs `build/ootf-run/tq/{disc,rv,wc,rv2}` (2560x1440, `PS2X_GS_SCALE=4`,
`GSR_TEXUP=auto`, the dist `drakan.ini` video block: anisotropic 16, filter game, hd_textures 1; harness script `tq/run.sh`).

## Checked and ruled out (measured)

* Terrain UV range per cell: every cell of LEVEL1NA (84674) and of the PC Ruined Village (28514) spans exactly 0..0xffff on both
  axes (16/16 in 4096 steps). No multi-cell mapping on either platform. Cell size is `CELL_SIZE` 2048 on both.
* Texture header fields, bilinear / mip flags: converted textures sample like the disc ones in gsr (the live upscaler and
  anisotropic filter apply); the shots at the user's settings show smooth ground and cliffs, no nearest-neighbour look.
* Splat weights: per vertex (cells+1 grid), blended by the engine; the converter writes the same resolution as the disc.

## Measured difference: ground texel density is half the disc's

Cells by texture size on the terrain: **LEVEL1NA (disc) 128x128 61.4 %, 64x64 37.9 %, 256x256 0.6 %**; **Ruined Village (PC) 64x64
99.8 %, 128x128 0.2 %**. The same 2048-unit cell therefore carries 64 texels on converted ground where most of the disc's ground has
128 (and the disc was already called blurry at scale 1 in `hd-textures.md`). The earlier "sources are 16..256 px, nothing can improve"
only compared against the PC files, not against the disc the engine is tuned for.

## Fix

The converter now enlarges opaque terrain textures to at least 128 px (`minSize`, default 128, env `DRAKAN_TERRAIN_TEX_MIN`, 0 = keep PC
sizes) with a Catmull-Rom filter that wraps around the edges (tiling ground stays seamless). Cut-outs with alpha keep nearest
resampling (no soft fringes). Result is stored 32 bpp (more than 256 colours after the filter). This makes the engine-side texture the
same density as the disc's, independent of the runtime upscaler (scale 1, `texture_upscale = off`, or no HD pack), and the HD pack
then starts from a 128 px image (x4 = 512 px per cell instead of 256).

## Verification (2560x1440, scale 4, texup auto, dist ini video block)

* Disc Surdana (`tq/disc`) at those settings shows the ESRGAN HD pack on every disc texture (grass blades, trunk bark detail). Converted
  levels (`tq/rv`, `tq/wc`) get only the live smooth upscale because their texture hashes are not in the pack: the second visible gap
  besides the 64 vs 128 px density. The pack must be regenerated after every converter change that alters texture pixels:
  `hd_pack.py modextract --zip <mod.zip> --work build/hdpack-mod` then `hd_pack.py upscale --work build/hdpack-mod` (done for
  `tq/rv4`: 1240 unique, 127 new, 12 s; files stay uncommitted in `dist/DrakanPC/textures`).
* First attempt stored the enlarged ground 32 bpp (more than 256 colours after the filter): Ruined Village then needed 80967 texture
  decodes instead of 3224 and gameplay began 30 s after the level load (run timed out): 64 KB per 128 px texture thrashes the GS
  texture memory. Enlarged textures are now median-cut to 8 bpp like the disc's ground: 2479 decodes, run passes (`tq/rv4`).
* Not measured: a pixel-exact before/after of the same camera (the level is dark and the harness walks to slightly different spots);
  the offline numbers above (cell usage by size) are the evidence.
