# HUD orb stray edge lines at render scale above 1

## Symptom

In the Order of the Flame campaign (DRAKAN_GAME=d1, converted level) a thin light line ran down the right edge and along
the bottom edge of both HUD orbs (health and the compass/navigation orb) once the internal render scale was 2 or more.
At scale 1 there was no line. The Drakan 2 (The Ancients' Gates) campaign HUD did not show it in the same test.

## Cause

Not HD textures (the line persisted with `DRAKAN_HD_TEXTURES=0` and `GSR_TEXUP=off`), not the sprite seam filler
(`GSR_SEAMFIX=0`), not the HUD anamorphic map (`GSR_HUDSCALE=1`, a 4:3 window) and not the existing sprite footprint
clamp (`GSR_SPRITE_FOOTPRINT=0` changed nothing). A debug skip of draws by texture base pointer isolated it: the lines
come from the orb sprites that sample a 128 x 128 render-target sheet (tex0 TBP 0x3280, e.g. a 60 x 58 native sprite
reading texels 28.5..88.5 x 60.5..2.5). `sprite_footprint` keeps the sampled texel inside the cells the original 1x
raster covered, but it was disabled for render-target sources (`!tb.isRT`). At scale 2 the last render pixel of the
sprite samples texel 88 (and row 2), which holds the white outline of the neighbouring piece in the sheet, so a one
render pixel line appeared just outside the intended region on the right and bottom edge.

## Fix

`runtime/gs/src/d3d11_draw.cpp` and `runtime/gs/src/wgpu_draw.cpp`: apply the native sprite footprint to render-target
sources too (batched and single draws). For a render target the sub-texel margin uses the render scale as the texture
factor (half a physical texel), since a render target is `scale` times denser than its logical size.
`runtime/gs/tests/test_sprite_footprint.cpp` gets a case with the measured orb geometry (factor 2).

## Verification

Private copy of dist/DrakanPC, OotF Ruined Village pack (campaign), window 1280 x 720, black background behind the orbs
(the intro level draws the HUD over black, which makes the line obvious):

| Run | Neutral light columns/rows in the orb area |
| --- | --- |
| scale 2 before | columns 266,267,1201,1202, rows 691,692 |
| scale 2 after | none |
| scale 3 after | none |
| scale 1 (always) | none |

Drakan 2 New Game HUD at scale 2 after the fix looks unchanged. Not verified: the WebGPU backend (same edit, not run),
other render-target sprites such as inventory pieces (the footprint only applies after EndWorld, never to world draws).
