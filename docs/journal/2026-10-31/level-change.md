# Level change: how the game does it, and how the host triggers it (2026-10-31)

Task: forcing a level change by calling `0x13b7c0` hangs. Find out what an exit trigger really does, trigger it safely from the
host, verify in game, and make multiplayer clients follow the host. Reference section:
[world-model.md section 10](../../reference/world-model.md). Code: `ps2recomp/ps2xRuntime/src/lib/drakan_levelchange.{h,cpp}`
(new), small hooks in `drakan_overrides.cpp` (install), `drakan_world.cpp` (F1 panel section), `drakan_netplay.cpp` (follow).

## 1. The mechanism (Ghidra decompile, `analysis/ghidra_scripts/InputDecompile.java` / `InputXrefs.java`)

* `0x13b7c0(path)` is `0x136628(); Level_Load(path, 0)` and has no static caller (it sits in a function-pointer slot,
  `DAT_004fd53c`). It is only the middle of a transition.
* The exit is a class `0x10037` object ("Level Transition"; the level data also names them "Portal Transition ...",
  "Transition from 1(A) to 1 (D)"). Its handler `0x365138(handle, msg)`; touch event `0x1001` -> `0x361440`; confirm / save
  screen path `0x361288` -> `0x361e18` -> `0x361f60` -> screens 5 (save) and `0x1a` (loading) via `0x2c4458`.
* **The load itself is done by the main loop.** `0x2cbf88` (the gameplay state function) checks `DAT_004fbba4` (pending trigger
  handle); property 6 of the trigger = 0 -> `0x362d38`, else `0x364c90`. Both run inline on the main thread: read the
  destination (property 0, a string id), write `<slot>/<level>.rlt` (level-change save, `0x169c28`), free the level lists,
  `Level_Load`, find the arrival marker (`0x362a60`), restore the player there, save `<level>.rsg`, re-enter gameplay (`0x2cb7e8(2)`).
* Arrival marker: a class `0x10037` object of the *destination* whose own destination text equals the old level's name (or the
  object named `portal` when property 1 is set). If there is none (Shadowmire has no exit back to Surdana) the player keeps the
  old position and falls out of the world.
* That is why the earlier test aid hung: `postGuestCall(0x13b7c0)` runs the load on a dispatcher thread while the main thread keeps
  ticking the world being torn down (priority 7: null vtable call; priority 1: the loader starves).
* Things that did **not** work: posting `0x361440` (nothing visible: it only queues the handle), posting `0x361e18` (the guest froze:
  it creates screens that need the UI thread), resolving the destination text with a nested guest call (the string manager
  blocks, the guest froze in the intro).

## 2. What the host does now

At a frame boundary (`PadMgr_Update`) it writes what the trigger writes: `DAT_004fb6fc = 0`, `DAT_004fbba4 = handle`. No guest
call at all; the game's own transition runs on the main thread. Before that it moves the player and the camera onto the trigger
(the transition keeps the player's offset from the trigger; fired from a spot 100 000 units away, the arrival lands outside the
level: Inquisitor's Lair froze that way and worked with the move). The level-change save slot name (`DAT_004fdec8`) stays empty,
so the save screen is skipped and `.rlt` / `.rsg` land in the card's root (setting a folder that does not exist gives "Save
Failed!"). The player's own walk into an exit goes through the normal save screen.

* `drakan_levelchange::exits()` lists the exits (handle, position, property block, destination id and, for Surdana, name:
  `kKnownDests`, observed by firing each exit). `requestExit`, `requestExitIndex`, `requestLevel(name)` (substring of the
  destination name), `followExit(pos)`, `setExitListener`.
* F1 > World (with `DRAKAN_WORLD=1`): "Level exits" list with a `Go` button per exit.
* Env `DRAKAN_LEVELCHANGE=<step>[,<step>...][@frames]`: step = exit index (nearest first) or destination name, one per level,
  `frames` (default 600) frames after the level is up. `DRAKAN_LEVELCHANGE_NOMOVE=1` skips moving the player onto the trigger.
  The old `DRAKAN_LIFECYCLE_EXIT` aid in `drakan_lifecycle.cpp` is superseded (left in place, still hangs).

## 3. Verified in game

Private play folder `build/lc-play` / `build/lc-play` per harness (exe copied, junctions to the assets), private memory card, PAL.

| Run | Result |
| --- | --- |
| `build/harness/lc3` (exit 0, Surdana -> Shadowmire) | loads, `.rlt` + `.rsg` written, Rynn falls and dies (no arrival marker), back to the title |
| `build/harness/lcx6` (-> City of the Damned) | Rynn standing on the arrival teleporter, 195 fps, HUD intact |
| `build/harness/lcx10` (-> Kragmor), `lcm8` / `lcx8` (-> Catacombs) | the load completes (`afterLevelLoad`), then Kragmor: Rynn dies / leaves the level, Catacombs: 0 fps (the main thread spins in a long loop, watchdog pc `0x1d....`) |
| `build/harness/lcx1` (-> Lair, not moved first) | freeze; `build/harness/lcm1` (moved onto the trigger first): Rynn on the ground in the cave, 310 fps |
| `build/harness/lcr1` (Surdana -> Lair -> Lair's own exit back) | both loads, `afterSaveRestore` of the `.rlt` in Surdana, 165 fps |
| `build/harness/lcn1` (`DRAKAN_LEVELCHANGE=kragmor`) | name lookup picks the Kragmor exit (`'kragmor' -> exit ... (Level 1B - Kragmor)`), same Kragmor arrival problem |
| `scripts/harness/level_change_run.py` | all four cases pass, section 3b |

Other destinations seen to load: Andrellian Isles (`lcx7`, not inspected further). Open: why Kragmor / Catacombs / Shadowmire
arrivals fail from a fired exit (the real walk-in was not tested: writing the player position onto the trigger did not make the
proximity test fire in 300 frames, so the player cannot be scripted onto it yet). Candidate causes: the skipped steps of
`0x361e18` (dequeue, held-object release at `player+0x10b4`), the arrival marker choice, or a level that needs its quest state.

## 3b. Harness

`python scripts/harness/level_change_run.py` (private play folder `build/lc-play`, output `build/harness/lc-<case>`): city, lair,
roundtrip (Surdana -> Lair -> Surdana) and chain (Surdana -> City -> Surdana -> Lair): all PASS (clean scripted quit, the expected
`afterLevelLoad` sequence, one fired exit per step); screenshots `s1.png`.. in each folder.

## 4. Multiplayer follow

**Update: the hidden chat line described here is replaced by the `LevelChange` message of protocol v6** (see
[coop-net.md](coop-net.md) and netcode.md "LevelChange"); the follow logic (`followExit`) is unchanged.

Host side: `drakan_levelchange::setExitListener` fires at the `.rlt` save (pending handle still set) with the exit's world position;
`drakan_netplay` sends the hidden chat line `@@LC x y z` (a first try with a `0x01` marker was stripped by the server). Clients (player id
other than 1, line from player 1) drop the line from the chat log and call `followExit(x, y, z)`: after 120 frames in a level the
exit with the same position is fired the same way. The client's own transition is not reported back (`g_following`).
Run: `python scripts/harness/netplay_two_instances.py --name lc-net4 --prefix lcnet --exe build/final-game/bin/drakan.exe --port 47914`
(A takes the City exit, `DRAKAN_LEVELCHANGE=city@1200`). Result in `build/harness/lc-net4`: A `Level 1D - City of the Damned loaded`
at 36.7 s, B `host changed level through the exit at 157696 193235 428032: following` at 31.5 s and the same level loaded at 32.6 s;
B's ghost of A is gone while it leaves and **comes back in the City** on both sides (`ghost of player .. spawned` twice each),
ghost motion checks pass. The check "ghost removed" now accepts any removal reason (B leaves before A's hash changes).

## 5. Why Kragmor, the Catacombs and Shadowmire failed, and the fix (second session)

**One cause for all three: the player's position is not where the host wrote it.** The position at `player+0x14` is rebuilt from
the physics copies every frame (docs/reference/input.md section 13 lists a dozen copies). The first version moved the player
onto the trigger at the `PadMgr_Update` boundary and set the pending state in the same tick; by the time the main loop reached
`0x362d38` the engine had thrown the player somewhere else (probes: `build/harness/lcC-custom`, `at the save the player is at
209975 187011 -17856`, different every run). `0x3620f0` stores the player's offset from the trigger, and `0x362310` puts the
respawned player at *arrival marker + offset*, so a garbage offset gave garbage positions (frame 1 to 3 in the arrival log:
-4 000 000, then wherever the physics settled; Rynn "left the level", "died", or the main thread spun with a NaN-sized world:
the Catacombs "0 fps"). The City / Lair arrivals only worked because those offsets happened to be small. Pinning the player for
90 ticks with a refile and a physics-body rewrite did not hold either (the position drifted away every frame).

**Fix:** `drakan_levelchange.cpp` now wraps the transition function `0x362d38` (`transitionEntry`): the pending state is still
written at the frame boundary, but the player (and the camera, keeping its offset from the player) are put onto the trigger as
the very first thing inside the transition, where the function reads them a few calls later. No guest call, no refile. Result
(arrival report at frames 1, 2, 3, 60, 200 in the log, `[levelchange] arrival report`): Rynn stands exactly on the destination's
arrival marker, stable: Kragmor `335872 172078 235520`, Catacombs `97280 150818 381952`, Shadowmire `195584 210676 459776` (all
three equal the destination's own exit position). Screenshots: `build/harness/lcC-custom/s1.png` (Kragmor, 173 fps),
`lcD-custom/s1.png` (Catacombs), `lcA-custom/s1.png` (Shadowmire, standing in the swamp water, 177 fps). The old note that
Shadowmire has no marker was wrong (the `lc3` run predates the move).

**Real walk-in ground truth.** Not established as an on-screen walk: Surdana's exits are 300 000 to 500 000 units from the start and
the player cannot be placed there (see above), so a scripted walk would need minutes of pad input. What the walk-in does
differently is only the front half (touch event -> queue -> `0x361e18` -> save screen -> loading screen); the main-loop half that
fails or works is the same code, and with the player on the trigger our path now produces the arrivals the game's own transition
produces. Remaining differences of the walked path that were not reproduced: the volume save / restore in `0x361288`
(`DAT_004fdebc/dec0/dec4`, three audio channel volumes set to 1.0 and restored in `0x362d38`) and the held-object release at
`player+0x10b4` in `0x361e18`; neither showed any effect in the arrivals.

**Other facts found.**
* `0x362d38` takes two branches after reading the destination: `uStack_f8` = the destination's own `.rlt` exists (visited before):
  restore it (`0x19c` of the loader object) instead of a fresh `Level_Load`.
* Property 2 (`*p == 1`, 1 for Kragmor, Catacombs, Shadowmire, Lair, 0 for the City and the Isles) = "relative placement": the
  player's offset from the old trigger is applied at the arrival marker. With 0 the player keeps the absolute coordinates (the levels
  share one world, so the City exit and its arrival marker are at the same world position).
* The destination text of an exit is a level name ("Level 1B - Kragmor") or, in some levels, the file path
  (`Levels\Level 1A - Surdana\Level 1A - Surdana.lvl`, Kragmor's exit back); both are normalised before the marker comparison.
* Strings are read through the string manager's cache (`*(ctx+0x344)`, `this = sm + s16(vt+0x90)`: 128 hash buckets at `this+0x20`,
  bucket `(id * 0x59) & 0x7f`, chain at entry `+0x14`, `entry+0` = id, `entry+8 & 3` = 3 -> text pointer at `entry+0x1c`).
  A cache miss makes the accessor (`0x1b9fb0` -> `0x12b640`) start a load and **spin** (`do {} while ((flags & 3) == 2)`), which is what
  froze every nested call (NoPreempt: the loader thread never runs). So the host only *reads the cache*, never calls it.
  The marker search of a level load reads the exit texts of that level up to the match, so the exits that matter are mostly cached.
  Names the cache does not have yet are learned: when a change through an exit finishes, `(old level, string id)` -> the level that
  loaded.

## 6. Open: Surdana's exit 80011 hangs

The exit with destination id `0x80011` (one of the four triggers stacked at `413696 188421 618496`, property 1 = 1 "portal") hangs
inside `0x362d38` before the `.rlt` is written, reproducibly (`build/harness/lcS-surdana-3`: `transition entry` is the last
`[levelchange]` line, no load, the run ends at `PS2X_MAX_SECONDS`). Its sibling `0x80008` (same position) loads Ravenshold fine.
Not analysed further: the destination text could not be read (not cached), so the cause (a destination whose level file is not on
the disc, or a conditional exit) is unknown.

## 7. Sweep: every exit of Surdana (`level_change_run.py --sweep surdana`)

Each row is one fresh run: fire exit `i` (nearest-first order from the start position), then the nearest exit of the destination
(= the arrival marker, i.e. the way back). Output folders `build/harness/lcS-surdana-<i>`; the sweep was split in two by an unrelated
build outage (rows 0 to 2 from the first part, 3 to 10 from the second).

| exit | leads to (loaded) | result |
| --- | --- | --- |
| 0 | Shadowmire | PASS, back in Surdana on the Shadowmire exit |
| 1 | Inquisitor's Lair | PASS, back in Surdana |
| 2 | Level 4A Ravenshold | arrival on the `portal` marker (`1017856 135308 778240`) verified; the way back was cut off by the outage, not verified |
| 3 | (hang) | FAIL: hang inside the transition, see section 6 |
| 4 | Level 5 Shiverbane | PASS, back in Surdana |
| 5 | Level 6 Valley of the Fallen | PASS, back in Surdana |
| 6 | City of the Damned | PASS, back in Surdana |
| 7 | Andrellian Isles | PASS (the Isles' nearest exit leads on to Level 3B Crystal Mine, loaded fine) |
| 8 | Catacombs | PASS, back in Surdana |
| 9 | Level 6 Valley of the Fallen (second exit) | PASS, back in Surdana |
| 10 | Kragmor | PASS, back in Surdana |

Not done: the exits of the other levels beyond the one return exit (`--sweep city --indices 0-4` etc. is wired but was not run), the
F1 > World panel in a screenshot (renderer screenshots do not contain the overlay and an OS-level grab of the window did not work in
this multi-monitor setup; the list itself is checked through the arrival report in the log), a scripted real walk-in.
