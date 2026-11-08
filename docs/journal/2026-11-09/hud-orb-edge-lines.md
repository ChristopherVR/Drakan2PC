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

## Follow-up: a thin vertical line still shows with the player's real settings (open)

The player still sees a thin vertical line just past the right end of the health orb and of the compass orb (screenshot
3397 x 2101, scale 5, widescreen, HD textures, DLSS quality). The first fix was only tested at 1280 x 720 / scale 2-3.

Reproduced in a private copy of dist/DrakanPC (all settings from the player's drakan.ini, OotF campaign intro level, HUD over
black or over the world) with a 3397 x 2032 window and PS2X_GS_SCALE=5. Presented-frame captures use
`DRAKAN_OVERLAY_SHOT_DIR` (the renderer `shot` does not show the HUD the same way).

Bisect (line present = yes), all at 3397 wide:

| Setting changed | Line |
| --- | --- |
| none (scale 5, HD, xBRZ, DLSS quality) | yes, faint, about 3 output px wide, orb height |
| `GSR_TEXUP=off` | yes |
| `DRAKAN_HD_TEXTURES=0` | yes |
| `DRAKAN_DLSS=off` | yes |
| `PS2X_GS_SCALE=3` / `2` | yes |
| `PS2X_GS_SCALE=1` | no |
| `DRAKAN_HUD_ASPECT=1`, `GSR_HUDSCALE=1` | yes |
| `GSR_SEAMFIX=0` | yes |
| `GSR_SPRITE_FOOTPRINT=0` | much brighter (pure white) line on the right and bottom edge: the earlier bug |

So HD textures, xBRZ, DLSS, the widescreen HUD map and the seam filler are not the cause. The line needs a render scale
above 1 and survives the footprint clamp (which only removes the bright white part, see the GSR_SPRITE_FOOTPRINT=0 row).
The Drakan 2 New Game HUD at the same settings showed no line in a 2000 x 1237 and a 3397 x 2032 run.

Draw log of the left orb (frame with `GSR_DRAWLOG`, tex0 TBP 0x3280 sheet 128 x 512 at origin y=128): the orb is drawn by
single 60 x 58/60 sprites at x 47..107 / 48..108 reading u 28.5..88.5, plus the frame sprite 54..109 (tex TBP 0x3380, not a
render target) and the 7 x 97 pillar fill 39..46; the pillar fill and face come in one 12-vertex batch. All carry the
footprint flag (consts fl bit 24). The remaining line is therefore not texel 88 bleed; the next step is to find which of
these draws leaves the column (a debug skip by tex0 TBP is the planned bisect, the build was blocked, see below).

Evidence: build/review/hud/before.png (4x nearest crops of both orbs with the line).

Status: not fixed. The incremental build could not run because ps2recomp/ps2xRuntime/src/lib/drakan_settings_host.cpp in the
shared working tree is a 0-byte file (modified 08:48 by another session; HEAD has 3366 lines), which breaks the link.
