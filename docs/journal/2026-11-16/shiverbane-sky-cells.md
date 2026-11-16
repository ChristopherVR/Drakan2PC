# Issue #8: Shiverbane "wall of cells" in the sky

Issue #8 reports a camera-anchored grid of translucent cells in the Shiverbane sky. On foot it looks like soft rounded
rectangles behind the peaks. On Arokh it looks like vertical bands hanging from the top of the frame. The reporter ran
the Vulkan (WebGPU) renderer at internal scale 3, `texture_upscale = 4`, view distance 1.23, FOV 80 and 16:9. They gave
no save and no spot.

**Result:** a matching artifact reproduces. It is the player model itself: the engine fades Rynn to vertex alpha 12 when
the camera is pushed into her, and the camera then sits inside her head. It shows on D3D11 with the owner's
`dist/DrakanPC/drakan.ini` and on Vulkan with the reporter's settings. Texture upscaling, HD textures and modern water do
not change it. The draws sample Rynn's own model texture, so none of the reporter's three hypotheses (upscaled tiling,
a backdrop that samples a render target, CLUT alpha) applies. **No fix was made.** The primitives come from the guest
(the GS kick path honours ADC), so the cause is outside `runtime/gs`. Whether the PS2 draws the same picture at that
camera position is not established.

## How to get there without a save

The Shiverbane New Game start (`play_run.py --level shiverbane`) puts Rynn on Arokh inside the starting ice cave. The
cave is its own view layer: with the debug Fly camera, even with `DRAKAN_VIEW_LAYER_FIX=0`, everything above the cave
is fog. Walking forward stops at the end of the cave. The open start is the arrival from Northern Tundra: Rynn beside the
fire basin, the palisade gate ahead, Arokh lands beside her after about 35 s.

```sh
python scripts/harness/play_run.py --name sb_user2 --level 4b --env DRAKAN_LEVELCHANGE=1 \
    --recipe <recipe>   # see below
```

The recipe (`clock milliseconds`) turns the mouse 12 x 100 from 21 s, pitches up 150 and turns 6 x 200. From 40 s it
taps E, Space, S, E, Space (the mount attempt fails), holds Space from 48.5 s and W from 56.5 s, releases both at 69 s,
then turns 8 x 150 with a shot every second (`g0` to `g7`) and pitches up 100 (`hup`). Rynn is stopped by the stone wall
and the dragon statue at the gate. The mouse turns swing the camera into her, and from `g5` to `hup` (about 4 s) the
camera is inside her.

## Runs (all from `dist/DrakanPC`, build of 2026-11-16 14:00, HEAD 76250403)

| Run (`build/harness/...`) | Settings | Ghost |
| --- | --- | --- |
| `sb_user2` | owner's ini: D3D11, scale auto (3840x2160), `texture_upscale = auto`, FOV 90, `modern_water = 1` | yes, `g5` to `hup` |
| `sb_rep2` | `PS2X_GS=vulkan`, `PS2X_GS_SCALE=3`, `GSR_TEXUP=4`, `GSR_TEXUP_FILTER=smooth`, view 1.23, terrain 1.38, LOD 4, 16:9, FOV 80 | yes, `g2` to `hup` (a face filling the sky in `hup`) |
| `sb_cal` | owner's ini, plus `GSR_WINSHOT` | yes, `g5` to `hup` |
| `sb_dl2` | owner's ini, `GSR_DRAWLOG=13500-14300`, `GSR_WINSHOT_AT=13500` | yes; `sb_dl2_w/w_13500.png` is the frame of the logged draws |
| `sb_texup1` | owner's ini, `GSR_TEXUP=1`, `GSR_TEXREPLACE=0` | yes, `g5` to `hup` |
| `sb_water0` | owner's ini, `GSR_MODERN_WATER=0` | yes, `g2` to `hup` (the path diverged a little) |
| `sb_orig` | FOV 80, 4:3, `DRAKAN_VIEW_LAYER_FIX=0`, `GSR_TEXUP=1`, LOD 1, shadow 1 | no, but this run proves nothing: the mouse turns differ at 4:3, Rynn ended on the snow mound and the camera never reached her |

For each run I compared the `g*` and `hup` screenshots with the same shots of `sb_user2`: same recipe, same level and
arrival, same time after start. A run counts as "yes" when a large translucent copy of Rynn (face, hair, arms) covers
the middle of the frame and the sky, while the normal Rynn is not visible.

## What the draws are

`GSR_DRAWLOG` over the ghost frames (`sb_dl2`, frames 13500 to 13534, all 35 frames with the ghost):

* two draws a frame with tex0 `...e62131338a` / `...c6213137c9` (frame 13500) or `...2621313014` / `...0621313423`
  (frame 13533), 768 to 3420 vertices, `test=5002d` (alpha test GREATER 2, depth test GEQUAL, Z32 buffer), `alpha=44`
  ((Cs - Cd) * As + Cd);
* the same vertex alpha **12** on every vertex (the engine's player fade; the normal draws of models nearby carry 88 to
  127), so each layer adds about 9 % of Rynn over the scene;
* vertex Z from 7.1e8 to exactly 0x80000000, against at most 1.07e8 for every other draw of the frame (Rynn standing a
  few metres from the camera is about 3e6). With Z growing as 1/w, these vertices are within a unit or two of the
  camera, and 0x80000000 is the saturated float-to-int of vertices at or behind it;
* bounding boxes over most of the 512 x 512 frame (`216,38..299,512`, `136,293..363,512`, `156,0..460,447`).

The overlapping shells of head, hair and body add up to roughly a third of Rynn's colour. Seen from inside, the hollow face
reads as a face looking at the camera, and limbs and hair become soft rounded patches across the sky. The ghost is drawn
after the gate and the far mountains but before the near rock, so the far scenery shows through it while the near rock
covers it. That ordering explains the "mountain silhouettes occlude it" observation without a depth fault. In the logs a draw
was counted as ghost when every logged vertex had the same alpha below 128 and the largest Z was above 2e8.

The GS side draws only what the guest kicks: `gs_core.cpp` passes `!adc` to every `kick()`, so vertices the microcode
marks with ADC (clipped or culled) never form triangles. The 0x80000000 Z values arrive from the guest as written.

## Hypotheses from the issue

1. Texture upscaling of a tiled sky texture: **no**. The ghost is unchanged with `GSR_TEXUP=1` and `GSR_TEXREPLACE=0`
   (`sb_texup1`), and the draws are a character model, not a sky or weather texture.
2. A big backdrop sampling a render target or HUD atlas: **no**. The draws sample the model texture pages with
   depth test on. They are not `ZTST ALWAYS` backdrops and not render-target reads.
3. CLUT or alpha decode of a weather sprite sheet: **no**. The translucency is the vertex alpha 12 that the engine writes,
   identical on D3D11 and Vulkan.

## Open questions

* Does the PS2 draw the same picture when the camera ends up inside Rynn? If the original microcode clips or culls these
  triangles, or the engine hides her below some camera distance and the port reaches that pose by another route, the
  difference is in the guest path (VU1 clip handling or a camera patch), not in `runtime/gs`. A PCSX2 run at the same
  spot (camera pushed into Rynn against a wall) would settle it. No PCSX2 is installed here.
* Is the camera reaching Rynn more often in the port? Every repro used mouse turning while she stood against the wall at
  the gate. The mouse camera (`look_mode`) and FOV / aspect could make that pose easier to reach than with the PS2
  stick. Not bisected: at 4:3 the same mouse counts turn the camera differently, so the path diverged.
* The flying case (vertical bands from the top of the frame) was not reproduced. The mount attempts failed: Arokh walks
  off, or Rynn walks into the gate cut scene. If the camera enters Rynn or Arokh in flight, it should look the same.

## What is needed from the reporter

* The two screenshots (on foot and in flight).
* Whether a large, faint copy of Rynn (face, hair, arms) is recognisable in them, or whether the cells are a regular
  grid.
* Whether it starts when the camera is squeezed against a wall or cliff, or swings close behind Rynn or Arokh, and
  goes away when the camera moves back out.
* A save near the spot (the PCSX2 import slot that showed it), or at least where in Shiverbane it happened, plus their
  `drakan.ini` and `drakan.log`.
* Whether `renderer = d3d11` shows it too. Both backends show the reproduced artifact here.

Note for builds: `build/final-game` (the WorkDir named in CLAUDE.md) holds no prepared tree. The only built tree is
`build/local-game`, so an `-Incremental` build into `final-game` fails until a full build has been made there. An empty
`build/final-game.build.lock` was left by that failed attempt.
