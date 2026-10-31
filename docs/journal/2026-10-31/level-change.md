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

Host side: `drakan_levelchange::setExitListener` fires at the `.rlt` save (pending handle still set) with the exit's world position;
`drakan_netplay` sends the hidden chat line `@@LC x y z` (a first try with a `0x01` marker was stripped by the server). Clients (player id
other than 1, line from player 1) drop the line from the chat log and call `followExit(x, y, z)`: after 120 frames in a level the
exit with the same position is fired the same way. The client's own transition is not reported back (`g_following`).
Run: `python scripts/harness/netplay_two_instances.py --name lc-net4 --prefix lcnet --exe build/final-game/bin/drakan.exe --port 47914`
(A takes the City exit, `DRAKAN_LEVELCHANGE=city@1200`). Result in `build/harness/lc-net4`: A `Level 1D - City of the Damned loaded`
at 36.7 s, B `host changed level through the exit at 157696 193235 428032: following` at 31.5 s and the same level loaded at 32.6 s;
B's ghost of A is gone while it leaves and **comes back in the City** on both sides (`ghost of player .. spawned` twice each),
ghost motion checks pass. The check "ghost removed" now accepts any removal reason (B leaves before A's hash changes).
