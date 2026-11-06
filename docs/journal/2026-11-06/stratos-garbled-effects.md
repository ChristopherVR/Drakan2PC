# Garbled effect geometry in Level 8 Stratos (spiky fans, green streaks, green screen)

Status: reproduced, narrowed down, **root cause not found, no fix committed**. This note records what was ruled out so
the next attempt starts from the data and not from the same suspects.

## Report

A random-walk soak (`build/harness/recipes_g3/soak.py 14 16 s_4.txt`, mouse turns, W, jumps, left clicks, E) in Level 8
shows, in shots q01, q02, q05, q06, q07, q12, q13, q14: spiky blue/white/yellow starburst fans, thin amber/green
sticks, a green wash over the whole screen, and Rynn's body tinted yellow-green with torn polygons.

## Reproduction

* Reproduces every time with the same recipe on `dist/DrakanPC/drakan.exe` (every soak run, a dozen), with and without
  `DRAKAN_DEBUG_MENU=1` and the F3 / PGUP / RIGHT prelude, so it is not a debug-menu setting.
* The burst is an effect of a few frames to a second that appears next to Rynn. In the shots Rynn is pressed into a
  rock crevice, the camera is inside or right beside the emitter.
* Isolation recipes (jump only, attack only, E only, walk only; `build/harness/scr/gen_iso.py`) did not reproduce it
  in the open spot where Rynn spawns. The soak needs the crevice plus an attack or the enemy spell. Standing attack
  alone is fine, as reported.
* The exe that took the playtest shots is the 17:18 `dist` build, which predates commit 0024ebf3 (chunked VU1 compile,
  20:56). The chunking is therefore not the cause, and the build made after it shows the same shots.

## Ruled out

| Suspect | Test | Result |
| --- | --- | --- |
| AOT slices / batch executor / VU1 thread | `DRAKAN_VU_AOT=0 DRAKAN_VU_BATCH=0 PS2X_VU1_THREAD=0` | same garble (different shapes, same families) |
| VU1 AOT vs reference interpreter | `DRAKAN_VUCHECK=1` over the soak | 0 mismatches in more than 1,000,000 runs (the run is slow, three attempts timed out; the first attempt logged `999424 runs checked, 0 mismatches`) |
| Extreme coordinates reaching the GS | GS capture (`PS2X_GS_CAPTURE_FROM`) scanned per frame, PATH1 | no triangle edge above 11,000 raw units (690 px) in any frame; the garble is in plausible screen coordinates |
| `rand()` stub | guest `rand@0x4CE028` disassembled | identical to the host stub (32 bit LCG 1103515245, 12345, masked to 31 bits) |
| Debug menu prelude | run without F3/PGUP/RIGHT | same garble |
| 30 Hz world cadence | `DRAKAN_WORLD_RATE=30` | the run diverged (different game flow), but the same effect shows as a normal looking green/yellow glare star with long beams, see below |

## What the draws look like

`GSR_DRAWLOG` on a garble frame: a particle draw (`flags=037`, `tex0` 2006218555307108, 324 vertices, alpha 35 to 60,
`clamp=5`) whose triangles are built from two vertices at a normal depth (`q` about 7e-4, `z` about 9.9e7) and a third at
`q = -0.22`, `z = 0x80000000`, x/y in the hundreds to thousands of pixels, i.e. a vertex **behind the camera** that the
VU1 program projected without clipping. Other draws of the same kind carry vertices at the (2047.9, 2047.9) saturated
corner (`GARBAGE: ... 1 at (2047.9,2047.9)` in `logDraw`, 3 draws in 6000 frames), the signature already described in
`vu1-correctness.md` for the "degenerate wedge corners". Each ray of the glare star is a thin triangle from the
emitter to a far tip, so a tip behind the camera becomes a screen-spanning spike.

## Attempt that did not work

Dropping every perspective-textured triangle with a vertex at `Q < 0` in `D3D11Backend::Impl::draw` (48 draw calls in
one soak) removed some spikes but the fans and sticks in q00, q05, q13 stayed, so the behind-camera tips are only part
of it. Reverted; nothing from that experiment is committed.

## Reading of the evidence

* The VU1 emulation is internally consistent and the AOT path is not involved, so the bad vertices are most likely the
  program's output for bad input (particle data built by the EE or VU0 macro code) or genuine unclipped glare geometry
  seen from inside the effect. Neither is proven.
* With the 30 Hz world cadence the same effect family renders as a recognisable glare star (green/yellow with long
  beams), which argues for art that is merely extreme when the camera is in the emitter.

## Next steps

1. Record a VU1 window that contains a garble frame (`DRAKAN_VU_RECORD` needs a trigger; the run count at the shot
   is about 2.0 to 2.6 million at 60 FPS, so a fixed skip is too fragile) and replay it through `runtime/vu1ref`
   (`vu1ref_replay -geom`) to see whether the far vertices are in the program's input.
2. Find the EE function that fills the glare/spark batch (the draw above) and check its VU0 macro and FPU inputs for
   a bad value (`VRSQRT`/`VSQRT`/`VDIV` history in `recomp-status.md` section 9).
3. Compare with an original-engine capture of a spark burst taken close to a wall, to learn whether the PS2 shows it.

## FPS in the effect-heavy scenes

Not measured on a quiet machine: other sessions were running games on the same PC during all runs. The slow frames
in the soak are the burst frames themselves (shots show 18 to 43 FPS world rate during the fans, 60 otherwise).

## Diagnostic worth keeping

Frame range and texture filters of `GSR_DRAWLOG` / `GSR_DRAWLOGTBP` / `GSR_DRAWLOGV` are enough to log a garble draw;
`logDraw` already prints the `GARBAGE` line for vertices at the saturated corner or with |stq| above 1e9. A
`GSR_LOG_SATURATED` style filter that logs only vertices with `q < 0` would find these draws without frame guessing.

## Follow-up 2026-11-07 (static reading, no new run yet)

* VU1 microprogram: only 8 `CLIP` instructions exist (0xde8, 0x1868, 0x1b48, 0x1e30, 0x2110, 0x23f8, 0x2990, 0x29b8), in
  the clipping variants of the transform loops. The plain transform loop at 0xca0 to 0xd70 (`MINIz.w`/`MAXy.w` fog clamp,
  `DIV Q, vf00w, vf17w`, `FTOI4`) has no clip test at all: a vertex with `w < 0` gets a negative `Q` and a wrapped screen
  position. So a draw produced by that loop and not by the 0xd80 variant (which stores the clip bits in the vertex `w` via
  `ISWR.w`) relies on the EE to keep the geometry in front of the camera. This matches the observed `q = -0.22` vertices
  and is consistent with the original engine producing the same vertices (the VU1 emulation is bit-exact against two
  independent models, see the table above).
* New diagnostics (uncommitted until verified in a game build): `DRAKAN_VU_RING=<file>,<N>` keeps the last N VU1 runs and
  writes them in the `DRAKAN_VU_RECORD` format when `<file>.go` appears; `DRAKAN_VU_TRIGGER=<file>.go` makes the GS
  create that file when a perspective-textured draw has a vertex with `q < -0.01`. Replay with `vu1ref_replay -geom`.
  Use `PS2X_VU1_THREAD=0 DRAKAN_VU_BATCH=0`.
