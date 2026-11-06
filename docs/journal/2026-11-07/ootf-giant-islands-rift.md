# Issue 5: the Alwarren war giant fight, Islands loads after long Alwarren sessions, the second save and Air Currents

Follows [Alwarren hammer](ootf-alwarren-hammer-and-scenes.md) and [quick-load LRU](ootf-quickload-lru.md). Runs, recipes
and helpers are private under `build/harness/ootf-i5/` (`run.py` = the `ootf-alw` driver pointed at the shared play folder,
`gen.py` / `fight.py` recipe generators, `fightsum.py` Rynn against the giant per RAM dump: positions, distance, facing
error and his hit points, `findcls.py` objects of a runtime class, `lru.py` the terrain LRU walk, `long.sh` the repeated
long sessions). Executable: `build/harness/issue5-dist/drakan.exe` (main `c8d511ea`, sha256 `bf2c040e...`), German tree
(`DRAKAN_LANG=german`; the log shows `field lists of an ODB with 578 own classes kept, the pack's 102 new lists appended`).
Pack: `pack` = formats `7392049` (HEAD; a git archive of it, resolved through a loader hook so uncommitted edits stay out),
full 23 levels; byte for byte the same files as the `5dfe29a` pack `ootf-lru/packfix` (all 10,698 CRCs equal). Direct
starts, god mode (`DRAKAN_PLAYTEST_GOD`, keeps Rynn's hit points at their maximum each boundary).

## 1. The starving war giant (2016)

Not achieved. What the runs established:

* **Aiming** (the open item from the last note): the `place` yaw convention is `yaw = atan2(-dx, -dz)` for the vector from
  Rynn to the target (0 faces -z, -pi/2 faces +x, `drakan_playtest.h`); `fightsum.py` reads her facing back from the
  matrix and prints the error. Placed with that yaw she faces the giant to within a few degrees in the dumps (`fi-2`:
  +2 to +13 degrees over 30 dumps). In `alw-13` the old recipe placed her at yaw 0 while the giant stood 65 to 100 degrees
  to her side, and his body pushed her out to about 1,050 units.
* **The giant moves**: he walks toward Rynn (or her saved spot after a load) and stops 1,000 to 1,900 units from her, so a
  yaw computed from an earlier dump is stale within seconds unless she is pinned. After a quick load his position is
  restored exactly (`sessA-1` -> `fs-1`) but the registry slots change (slot 969 -> 931), so the helpers find him by class
  (0x1003b) and place.
* **The sword**: the old equip (Tab, Enter, Enter) only puts the Mithril Long Sword on a hot slot in this executable (Enter is
  Cross = Hot Slot in the inventory; Use is E = Square); the hand stayed empty (data+0xa48 = 0, `sessA-1`..`sessA-3`).
  Tab, E, Tab (or `1`, the first quick slot) equips it (data+0xa48 = the sword, `fb-1`). Damage 15, the giant has 150.
* **The locks** need the alw-6b recipe (each lock from both sides, default 240-boundary hold): with a 20-boundary hold three
  of the six (227, 229, 225) never took their crystal in `alw-13`, `sessA-1`, `sessA-2`; with the alw-6b recipe counter 679
  counts 6 -> 0 (`sessA-3`).
* **Hits do land**: 150 -> 120 (`fb-1`), 150 -> 90 (`fd-1`: free fight, Rynn turned 45 degrees by the mouse between bursts
  of three attacks), 150 -> 120 (`fi-1`). Hits came at 1,500 to 1,900 units while he reached for her.
* **What stops the kill**: the giant grabs Rynn, carries or throws her (she lands in the same corner at about
  284218 454175 every time, `fc-1`, `fh-1`, `sessB-1`) and eats her. God mode does not prevent that death (`fd-1`, `fd-2`,
  `fg-1`, `fi-1`: "load last save" prompt, the run's attack presses then reload the save, giant back at 150). Pinning her
  with a long hold (`DRAKAN_PLAYTEST_HOLD=30000`) keeps her alive in his hand but then no attack lands (`fe-1`: 1 hit,
  `fe-2`, `ff-a`, `ff-c`: none); `DRAKAN_PLAYTEST_SOFT` does the same. Pinned in the throw corner, sword in hand
  (data+0xa48 set) and facing him to within 11 degrees, she attacks for over a minute while he stands 1,816 units off
  without reaching for her: no hit (`fi-3`). A quick save is refused while he is engaged
  (`fb-1`, `sessB-1`: F5 never ran), so the fight cannot be split into saved steps.
* `F` (Toggle Targeting) did not turn Rynn toward him (`fc-1`, `fh-1`).

So the converted chain is not the blocker: the giant is alive, aggressive, takes sword damage and kills Rynn the PC way.
Killing him needs reactive input (dodge the grab, turn to him, strike), which a time-based input script cannot give; the
harness would need a closed-loop driver (read his position and state, then steer) or a scripted-input command that faces
a target object each boundary (a runtime change, not made). Not a converter finding.

## 2. Islands after a long Alwarren session

Recipe `long.txt`: crystals, both-side locks of `alw-13`, the sword, 12 fight rounds, the exit 154, first save, the Islands,
W. About 230 to 260 s in Alwarren before the exit.

| Run | Alwarren -> Islands | In the Islands | Terrain LRU |
| --- | --- | --- | --- |
| `long-1`, `-3`, `-4`, `-5`, `-6`, `-7` | 6 of 6: exit taken, save, load, gameplay | Rynn (0x10009) on foot at 304128 31114 279552, W walks her about 4,300 units | before the exit 67 to 77 nodes, none without pages; after the load 22 nodes, no cycle |
| `long-2` | not reached: the renderer stopped at the Alwarren start (`wgpuDeviceCreateTexture ... mip level count 8 is invalid, maximum allowed is 1`, a panic in `wgpuQueueSubmit`, exit 3) | | |

No allocator spin and no watchdog in any run; the old stalls (`alw-13` to `alw-15`) had two or three pageless layers in the
list before the exit. This confirms the formats `5dfe29a` fix for the Alwarren exit. The renderer stop is unrelated (it hit
`fi-3` once too, again at the first frames of a level; 2 of about 30 runs): left to the renderer work.

## 3. Bell, second save, Rift Crystal

Run `bell-2` with the test aid `packaid` (this pack with timer 1764 unlinked by `aid.mts`, so the hammer is not hidden;
needed because of 1): hammer taken (525 leaves the world cell, inventory cell 0), exit, first save (slot 1), Islands on foot,
dismount, tower, column: scene "Bell Hammer" (`0x00080011`) 166.0 to 175.0 s, doors, rift box, message box, save screen.
**Second save**: the save grid has slot 1 used and the cursor on it; Cross there opens the overwrite question, which is why
the earlier taps never saved (`alw-17`, `bell-f`). Down moves to slot 3 (empty), Cross saves: `BESCES-50006Slot3` holds
`OotF 08 - Rift Crystal.rsg` beside the Islands save in slot 1, `exit taken in 'OotF 06 - Islands'`, Rift Crystal loads
with Rynn (0x10009) at its start (95232 220938 185237) and W moves her 4,000 units; clean scripted quit. The same Down,
Cross works on a card that already holds a save at the Alwarren exit (`save2-1`).

## 4. Air Currents

`OWN_DEFAULTS_RFL` keeps the PS2 class defaults for Air / Water Currents (111). In the PC Islands 207 of 241 current objects
write their speed themselves (5, 16, 2, ...), and the converter keeps those; with the PC class default written
(a private build of the converter without 111 in the set) only **one** Islands object changes: Air Current 5208, the last
node of the 69,628-high stream, `Speed 1 (PS2 16)` (Alwarren: 3 objects, 10 for 16; four arena / mod levels 20 for 16).

In game (Arokh placed, no input, a dump every 60 frames):

| Placed at | Converted (16 at 5208) | PC default (1 at 5208) |
| --- | --- | --- |
| node 5368 (speed 16) | carried 11,800 units toward its slow partner 5369 in about 3 s, then stops | the same |
| open air off the stream (330000 69628 700000) | does not move | does not move |
| node 5208 (the end) | pushed 1,700 units at once, then stops at 265413 503606 | pushed slowly, 4,400 units, stops at 268078 502635 |
| node 5976 (speed 16, before the end) | pushed 1,900 units, stops at 259915 505687 | carried on 10,000 units to 268096 502707 |

The currents work as converted: they carry Arokh in their direction and nothing drifts outside them. The only difference
is where the stream lets him go at its very end (2,700 to 8,000 units earlier with the PS2 speed). Not broken, so not
changed; the converter comment's worry (Islands currents slowed from 16 to 1) does not apply, since only that end node
uses the class default.

## Still open

* The war giant kill (above): needs a closed-loop driver; the PC reveal "Bellhammer1" (timer 1764, toggler 1763) is
  therefore still unplayed, and the second-save and Rift Crystal step was played with the test aid.
* The renderer's `mip level count` validation stop at a level start (2 runs).
