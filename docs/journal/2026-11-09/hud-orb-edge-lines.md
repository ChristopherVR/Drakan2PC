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

## Follow-up: the line past the right end of both orbs with HD textures (fixed)

The player still saw a thin vertical line just past the right end of the health orb and of the compass orb (screenshot
3397 x 2101, scale 5, widescreen, HD textures, DLSS quality). The first fix was only tested at 1280 x 720.

Reproduced in a private copy of dist/DrakanPC (all settings from the player's drakan.ini, OotF campaign intro level over
the world, 3397 x 2032 window, `PS2X_GS_SCALE=5`), presented frame captured with `DRAKAN_OVERLAY_SHOT_DIR`. Only visible
over a world background, from about 36 s into the intro level (the HUD sheet is then an ordinary 128 x 128 texture with
the HD replacement, 4 physical texels per texel, not a render target).

Cause. A temporary skip of the single orb-face sprite (native 47..107, tex0 TBP varies per frame, found by its mapped
right edge) removed the line; skipping the tint sprite or the frame sprite did not. The footprint clamp was already
applied (`valid=1`, u 28.12..87.88) but its margin was half a physical texel (.5 / factor = 0.125 texel). The outer
sub-texels of the HD replacement of texel 87 blend towards the opaque white outline of texel 88, so the last 0.125 texel
still drew a faint light column. Margins of 1 physical texel (0.25) kept the line, 1.5 (0.375) and 2 (0.5) removed it.

Fix. `runtime/gs/src/sprite_footprint.h` now clamps to the centres of the first and last sampled texels
(`floor(p) + .5` .. `floor(q) + .5`, exactly the positions the original raster sampled), for every texture factor and for
render-target sources. Shared by the D3D11 and WebGPU backends. `runtime/gs/tests/test_sprite_footprint.cpp` is updated
(factors 1 to 8 give the same bounds) and covers the HD case (3, 4, 5, 8). Cost: the outer half texel of a native HUD
sprite shows the centre colour of its edge texel instead of the HD detail.

Verification (same frame phase, 4x nearest crops in build/review/hud/before.png and after.png): vertical-line score of
the columns around the orbs drops from 53.6 (compass) / 60.9 (health) to 11.2 / 13.1 (only real orb features left).
The Drakan 2 HUD at the same settings looks unchanged. Not run: the WebGPU backend, the Ruined Village gameplay HUD (the
level opens with minutes of cutscenes, and the player's HUD sat at the screen corners without the health pillar, a state
I could not reproduce), `GSR_TEXUP=off` with the HD pack still on showed the line before the change, and was not
re-checked after.
