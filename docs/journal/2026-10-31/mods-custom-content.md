# Mods: whole files, custom levels, custom weapons and models (2026-10-31)

Format and rules: [modding.md](../../reference/modding.md). Code: `runtime/hle` (`ModPack.h/.cpp`: `installFiles`, `patchSrsc`,
`extraLevels`; `IsoFs::addMemoryFile`), one call added in `drakan_overrides.cpp`. Tests: `runtime/hle/tests/mod_test.cpp`
(103 checks, synthetic, in the `runtime-tests` ctest run). All in-game material below lives in the private folder
`build/mods-files-play` (built from the player's ISO, nothing committed).

## What was built

* `IsoFs` overlay: a node can hold its bytes in memory, so the file service serves pack files with no change to
  `FileService` (open size, read, lseek, SRSC parse all work).
* `files/<path>` replaces or adds disc files (later pack wins, conflicts logged, SRSC validity and the 0x40800 entry limit
  checked for replacements, 256 MiB cap).
* Entry overrides for `.LVL` (read with plain 0x1201 reads) are applied by rebuilding the container (`patchSrsc`);
  `new/<FILE>/...` adds SRSC entries. Only the running language tree is touched.
* `levels = [...]` in the manifest registers levels; `LEVELLST.TXT` is regenerated; `extraLevels()` lists them.

## In-game proofs

1. **Editor-style LVL override** (the level-edit agent's open question). Pack with `overrides/LEVEL1NA.LVL/0020_0_0.bin`
   (one Birch Tree f32 x moved by -5000 in place). Log: `LEVEL1NA.LVL rebuilt with 1 entry patch(es)`. `DRAKAN_WORLD=2`
   dump (`DRAKAN_WORLD_DUMP_FRAME=2500`): unmodded run has resId 0xb0658 at 451133,169966,349383 and none at 446133; the
   modded run has it at 446133.03,169966.27,349383.03 and none at the old spot. 4602 entities both.
2. **Cloned level.** Pack `clonelvl`: `files/ENGLISH/LEVELS/LEVEL9ST/{LEVEL9ST.LVL,LEVEL9TS.DB,LEVEL9TS.TXD}` (copies of
   Level 1A's `LEVEL1NA.LVL` and `LEVEL1TS.*`), `levels = ["Level 9X - Clone Test"]`. Log: `level ... registered at list
   index 14`. With `DRAKAN_START_LEVEL=Level 9X - Clone Test` the game opens
   `Levels/Level 9X - Clone Test/Level 9X - Clone Test.lvl` -> `LEVEL9ST.LVL` (1652778 bytes, srsc 1/7), then the splats files
   `..._splats.{db,txd}` -> `LEVEL9TS.*` (the others `not found`, as for Surdana), shared `LEVEL1ON` data and plays: screenshot
   at 200 frames shows Surdana with Rynn, HUD and objective text. No crash. The Multiplayer/New Game menus that read
   `LEVELLST.TXT` get the 15-line merged list (the stock game never showed the list in a menu on PAL, so the in-game proof
   is the redirect path, not a menu click).
3. **New weapon class, model and textures.** Pack `custom` (only `new/` and one `.LVL` override):
   * `new/RESOURES.ODB/0102_16630_11521.bin`: copy of the class record "Iron Mace" (id 8569, group 11521, name field
     changed in place, 9 characters), model reference (u16 at record offset 14) set to a
     new model id 6070.
   * `new/RESOURES.MOD/{0200,0203,0204,0211,0207}_6070_2379.bin`: copies of the five entries of model 6063 (the mace). The
     `0200` header entry is `u16 len, name ("Iron Mace_Handle"), ...` and u32 texture ids at +32, +36, +40 (5987, 6127,
     6498 here); the clone points them at new ids 3400..3402.
   * `new/RESOURES.TXD/0040_3400..3402_0.bin`: copies of those three textures (8 bpp, palette kind 1) with the palette turned
     green.
   * LVL object-list override adding placed maces (class 8569 control, class 16630 clone, db index 3 = RESOURES).
   Result: log shows `RESOURES.{MOD,ODB,TXD}` and the LVL rebuilt (9 new entries); the world dump has the control actor
   (resId 0x32179, model 0x317af) and the clone **as a live actor** with resId 0x340f6 = (3<<16)|16630, class 0x1000e, and
   model **0x317b6** (the cloned model id). So the engine accepts a new class id in an existing ODB, resolves its model
   from a new MOD entry set and its textures from new TXD entries, with no other registration.
   **Not proven: the visual.** Neither the control nor the cloned mace is drawn in the screenshots (the stock placed item is
   not visible at the spot either, probably a hidden/inventory-state item), so the green retexture was not seen on screen.
   Use a drawn prop class for a visual proof.
   * An earlier attempt with class id 60001 (above the file's largest id 16930) did not spawn; ids must be free ids inside
     the existing range (16630 worked). Cause not investigated (probably an id-indexed table sized from the ODB).

## What the engine allows / what needs format work

| Want | Works today | Needs |
| --- | --- | --- |
| New map | Yes: clone or hand-made LVL via `files/`, `levels`, LEVELLST merge | Authoring a new LVL (terrain layers 0x0001 are opaque) |
| New placed object, moved or deleted | Yes (LVL 0x0020 override, editor export) | - |
| New class (weapon, prop) | Yes as a copy of an existing class record (new id inside the ODB's range) | Class record body decoding to change damage, anim, grip, name length (name must keep its length unless the body offsets are re-based) |
| New model | Copy of existing model entries under a new id works as far as spawn; the `0200` header's texture id list is the retexture hook | MOD geometry format (`0203 0204 0211` undecoded) for real new meshes |
| New texture | New TXD entry (copy and edit pixels/palette) | A TXD writer for resizing; plain edits need none |
| New weapon behaviour (script/damage tables) | No | Class record fields, `0x0103` schema, runtime class (record +80/+16) semantics |

Open items: visual confirmation of the retexture, the id limit above, and whether pickup visibility needs an inventory flag.
