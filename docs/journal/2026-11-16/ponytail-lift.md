# Rynn's ponytail held out level while walking: the cloth step's frame rate and its air drag (2026-11-16)

Owner report: Rynn's ponytail sometimes goes horizontal when she just walks, in scenes too. The Intro hand-over race that
froze the cloth was fixed on 15 November ([character models](../2026-11-15/ootf-character-models.md), 2(a)); what was
left, the tail held out at 60 to 90 degrees whenever she moves, was recorded there as the engine's own behaviour, the
same in Drakan 2's Surdana. The owner accepted changing it in both games. Fixed in the runtime
(`ps2recomp/ps2xRuntime/src/lib/drakan_ponytail.cpp`, policy `drakan_ponytail_policy.h`, test
`tests/ponytail_policy_tests.cpp`); `DRAKAN_PONYTAIL=0` restores the engine's step. Scratch, probes, runs and frames:
`build/review/ponytail/` (not tracked).

## The engine's cloth step

`0x11d368 (cloth state, object)`, called from the skinning pass `0x116990` once per drawn frame. Read in a decompile
(`build/review/ponytail/dec/`, not tracked) and in RAM (`tools/hairprobe.py`, every 30 ms, read only):

* 18 particles (position, velocity) kept in the body's frame, 8 anchor vertices on the head, 72 springs. Per step:
  `0x11fd58` re-expresses the particles in the body's moved frame (they stay where they were in the world) and
  measures the anchors' velocities; forces; semi-implicit Euler; `0x11ead8` min / max length constraints (positions
  only); `0x11f1a0` collisions with spheres round the anchors (sets the particle's velocity to the sphere's).
* Force per particle: springs `- k28 (v - u) - k2c u` + level wind + random gusts, then gravity. `u` is the body's
  velocity, `(position - last position) x 1/dt`. Rynn's resource, the same in Surdana and Ruined Village: k28 = k2c =
  5 per second, springs 80 / 40 / 60, mass factor 1. Gravity (`0x4dea14`) 4263 units/s^2, level wind up to 2000.
* So the tail feels an air drag of 5 per second against the body's speed: its terminal speed is G / 5 = 850 units/s.
  Walking is 1200 units/s, running 2900, the scene walk of 1.1 about 450.
* dt is the world tick `0x4de604`, capped at 0.03 with 1/dt = 33.33 above it.

## Why it lifts

Angle of the tail (three tip particles against the six top ones) from the body's vertical, median per ground-speed bin,
Surdana (`tools/walk_d2_noshot.txt`: run 10 s, walk 8 s, turn, run 8 s; no screenshots during measured runs):

| Engine step unchanged | stand | slow 100-700 | walk 700-1500 | run > 1500 |
| --- | --- | --- | --- | --- |
| world_rate 30 (`d2_30_off`) | 15 | 60 | 78 (94 % > 60) | 85 |
| world_rate 60 (`d2_60_off`) | 15 | 64 | 80 (95 %) | 85 |
| uncapped, 90-220 fps (`d2_unc_off`) | 16 | 81 | 83 (100 %) | 87 |

Three causes, found by switching parts of the step from the hook (experiments, all uncapped unless named):

1. **Air drag against the body's speed.** At 30 Hz, removing the drag while moving (`d2_30_s1`) brings walking from
   78 to 25 and running from 85 to 40 degrees. 5 x 1200 = 6000 against 4263 of gravity: the drag alone holds it above
   45 degrees from about 850 units/s. That is the data, the same on the PS2.
2. **The step's frame rate.** With the drag removed, uncapped still gives walk 69 / run 81 (`d2_unc_s0`) against
   25 / 40 at 30 Hz. The passes that do not scale with dt (the re-basing, the length constraints, the collisions) run
   three to seven times as often per second as on the PS2 (25 to 30 steps per second, dt capped at 0.03) and hold the
   tail stiffly out. Forcing dt to 0.03 on every frame removes it (walk 18, `d2_unc_x1`; not a fix, the cloth then
   runs in fast motion); skipping only the constraints or only the collisions on in-between frames does not (walk
   64 / 70; both: 49).
3. **Velocity spikes.** Above the cap the speed is the frame's displacement x 33.33: 13 % high at 30 Hz, 3.3 times
   after a 0.1 s hitch (a screenshot at 4K takes about 100 ms, so shots kick the tail). Animation changes snap the
   body up to 100 units in one step: one-step "speeds" of 4,000 to 13,000 in the log (`DRAKAN_PONYTAIL_LOG=2`).

## Fix

A wrapper on `0x11d368` (and on its three passes, which only it calls), PAL only:

* **Cadence.** Above 33 frames per second the step runs only when the gathered frame time reaches a 30 Hz frame
  (`cadenceStep`), with that time as its dt (and 1/dt); on the frames in between it runs with dt 0 and the three passes
  return at once, so the particles stay put in the body's frame and are written to the model as they are, and the body
  frame of the last real step is put back so the next step re-bases over the whole interval. 30 steps per second at 60
  and 120 fps, 30 to 36 at 144 and more; 33 fps and slower are unchanged. Counted in the log: 3 passes skipped per
  held frame.
* **Drag easing.** For each real step the drag coefficient (resource + 0x2c) is scaled so the drag sees
  `v / (1 + v / 300)`, v the true speed over the real step time (`dragScale`). At rest the scale is 1 (the standing
  hang and the level wind are unchanged); the drag force from motion saturates towards 5 x 300 = 1500 (about 20
  degrees on its own); a one-step snap or a hitch no longer kicks. The coefficient also scales the wind's push while
  moving, and (k28 - k2c) x g x dt then adds up to 15 % gravity while moving; both only lower the tail.
* Restored right after the call, also when the step is preempted inside (it can wait on the heap semaphore). The world
  dt (`0x4de604` / `0x4de608`) is rewritten for the duration of the call only.
* Applies to every cloth the engine steps; in Surdana and Ruined Village that is Rynn's ponytail alone (one state).

`DRAKAN_PONYTAIL=0` off, `DRAKAN_PONYTAIL_SOFT_SPEED` (default 300; 0 = cadence and dt correction only),
`DRAKAN_PONYTAIL_LOG` ([env flags](../../reference/env-flags.md)).

## Result

Same exe, `DRAKAN_PONYTAIL=0` / default:

| Run | stand | slow 100-700 | walk 700-1500 | run > 1500 |
| --- | --- | --- | --- | --- |
| Surdana 30, off / on (`d2_30_off` / `d2_30_c`) | 15 / 14 | 60 / 23 | 78 / 41 (>60: 94 % / 18 %) | 85 / 50 |
| Surdana 60, off / on (`d2_60_off` / `d2_60_c2`) | 15 / 15 | 64 / 37 | 80 / 35 (95 % / 12 %) | 85 / 70 |
| Surdana uncapped, off / on (`d2_unc_off` / `d2_uncapped_c2`) | 16 / 13 | 81 / 35 | 83 / 38 (100 % / 8 %) | 87 / 67 |
| Ruined Village scene 1.1 walk, uncapped (`rv_scene_p0` / `p1`) | 15 / 10 | 74 / 33 (>60: 72 % / 10 %) | (few samples) | |

Run-to-run spread is about 8 degrees (two 30 Hz runs with the same settings: walk 33 and 41). Running still lifts
the tail further than walking (60 to 70 degrees median), and stops, turns and jumps still swing it. Ruined Village
gameplay gave too few walking samples (Rynn placed in the 1.1 field mostly ran into slopes, `rv_unc_q0` / `q1`); its
cloth parameters read from RAM are identical to Surdana's, so the Surdana table stands for it.

Frames (uncapped, the owner's setting; screenshots kick the off runs harder than the on runs, see cause 3):
* `build/review/ponytail/evidence/d2_walk_zoom_off_top_on_bottom.jpg` (Surdana, runs `d2_shot_p0` / `d2_shot_p1`,
  camera behind her): walking, off, the tail is a short stub pointing back at the camera (held out level); on, it hangs
  down her back. Running, on, it hangs and swings a little out. `d2_walk_off_top_on_bottom.jpg` is the wider view.
* `build/review/ponytail/evidence/rv_scene_zoom_off_top_on_bottom.jpg` (Ruined Village scene 1.1, runs
  `rv_sceneshot_p0` / `p1`): in the shot from her side (4th column) the tail is out level behind her shoulder off and
  lower on. The scene's other shots show her from the front or above, where the difference is hard to see.

## Not verified

* Real console footage: whether the PS2 held the tail out like the off rows at its 25 to 30 Hz is still not compared.
  The 30 Hz off row is the closest the port has; the fix changes it too (by the drag easing).
* Other cloths (none seen in the two levels) and co-op characters' ponytails: covered by the same code, not looked at.
* A visible 30 Hz step of the tail at high frame rates (the tail moves with the body between steps): not seen in the
  frames, not checked frame by frame in motion.
* Drakan 2 levels other than Surdana; NTSC builds (the hook is PAL only).
