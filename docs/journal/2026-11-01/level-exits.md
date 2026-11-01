# Level exits: walk-in attempt, Crystal Mine / Tundra / Stratos sweep, Isles and Valley arrival heights (2026-11-01)

Earlier work: [2026-10-31/level-change.md](../2026-10-31/level-change.md), reference
[world-model.md](../../reference/world-model.md) section 10. Everything here ran on the PAL build with private
harness folders (`build/harness/...`, not committed; the screenshots live there).

Host changes (`drakan_levelchange.cpp`):
* the arrival report (`DRAKAN_LEVELCHANGE=<steps>`) now logs the player position every 100 frames for as long as the
  level runs (it stopped at frame 200). That alone settled the "arrival height" question (section 3).
* `DRAKAN_LEVELCHANGE_TRACE=1` logs every call of the touch handler `0x361440` and of the exit class handler `0x365138`
  (the second argument of `0x365138` is a plain message code, not a pointer: 1 init, 2, 0x20 event, 0x28 / 0x40 seen).
* `fire()` refuses an exit whose destination is not on the disc (`isMissingLevel`: "Level 6 - Transition to Desert",
  Stratos exit 0), see section 2.

Harness changes (`scripts/harness/level_change_run.py`): `--via a,b` names the steps that reach the `--sweep` level (a
name or an exit index per step, e.g. `--sweep crystal --via andrellian,crystal`, `--sweep tundra --via ravenshold,2`);
the exit count and the table work for any depth; the private exe copy is retried while the previous game is still shutting
down; a refusal for a missing level counts as SKIP like a portal marker.

## 1. Genuine walk-in: NOT achieved

What was tried, with real key input (W / S through `play_run.py`) and no teleport onto the trigger:

* Surdana's Kragmor exit approached from 3000 units (`level_change_walk.py --exit kragmor --dist 3000`): Rynn is placed,
  runs (screenshot `build/harness/lcw-walk-kragmor/walking.png`) along a rock corridor and stops at rock 2800 units short
  of the trigger (that side of the exit is not walkable; no map of the approach was built).
* The arrival trigger in Kragmor (fired in from Surdana, then S 2.5 s and W 3 s, `build/harness/kragwalk2`): Rynn leaves
  the trigger by about 3000 units, walks back through it and 4300 units beyond (x 335872 -> 338953 -> 335325 -> 331551
  at frames 1900 to 2300, trigger at `335872 172078 235520`). **No call of `0x361440` or `0x365138` happened after the
  load (trace), no save screen, no exit.** The trigger's instance state stays 9 (bit 0 set: the arrival marker's state);
  ordinary untouched exits are 8.
* Correction of the 2026-10-31 notes: the arrival report (and the old `[playtest] +N boundaries` lines) was capped at
  frame 200 / the first minute, so the walks of that day were judged on stale values. With the report uncapped, the
  position follows the walking. The conclusion stands: walking over a trigger does not produce the touch event.

So the front half of the walk-in (touch `0x1001` -> `0x361440` -> queue -> save screen -> loading screen) is still not
reproduced. The trigger does not get the event from the player passing through its centre point, so the touch comes from
a volume test whose shape or condition is unknown (not the object position; candidates: a collision body the engine tests
only against physics objects with a certain flag, or a volume offset from the trigger). The next step is the decompile of
the touch sender (who calls `0x365138` with message 0x20 and the event block `*(ctx+0x4e0)`), not more walking.

Co-op follow was tested with a fired exit only. A walked exit takes the same path on the partner side (`setExitListener` is
called where the `.rlt` save is written), so it needs no separate proof once a walk-in exists.

Co-op follow on the current build (`netplay_two_instances.py --port 28100`, `build/harness/lcfollow`): A fires the
Surdana -> City exit, B logs `level follow: arrived in the leader's level (hash 6bd7d98b)`, both ghosts come back in the
new level, ghost movement checks pass. One harness check failed: "ghost removed when A left the level" (no
`ghost of player N removed` line in B's log, the ghost is cleared another way). Not investigated; `drakan_netplay.cpp` has
uncommitted work from another session.

## 2. Sweep: Crystal Mine, Northern Tundra, Stratos

Reached from Surdana through the named steps, one fresh run per row. PASS = the destination loads, Rynn stands in the
level, clean scripted quit. Positions are the frame 3 / frame 200 reports.

| from (via) | exit | leads to | arrival | result |
| --- | --- | --- | --- | --- |
| Crystal Mine (Isles, Crystal) | 0 (only exit) | Andrellian Isles | `619517 202514 1026371` (the Crystal exit's own spot in the Isles) | PASS |
| Isles -> Crystal Mine | arrival | Crystal Mine | `619584 199414 1019517`, unchanged at frame 200 | PASS, Rynn on the cave floor (`build/harness/lcsC2-crystal-via-andrellian_crystal-1/s3.png`) |
| Northern Tundra (Ravenshold, 2) | 0 | Ravenshold | `821211 145188 970763` (marker at the Tundra door) | PASS |
| Northern Tundra | 1 | Shiverbane | `289792 114898 869376` | PASS, Rynn beside the fire basin in the snow (`build/harness/lcsT-tundra-via-ravenshold_2-1/s3.png`) |
| Northern Tundra | 2, 3 | n/a | n/a | the level has two exits |
| Ravenshold -> Tundra | arrival | Northern Tundra | `821211 145188 970763` | PASS, cave ground (`lcsT-tundra-via-ravenshold_2-0/s3.png`) |
| Stratos (Valley, Stratos) | 0 | "Level 6 - Transition to Desert" | n/a | **the level is not on the disc**: the game's own load fails (`afterLevelLoad ... ok=0 result=-4`) and the game drops to the title screen with a "Save Failed!" box (`build/harness/lcsstratos-stratos-via-valley_stratos-0/s2.png`). The host now refuses it (SKIP, log line `not on the disc`) |
| Stratos | 1 ("Out") | `Levels\Level 0 - Intro\Out.lvl` | `173995 192514 151979` | PASS: the outro level loads (loading screen, then the ending); clean quit |
| Valley -> Stratos | arrival | Stratos | `651612 147514 961906` -> `652330 147514 961436` | PASS, Rynn on the fortress platform |

Every destination is the right level, the player lands on the marker or the level start, nothing hung. Running three
games at once cost two cases (a `no clean quit` for Crystal exit 0 and a locked exe copy); the rows above come from runs
whose logs hold the complete chain, or from sequential re-runs.

## 3. Isles and Valley arrival heights: no placement bug, two scripted cutscenes

Position logged every 100 frames (`build/harness/isles-a`, `val-b`, `val-a`, `rav-a`):

* **Isles from Ravenshold (or Surdana, the same spot):** arrival `859626 185498 524421`, constant for frames 1 to 100
  (Rynn stands on the ground, no drop). Between frame 100 and 200 the level's entry cutscene ("Level Entry (Grull
  Base)", a conversation with a man on a stair) takes over and moves the player object through `744287 250195 1414042`
  (the y 250 000 noticed on 2026-10-31) and `811827 225419 1330540` to a resting place `825804 204507 1217679` that holds
  from frame 1300 on. The high values are the cutscene's own positions, not the arrival. `build/harness/isles-a/t00..t11.png`.
* **Valley from Surdana:** the player stays at exactly `434176 230667 309248` (the marker, trigger height) for 1500
  frames with the HUD hidden: the Valley opens with a long conversation cutscene (the prince and the skeleton king;
  `DRAKAN_START_LEVEL=valley` shows the same freeze, `build/harness/valstart`). Rynn stands on a grass ledge
  (`build/harness/val-b/t00..t12.png`), so the trigger height is the correct arrival height.
* **A real defect of firing during a cutscene:** the Valley -> Surdana run (`val-a`) fires the exit while the cutscene
  holds the player. Surdana then loads with Rynn frozen at the trigger height `413696 188421 618496` for 1600 frames and
  no HUD: the cutscene / input lock survives the level change. The same arrival from Ravenshold behaves normally (Rynn
  falls 5700 units from the marker and stands at `413696 182722 618496`, HUD visible, `rav-a`). A player cannot leave
  during the cutscene, so this only concerns host-fired exits (harness, co-op follow). Not fixed: the lock flag is not
  identified. Fire exits after the entry cutscene instead (harness `--frames` large enough, or watch the HUD / position).

Nothing needed fixing in the placement for the Isles or the Valley.
