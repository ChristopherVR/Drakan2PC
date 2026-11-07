# Playtest gaps: exits, death and reload, soak (2026-11-07)

Follows [clean-playtest.md](clean-playtest.md). Own exe copy (`build/gaps/play`, the 7e017f08 build plus the
`--keep-cutscene` flag), harness files in `build/gaps/` (not committed). At most two games of ours at a time; other
sessions ran 3 to 5 more `drakan.exe` copies the whole time, so FPS numbers are pessimistic.

## 0. Why 15 exit approaches "never reached a detector"

Not geometry. The generated recipes (and the earlier tour recipes) began with `500 key tap F3`, `PGUP`, `RIGHT`,
`2800 key tap ESC`, meant for the debug menu. The debug menu is off by default, so F3/PGUP/RIGHT do nothing and **ESC
opens the pause menu (screen 23 = 0x17)**. The whole placement, hold and walk then ran behind the pause menu and
ended when the later Cross taps resumed the game. The three exits that "worked" (1E, 2, 4B 5592) only did because
their walk phase happened to outlast the menu. With the four keys removed (`build/gaps/gen_exit3.py`) the
approaches of 1C, 3B, 4A (6496), 4B (5595) and 5 (4393) all walk into their detector and complete the chain.
(Also: the hold after `place` counts input boundaries, not milliseconds, so on a busy machine a 7 s wall-clock wait
before pressing W can still be inside the hold; `DRAKAN_PLAYTEST_HOLD=60..90` fixes that.)

## 1. Exit verification

Chain checked per exit: gui screen 2 (message box), 5 (save screen), `[levelchange] exit taken`, and a
`level '<name>' loaded` line afterwards. "Teleport" = `place` straight into the detector volume (same detector code,
no approach), "walk" = `place` 2000 to 4500 units short and hold W. Runs started with `--level`, so destinations
that name Surdana reload the redirected level; every destination that is not Surdana is the real level (Surdana
runs from 1A are fully real: nothing is redirected there).

| Level | Detector (exit) | Result | Destination loaded |
| --- | --- | --- | --- |
| 1A | 12249 | teleport pass (walk proven 2026-11-01) | 1B Kragmor |
| 1A | 12413 | teleport pass; walk from the east ends against a wall (screenshot), no other approach found | 1C Catacombs |
| 1A | 12419 | teleport pass | 1D City of the Damned |
| 1A | 7973 | teleport pass | 6 Valley of the Fallen |
| 1A | 12479 (Lair) | NOT reached: the box sits under the surface (y 150430, terrain 160498); `place` lifts her to the surface, so no teleport or walk worked. The return from 1E (12204) is proven and arrives at the teleporter pad 444672 150024 503424 | not tested |
| 1A | 7969 (Shadowmire) | NOT reached for the same reason (box y 150563, surface 173016). 2 -> 1A (8310) is proven | not tested |
| 1A | 18028 (to 3) | walked through the plane from both sides at 100 x 50 cells: no trigger. `Detect Which? = 1`, probably dragon only (not testable on foot) | not tested |
| 1A | 7997, 12873, 12874 (portals to 4, 5, 6) | gated: `Initial State = Disabled` until a timer chain (buttons at 419834 183577 619454) enables them | not tested |
| 1B | 12276 | teleport pass | (1A name, redirected) |
| 1C | 12280 | **walk pass** | (1A name, redirected) |
| 1D | 12277 | gated: `Initial State = Disabled`; the Crystal Skull (190466 165052 507929) and the Death Mage kill feed the timer 12335 that enables it. Walking into the disabled box does nothing (confirmed); picking up the skull in the run did not enable it within the harness | not enabled |
| 1E | 12204 | walk pass (clean-playtest) | (1A name, redirected) |
| 2 | 8310 | walk pass (clean-playtest) and teleport pass | (1A name, redirected) |
| 3A | 11517 | teleport pass (after the 90 to 130 s opening scene); walk: placed spot lands on a mountain slope | 3B Crystal Mine |
| 3B | 11519 | **walk pass** and teleport pass | 3A Andrellian Isles |
| 4A | 6496 | **walk pass** and teleport pass | 4B Northern Tundra |
| 4A | 6659 | gated (`Initial State = Disabled`, portal to 1A) | not tested |
| 4B | 5595 | **walk pass** and teleport pass | 5 Shiverbane |
| 4B | 5592 | walk pass (clean-playtest) and teleport pass | 4A Ravenshold |
| 5 | 4393 | **walk pass** and teleport pass | 4B Northern Tundra |
| 5 | 4387 | gated (`Initial State = Disabled`, portal to 1A) | not tested |
| 6 | 18190 | gated (`Initial State = Disabled`, "LT : Aachen" portal) | not tested |
| 3A 8021, 4A 5132, 6 9779 / 9781, 8 8143 / 10481 | no detector links them | message driven (timers, a lever, boss or story events). The Valley lever 18379 at 1022825 224190 343014 ends in "Transition to Desert"; Rynn reached the lever but E and Cross did nothing in the short test (its timer chain 20860 expects another message first) | not tested |

Detector `Initial State` (enum Enabled / Disabled) is the explanation for every "walked in and nothing happened":
it is in the level data (`build/lvl-objects/*.json`, parameter `Initial State` = 1) and `lvl_objects.py` shows it.

## 2. Death and reload

Method: no `DRAKAN_PLAYTEST_GOD`; F5 quick save or none; Arokh dives (`place` 25000 above ground, W held, mouse down
300 x 200 frames) and crashes at about 45000 units/s; then ENTER taps (the death prompt reads "Esc: Hauptmenue
oeffnen. Eingabetaste: letzten gespeicherten Spielstand laden.").

| Case | Result |
| --- | --- |
| 4A, quick save present | crash at about f 8500 (HUD health empties, Arokh and Rynn lie still, prompt over the picture). ENTER: gui screen -1 -> 14 -> -1 in 8 guest frames (0.1 s), Rynn flying again, full health. No loading screen. |
| 4A, no save | ENTER (or Start from the autopress): gui screen -1 -> 8 (title) with Load Game greyed out; New Game from there loads normally (screen 29, 3.4 s). No hang. |
| 4A, no input at all | the prompt waits about 15 to 20 s, then the game goes to the title by itself. No hang. |
| 5, quick save present | killed on foot by the Savage Footmen at 334000 105000 680500 (no god mode, left idle with attack taps). ENTER: gui screen -1 -> 14 -> -1 in 8 guest frames (0.06 s), Rynn back at the F5 spot (290311 115741 882601). |
| 5, no save | the prompt timed out by itself to the title (gui screen -1 -> 8), Load Game greyed out; later New Game loaded normally. No hang. |

A level-start autosave does not exist: the game only writes saves at the level-change save screen (screen 5) and on F5.
After a level-change save the same `Level ... .rsg` is what ENTER loads, so the quick-save case is the autosave case.

Harness note: after the reload the `[in-pos]` line reads 0 0 0 (the position probe keeps a stale player pointer); the
picture and the frame rate are fine, it is not a game problem.

Level 5 falls: no fall kills her in the harness. Placements above about 132000 hover (y falls 0.05 units per 100 frames,
jt=2 with a frozen animation) and from 19000 above the ground she lands alive, so the death in Level 5 above came from
enemies instead.

## 3. Shadowmire spot (320168, 205764, 503600)

Not a soft-lock. The earlier report read the end of the recipe: W was held 2 s, S 2 s, then the keys were released.
`build/harness/cp_shadow` shows z moving 505257 -> 500224 under W (5000 units, stops against a wall) and back to 502128
under S, so she responds the whole time. She drops 23600 units because the spot (y 205764) is 4100 below the
Shadowmire floor (209900) and lands in a lower cavern whose floor is about 182100. That cavern is part of the level
(Generic Detector 11194 at 326016 180346 508331, skeleton props near y 190350). Whether a player can fall into it
legitimately was not established; no fix needed.

## 4. Level 4B intended route

On foot at the start, Arokh 2.8k units north-west. W walks to him, E mounts (screenshot: Rynn on Arokh inside the
walled cave yard, opening to the left). Flying north with W held reaches z 1007602 and stops at a cyan magic barrier
(spd 0, still controllable for 40 s, no soft-lock). The route out of the yard was not found by blind flight; the
exit detectors are reachable by the exits table above (5592 to 4A, 5595 to 5). Not walked end to end.

## 5. Level 6 Transition to Desert (cut scene level)

New runner flag `--keep-cutscene` (`DRAKAN_AUTOPRESS_STOP_AFTER_LOAD`): no autopress taps once the first level after
the intro has loaded. Run: `play_run.py --level "Level 6 - Transition to Desert" --keep-cutscene --recipe` (a recipe
with only `clock milliseconds`, shots and `quit`; `sync play` would wait for control that never comes). The cut scene
plays 120 s (subtitled dialogue with Arokh), then the end movie `level0ro_6` (60 HD frames), then gui screen -1 -> 1
and the title with the box "Speichervorgang fehlgeschlagen!" (Save Failed!). That is the documented behaviour
(`world-model.md`): the Desert level is not on the disc, the load fails and the title comes up. It never hangs.

## 6. Soak (21 minutes of random input, god mode, 3A and 4A in parallel)

Random W/S/A/D holds, jumps, attacks, mouse turns, E taps, a `place` back to the start every 3 minutes
(`build/gaps/gen_soak.py`), 2 min opening wait in 3A is not honoured by the generator (default delay), so the first
3 minutes of 3A idle in the opening. Both runs ended with a clean scripted quit, exit 0, about 1470 s wall each.

| Metric | 3A | 4A (Arokh) |
| --- | --- | --- |
| host working set | 1765 MB after load, plateau 2000 to 2430 MB from minute 8, 2030 at the end | 1750 after load, 2100 to 2370, 2250 at the end |
| host private bytes | 3100 to 4870 MB, no trend (3560 at the end) | 3100 to 5420 MB, no trend (5110 at the end) |
| guest RAM, non-zero 4K pages (5 dumps) | 25.30 -> 25.94 MB, flat after minute 15 | 26.85 -> 27.01 MB |
| vblank/s median per fifth | 59.8, 59.9, 59.9, 59.9, 59.9 | 59.8, 60.0, 60.0, 59.9, 59.9 |
| flips2/s median per fifth | 55, 55, 51, 69, 54 | 55, 56, 42, 43, 48 |
| seconds below 30 vblank/s | 25, all in the first 24 s (boot) plus the level load at second 43 | same |
| stalls | `[perf-stall]` only in the first 8 s (boot) | same |

No leak and no stall; 4A's world cadence is about 20 percent lower in the second half (42 to 48 vs 55 flips2/s) while
vblank stays 60, which is within what other sessions' games cause. The perf log covers about 965 s of the 1470 s run;
the rest was not logged by the perf line (not investigated).

## Findings and changes

* Harness: the ESC in the old recipe preamble; `--keep-cutscene`; `place` is a no-op while the player is not
  materialised (3A opening, Valley opening); placements into solid ground are lifted to the top surface.
* Game bugs found: none new. No crash, hang or soft-lock in any run of this session.
* Not done: 1A Lair and Shadowmire walk-ins, 1A to 3, gated portals, message-driven exits, 4B full flight.

## Follow-up (same day)

### Checks run

`clang-format --dry-run --Werror` (VS 2022 LLVM `clang-format.exe`) on `drakan_input_glue.cpp`, ruff check and format on the
touched Python, `repository_guard.py` (2487 approved, 0 prohibited) and `ctest --preset runtime-tests` (122 of 122 pass,
6 data-dependent tests skip: hle_test, fmv_vdb, gameui_ram_test, world_ram_test, world_pose_ram_test, live) are all clean.

### True campaign chain (normal New Game, no `--level`)

`gen_chain.py`: New Game, intro skipped by the autopress, then per hop `place` into the detector, Cross on screens 2 and
5, wait for the arrival, F5 then F8 in each level. (A recipe line needs a time prefix: `0 sync screen 5 1`; a bare
`sync ...` line is silently ignored as an unknown command, which made my first two chain runs fire every hop at once.)

| Hop | Result |
| --- | --- |
| New Game -> intro -> `Level 1A - Surdana` loaded | F5 (save screen 23 -> 5 -> gameplay) and F8 (23 -> 4 -> 14 -> gameplay, level reloaded) work |
| 1A detector 12249 -> `Level 1B - Kragmor` (real level) | screens 2, 5, `exit taken in 'Level 1A - Surdana' at 729088 152750 772096`, `Level 1B - Kragmor loaded`; F5 and F8 work in 1B |
| 1B detector 12276 -> 1A | **does not fire** for Rynn on foot: teleported into the box, and walked through it from the arrival pad (342023 172626 235531) to a wall at x 331551 |

The 1B exit detector has `Detect Which? = 1` (Rynn on a mount, see `ootf-campaign-walkthrough.md`: the class default does not
fire for a mounted Rynn and 1 fires only for one). The level data has Arokh ("Dragon" 0x1001c id 2 at 354326 177002
275981 and an "Arokh Level Trans Teleporter") but he is not in the world at the start (screenshot after walking to his
spot and pressing E: nothing to mount). So the way back from Kragmor is by Arokh, who appears through story logic not
found here. This is gating, not a crash or hang. The earlier redirected 1B run fired because that run is a different
start state; that contradiction is not explained. The same enum explains 1A 18028: `Detect Which? = 1`, mounted only.
The chain therefore stops in Kragmor; 1C, 1D, 1E, Shadowmire, 3A/3B, 4A/4B, 5 and the Valley could not be chained
forward from 1A on foot (Lair and Shadowmire detectors underground, portals disabled, 18028 needs Arokh).

### Exits that need other triggers (evidence only, not driven)

* 1A Lair 12479 and Shadowmire 7969: boxes below the surface, `place` lifts to 160498 / 173016; no tunnel mouth found.
* Valley lever 18379: its timer 20860 is triggered by the death of the boss "Zola Dane (Combatant)" (9274, 428075 230505
  84352), so the lever only works after that fight. Not tried.
* 3A 8021: timers 13610 and 13551, fed by three mounted-only detectors (7318, 13786, 13794) at y 215000-229000 in the
  air: it is the end-of-level flight, Rynn on Arokh.
* Level 8 8143 and 10481: no incoming link (10481 only via its timer 10482), driven by the boss/story script.
* 4B: the cave yard mouth is a Trigger Detector with Task 7 (Cave Entrance, "allows dragon ground movement", 839903
  147014 995843) followed by a Task 3 (No-Fly Zone, mounts only, 839688 146957 997215): Arokh has to walk through the
  cave, not fly. My flight stopped at z 1007602 on the ground in front of a snow slope (screenshot f34.png), not at a
  barrier object; the walking route beyond was not found.

### 4A soak rerun, alone (no other game of ours; others' games still ran)

21 minutes of random input in 4A on Arokh, god mode, clean scripted quit (exit 0, 1314 s wall). Perf seconds 1144, in
sixths (median per second): flips2/s 259 (first sixth, mostly the quiet start spot), 146, 142, 132, 133, 133; vblank
60.0 in all six; ee ms per flip 3.3, 5.0, 4.8, 5.0, 5.2, 5.7; GIF packets/s 560k to 640k with no trend; VU1 runs/s
127k to 158k with no trend. Host working set 1.9 GB after load, 2.1 to 2.2 GB from minute 8, flat; private bytes 3.6 to
4.5 GB with no trend; guest RAM non-zero 4K pages 25.9 -> 26.6 MB in one step around minute 10, then flat.
Result: the earlier "20 percent drop in the second half" is not drift. Alone, the rate settles after the first minutes
and stays flat (133 flips2/s over the last three sixths); the two-game run was contention. No growth to chase.

### Second follow-up: Kragmor arrival, loud recipes, Valley

**Loud recipe parser (done, tested).** A recipe line without a leading time (`sync screen 5 1`) or a lone number is now
reported as `line N: ...` and the run stops with exit 2 at load (`drakan_input_glue.cpp`, and the USA path in
`drakan_overrides.cpp`), unless `DRAKAN_INPUT_SCRIPT_LENIENT=1`. Blank and comment-only lines stay silent.
`runtime/input/tests/input_test.cpp` `testScriptLoudParse` (input_test passes). Commit 6faeb551 contains only these hunks;
the other sessions' edits of `Script.cpp`, `Script.h`, `drakan_input_glue.cpp`, `input_test.cpp` were left unstaged.

**Kragmor (what the data and runs show, not a verdict).**
* 1A detector 12249 (to 1B) has no `Detect Which?` (default), so Rynn on foot enters Kragmor and arrives on the Rynn pad
  (14071, 343210 172681 236203; observed 342023 172626 235531). 1B also has an Arokh pad (14072) used when she is mounted.
* The Dragon object of 1B (id 2, 354326 177002 275981) has `Initial Mode 0` (mounted and flying at level start;
  `ootf-mounting-volcanic.md`: 0 mounted flying, 1 on foot with Arokh landing near her, 2 mounted landed; a landed Arokh is
  mounted by walking into him, no key). That is why `--level 1b` (a level start) puts her on Arokh and the 12276 detector
  (`Detect Which? = 1`, mount only) fires, and why my earlier redirected run passed.
* After an exit the arrival is on foot and no Arokh is in the world: 1A -> 1B (screenshots), and the same for 4B -> 4A
  (`gp_tp4b_5592/after20.png`: Rynn on foot in Ravenshold, whose New Game start is mounted). Arokh in 1A is parked on the
  cast stage (y 200000, a platform with NPC models, not a mountable place; Initial Mode 1), so Rynn cannot be mounted when
  she leaves 1A. A Fireball Power Up linked both ways to the Dragon sits in a water cave in 1B (328704 191656 235072); placing
  Rynn there did nothing visible. So with the on-foot arrival a player in Kragmor has no Arokh and the exit needs one.
  Whether the original summons Arokh by a story event I did not find, or whether this is a port gap in the level-change
  arrival (mounted-start levels not applying `Initial Mode` after a transition), is open: it needs the guest transition code
  read, not more recipes. Treat "possible soft-lock in Kragmor" as unresolved and the highest priority from this session.
* So the on-foot -> mount -> exit flow in the true chain was not completed.

**Not done in this round:** the forward chain beyond Kragmor (blocked by the above for 1B; the other hops need detectors that
are disabled, underground or mount-only), 4B route beyond the snow slope, 1A Lair / Shadowmire entry, and the Valley lever.
Valley: a 2 min attack-tap run at Zola Dane's position (428075 231400 87500) found Rynn mounted on Arokh in the sky (the Valley
starts mounted) with the world clock at 0 in the later screenshot; no kill could be shown, and the lever test after it gave
no exit (screen 2 never appeared).
