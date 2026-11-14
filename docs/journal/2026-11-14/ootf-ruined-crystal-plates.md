# OotF Ruined Village: invisible trigger plates and see-through floor in the tomb of Heron's Crystal

Owner report (round 7): "We've got cases where you pick up Heron's Crystal the trigger plates are not visible at all and you
can see through the floor." Fixed in the converter (formats `f568393`); the runtime is unchanged. Scratch, runs and evidence
are under `build/review/rv-plates/` (not tracked); nothing from either game is quoted here.

## 1. The room and its plates

The crystal lies in the domed tomb in Ruined Village (room centre 286720, 200704; see
[tomb push puzzle](../2026-11-12/ootf-lair-push-puzzle.md)). Three Pressure Plates (PC ids 988, 991, 992, engine 29) are
flat tiles of about 634 x 642 units set into square recesses that the floor model (Heron's Tomb) leaves open for them; under
the floor there is only the invisible push-floor terrain patch and the pit's invisible layers. What taking the crystal does
in the PC data: timers show the spiral Stairs (Visibility Toggler 2321), hide the Rising Floor Ring (Toggler 2324), lower the
stairs into the pit after 5 s and switch off a death trigger at the lower corridor. Nothing in that chain touches the plates.

## 2. Cause

Since the plates became native Pressure Plates (formats `21128e2`, 12 November) their class keeps the converted PC model, and
that model was written without the strip display list (MOD record 0x0211). A rigid model without it draws nothing while its
collision and plate logic stay. Both disc plate models (LEVEL4ON "Brick Playe Plate 01", LEVEL8OS "Necro Pressure Plate")
carry the list. The converter adds the list to synthesized classes whose engine id is in `DRAWN_RIGID_RFL`
(`ootfpropdraw.ts`, journal 2026-11-08 ambush props), and 29 was missing because plates were still static props when the list
was made.

So the plates were never drawn, before or after the crystal, from any position: each tile showed as a square hole in the
floor through which the clear colour shows (a pale lilac or dark blue square), and Rynn stood "on nothing" when she pressed a
plate. The owner noticed it around the crystal because that is where the player looks at the floor of the room. The same
holds for the other 39 plates of the campaign (Islands, Rift Crystal, Grotto, Wartok Canyons): 9 plate models in the pack had
no list.

Checked and not the cause: the plates' class and links (the puzzle plays), terrain hole cells (the floor is a model), the
pickup chain (it touches only the stairs, ring, lights and a death trigger), visibility layers (the casket, dome and statues
at the same origin draw).

## 3. Fix

`DRAWN_RIGID_RFL` now includes 29 (formats `f568393`, test in `ootfpropdraw.test.ts`). In a full pack converted with it
(`packs/fix1`), only the 9 plate models of RESOURES.MOD change: each gains a 0x0211 record; their bounds (the plate box with
headroom of `plateBounds`), header, vertices and polygons are byte-identical to the dist pack's.

## 4. Played

Play folders as the round 7 rules ask: `play` (copy of `baseline-play`, the dist exe of 9420be1f and the dist pack) and
`play-main` (exe built from main at 90dff87b, `-DistDirectory` into the scratch folder, the owner's ini and input ini copied
in, HD textures of dist). Runs by `scripts/harness/live_drive.py` through `tools/push.py` (W with mouse steering, Space, E,
F5, F8). Differences from the owner's settings: a 1280 x 960 window instead of borderless full screen, `game = d1`, sound
off, a fresh memory card, a direct start of Ruined Village with the opening skipped by the harness's Cross taps; the main exe
added its newer defaults to the ini (`evidence/ini_diff_run.txt`).

Plan `tools/crystal.py` (the proof3 route of 12 November plus fixed viewing poses), one placement into the room, then real
input: the wrong-step chase over all three plates, the block pushed onto 992, F5, walk off, F8, plates 991 and 988, the ring
rises, over the rim, onto the casket platform, E takes the crystal ("Heron's Crystal" shown), looks at the tiles from the
platform, F5, placed views of each tile, F8, the views again.

| Run | Exe, pack | Puzzle | Tiles |
| --- | --- | --- | --- |
| `b1` (before) | dist exe, dist pack | chase, push, F5 / F8, ring 1544 to 7688, crystal taken | see-through square holes at 992 and 988 before the pickup, after it and after F8 (`evidence/before_*.png`) |
| `a1` (after) | main exe, `fix1` | the same, every plate state identical (`evidence/b1_summary.txt`, `a1_summary.txt`) | all tiles drawn, raised and sunk, before the pickup, after it and after F8 (`evidence/after_*.png`, `pair_992_988.png`) |
| `pa1` | main exe, `fix1`, loading the quick save made just after the pickup | stairs at their end positions | tiles drawn; the stairs and the open pit seen from the platform (`evidence/after_pit_and_tiles_from_save.png`) |
| `rcb` / `rca` | main exe, dist / `fix1`, Rift Crystal | Rynn walks onto plate 696: same contact states in both | no tile before, a tile under her after (`evidence/rift_crystal_before_top_after_bottom.png`) |

Saves made after the crystal: `build/review/rv-plates/saves/after-crystal-fix1` (fix1 pack) and `saves/after-crystal-dist`
(dist pack), card folders with the quick save in slot 33.

## 5. Not verified

* The before and after runs used different exes (dist 9420be1f against main 90dff87b) and HD texture sets; the change is in
  the pack only and the plate states match, but no run had the dist pack on the main exe.
* The look of the pit after the ring is hidden was not compared with the PC game. From the platform it shows the stairs and a
  dark pit, the same as before the fix; if the owner's "see through the floor" also means the pit, that is not addressed.
* Walking down the spiral stairs and on to Arokh's lair, the plate sounds, hand play, full screen.
* The placed views `p_kerb0` and `p_kerb3` put the camera inside a statue niche and show the clear colour; that is a
  placement artefact, as is the dome missing in the first frame after one placement in run `x1`.
* Plates in Islands, Grotto and Wartok Canyons were not looked at; only one Rift Crystal plate was.
* Tests: the formats suite (61 files, 639 tests), `tsc`, prettier. The editor workspace's other tests and the Python tests
  were not run.

Needs a regenerated pack and a new campaign game (saves made on another pack build do not load). No new exe is needed.
