# OotF Ruined Village: the push puzzle in the tomb of the Soul Crystal

Owner report (playing the Order of the Flame campaign): "in the lair we are unable to push objects meant to be pushed to
solve the puzzle in there." This note finds the place, the PC logic, why it failed in the PS2 engine, what the converter
does now and what was played. Private runs and evidence under `build/review/ootf-push/` (`runs/`, `packs/`, `evidence/`);
nothing from either game is quoted.

## 1. Which lair, which puzzle

Ruined Village (level 1). All five saves of the owner are in that level, and the last one (2026-11-09 15:05, the quick save)
stands inside a domed room at 279421, 8130, 201546: the tomb with Heron's Soul Crystal on a casket platform in the middle of a
pit (`evidence/old-pack/owner_save_1505_room.png`). That room holds the only push puzzle of the campaign. Succubus Lair has no
Rigid Body and no pressure plate at all. Arokh's own lair (487424, 22000, 200704) has neither; the crystal from the tomb is
what the route to it is about.

The PC objects (read with the formats loader, ids of `Ruined Village.lvl`):

| Object | Class (engine) | What it is |
| --- | --- | --- |
| 1101 | Push Block (16 Rigid Body), model `statuemove` 532 x 969 x 532 | Push Mode Slide, Friction 4, not waiting for a trigger, immortal; linked to plate 992; stands 559 units from the tile of plate 991 |
| 988, 991, 992 | Pressure Plate 01 / 03 / 02 (29) | three tiles of 634 x 642 units, 4,800 to 7,000 units from the objects' common origin in the room centre; 988 and 991 start lowered, 992 raised; each sends On at the bottom to counter 1009 and to the next plate (992 -> 991 -> 988 -> 992), "When Triggered" Both, "Send Off Message When Triggered?" on, not Rynn only |
| 1009 | Count down to zero (7), initial 1 | sends On to 2176 and a light |
| 2176 | Rising Floor Ring (35 Mover) | rises 3 lu out of the pit and bridges it |
| 1103 | Heron's Soul Crystal (117) | on the casket platform; taking it starts timers (stairs, ring) |

How it works on the PC, read from the PS2 engine's own handlers of the same classes (PAL SCES-500.06, Ghidra): the plate
handler `0x45de10` sinks a plate under Rynn or under a Rigid Body (class `0x10010`, only when "Triggered by Rynn Only?" is off),
a plate that receives On rises again and sends Off, unless something stands on it; the counter handler `0x396428` fires only
when it is still 0 two ticks after it reached 0. So stepping on the raised plate lowers it (counter 0) and raises the next one
(counter 1 again): the plates chase each other for ever. With the block pushed onto plate 991, that plate cannot rise, the
counter stays 0 and the ring rises. Pushing is walking into the block: no key. That is also how the disc's own pushable block
moves (Valley of the Fallen, class "pushable block", run `d2foot1`: Rynn on foot moved it 1,572 to 2,690 units in 4 s of
walking into it, also with Arokh in `d2push2`).

## 2. Why it did not work

Seen in game on the owner's executable, pack and saves (`evidence/old-pack/`):

1. **The block is not in the room.** In all four Ruined Village saves of the owner and in a fresh level the block entity lies
   at 286655, 1500, 207019: on the terrain under the pit, 6,300 units below the floor (`scan1`, `scan-own2..4`). During the
   opening scene it is still at its placed height. The PS2 Rigid Body rests on terrain only: its ground probe (`0x230648`
   from the simulation `0x3ccbe0`) asks the terrain layers and never an object, and the tomb floor is a model over an
   invisible terrain layer at 1500.
2. **The plates could not be pressed.** Plates are static props with a Trigger Detector box at the object origin
   (`ootfpuzzle.ts`); here the origin is the room centre, over the casket platform, behind the pit.
3. The block's PC bounds are spheres, which do not stop Rynn on the PS2 (the cage finding of 2026-11-08).

So the room showed three tiles and no block, nothing reacted, and the Soul Crystal was out of reach.

Things tried on the way that did not work (single-level packs `rv1` to `rv11`, results in `evidence/experiments/`):

* An invisible terrain layer under the block at floor height (`rv1` to `rv5`): the block rests on it, and is drawn once the
  layers that see the room list the new layer, but the block is filed on that layer and Rynn stays on the room's own layer:
  she walks through it (`tomb3`, `tomb4`, `tomb5`). Raising the room's own invisible layer instead is not safe: a corridor and
  a lift run under those cells.
* The converted block as a Rigid Body on real terrain, on Rynn's layer (`ctl1` to `ctl3`): it stops her and wakes up, but
  does not move, with the PC friction 4, with the disc block's 0.6 and with the disc block's pivot. Not explained.
* Native Pressure Plates written from the PC classes (`rv1` to `rv11`): they load and sit lowered or raised as on the PC,
  but Rynn walks over the tiles without pressing them, with the PC collision box, with a plain block and with a block 56 units
  over the floor (`plate1`, `plate2`). Not explained either (the disc's plate classes live in LEVEL4ON / LEVEL8OS and were
  not compared).

## 3. What the converter does now

Formats `src/ootfpush.ts` (new), `ootfpuzzle.ts` (`pushPuzzles`), `ootfclass.ts` (`PlanOptions.pushBlockMover`),
`ootfassets.ts` (`requireSolidBox`), `ootfpipeline.ts` (stage "push puzzles"), tests `test/ootfpush.test.ts`.

* A push block is a Rigid Body with Push Mode Slide that does not wait for a trigger. Its plate group is every plate it is
  linked to plus the plates linked with those.
* Its model gets one solid box, so Rynn is stopped by it.
* A push block that has plates and stands more than 256 units over the terrain (on a model) is cloned from a Mover (35) with
  the block's model instead of a Rigid Body. In the puzzle pass:
  * a Trigger Detector around the block (260 units past its faces) starts the slide: 2 s along the world axis toward the
    nearest plate tile of its group, onto the tile's centre. Walking into the block is the push;
  * a timer reports the arrival after the slide;
  * each plate that raises the block's plate on the PC and starts raised gets a Trigger Detector on its tile (not at the
    object origin);
  * a copy of the PC counter starts at 1 + the number of those plates, the arrival and each of those plates count it down, and
    at 0 it sends what the PC counter sends. The PC counter gets no message any more;
  * plates that start lowered on the PC are placed lowered.
* The Mover's slide axis was read off in game: axis 0 with a positive distance is +z, axis 2 is -x, axis 1 up.

What this is and is not: the block moves once, on a fixed path, when Rynn touches it from any side; it is not a free physics
push, and the plates do not sink or rise. The condition that ends the puzzle is the PC one (block on the plate next to it, the
plate beside the statue pressed, in either order). A wrong first step cannot make the plates chase each other as on the PC.

By the rule above only Ruined Village's block qualifies in the campaign (section 5); the other plate puzzles keep their
detector stand-ins. The new full pack was not diffed against the previous one (other converter changes of the day are in it).

## 4. Played

Own play folder `build/review/ootf-push/play` (a copy of `baseline-play`: executable sha256 `4fddfbc8...`, the owner's
`drakan.ini` and `drakan_input.ini`, HD textures, DLSS quality), full 23-level pack of the committed converter (`packs/full1`,
sha256 `d5302455...`, 11,227 files) installed there beside `drakan-character-motion`. Driver: `push.py` on
`scripts/harness/live_drive.py` (it reads positions out of the process and sends keys and mouse counts; nothing is written
into the game).

Settings that differed from the owner's: a 1280 x 960 window instead of borderless full screen, `game = d1` and
`title_menu = modern` (`evidence/ini_diff_proof.txt`), sound off (the driver's default), a fresh empty memory card, a direct
start of Ruined Village (`DRAKAN_START_LEVEL`, `DRAKAN_FAST_START`) with the opening scenes skipped by the harness's Cross taps.
No god mode in the two proof runs. `drakan_input.ini` is the owner's file unchanged.

`proof1` (`evidence/proof1/`, 50 frames, `block_and_ring_changes.txt`, `result.json`):

| Step | Input | Result |
| --- | --- | --- |
| start | placed once at the spot of the owner's 15:05 save (279421, 201546) | inside the room, west side; block at z 207019, ring at y 1544 |
| to the block | W with the facing steered, round the floor | five waypoints reached |
| push | W into the block from the west | block z 207019 -> 207578 in about 2 s (`03_push_00..15.png`), onto the tile |
| save in the puzzle | F5, walk 6,000 units back, F8 | Rynn back at the saved spot to the unit, block still at z 207578, ring at 1544 (`05_after_quick_load.png`) |
| the plate by the statue | W round the floor onto the tile | ring y 1544 -> 7688 in about 4.5 s (`07_plate_00..21.png`) |

`proof2` (`evidence/proof2/`): the same push and plate from a placement beside the block, then with W and jumps over the pit's
rim onto the risen ring (`21_on_ring.png`), onto the casket platform (`22_on_platform.png`) and to the crystal, E three times
(`24_use_*.png`, `25_after.png`). Afterwards the five stair movers of the room stand at y 7688 instead of their placed 8507 to
11784, which is what the crystal's timers do; no pickup line is logged, so the pickup itself is inferred from that. Then a
placement on the ledge in Arokh's lair and W: the awakening scene plays (`31_lair_*.png`), as on 2026-11-07.

## 5. Every pushable or movable puzzle object of the campaign

Rigid Bodies (engine 16) in the 13 campaign levels, from the PC data (`census.ts` in the scratch folder):

| Level | Object | Push Mode, trigger | Puzzle role | State |
| --- | --- | --- | --- | --- |
| Ruined Village | Push Block 1101 | Slide, at once, on a model, linked to plate 992 | holds a plate of the tomb puzzle | works as a sliding stand-in (section 4) |
| Islands | pushcrate 2645 | Slide, at once, on terrain, no links, no plate near | none found in the data (a crate that can be pushed) | not played. Stays a Rigid Body, now with a solid box. A converted slide-mode Rigid Body on terrain did not move when Rynn walked into it in the control runs (`ctl1` to `ctl3`), so it is most likely a fixed crate in the pack |
| Islands | Cage 2347 | Slide, waits for a trigger, 1,679 over the terrain | a trap that drops | not a push object; not played |
| Ruined Village | Boulder 366, 2605 (at once), 2790 and "Boulder for orc" 2780 (wait) | Roll | rolling boulders, damage | not played here |
| Wartok Canyons, Alwarren, Islands Night | Boulder (1, 4, 1) | Roll | scenery or traps | not played |
| Grimstone Mines, Grotto | Boulder Damage Player (10, 5), Snow Ball (23) | Roll | traps | not played |

Movers named "Push ..." in Wartok Canyons (Push Pillar, Push Base, Push Brace) are static props of engine 17 with a sound;
they were not looked at further.

Pressure plates elsewhere (Islands 7, Rift Crystal 30, Grotto 1, Wartok Canyons 1) keep the detector stand-in at the object
origin. Their tiles lie up to about 1,550 units from the origin on an axis, inside the 4,096 unit box; the plates that start
lowered on the PC (six of the seven Islands plates, eight in Rift Crystal) can be pressed at once in the pack, so those
puzzles cannot block on a plate, but their order is not enforced. None was played in this pass.

## 6. Not verified or only inferred

* The owner's saves: a save restores its objects, so the saves made with the old pack keep the block under the pit. The puzzle
  needs a level started on the new pack (a new game). Loading an old save on the new pack was not tried.
* The walk from the village into the tomb was not walked; `proof1` starts from one placement at the owner's saved spot inside
  the room, `proof2` from placements.
* The Soul Crystal pickup is inferred from the stair movers; the inventory was not read.
* The walk from the tomb to Arokh's lair was not walked (the lair scene was started from a placement on its ledge).
* Sound was off; the Mover's sounds are set to none, so the block slides silently.
* The Islands crate, the rolling boulders and every other plate puzzle were not played.
* Why a converted Rigid Body does not move on terrain and why a native Pressure Plate does not sense Rynn are open; both
  would be needed for a real physics push.
* Hand play, full screen, the German tree and co-op were not tried.
* Only the formats test suite was run (59 files, 605 tests), plus `tsc` and the repository guard; the editor workspace's
  other tests and the Python tests were not.
