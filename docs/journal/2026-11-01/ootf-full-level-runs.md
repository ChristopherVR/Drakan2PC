# All 13 single-player OotF levels, full pipeline, in game (2026-11-01)

Run: `build/ootf-run/full.sh f` (one level per pack, no `--max-classes`, `recipe_long.txt`, `PS2X_WATCHDOG=60`, timeout 120 s, game
lock `build/ootf-run/game.lock`), binary `build/final-game/bin/drakan.exe` with the ODB header count fix. Helper scripts
(uncommitted, under build/): `full.sh`, `one.sh`, `bis2.sh`, `split8.sh`. Output `build/ootf-run/full_f.out`.
PASS = level registered and loaded (`afterLevelLoad ok=1`), `gameplay=1` (Rynn under control, camera look callbacks), the recipe walked and quit.

| # | Level | Entities | Result |
| --- | --- | --- | --- |
| 0 | Intro | 115 | PASS, Rynn control |
| 1 | Ruined Village | 1669 | PASS, control (gameplay=1 at t=56 s with 3 other games running; the first run with a stuck pause-screen loop was a slow load) |
| 2 | Ruined Village Book (cut scene) | 24 | loads; no player (cut-scene level) so the recipe never reaches `play`; not a failure of the level, the harness cannot score it |
| 3 | Wartok Canyons | 1990 | PASS, control |
| 4 | Grimstone Mines | 1542 | PASS, control |
| 5 | Grotto | 486 | PASS, control |
| 6 | Islands | 2350 | PASS, control |
| 7 | Alwarren | 1470 | (fixed, see below) loads, **crash after load**: `missing-target` JALR 0 at 0x10822c (`lw v1,0(a2); lw v0,0x2c(v1); jalr v0` with a2 = `this+0xd4`, a null vtable slot) |
| 8 | Rift Crystal | 1294 | PASS, control |
| 9 | Islands Night | 713 | PASS, control (2 cells on fallback texture) |
| 10 | Succubus Lair | 1022 | PASS, control |
| 11 | Volcanic | 1457 | (fixed, see below) loads, **hang** after load (main thread loops at pc 0x4ac960, ra 0x126d2c; the allocator-corruption signature of earlier hangs) |
| 12 | Rift World | 164 | PASS, control |

11 of 13 reach control (Book excluded: no player). Class ids above the ODB maximum make no difference to any result.

## Alwarren bisect (open)

* Essentials only (`DRAKAN_DBG_NOOBJ=1`): PASS. Full objects: crash. `DRAKAN_DBG_OBJSPLIT=2,k` both halves crash; `8,k`: k=0,1,3 crash,
  k=2,4,5,6,7 pass. So several independent triggers, or a trigger that is common to several object kinds.
* Classes present in all three crashing eighths and absent from the passing ones: Ballista, Brick14n, Looping Sound, Main Gateway,
  Pit Corner, Wall Corner. Dropping them all (`DRAKAN_DBG_DROPNAME`) or only Ballista still crashes, so it is not (only) those.
* Same crash with `OOTF_IDS_BELOW_MAX=1`. New converter test aid `DRAKAN_DBG_LISTCLS=1` prints the class histogram of the placed objects.
* Next: split the crashing eighths again (16/32), and look for what sets `this+0xd4` (a per-object controller) in the caller of 0x108208.

## Volcano fixed (visibility list total)

Symptoms varied run to run (allocator hang in the first full run, then wild `JALR` at 0x10a45c / 0x10a3e4 / 0x10ba48 / 0x17a068 in the
object teardown code, `target` a heap value, at the first kill-list flush after the level load). That is heap corruption, not one
object: `OBJSPLIT` 2,0 crashed while 4,0 and 4,2 passed; dropping all lights passed, but 64 or 128 lights, `AnimWay`, `Lava`
dropped all still crashed; so objects only changed where the corruption landed.

Bisect on terrain (`DRAKAN_DBG_MAXLAYERS`): 470 and 700 layers pass, 800 and 900 crash. Then per-layer visible lists: capping every
list to 60 entries (`all 941 layers kept`) passes, 110 crashes. Disc statistics (all PAL LVLs, `parsePs2Terrain`): at most 660 layers,
the largest `visible` total is **41362** (LEVEL1NA, 483 layers, longest list 221). Volcano had 941 layers, **67105** entries
(longest 162); Islands 20102, Wartok 17000, Alwarren 6927. So the engine sizes something by the total visible entries.

Fix: `limitVisibleEntries` (`ootfterrain.ts`, budget `MAX_VISIBLE_ENTRIES = 40000`) cuts the longest lists first, keeping the nearest
layers by origin; levels under the budget are unchanged. Volcano then reaches control twice in a row (`autostart` at frames 8894,
8339). Test aids added: `DRAKAN_DBG_MAXNAME=<regex>=<n>`, `DRAKAN_DBG_DUMPNAME=<regex>`, `DRAKAN_DBG_CLSINFO=<regex>`.
Harness: `build/ootf-run/bis3.sh <tag> <level> <n> <k>` (descends into the failing OBJSPLIT half; one run is 4 to 8 minutes).

## Alwarren fixed (bisect result)

Bisect with `build/ootf-run/bis3.sh` (descends from a failing `OBJSPLIT n,k` into the failing half, 2048 splits, one object left):
the single object index 32 plus the essentials still crashed. It was a **Left Turn** (castle piece, PC engine class 17), whose class is
a new record cloned from the PS2 class "Nichol's Tome Stomp" (new test aid `DRAKAN_DBG_CLSINFO=<regex>` prints each synthesized
class with its template). Other crashing sets held "Staright Piece", "Entrance", "Alwarren Entrance", which were cloned from the
BloodVialBlood family.

Cause: the class body has a u32 flags word at body offset 6 (after u16 group, u16 model id, u16 model db). Over the 1344 RESOURES
classes it is 0 (966), 1 (340), 33 (30), 32 (5), 3 (3). The clones that crashed had template flags 32 and 3. The engine builds a
per-object controller (`this+0xd4`, virtual call at 0x10822c) for those, and with the PC model patched in it ends up with a null vtable.

Fix (`planClassMapping`, wants-model path): drop templates whose flags word is not 0, 1 or 33 while other candidates remain. Alwarren
now reaches Rynn control (`autostart` at frame 6212, no `missing-target`).
