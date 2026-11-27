# Dragon combat frame drops in Wartok Canyons: not reproduced

## The report

The player's 18:22 session (issue report log `drakan-issue-20261127-082218-913Z-64148.log`) continued the
Order of the Flame quicksave in slot 33 (Wartok Canyons). About 65 s after the load, in the narrow canyon by the
boulder-blocked cave where that save starts (Arokh at `(110182, 23769, 103571)`; the log's breath line is at
`(109922, 23769, 104557)`), the `[frame-uncapped]` windows fell and recovered:

| log t (s) | completed flips / window | fps |
| --- | --- | --- |
| ~87 | 128 / 2.01 s | 64 |
| ~89 | 67 / 2.10 s | 32 |
| ~91 | 23 / 2.12 s | 10.8 |
| ~93 | 46 / 2.02 s | 22.8 |
| ~96 | 51 / 2.43 s | 21 |
| ~98 | 60 / 2.01 s | 29.9 |
| ~100 | 103 / 2.00 s | 51 |

Arokh's health went from 100 to 77 in under two seconds while it fell, then regenerated. That fits his own
fireballs and breath hitting the canyon wall at close range. The fall started about two seconds before the first
health loss. Apart from health, the window logged only `[extended-heap] retail heap break 0x1900000`,
two `[pvp] breath flame resource` lines and three suppressed near-camera fireball impacts at about 96 s. A later
fight at t = 177 s in the same place ran at 60 fps.

## What was tried

All runs used `scripts/harness/play_run.py` on the installed `dist/DrakanPC` (exe SHA-256 `9d7f71a3...`, the
`build/final-game` build) with the player's `drakan.ini`: D3D11, 3840x2160 borderless, auto scale (6), uncapped
world rate, HD textures, DLSS off and native water. The runs also used `DRAKAN_PLAYTEST_GOD=1`, `PS2X_PERF=1` and
`DRAKAN_FRAME_AUDIT=2`. The fps values below are world frames per one-second window, counted from the `[ft] F`
lines. Run folders are under `build/combat-perf/` and are not committed.

| Scenario | Runs | Before the fight | During the fight |
| --- | --- | --- | --- |
| Player's own quicksave (`--save-ootf` with slot 33), canyon spot. A burst of ten fireballs plus 2.8 s of breath at the boulder, repeated six times, flying into the blast. Arokh's health fell to 0. | `cp-boulder1` (audio off), `cp-boulder-audio1` (audio on) | 52 to 53 | 53 to 55 in every window |
| Same with the Modern inventory used first (browse, hot slot, close, weapon key 5), as in the player's log | `cp-inv1` (background window), `cp-inv-fg` (foreground) | 52 to 53 | 51 to 57 |
| Direct start in OotF 03 at the Wartok group near `(98680, 14000, 50500)`. The first shot comes after a 60 s warm-up, then fireballs and breath at Wartoks, killing one. The machine was otherwise idle. | `cp-late-q1`, `cp-late-q2` | 64 to 72 | 55 to 70 |
| Same, while Dota 2 was running and using about 47 % of the GPU before any game started | `cp-late0`, `cp-late-prof`, `cp-late-gpu` | 38 to 51 | `cp-late0` fell to 23 to 31 for about 16 s; the other two did not fall (47 to 64, 38 to 66) |
| Direct start, idle vs. firing at the same spot, earlier in the level | `cp-idle0`, `cp-fire0`, `cp-burnidle0`, `cp-burn0`, `cp-alt0` (alternating 8 s idle and 8 s firing in one run) | | Firing costs about 5 to 10 % (`cp-alt0`: 65 to 72 idle, 58 to 66 firing). No collapse. |

On a quiet machine none of these runs reproduced the drop. Fireballs, breath, Wartok kills, a burning
Wartok, self-damage and the inventory each cost at most about 10 %.

## Trace of the only slow window (`cp-late0`, Dota 2 running)

In each 1 s `[perf]` window and 2 s `GSR_PROFILE` window, before vs. during the fall:

* world fps 38 to 42, then 23 to 31.
* EE thread 47 to 58 % of a core, then 29 to 50 %. EE ms per world frame stayed 11 to 16. The guest did not
  do more work per frame.
* VU1 worker 44 to 62 %, then 32 to 47 %. The GS worker went from 63 to 77 % to 47 to 68 %. VU1 pairs per
  frame did not change (about 8 M).
* The unnamed host main (present) thread rose from 45 to 62 % to 85 to 100 % of a core. `presentCall`
  exclusive time rose from 350 to 640 ms to 700 to 850 ms per 2 s window. That thread waited in `Present`.
* Texture decode and upscale ran 0 to 2 times per window, so no effect textures were being prepared.

With `GSR_GPU_PASS_TIMING=1` in the comparable run (`cp-late-gpu`), the world pass took 1 to 5 ms of GPU time
per frame and the display passes under 1 ms. The rest of each 12 to 25 ms frame was idle. So our own GPU work was
not the limit. In the one slow run, `Present` blocked while another process used the GPU. The same recipe with
the same background load did not drop again, so this is not a reproduction of the report.

## Conclusion

The drop was not reproduced, so no change was made. A fix would have to come from a trace of a run that
shows it. The combat work itself (fireball and breath effects, burning, self-damage, Wartok kills) costs 5 to 10 %
in the same scene from the player's save. Neither the guest, VU1 nor the GS worker cost grew with it.

Unexplained:

* The player's 64 to 11 fps fall at t = 88 to 99 s. Dota 2 started at 19:21, after the player's session, so it
  was not the cause. A local game build started at 18:22:30 (lock file), after the drop (about 18:20:40 to
  18:20:50 when worked back from the report time). Other sessions' CPU or GPU work at that time is not recorded.
* "Below 1 fps" was not seen in any run.
* The 56 s stall: one `[frame-uncapped]` window of 56.05 s with one flip, right after the Modern pause menu
  opened (frame 8639) and after `[gsr-presentation] host handoff timed out at latch 8841`. Later pause-menu
  windows flipped at 62 fps. It was not seen in any run and needs a timed trace (`DRAKAN_FRAME_AUDIT=3`) of a
  pause-menu open after combat.
* A warm-up seen in every direct start: 25 to 45 fps for the first 30 to 50 s after the level load, rising
  to 65 to 85 fps. The player's log shows the same, 50 to 70 fps for about 30 s after the load. It was not
  investigated here.

Next time: if the player sees the drop again, a run of the same save with `PS2X_PERF=1`, `GSR_PROFILE=2` and
`DRAKAN_FRAME_AUDIT=2` would split it into EE, VU1, GS worker and present time like the trace above. A
`tasklist` taken at the time would show other processes.
