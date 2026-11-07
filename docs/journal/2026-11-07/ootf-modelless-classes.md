# Converted OotF levels: classes with no model reference in the editor

Question: when the editor opens converted Order of the Flame levels, what goes wrong with objects whose class has no model,
or whose model reference does not resolve?

## Method

I converted all 23 PC levels from the local install with `editor/scripts/ootf-convert.ts --campaign 1 --audit .`, against
`LEVEL1NA` with `build/odbx/data`. I also did a two-level run (Ruined Village and Relic). Then node probes opened each
converted level the way the worker does: the pack laid over the player's ISO with `overlayDisc` and `patchedContainers`,
then `readLvl`, `documentFromLvl` and `loadModelData` for every class placed. I ran the same probes on the 14 disc levels
for comparison. The probes and their output are in `build/ootf-nomodel/` and are not committed.

## Findings

Per level, over all 23 converted levels:

| Result | Classes |
| --- | --- |
| Class model decoded | 2,398 |
| No model reference (model id 0) | 368 |
| Model reference that does not resolve (missing .DB entry, MOD or model id) | **0** |
| Model with no polygons ("Bounding Vertices", 8 vertices) | 7 (Potion Raising, Current Mover, catching fire object, Dragon health power-ups) |
| Unresolved class names | 0 |

So the converter writes no dangling model references. Every model-less class is a real one: Timers, Count down to Zero,
waypoints, Generators (Soul Flare, Soul Lightning, Blow Up Dragon), Kill Box and death trigger, Visibility Toggler, Fire Fly
groups and axes, fader, Stomp Player, Level Transition, Level Properties, Landmorph, Camera, Dynamic and Powerup Lights,
Burning Flame, Water Droplet, Triggered Music Loop and break-away floors. The disc levels show the same pattern: Surdana has
94 model-less classes and 4 empty models.

Issues, by category:

1. **(b) Model-less classes were reported as failures.** `loadModelData` returned `model: null` both for "no model
   reference" and for "the reference failed", and `ModelLayer` put both in `failed`. The loading card then said "N object
   types have no model and show as markers", the status line said "no model or undecodable", and a toast fired. On a
   converted campaign level that was 15 to 43 false reports, every time the level was opened. **Fixed:** `ModelData`
   now carries `noModel` and `rfl`. The layer keeps model-less classes, and models with no polygons, apart from real
   failures (`modellessResources` / `failedResources`). The card, status line and toast now count only references that
   failed: "N object types could not load a model and show as markers".
2. **(b) Converted helpers lost their editor icon.** Icons were picked by class name ("Trigger Detector", "Timer") or
   learned from the disc's debug-box models. Converted classes keep their PC names and have no model, so "Soul Flare
   Generator" and similar drew as plain cubes. **Fixed:** a model-less class now gets its icon from its engine class:
   detector, timer, count-down, camera, waypoint, null marker, generator, lights, sounds (`helperGroupOfRfl`). The PS2 ids
   were checked on the converted records. They follow the same Show menu toggles and counts. Other model-less logic stays a
   kind-coloured marker. Markers and icons are pickable, selectable and editable as before.
3. **(b) Break-away floors drew as a marker.** A break-away floor (engine 44) has no class model. Its `Model List` field
   names pieces that are modelled in place, so together they make the intact floor (checked on the disc's bridge chunks).
   A Morph Plug's `Morphing Models` works the same way. **Fixed:** for these two fields, `loadModelData` merges the
   pieces (or the first frame) into one stand-in model (`standInModelRefs`, `mergeModels`). The model is drawn at the
   object. It applies to 10 converted and about 10 disc classes. Object-level overrides of the field are not used yet;
   the class default is drawn.
4. **(c) "missing ODBs: <level>_splats" on every level.** The terrain splat dependency (`OOTFxxTS` / `LEVEL1TS`) is a
   `.DB` and a `.TXD` with no class table. It was listed as a missing ODB in the status line of every disc and converted
   level. **Fixed** in `readLevel`: a dependency with a TXD and no ODB is not reported.
5. **(a) Converted break-away floors keep the template's pieces.** The converter converts the extra models of a Morph Plug
   (99) but not of a break-away floor (44). It clones the floor from a PS2 template outside RESOURES (LEVEL1ON
   "Bridge Breakway Pieces", "Dirty / Brocl Break Away Floor"), and `mapModelFor` only maps host (RESOURES) models. So the
   clone keeps the template's `Model List`. The 2-level run had 23 PC floor classes with the template's piece ids. In the
   full run they all became one class with identical data ("Brick1", pieces 950 to 963 = the disc bridge chunks). The editor
   now shows exactly that, the bridge chunks. **Not fixed:** the fix needs the PC pieces hosted in the template ODB's own
   MOD (the cross-ODB route that hung level loads in `2026-11-01/ootf-props-audit.md` point 5). It also needs an in-game
   check of breaking, which this session could not run.
6. **Cosmetic.** Name-mapped player and creature classes (Rynn, Dragon, Spider) show `model=not converted` in the audit.
   That is expected: they use the PS2 model.

Saving: model-less objects were already part of the document. Export is a diff against the loaded list, so nothing about
models is written. A new test checks byte-identical export, a move, and the autosave JSON form.

## Changes and tests

`editor/packages/app/src/scene/decode.ts`, `layer.ts`, `instances.ts`, `sceneload.ts`, `editor/loadsteps.ts`,
`level.ts` (icon lookup), `worker.ts` (splats). Tests: `app/test/modelless.test.ts`, which uses a synthetic ISO and models
only, plus `loadsteps.test.ts`. Full editor suite: 115 files and 1,085 tests pass, and the typechecks pass. The running
app was not checked: the shared Playwright browser was in use by another session.
