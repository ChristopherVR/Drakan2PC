# Level exits: the walk-in works (trigger detectors), cut scene lock not solved (2026-11-01)

Follows [level-exits.md](level-exits.md). PAL build, private harness folders under `build/harness/` (not committed).

## 1. What fires an exit: a Trigger Detector linked to it

Nothing sends message `0x20` to the exit from the player's position. Every exit (class `0x10037`) in the level data is the
**link target of a Trigger Detector (`0x1003d`)**, a box volume placed *beside* the exit object (decoded with
`analysis/scripts/lvl_objects.py`; incoming links of each `0x10037`). The detector fires on the player entering its box and
the exit gets the message (`0x365138` message `0x20`, then `0x361440`). The previous walks went through the exit object's own
position, which is outside every detector box. Surdana (cell = 2048 units, scale triple in cells):

| exit | exit object | detector centre | detector scale |
| --- | --- | --- | --- |
| Kragmor (12250) | 729088 152750 772096 | 735232 154397 772096 | 6 x 3 x 1 (12288 wide in x, only 2048 deep in z) |
| Lair (12678) | 441344 149800 508928 | 447446 150430 500779 | 1 x 1 x 5, rot 90 |
| Shadowmire (7970) | 521216 150563 447488 | 514048 150563 444416 | 5 x 1.5 x 1 |
| Catacombs (12414) | 508928 151050 791552 | 510979 151579 791501 | none (default box) |
| Valley/portal exits | 413696 188421 618496 | 413654 188410 627243 | 4.5 x 3.75 x 0.5, rot 90 |

Several detectors sit 6000 to 9000 units from their exit, so the volume has to be found from the link, not the exit
position. The editor shows the same data (`logic.ts` links).

**Proof (real keyboard walk, no teleport onto a trigger):** `level_change_walk.py --exit 735232,772096 --dist 5000 --y 153300
--prefix lcx` places Rynn 5000 units short of the Kragmor detector (due -z) and holds W. Log chain: `exit handler handle
c034e0 msg 20` -> `touch handler ... event` -> gui screen 2 ("In order to progress beyond this point the game must be
saved", OK) -> screen 5 (Save Game, "Level complete") -> `exit taken in 'Level 1A - Surdana' at 729088 152750 772096` ->
`afterLevelLoad 'Level 1B - Kragmor'`, arrival `342016 172626 230520` at frame 200. The walk screenshot sequence is in
`build/harness/lcx-walk-735232_772096/`. (An earlier try on this machine fell back to the title screen under heavy load, a
flake.)

Harness fixes (`level_change_walk.py`): the touch first raises the message box (screen 2), so the recipe now waits for screen 2,
taps Cross, then waits for screen 5; the save-screen taps are spaced 2 s apart because the memory card read takes seconds.
Note the arrival-report position stays at the playtest placement while the world is paused behind the message box.

## 2. Co-op follow on a walked exit: proven

`netplay_two_instances.py --name lcfollow2 --prefix lcf --port 28300 --no-level-change --timeout 500 --recipe-a <walk recipe>
--recipe-b <idle> --env-a DRAKAN_PLAYTEST_POS="735232 153300 767096" --env-a DRAKAN_LEVELCHANGE=99@100000000`: A walks into
the Kragmor detector with W, confirms the message box and save screen, and Kragmor loads (A: `exit taken ... 729088 152750
772096`, `afterLevelLoad 'Level 1B - Kragmor'`); B, idle in Surdana, logs `afterLevelLoad 'Level 1B - Kragmor'` and `level
follow: arrived in the leader's level (hash d6d42c8a)` about 0.3 s later. Not checked on this run: the ghost checks (the
harness is run with `--no-level-change`, which skips them).

## 3. Cut scene lock after a host-fired exit: NOT solved

Fired Valley -> Surdana during the Valley's entry conversation (`level_change_run.py --steps valley,surdana --frames 300`), Rynn
stays at the trigger height (`413696 188421 618496`) with no HUD. What was ruled out with new diagnostics (the arrival report
now also prints a "lock state" line and a dump of `ctx+0x490..0x4df` every 100 frames):

* `DAT_004fbafc` ("scripted") is 0 throughout. It is the inventory / pause flag, not the cut scene flag.
* `player+0x90 / +0x94` are NOT flags: they are the inventory list (pointer, capacity) of the player; the record is cloned from
  the old player by `0x362310`, so they differ per level. An attempt to restore "normal" values was wrong and was removed.
* `ctx+0x4d4 / +0x4d8 / +0x4dc` (pointer, 0x10, count `0xe`) is a list that is empty in normal Surdana and filled in the
  Valley (from frame 2) and in the frozen Surdana. Zeroing the count at the transition entry did not unfreeze Rynn (the list
  came back filled in the new level), so it is a symptom, not the lock.
* The frozen Surdana is loaded `fromSave=1` and the respawned player is cloned from the Valley's player record (`0x362310`
  copies it), so the lock most likely lives in that cloned player's state or in the cut scene object referenced from it. Next
  step: decompile what the Valley's cut scene player (class `0x10033`) sets on the player at start and what ends it (a
  key press in the conversation), then clear it in `transitionEntry`.

Workaround stands: fire exits after the entry cutscene (the conversations end on a button press, so a harness cannot wait it
out) or use exits of levels without an entry cutscene. Walked exits cannot happen in a cut scene.
