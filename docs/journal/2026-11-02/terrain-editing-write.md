# Terrain editing: the write path and the in-game proof (2026-11-02)

The editor's Landscape tools (sculpt, paint, expand, add layer; `editor-core/src/terrain-edit.ts`) change
`LevelDocument.terrain`; this note covers how those edits become a LVL 0x0001 override in a level pack and what the game
does with them. Format facts are in [level-format.md](../../reference/level-format.md) section 10 / 10.1.

## Code

* `editor/packages/formats/src/terrainwrite.ts`
  * `readPs2Blob` / `writePs2Blob`: a real walk of the per-layer inline blob (no more resync). Byte-exact for **every** layer
    of the 14 PAL level LVLs (5,100 layers).
  * `rebuildPs2Terrain(original, layers, opts)`: rebuilds the payload from the disc bytes. Unchanged layers are copied byte
    for byte (definition, blob, zlib block), so an unedited list returns the disc payload itself. A changed layer keeps as
    much of its disc blob as the edit allows: heights only = chunk tables kept, normals recomputed for the vertices whose 3x3
    neighbourhood moved; texture edits = chunk rectangles kept, texture lists merged (new refs placed by the level's
    texture order, `levelTextureOrder`), weight bytes recomputed only around repainted cells, chunk re-cut when a chunk would
    exceed 10 textures; resize / new layer = blob regenerated (`buildPs2TerrainBlob`) with the level's `level_tag`, a slot
    that collides with no other member of the group (own corner first, then any group with room, else a new `seq`).
    Heights that leave the u16 range around `world_height` move `world_height`; a hill on a layer flagged flat (0x04) clears
    the flag and stores per-vertex normals; layers without draw data on the disc stay without.
  * `linkVisibleLists`: an appended layer lists itself first, and the layers it names list it back (see the hang below).
  * `extendTerrainLayer`, `cloneTerrainLayer`, `ps2AdjacencyMask` (helpers; the UI has its own `expandLayer`).
* `editor/packages/formats/src/terrain.ts`: `splitPs2Terrain` (definition / blob / block offsets, shared by parser and writer).
* `editor/packages/editor-core/src/terrain-pack.ts`: `hasTerrainEdits`, `terrainPackEntry` (attaches the disc terrain to a
  document read from JSON first, size check against the runtime's entry limit).
* App: `LevelLoadResult.layers` (the worker's `loadLevel` now returns the 0x0001 entry and its disc bytes; `exitIndex` does not
  read them), `modio.ts` `buildLevelPack` / `levelTerrainEntry` / `hasLevelEdits`, `editor/project/export.ts`
  `buildProjectPack` (disc levels get the 0x0001 override next to the 0x0020 one; level copies get it patched into the whole
  LVL) and `levelEditCounts`; Play / play test treat terrain-only edits as edits (`panels.ts`, `playtest.ts`).
* Dev tool: `editor/scripts/terrain-pack.ts` (`--hill`, `--paint`, `--extend`, `--new-layer`, `--paint-new`, `--layer`,
  `--slots`), writes a pack folder + `mods.ini` and prints a `DRAKAN_PLAYTEST_POS`.
* Tests: `formats/test/terrainwrite.test.ts` (14), `editor-core/test/terrain-pack.test.ts` (3); synthetic layers only.
  Headless check against the ISO (not in the suite): `build/scratch/terrain-write/roundtrip.ts` (identity rebuild byte-equal
  for all 14 levels; height / paint / extension edits touch only the edited layer's spans).

## In game (LEVEL1NA, `play_run.py --level 1a`, `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`, `DRAKAN_PLAYTEST_POS`)

Runs and screenshots: `build/scratch/terrain-write/runs/<name>/` (private). Rynn's standing y is the surface + about 380.

| Test | Pack | Result |
| --- | --- | --- |
| Hill +3000, radius 4 cells, Rynn on top (`tw-hill-top`) | `mods-hill` | stands at y 175634 for the whole run. Control without the mod at the same pose (`tw-control-top`): falls about 3000 and dies (load-last-save prompt). **Collision follows the edited heights.** |
| Hill +3000, radius 3 cells in the meadow, seen from 7 cells away (`tw-hill2-view` vs `tw-control-view`) | `mods-hill2` | a lit, textured mound where the control shows flat meadow. A birch placed on the old ground is now inside the hill (objects keep their y). |
| 5x5 cells repainted with 0x90233 (a steep-slope texture of the level, pale) (`tw-paint-view`) | `mods-paint` | the patch draws pale, blended into the grass at its border (the weight arrays work). |
| 5x5 grass cells (0x907ef) repainted with the dirt ref 0x93553 (`tw-paint2-view` vs `tw-control-view`) | `mods-paint2` | the grass around Rynn becomes textured dirt (the grass-blade objects stay). |
| Layer 305 extended by 3 rows at its top edge (rim of the level) | `mods-extend` | Rynn is placed on the new rows at once and stays (`tw-extend-on`, y 207234 = edge + 400); the control without the mod at the same point is refused 16 times by the playtest placement (no floor there). The rim spot itself shows `World 0.0 fps` with an empty view when facing out, **with or without the mod** (`tw-ext-control`), so it cannot show the strip. |
| Layer 305 extended by 3 rows at its bottom edge (inside the valley), new rows painted 0x90233 | `mods-extb` | **first try (slot in a new group, `seq` 51): the whole layer 305 vanished** (`tw-extb-view`: Rynn stands on invisible ground, collision kept). With the slot in its own group (seq 38, overlapping layer 323, which no `visible` list links to it): the layer draws, the pale new rows continue the edge (`tw-extb2-view`) and Rynn stands on them (`tw-extb2-on`). The writer now never opens a new group unless nothing else fits (and warns). |
| New 3x3 layer, 1200 above the meadow (`tw-new-view3`, `tw-new-on4`) | `mods-newlayer` | the platform draws (dirt texture, slot in an existing group) and Rynn stands on it (y = platform + 398), see below for the first attempt. |

### The appended-layer hang

The first new-layer pack copied the `visible` list of the layer below (`[305, ..., 305]`, no entry for itself). Seen from 7
cells away it drew fine; with the player placed on or next to it the guest hung every time (3 of 3, watchdog: thread 1
looping in the allocator around 0x4acc40, called from the terrain code at 0x161d54..0x162658). Every disc layer lists itself
(483 of 483 in LEVEL1NA; first in 175 of them). With the new layer's own index first (`linkVisibleLists`) the same position
loads and Rynn stands on the platform. The UI's `makeLayer` copies the template's list without the new index; the writer now
fixes that on export, but the UI should add it too.

## Facts learned (also in level-format.md 10 / 10.1)

* Blob walk: header `u32 0` (4 bytes, no chunks) = a layer **without draw data**; 196 of 483 LEVEL1NA layers, mostly type 2
  ("in between", 156) but also floors and ceilings, and 171 of them do have visible cell textures. Then `u32 0` and the normals:
  one entry (`00 7f 00 7f`) exactly when definition flag 0x04 is set, else one per vertex. No exceptions in 14 levels.
* Slot rectangles of a group **never overlap** on the disc (0 of about 14,000 same-group pairs). The earlier note about
  overlapping ranges in LEVEL5NE came from the old resync cutting blobs wrongly.
* `visible`: every layer includes itself; 90 % of links are symmetric.
* Vertex mask word: the upper byte is 0 on every disc vertex; the low nibble is "neighbour cell exists and is not a hole"
  (98.8 % of LEVEL1NA vertices; "exists" alone 91 %).
* LVL 0x0001 is larger than the runtime's 0x40800 whole-entry limit on 10 of 14 levels (streamed in chunks), so a grown
  override is accepted there; on the small levels (LEVEL1BS 40 KB) growth below the limit is allowed and logged.
* Splat files (`<6>TS.DB/.TXD`) did not need changing: painting with a ref the level already uses draws without them.
* A layer whose slot moved to a **new group** (`seq` 51, one past LEVEL1NA's highest) loads but is **not drawn**; overlapping
  the slot of an unlinked member of an existing group draws. Why a new group fails (group table sized elsewhere, LVL 0x0003?)
  is open. `rebuildPs2Terrain` places a slot: own corner, free rectangle in its / the nearest group, then overlap with
  unlinked members, and only then a new group (with a warning).
* Harness note: the playtest placement sometimes loses a race with Surdana's opening and Rynn is put back at the level start
  around +240 boundaries (`tw-extbov-view`); shots at frames 0..150 avoid it.
* LVL 0x0003 is one u32 per level (0x320c8 in LEVEL1NA, 0x7fee0 in LEVEL0RO), meaning unknown, left alone.

## Not done / open

* Object heights are not adjusted to edited ground (a sculpted hill swallows trees, a lowered one leaves them floating);
  the UI could offer "drop objects to the ground" over the brushed area.
* Normals are central differences; baked disc values near edits are replaced (about half match exactly anyway).
* Weight bytes at layer borders follow the generator's `outside` rule, not the neighbour layer's data.
* No runtime change was needed.
