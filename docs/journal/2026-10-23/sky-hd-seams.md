# Sky seams, light-streak lines and smeared sky tiles from the HD pack (23 October 2026)

Report: 3840x2160, scale 4, DLSS Quality, HD pack on. The sky looks stitched: a hard vertical seam with a
smeared/blurred side, a ghosted moon, thin straight lines across the sky.

## What the sky is

The sky layers are drawn with **alpha-only textures**: RGB constant white, the picture in alpha. Examples:
`WORLDCON.TXD#167..170` (Surdana light-ray fan), `LEVEL4ON.TXD#6081..6085`, `LEVEL3ON.TXD#6036..6039`,
`LEVEL2RE.TXD#554..558`, `LEVEL8OS.TXD#5965..5969`: four or five 128/256 tiles that form one horizontal
panorama (tile N's right edge continues in tile N+1's left edge, not in its own left edge), plus round cloud/halo
discs such as `3fe25ad41cc5eaaa` (`LEVEL3ON/4ON/5NE`).

## Cause (HD pack generation, `analysis/scripts/hd_pack.py`)

1. `tile_axis_ok()` decided wrap vs mirror padding from the **RGB only**. Constant white RGB always "tiles", so the
   alpha plane was wrap padded although it does not tile (source alpha opposite-edge difference 23..53 levels
   against 0.6..1.2 between neighbouring columns). ESRGAN then pulled the opposite edge into the border columns:
   in `112bca9ba08e355d` the last HD column drops from 52 to 31 (mean alpha), in `7f162957d592102f` it jumps from
   26 to 47. Sky strips are huge on screen, so that one HD column becomes a visible straight line along every
   strip edge (the "light streak" lines). 67 pack textures are affected.
2. The alpha of these textures went through `realesrgan-x4plus-anime`. On soft cloud alpha the anime model invents
   pen strokes, glyph-like squares and hard internal tile seams (e.g. `d1d56a3a128a6ebb` scribbles,
   `cfe1ae3ff0680867` a vertical and horizontal seam, `3fe25ad41cc5eaaa` a hard horizontal cut through the disc).
   Since the exact-hash path uses the pack alpha as is (`d3d11_textures.cpp`, `applyGameAlpha` only runs for alias
   matches), those artefacts reach the screen as smeared regions with a hard edge next to a clean neighbouring
   tile. 37 pack textures (constant RGB, non-binary alpha).

The renderer's sampling is not at fault: REPEAT / REGION_* are resolved in logical texels for any HD factor
(`gs_shaders.h` `wrapCN`).

## Evidence

`play_run.py` in Surdana, looking up at the moon and the light-ray fan (recipe in the scratch dir of the session,
`0 sync play 60`, `mouse 0 -60 10`, then `mouse 40 0 10` turns with shots). Runs in `build/harness/`:

| Run | Line across the sky |
| --- | --- |
| `sky-seam-up` (installed settings) | yes |
| `sky-ab-nodlss` (`DRAKAN_DLSS=off`) | yes |
| `sky-ab-notexup` (`GSR_TEXUP=off`) | yes |
| `sky-ab-nohd` (`GSR_TEXREPLACE=0`) | no |
| `sky-ab-plain` (all of the above off) | no |
| `sky-fix-old` (`GSR_TEXDIR` = only the 4 old ray textures) | yes |
| `sky-fix-new`, `sky-fix-lanczos` (same 4 textures regenerated with the fix) | no |

Comparison crops: `build/harness/ab_s5.png`, `fix_cmp.png`, `fix_cmp2.png`.

The large smeared seam with the ghosted moon in the user's screenshots was **not reproduced**: the level was
not identified (Surdana, 1B, 3A, 4A, 4B tried with a fixed look-up recipe). The user's own texture dump contains
the `LEVEL4ON` panorama tiles and `3fe25ad41cc5eaaa`, whose shipped HD alpha shows exactly this kind of damage,
so it is the most likely cause, not a proven one.

## Fix

`hd_pack.py upscale`: an axis is wrap padded only if the alpha tiles too; constant-RGB textures with soft alpha
get a Lanczos 4x of the (padded) alpha instead of the anime model. Regenerated `17f2f3`, `fdc1d7`, `d1d56a`,
`b76741`, `cfe1ae`, `3fe25a`, `112bca`, `7f1629`, `e55004`, `f4b14c`: border step 0.0..0.8 levels (was up to 37),
no invented strokes, the four `LEVEL4ON` tiles join into one continuous panorama.

To apply it to the installed pack, delete the 74 affected files from `dist/DrakanPC/textures` and run
`python -I analysis\scripts\hd_pack.py upscale` (it only fills missing files). Not done here (the pack is the
user's installed data).
