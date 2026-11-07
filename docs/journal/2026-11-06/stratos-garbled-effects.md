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

### Result of the recording (2026-11-07): the straddling triangles do not come from VU1

Tooling: `DRAKAN_VU_RING` + `DRAKAN_VU_TRIGGER` (VU1 ring, see above), `PS2X_GS_CAPTURE_ROLL=<file>` (rolling GS capture: with
`PS2X_GS_CAPTURE=40` it restarts every 8 frames until `<file>` appears, then records up to frame 40; the GS creates
`<DRAKAN_VU_TRIGGER>` and `<DRAKAN_VU_TRIGGER>.2` at the first bad draw), `vu1ref_ringscan` (replays the ring through
`vu1ref`, needs the `.states` file the ring writes next to the dump) and `analysis/scripts/gscap_straddle.py`.
Run: `play_run.py --exe <private copy> --level Stratos --recipe build/harness/recipes_g3/s_4.txt` with
`PS2X_VU1_THREAD=0 DRAKAN_VU_BATCH=0`; it triggers within about 100 s (frame 6200 to 8500).

* VU1 side: 6000 VU1 runs before the trigger (24,215 PATH1 packets, all ST/RGBAQ/FOG/XYZ2) replayed in `vu1ref`; the replay
  is faithful (99.4 percent of the kick-region quadwords equal the next run's recorded VU memory). Triangle level
  analysis (strip/list/fan, ADC honoured): **0 triangles that mix positive and negative Q**. The program is not
  producing the spikes. (Negative Q alone is not abnormal: the screen-space passes use `w = -1`, many world draws have
  Q < 0, so `q < 0` is not a valid trigger by itself; mixed sign inside one triangle is.)
* GS capture of 40 frames around a trigger: PATH1 566,068 all-positive + 400,389 all-negative triangles, **0 mixed**.
  PATH3 (EE-built GIF packets via DMA channel 2): 796 all-positive and **16 mixed** triangles, all in 4-vertex
  `PRIM=0x7d` strips (ST, RGBAQ, FOG, XYZ2, TME + ABE + FST=0): the first two vertices have `q` about 2e-4 and an
  ordinary Z (0x1df7a78 to 0x2344f74), the other two have `q` -0.002 to -0.014 and Z about 0xf0bca440 / 0xcbc53300 /
  0x8f1eea00, i.e. a negative integer Z interpreted as unsigned, and XY hundreds to two thousand pixels off screen.
  That is the signature of the garble draws, and they are software-projected on the EE/VU0 side.
* So the root cause is the producer of those PATH3 particle strips (EE or VU0 macro code projecting the glare/spark rays
  with `1/w` and no near-plane test; a ray from an emitter in front of the camera to a tip behind it). The VU1
  microprogram, its AOT/batch variants and the GS front end are not involved.
* Still open: (a) whether the original EE code has a near test that our EE/VU0 emulation defeats (the quads whose
  corner pair has `w < 0` would then never be emitted), (b) whether the real GS draws them (Z = negative as unsigned
  against a 24 bit Z buffer, far off screen XY). Neither can be decided without the EE function (locate it by tracing
  the DMA ch2 packets with `PRIM=0x7d`, nloop 4, back to the EE code that built them) or an original-hardware capture.
  No GS-side guard was added: a guard would only be justified if the original shows the same result.

### Producer found (2026-11-07): the engine's polygon pipeline draws unclipped ribbons

Method: `PS2X_GIFDMA_FIND` (GIF DMA payloads starting with a 4 vertex `PRIM=0x7d` ST/RGBAQ/FOG/XYZ2 strip), a page
protection write watch (`PS2X_WATCH_RDRAM`, resolve host RIPs with `bin/drakan.map`), and `DRAKAN_TRACE_FN=1d9de8` with a
fan-straddle report in the trace hook.

* The packets are not written by particle specific code. `FUN_001d9de8` (the GIF "emit vertex list" helper; per vertex it
  does `st = (s*q, t*q, q)`, unpacks the RGBA bytes and `FTOI4`s x,y,z,w with VU0 macro ops) takes already screen
  projected floats (`x y z w` / `s t q rgba`, 32 bytes per vertex) from `FUN_00146880` (fan, PRIM 5) / `FUN_00146810`
  (copies the vertex pointer list), which is called from `FUN_00146378`, the generic polygon draw
  (`draw(ctx, poly, vertexPtrs, count)`).
* Every straddling fan observed has the same shape: `n=4`, the first edge at positive depth (`q` about +6.6e-4, `z` about
  +5.6e6), the second edge behind the camera (`q` about -5e-4 to -8e-3, `z` -4e6 to -6.6e7), screen x/y all within a few
  hundred pixels of the screen centre, UV (0,0)(0,1)(1,1)(1,0): a textured ribbon / beam quad crossing the camera plane.
  Caller chain at the report: `ra=0x1468a0 (146880)`, `wra=0x14686c (146810)`, `cra=0x146458 (146378)`.
* `FUN_00146378` only tests that at least one vertex is beyond the near distance of the polygon (`poly+108`, `c.olt`
  against the vertex depth at `+48`, scalar FPU code) and then draws the whole polygon; there is no clip against the near
  plane in that path (no VU0 `VDIV/VRSQRT/CLIP`, only `FPU_MUL_S/CVT_W_S/C_OLT_S` and the `FTOI4` in `FUN_001d9de8`,
  whose saturation (`0x7fffffff` for positive overflow, `0x80000000` for negative) matches the PS2 FTOI rules).
* So the geometry we send to the GS is what the game code produces from its inputs. A hardware GS interpolates S,T,Q
  linearly in screen space and divides per pixel, so a quad whose Q goes through zero shows the same texture
  singularity (starburst streaks) on real hardware; XY here is well inside the 4096 window (no 16 bit wrap) and the
  D3D11 path already clamps Z to the buffer range. No deviation of the VU1, VU0 macro or FPU emulation was found, and
  nothing in the GS path is wrong, so no GS guard was added (a guard would be a presentation change that the original
  does not make). If the effect should be hidden anyway, the place is a game side override of `FUN_00146378`
  (skip the polygon when its vertices straddle the camera plane), not the GS.

### Fix (2026-11-07): near-plane and window clip of the engine's fans (`poly_near_clip`, default on)

* Hook `drakan_polyclip.cpp` on the fan drawer `FUN_00146880` (a1 = vertices, a2 = count, PAL addresses). The pure math is
  `runtime/world/include/world/PolyClip.h` (unit tested: `world_polyclip_test`, run by ctest in the `runtime-tests`
  preset). Polygons that need nothing take the original path unchanged (the fast check reads at most 8 vertices).
* A polygon is clipped when it straddles the camera (some q > 0 and some q < 0) or when a vertex lies outside the GS
  coordinate window (|x - 2048| or |y - 2048| > 2040; the GIF XYZ2 fields are 16 bit, so the hardware wraps those
  vertices too: a ribbon with its tip a few units from the camera has q > 0 but x = 13312 and is just as garbled).
  Sutherland-Hodgman per fan triangle in homogeneous space against depth >= `DRAKAN_POLY_NEAR_W` (8) and a +-1800 px
  guard band; position, z, s/t, fog and colour are interpolated where they are linear (x,y,z are lifted by the depth
  1/q), surviving vertices are copied bit for bit, each remainder is drawn through the original fan drawer from scratch
  vertices below `$sp`.
* Switch: `[video] poly_near_clip=0` or `DRAKAN_POLY_NEAR_CLIP=0` restores the original look. No settings menu entry yet.
* Verification (Level 8 Stratos, `recipes_g3/s_4.txt`, private exe copy): clip off: spiky fans, green wash, sticks in
  8 of 16 shots and the GS trigger fires every run (frame 8657, `q=-0.32`); clip on: 0 of 16 shots garbled in the
  final run, the trigger never fires, 2305 fans clipped of 189,400 drawn, 0 dropped. Two earlier runs with the
  straddle-only version still showed sticks and a green wash until the window clip was added. Levels 1A and 4B
  (`fx.txt`, attacks and walking): identical images on and off and no fan was clipped there (no behaviour change off the
  affected effects). Cost: one pointer-sized check per fan (about 1,500 fans per second), not measurable next to the
  game; the machine was shared with other runs so FPS numbers were not compared.

## Verification of the near-plane clip, 2026-11-07

Option: F1 Video page row "Clip effects at the camera (fixes streaks and flashes)" (`[video] poly_near_clip`, live
apply). Runs: random-walk recipe (attacks, E, turns, 12 legs) in 1A, 3A, 4A (Arokh), 5 and 8 with
`DRAKAN_POLY_NEAR_CLIP=1` against `=0`, a 6x6 montage of the shots read as PNG per run (`build/harness/pc_<level>_1_c1|c0`).

* Clip on: no vanishing or popping effects in any of the 60 shots. Arokh fire breath (4A, 5), the breath impact
  explosion in Level 5 and the green enemy glare in 8 render whole.
* Clip off: Level 5 shot 11 shows a translucent yellow slab across the whole screen where the clip-on run shows the
  explosion as a normal fireball; Level 8 shots 5 and 9 show white spiked fans around Rynn and a rainbow smear that
  the clip-on run does not have. The two runs diverge in game flow, so these are matched by situation, not frame.
* Rynn spells: a retail-cheat plus inventory spell selection recipe in 1A launched the sword, not a spell, so no
  Rynn spell shot exists yet. Not verified.
* Cost: `DRAKAN_POLY_NEAR_CLIP_LOG=1` counted about 9300 fans in a 60 s Level 8 soak with 1 clipped and 0 dropped
  (clips are rare). The cost on the 99.99 % unclipped path is a copy of at most 8 x 32 bytes plus an
  `outsideWindow`/`straddlesCamera` scan. `PS2X_PERF=1` frame times (`ee_ms/flip`) on and off were swamped by noise:
  three other game instances ran on the machine and the three runs shared it, so the means (5.3 ms off, 8.9 ms on,
  2.6 ms on with the log) are contaminated and say nothing about the clip; no measurable cost.

## Spell casts and hook cost, 2026-11-07 (follow-up)

* Rynn spell setup that works in a new game, no save: the native pad cheat (L1 R2 L2 R1 Up Down Left Right Right Left
  Down Up) unlocks the spells; Tab, Enter (info popup), E, E (Spells tab), Enter (info popup), Enter (Summon Feuerball),
  Esc. The fireball then sits in her hand. A tap of attack only swings the flourish; RMB held plus the gesture W A S D W
  and an attack tap gives the ready sparks and the fireball animating around her. Recipes and montages are in
  `build/pcset` (not committed); `exp2.txt` is the cast sequence.
* Clip on against off, same recipe: Level 1A (Rynn spell prep, ready, cast sparks) and Level 3A (Arokh fire ring, rain
  sparks, 15,000 fans/s) look the same in both; no effect vanishes or pops with the clip on. Dust (1A random walk) and
  the Level 8 enemy glare were covered in the earlier section. A projectile flying off and an enemy casting at Rynn were
  not captured; `Rynn spell` shots show the cast, not the hit.
* Cost, deterministic: `PS2X_PERF=1` now prints `clip_fans/s`, `clip_us/s` and `clip_ee_pct` (time inside the hook with
  the original draw excluded and the steady_clock cost subtracted). Per fan 74 ns (3A), 98 ns (1A cast), 162 ns (1A
  random cast); the busiest scene, the 3A Arokh fire at up to 71,700 fans/s, costs 0.22 % of the EE thread on average and
  0.78 % in its worst second; 1A casts 0.05 to 0.06 % mean. Below the 0.5 % mean threshold, so the fast path (copy of at
  most 256 bytes plus one scan) was not changed.
