# Object categories: Outliner, Content Browser, Show menu (2026-11-02)

The user: "There is also a lot of 'other'." In Surdana the Outliner showed **Other 1313 of 4675** objects and the Content
Browser had a big "Other" chip. Goal: UE5-style groups with plain names, with "Other" a small remainder.

## Why there was so much "Other"

`LevelObject.kind` for an LVL object comes from `kindOfClassName` (catalog.ts): a regex over the class name (the LVL has
no entity type). Names the regex misses fall into `other`: "Snowy Pine" (1722 placed on the disc, no "pine" in the tree
rule), "Skull", "Icy Brick", "peg 1", "Green Trog : Center" (a creature), "Scavenger", "Looping Sound", "Generic Detector",
"AmbientTransition" and so on. The heuristic also misfiled the other way: "Stomp Player" (the cut scene player) counted as
Player, "Dragon Barrier" and waypoints as Creature, torches as Usable.

## Measurement (editor code against the PAL disc)

Headless with vite-node and the app's worker, like `build/scratch/copydirect/make_copy.ts`: `documentFromLvl` +
`buildCatalog` per level, then counts by kind, by engine class (RFL id, `AssetClass.rflClass`) and by class name. Scripts
and dumps under `build/scratch/other-kinds/` (not committed: `measure.ts`, `census.ts`, `before.txt`, `after.txt`,
`census.txt`).

Before (kinds, all 14 levels, 38415 objects): other 13491, static 12283, usable 8872, creature 3005, weapon 708, player
42, camera 14. Surdana: other 1313, static 1754, usable 1010, creature 487, weapon 102, player 8, camera 1.

The census by RFL id shows the engine class is the reliable signal: every class with one RFL id behaves the same way
whatever the designers called it (0xd3 = static props, 0x11 = breakables, 0x3d = trigger detectors, 0x31 = humanoid
NPCs ...). 171 RFL ids exist across the ODBs; 98 have placed objects.

## The change

`editor/packages/editor-core/src/categories.ts` (exported from the package index):

* `ObjectCategory` and `OBJECT_CATEGORIES` (display order, `label`, `hint`, `color`, `icon`): Player, Characters &
  creatures, Paths & waypoints, Items & pick-ups, Chests & breakables, Doors & mechanisms, Triggers & logic, Cut scenes,
  Quests & dialogue, Level exits & teleporters, Barriers & zones, Lights, Sound & music, Effects, Sky & weather, Level
  settings, Cameras, Scenery, Other.
* `categoryOf({ name, rfl, kind })`: the RFL table first (every RFL id seen on the disc is mapped, with the class names in
  comments), then name rules when the RFL id is unknown (world dumps, missing ODB), then the stored kind. Scenery or
  breakable classes named "Invisible ..." / "jump plug" are Barriers & zones.
* `classCategory(cls)` for catalog classes, `categoryIndex(catalog)` (cached object-to-category function for one level),
  `categoryInfo(id)`, `countByCategory(items, fn)`, `hasRflCategory(rfl)`.

Display only: `LevelObject.kind` is unchanged, so the marker colours, `diffDocument`, the LVL writer, content packs
(`content-format.md` `kind`), the Unreal export and `playtest.ts` see exactly what they did.

Used by:

* Outliner (`app/src/editor/outliner.ts`): top-level groups are categories (tooltip = hint), search also matches the
  category name.
* Content Browser (`app/src/editor/assets.ts`): the filter chips are categories (one scrolling row, a Clear chip when any
  is on); the folder tree has a "Types" section (`cat:<id>` folders, icon in the category colour) above Containers; a
  container's sub-folders are its categories (`db:<STEM>/<id>`); the detail pane shows the category.
* Show menu (`toolbar.ts`): "Show categories" with counts replaces "Show kinds". `LevelView` has `hiddenCategories` and a
  `categoryOf` function (`setFilters({ categories, categoryOf })`, `passes()` checks it), so hidden categories also drop
  out of Select all / Invert / Select same class.
* The Place sidebar (`app/src/editor/placecats.ts`, shell work) builds on `classCategory` and `categoryInfo`.

## After

All 14 levels: placed objects in Other **0** (was 13491). Scenery 20060, Lights 3761, Chests & breakables 3476, Triggers &
logic 2871, Characters & creatures 2000, Paths & waypoints 1588, Barriers & zones 1085, Sound & music 1024, Doors &
mechanisms 715, Effects 593, Items & pick-ups 477, Quests & dialogue 284, Sky & weather 223, Cut scenes 146, Level exits &
teleporters 59, Player 25, Cameras 14, Level settings 14.

Surdana (4675): Scenery 2724, Triggers & logic 400, Lights 348, Paths & waypoints 211, Sound & music 204, Characters &
creatures 168, Chests & breakables 142, Barriers & zones 140, Quests & dialogue 91, Items & pick-ups 71, Doors &
mechanisms 70, Effects 46, Cut scenes 27, Sky & weather 18, Level exits & teleporters 11, Player 2, Cameras 1, Level
settings 1. Its old 1313 Other objects went to Scenery 727, Sound 199, Characters 82, Quests 82, Waypoints 63, Triggers
53, Effects 29, Breakables 24, Barriers 13, Exits 11, Doors 9, Cut scenes 9, Items 7, Sky 4, Settings 1.

What remains in Other: only catalog classes, no placed objects. The DEBUG.ODB helpers (Bounding Node Generator, Box,
Collision Response Generator, Red Sphere, Sphere, Waypoint Generator; RFL 0x17, 0x18, 0x19, 0x91, 0xcc) and Shadowmire's
"AI Debug" (0x19). They are developer tools the game never places, so they stay in Other on purpose. A class with an RFL id
the table does not know and a name no rule matches also lands there (none on the PAL disc).

Notes:

* Characters & creatures is one group (enemies, villagers, dragons, wildlife, turrets such as the Ballista and Evil Tree
  Eye); the Place sidebar splits it further.
* Torches (RFL 0x14) count as Lights; they are props that also carry a light.
* The stored `kind` heuristic is still wrong in the ways above; it only colours viewport markers now and is written into
  content packs as a hint. Changing it would change those outputs, so it was left alone.

Checks: `npx vitest run` (74 files, 706 tests, `editor-core/test/categories.test.ts` new, synthetic inputs), `tsc` for app
and editor-core clean, Prettier clean. Static `vite build` + private Playwright on Surdana: 18 Outliner groups, no Other
group; Show > Scenery off leaves 1951 of 4675 shown. Shots under `build/scratch/other-kinds/` (not committed).
