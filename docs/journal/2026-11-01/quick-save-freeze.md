# Quick save freeze report (F5), 2026-11-01

User report: quick saving freezes the game. Status: **not reproduced, root cause not found**; diagnostics added so the
next freeze names its own pc.

## Evidence from the player's own run (dist/DrakanPC/drakan.log, 19:29 build)
Drakan 2, PAL, uncapped, DLSS, audio on, `save_pages = 4`, mod `editor.playtest` loaded. Surdana, title Load of a
Catacombs save, then a real level change Surdana -> Catacombs (level-change save screen, `SelectSlot(1)`,
`ConfirmOverwrite`), inventory use, bow, quick slots, then at t=417 s (frame 81357) F5:

    quick save requested -> modern pause bound -> screen -1 -> 23 -> 5 (save) -> slot screen flags d -> flags 9, entries 0
    [run] game thread did not stop within 3 s, exiting anyway

The log ends there: no `flags 1, entries 4` (the scan normally finishes one frame after `flags 9`), no 12 s
`timed out; native screen restored` (that runs on the same guest thread), no `[frame-uncapped]` or `[resident-models]`
lines afterwards. So the EE/guest thread stopped inside the native slot scan frame (or in a host call it made), and
the exit path had to abandon it. The quick-save state machine itself cannot loop: every phase is bounded by the 12 s
timeout and runs from the same input boundary.

## What was tried (all green, no freeze)
Private copy of the player's card (`iso/mc0`, 4 pages, Slot13 present, no Slot33), `dist` build, harness
`play_run.py --save`:
* `recipes/quick_save_load.txt` (missing F8 ignored, F5 save, F5 overwrite, F8 load).
* 49 quick saves + 7 quick loads in one run (`recipes/quick_save_stress.txt`), audio off and **on**, `PS2X_WATCHDOG=5`.
* quick save after a host-driven level change (`DRAKAN_LEVELCHANGE=city`) and after a menu save; F5 in Catacombs loaded
  from the title, as in the report.
* `DRAKAN_MC_TRACE=1`: one quick save is ~40 card calls on one host thread (GetInfo/GetDir/Sync, `Sync` mode 1 always
  finds `pending=1`), so a lost `g_mcCommandPending` between two guest threads is not visible; opens/closes balance
  (the one unmatched Open is the failed probe of the not yet existing Slot33 file).
Not covered: the real level-change save *screen* before F5 (the host-driven change skips it), real mouse/keyboard focus,
long sessions (~7 min of play before F5), the `editor.playtest` level.

## Suspects left (unconfirmed)
* Card HLE (`MemoryCard.cpp`) is synchronous and mutex-light; no wait, no unbounded loop. Unlikely.
* `hookAnySave` / `writeSlotNames` rewrite the shared slot-name cells while a scan can be in flight (same thread,
  between frames); would give wrong names, not a hang.
* The uncapped flip worker (`drakan_frame_rate.h`, entry 0x1d78f0): `sleepHook` cancels the vsync wake and relies on a
  cycle-based retry that is only posted when `object+4 == 0`; a lost wake there stops every frame, anywhere, not only
  at F5. Worth checking first if the next `[stall]` dump shows thread 6 sleeping (`pc=0x4bc088 ra=0x1d7a24`) and the
  main thread waiting on it.
* A GPU/driver stall (4K, DLSS) at the first draw of the "Quick saving..." card: nothing in the log distinguishes it.

## Changes
* **Always-on stall detector** (`ps2_runtime.cpp`, `drakan_present_clock.h`, `drakan_input_glue.cpp`): the input boundary
  bumps `g_guestHeartbeat` once per guest frame; the host watcher logs `[stall] guest input boundary silent for 8 s`
  plus the full watchdog dump (live pc/ra, dispatch history, semaphores, every thread's pc/ra/wait reason), at most 6
  times, without `PS2X_WATCHDOG`. Verified with `DRAKAN_TEST_FREEZE_AT=3000` (one dump, game continues); no `[stall]`
  line in the normal quick-save runs. A long level load that never reaches the input boundary for 8 s also prints it
  (diagnostic only).
* `DRAKAN_MC_TRACE=1` card call trace, `recipes/quick_save_stress.txt`.

Next step when the freeze recurs: send `drakan.log` (the `[stall]` block names the guest thread states), ideally with
`DRAKAN_MC_TRACE=1`.
