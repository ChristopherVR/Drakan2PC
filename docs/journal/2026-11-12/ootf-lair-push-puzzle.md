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

## 3. What the converter did after the first pass (replaced, see section 7)

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

## 7. Second pass: the engine's own classes (same day)

The owner accepted the stand-in of section 3 as honest but wanted the real thing. This pass answers the two open questions
of section 6 and the floor question, and replaces the stand-in: the block is a Rigid Body again, the plates are native
Pressure Plates, and the engine plays the puzzle. Formats `21128e2`. Sections 1 to 6 stay as the record of the first pass;
where they disagree with this one, this one is right, and the reason is in 7.4.

### 7.1 Why a converted Rigid Body never moved: box bounds

Traced on the disc's pushable block (Valley of the Fallen, `runs/d2dump1`) and on the converted block on terrain
(`runs/cdump1`, pack `rv7` of the first pass), the body's state words read every 50 ms while Rynn walks into it.

* Rynn's push is not a collision impulse. The player controller (0x331270) stores her movement vector in the body
  (0x45eca0: state +0x1c4, state bit 0x20), and the body's tick (0x459000) takes it as its velocity only when
  `(state & 0x60) == 0x20`.
* Bit 0x40 is set when the body is woken (0x45ed18) and cleared by the body's own contact query (0x459a30, run from event 8
  when "Detect Objects?" is on), but only when the body's contact sphere index (state +0x1a4) is not negative.
* That index is set once (0x458d88) from the model's bounds: 0 without shapes, the lowest sphere of a sphere hierarchy, and
  -1 when the bounds are boxes (`is_obb` 1).

Disc block during the push: state `0x22d`, +0x1a4 = 1, velocity rising to 809 units per second. Converted block: state
`0x26d` for ever, +0x1a4 = `0xffffffff`, the stored push velocity 808.9 at +0x1c4 and a velocity of 0. The first differing
field is +0x1a4, and its cause is the "one solid box" the first pass gave the block's model: the PC bounds of `statuemove`
are three spheres, which is exactly what the engine needs. With the PC spheres the same block on terrain took the push
(`runs/e1a`: velocity 621; the spot of the control runs has a prop 123 units from the block, which is why it stopped again
there). Friction, pivot, the header word at +6 and the shadow flag are not involved. "Spheres do not stop Rynn" holds for
props; a Rigid Body is pushed, not walked into.

### 7.2 The floor: a layer of its own, and the order of the layer lists

The Rigid Body rests on terrain only; that stays true. The first pass gave the block a small terrain layer and found that
Rynn then walked through it. The reason is how the engine finds what an actor can touch:

* The contact query collects its candidates from the object lists of the layers found under the 3 x 3 cells around the
  actor (0x123bf0, 0x124060). An object filed on a layer the actor's lookups do not return does not exist for her.
* The layer lookup with an old layer (0x14b6b8) walks that layer's `visible` list upward from the layer's own entry,
  keeps the last layer the point is above, and stops at the first layer the point is under. The converter had appended
  the new layer at the end of the room layers' lists, behind the hill layers over the tomb: the walk met a layer above
  Rynn first and never reached the patch (`runs/t1a`: block on 346, Rynn on 30 over the same cells).

Fix (`ootfpush.ts` `pushFloor`, `addPushFloor`): the patch covers the room (the flat, fully invisible floor layers that
touch the one under the block: PC layers 29 and 30, 9 x 9 cells), lies 8 units under the model floor at 7688, is invisible,
and has a hole in every triangle with no floor model over it (the pit). It is named right after the room's layers in every
list that names them, and its own list names the room's layers before itself. `runs/t2a`: Rynn and the block both on layer
346, the block follows her at up to 806 units per second and stops when she stops.

Known limits of the patch: its cells are 2,048 units, so a strip of up to 500 units inside the kerb of the pit is solid
invisible ground at floor height (the cells that straddle the kerb); a block pushed over the kerb rests there or falls
through a hole to the layers at 1500 and is lost. The kerb does not stop the block (it rides up it, `runs/t2a`), as its
sphere bounds suggest it does not on the PC either; that was not compared.

### 7.3 Why a native Pressure Plate did not sense Rynn: it was looked for a quarter turn away

The plate handler gets its contact from the player's contact pass (0x3085e8 reports through 0x1280b0, return address
0x309984 with the plate as partner, traced in Stratos on the disc's own plate, `runs/d2plate3`). The converted native
plates were never even candidates of the precise test (0x122340: no call with a plate, `runs/t5b`). Their world boxes
(`runs/t5e`) showed why: the tile of "Pressure Plate 02", which lies at +5854 on the model's x axis, is at world z +5854.
An unrotated object shows its model's +x toward world +z: the loader applies the stored Y degrees minus 90
(`docs/reference/level-format.md` section 3, and `ootfcull.ts` / `ootfshell.ts` already do it). The first pass placed model
boxes without that quarter turn, so it walked Rynn over the places where the tiles would be on a map of the PC model, found
nothing, and concluded that native plates do not work. `runs/t5f`, same pack, Rynn walked over the engine's positions: a
lowered plate counts her (data word 3 = 2), the raised one sinks, sends On at the bottom and the next plate rises.

So the real layout is (world x, z; the room centre is 286720, 200704):

| Plate | Tile centre | Starts |
| --- | --- | --- |
| 992 "Pressure Plate 02" | 285700, 206558 (north, 1,060 units from the block) | raised |
| 991 "Pressure Plate 03" | 279846, 200704 (west) | lowered |
| 988 "Pressure Plate 01" | 291547, 200704 (east) | lowered |

and the block is linked to 992 because that is the plate it belongs on: the Rigid Body's collision response (0x45aec0)
pulls a slide-mode body onto the centre of the plate that is its first link once it is within 300 units, and keeps it
awake there. The stand-in of section 3 slid the block 559 units north, where the engine has no tile; what its frames
show there was not looked at again.

Two more things the native plate needs, both from the same test (0x1242d8 compares world boxes before any shape):

* A box with height. A flat PC tile has a box of no height. `ootfassets.ts` `plateBounds` gives the plate model one box
  that lists the tile's polygons and reaches the sink distance plus 96 units over the tile. Rynn still stands on the
  polygons.
* A block that still touches the tile at the bottom. The body does not follow a sinking plate, and its box ends at its
  base: with a low plate box the block's own query lost the sunk plate (`runs/t7b`: 70 reports body to plate while the
  plate was up, none after), the block fell asleep, and the plate rose again under it when the next plate told it to. With
  the plate box above and the block's lowest sphere moved down by the sink distance plus 25 units (`sinkLowestSphere`) the
  contact holds, the engine pulls the block onto the plate centre, the block ends on the sunk tile at y 7612
  (`runs/t8a`), and plate 992 keeps counting it.

The plate class itself (`ootfclass.ts`): the databases a converted level names hold no Pressure Plate, but the handler
0x45de10 is registered and the PC class has the same 13 fields in the same order, so the record is written from the PC
defaults with the converted model (as the Trail Master is). The two plate sounds and the block's "Looped Roll Sound" are
converted and set as object parameters; "Detect Objects?" (a PS2 field) is set on the block.

### 7.4 What the first pass got wrong

* The quarter turn (7.3). Everything it says about where tiles are, and "native plates do not sense Rynn", follows from it.
* The solid box on the block (7.1) is what kept the block from moving; it was added to make the block "stop Rynn".
* The order of the solution in section 1: the block goes onto 992 (raised, sinks under it, raises 991), then Rynn presses
  991 (raises 988), then 988: 992 cannot rise, the counter stays 0 for two ticks and the ring rises.

### 7.5 Played

Play folder and settings as in section 4 (1280 x 960 window, `game = d1`, `title_menu = modern`, sound off, fresh memory
card, direct start of the level with the opening skipped by the harness's Cross taps; the owner's `drakan.ini` otherwise and
his `drakan_input.ini` unchanged, no god mode). Pack: the full 23-level pack of formats `21128e2` (`packs/full3`, sha256
`f99f7834...`, 11,239 files) installed in `play/mods`. Plan `plans/proof3.py`, run `p4`, evidence `evidence/proof3/`
(`summary.txt`, `result.json` with every change of block, plates, counter and ring, `drive.jsonl` with Rynn's and the
block's position at every step, frames). One placement: at the start, onto the spot of the owner's 15:05 save inside the
room (279421, 201546). Everything after it is W with the facing steered, Space, F5, F8 and E.

| Step | Result |
| --- | --- |
| start | 992 raised (7789.4), 991 and 988 lowered (7714.4), counter 1, ring at 1544, block at 286655, 207019 |
| wrong steps, no block: Rynn onto 992, 991, 988 | 992 sinks and 991 rises; 991 sinks and 988 rises; 988 sinks and 992 rises: back at the start, counter 1, ring down |
| short push (W for 0.6 s from behind the block toward 992) | block 286655, 207019 -> 286233, 206880, then it stands while Rynn stands |
| push on | block -> 285714, 206562 on the sunk tile (y 7612), 992 down and counting it, 991 raised |
| F5, walk a third of the room away, F8 | Rynn back at the saved spot, block at 285714.4, 7612.0, 206561.5, plates as saved, 992 still counting the block |
| Rynn onto 991, then 988 | 991 sinks and 988 rises; 988 sinks, 992 stays down, counter 0, ring 1544 -> 7688 |
| Space and W over the rim, to the casket platform | on the ring (289433, 200672), on the platform (286663, 8708, 200384) |
| W to the crystal, E three times | stopped 550 units from the crystal's position against the casket; the stair movers then stood at 7688 to 7932 and falling (placed 8507 to 11784), which is what the crystal's timers do. The pickup itself was not seen |

The same plan ran before on the single-level pack of the same code (`p3b`) with the same result.

### 7.6 The other plates and the Islands crate

Every pressure plate of the campaign is a native Pressure Plate now (42: Ruined Village 3, Islands 7, Rift Crystal 30,
Grotto 1, Wartok Canyons 1; the converter log counts them per level and no detector stand-in is left), so their links,
"When Triggered" and "Stay Down" values are the PC ones and the engine enforces the order. Played on the same full pack,
god mode off, one placement beside each plate, then W (`plans/plates.py`; summaries and frames in `evidence/pass2/`):

| Level | Run | Seen |
| --- | --- | --- |
| Rift Crystal | `rift1` | plates 1147 and 1151 (raised, at 143360, 67584 and 143360, 75776) sink 70 units under Rynn and count her. No plate rose again in this group; their counters and the doors behind them were not followed |
| Islands | `isl1` | plate 3924 (the raised one of the seven) sinks and 3925, which it is linked to, rises; 3925 sinks under Rynn; at the third placement, beside 3922, plate 3923 stood raised. The portcullises behind the last plates were not followed |
| Wartok Canyons | `wart1` | plate 2428 sank 100 units while Rynn settled beside it. Its Secret Entrance mover was not looked at |
| Grotto | `grot1`, `grot2` | not shown. In `grot1` the plate did not change; in `grot2` it went to "sinking" while Rynn stood somewhere else (the placement had failed). Not played |

The Islands crate (object 2645, "pushcrate", Rigid Body Slide on terrain, no links): its PC bounds are spheres, so it is a
pushable Rigid Body in the pack, as on the PC. `crate1`: Rynn placed 1,100 units east of it and W held for 4 s, the crate
went from x 557692 to 556863 (829 units) in front of her (`evidence/pass2/crate1/drive.jsonl`, `E_0.png`, `E_1.png`). The
second push of that run did not happen (Rynn did not move after the second placement and the run ended on an error of the
driver). No puzzle uses the crate.

### 7.7 Not verified or only inferred (second pass)

* The walk from the village into the tomb, and from the tomb to Arokh's lair: not walked. The proof starts from one
  placement inside the room, on the spot of the owner's save; no route into the tomb was scripted.
* The Soul Crystal pickup: not seen. Rynn stopped 550 units from the crystal's position; the falling stair movers are the
  only sign that E took it. The inventory was not read.
* Sound: off in every run. The plate sounds (parameters 70 / 71 in the single-level pack) and the block's "Looped Roll
  Sound" are converted and set; nobody heard them.
* The owner's old saves keep the old objects (a Mover block, static plates); the puzzle needs a level started on the new
  pack. Loading an old save on the new pack was not tried.
* Pushing the block over the kerb into the pit, off the room through the doorway, or off plate 992 once the engine holds it
  there: not tried. From 0x45aec0 a block within 300 units of the plate it is linked to is pulled back to its centre, so it
  should not come off 992; plates 988 and 991 do not hold it.
* The invisible strip of patch ground inside the kerb (7.2) was not stood on.
* A second push block in one room, a rotated or scaled plate under a block, and a push block with box bounds (the
  converter only warns) do not occur in the campaign and were not tested.
* Grotto's plate; what the plates of Rift Crystal, Islands and Wartok Canyons switch; hand play, full screen, the German
  tree, co-op.
* PC behaviour was not run: what the PC engine does at the kerb or with the block's link is read from the PS2 handlers of
  the same classes.
* Tests run: the formats suite (61 files, 619 tests), `tsc`, the repository guard. The editor workspace's other tests and
  the Python tests were not run.
