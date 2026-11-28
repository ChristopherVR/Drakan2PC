# Grimstone Mines quicksave at 30 fps: other game instances on the machine

Report: after the current quicksave (Slot33, `OotF 04 - Grimstone Mines`, saved 17:21:10) the game runs at about
30 fps. Source: the player's live `dist/DrakanPC/drakan.log` (process started 17:09:40; log `t` is seconds since
then, the last line t=694.85 matches the file time 17:21:15).

## Timeline

`[frame-uncapped]` windows averaged per 20 s, next to the harness game runs another session started from
`build/harness/` (folder creation to last stdout write):

| Clock | t (s) | Player fps | Other `drakan.exe` instances |
| --- | --- | --- | --- |
| 17:14:40 to 17:15:40 | 300 to 360 | 157 to 178 | none |
| 17:16:00 to 17:17:20 | 380 to 460 | 83 to 102 (Grimstone, same area) | none |
| 17:17:40 to 17:18:20 | 480 to 520 | 46 to 55 | `gasb-a1` (17:17:20), `arokh-scene-base1` (17:17:59) |
| 17:19:00 to 17:20:40 | 560 to 660 | 35 to 63 | `arokh-scene-base1` until 17:19:30, `gasb-a2` 17:19:15 to 17:20:42, `arokh-scene-base2` from 17:20:02 |
| 17:21:00 | 680 | 51 to 62 | `arokh-scene-base2` until 17:21:38, `gasb-w1` from 17:21:26 |

The fall starts in the same 20 s window as the first harness run and is deepest (28 to 35 fps) while two harness
instances ran at once. The quicksaves themselves (t=411, 462, 526, 608, 648, 688) cost one or two frames each
(the 4096x4096 preview is reduced to 1024x1024) and the rate did not step down at any of them. The guest did not
log anything new in the slow windows (no `[stall]`, no texture budget misses).

## Conclusion

Not a quicksave or Grimstone fault: the game shared the CPU and GPU with up to two harness instances of
another session. The same area ran at 83 to 102 fps with no other instance. No code change.

Before attributing a live frame drop to the game, check `tasklist | grep drakan` (or the creation times under
`build/harness/`) for the slow window. A clean measurement of this save needs a quiet machine (no harness runs);
use `PS2X_PERF=1`, `GSR_PROFILE=2` and `DRAKAN_FRAME_AUDIT=2` as in `../2026-11-27/combat-frame-drops.md`.
