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
| 7 | Alwarren | 1470 | loads, **crash after load**: `missing-target` JALR 0 at 0x10822c (`lw v1,0(a2); lw v0,0x2c(v1); jalr v0` with a2 = `this+0xd4`, a null vtable slot) |
| 8 | Rift Crystal | 1294 | PASS, control |
| 9 | Islands Night | 713 | PASS, control (2 cells on fallback texture) |
| 10 | Succubus Lair | 1022 | PASS, control |
| 11 | Volcanic | 1457 | loads, **hang** after load (main thread loops at pc 0x4ac960, ra 0x126d2c; the allocator-corruption signature of earlier hangs) |
| 12 | Rift World | 164 | PASS, control |

11 of 13 reach control (Book excluded: no player). Class ids above the ODB maximum make no difference to any result.

## Alwarren bisect (open)

* Essentials only (`DRAKAN_DBG_NOOBJ=1`): PASS. Full objects: crash. `DRAKAN_DBG_OBJSPLIT=2,k` both halves crash; `8,k`: k=0,1,3 crash,
  k=2,4,5,6,7 pass. So several independent triggers, or a trigger that is common to several object kinds.
* Classes present in all three crashing eighths and absent from the passing ones: Ballista, Brick14n, Looping Sound, Main Gateway,
  Pit Corner, Wall Corner. Dropping them all (`DRAKAN_DBG_DROPNAME`) or only Ballista still crashes, so it is not (only) those.
* Same crash with `OOTF_IDS_BELOW_MAX=1`. New converter test aid `DRAKAN_DBG_LISTCLS=1` prints the class histogram of the placed objects.
* Next: split the crashing eighths again (16/32), and look for what sets `this+0xd4` (a per-object controller) in the caller of 0x108208.

## Volcano (open)

Terrain-only Volcano (`--only textures,singletons`) also failed earlier (941 layers), then loaded after the chunk-texture fixes;
with objects it hangs in the allocator code, so a size-related overflow (layers / textures / objects) rather than one class is likely.
Not bisected.
