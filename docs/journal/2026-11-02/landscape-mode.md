# Landscape mode: sculpt, paint and grow the ground in the editor (2026-11-02)

An Unreal Engine 5 style Landscape mode for the level editor: raise / lower / smooth / flatten / ramp / noise brushes on
the terrain heights, painting cells with the level's own ground textures, holes, growing a layer edge and adding a new
layer. This note covers the model, the tools and the UI; the LVL write path and the in-game proof are in
[terrain-editing-write.md](terrain-editing-write.md). Format facts: [level-format.md](../../reference/level-format.md)
section 10.

## Model (`editor/packages/editor-core`)

* `document.ts`: `LevelDocument.terrain: TerrainLayer[] | null` (formats `parseTerrain` layers) and
  `terrainChanged: Set<number>` (edited or appended layer indices), `terrainBaseCount` (disc layer count).
  `attachTerrain(base)` joins the disc layers with edits read from JSON (pending until then); `writableLayer(i)` is the
  copy-on-write step (the disc read is never mutated); `setTerrainLayer` replaces / appends / removes the last layer;
  `markTerrainEdit` counts applied edit commands per layer so undoing every edit drops a disc layer from
  `terrainChanged` again; `onTerrain` reports every change (layer + cell rectangle, or whole layer).
* JSON (`terrain-json.ts`): `toJSON().terrain = { baseCount, layers }`, only changed layers, each as base64 of its arrays
  (heights as the raw u16 the LVL stores; the right-triangle refs only when they differ from the left ones). Older
  documents without the field load unchanged.
* `terrain-edit.ts` (pure, `test/terrain-edit.test.ts`, 16 tests on synthetic grids):
  * `TerrainStroke`: one brush stroke; `sculpt` (raise with `invert` = lower, smooth, flatten to a target, noise), `ramp`,
    `paint` (a ref or `TEX_HOLE`), `finish()` = one `TerrainEditCommand` (sparse before/after per vertex / cell),
    `cancel()`. Heights accumulate unquantised during the stroke and are written on the raw u16 grid, clamped to it.
    Raise and noise speeds scale with the brush radius (a big brush builds a big hill as fast as a small one a small one).
    Smooth reads neighbours across layer edges from the stitched layers, so seams stay closed.
  * `connectedLayers`: layers stitched to the one under the brush (shared grid points at the same height, within 4 units).
    Surdana's ground is hundreds of small layers; a brush that edits only one opens cracks at its edge. The UI default is
    "Blend into neighbouring layers" on.
  * `brushWeight` (smoothstep falloff), `quantizeHeight`, `layerHeightRange`, `valueNoise` (seeded per stroke),
    `layerAt` (layer under a ray hit, by height), `terrainPalette` (refs in use, most used first).
  * `expandLayer` / `expandLayerCommand` (new vertices at the edge height, new cells copy the edge cell's texture, flags
    and UVs; west / north move the origin and refuse to go below 0; vertex mask bits read off the layer's own corners),
    `makeLayer` / `addLayerCommand` (flat layer, lighting / flags / UVs from a template layer, flag 0x04 cleared).

## View (`editor/packages/app`)

* `scene/terrainlive.ts` `EditableTerrain`: replaces `buildTerrain` for the level view. Untouched layers stay merged per
  (32-cell origin chunk, texture) like before; the first edit of a layer moves it into its own 16x16-cell chunks, and a
  dab rebuilds only the chunks it touched. Heights are read from the document's array, so `groundHeightAt` (placement,
  Snap to ground, the play-test standable check, the fly camera clamp) follows the edits with no extra wiring.
* `editor/landscape/terrainbind.ts`: the scene loader hands the decoded terrain here; every document the view gets later
  (`LevelView.documentListeners`, new) is attached to the same disc layers, so a restored autosave shows its ground at
  once. `sceneload.ts` calls `bindLevelTerrain` / `clearLevelTerrain`.
* `editor/landscape/landscape.ts` (+ `overlay.ts`, `palette.ts`, `landscape.css`):
  * Toolbar mode switch "Objects | Landscape" at the left (`toolbar.ts`), Shift+L, a "Landscape" button in the Place
    panel head, palette commands `landscape.mode` and `landscape.tool.*`. The Landscape panel takes the Place panel's spot
    in the left dock; the viewport gets a thin green frame and a tool chip.
  * Tabs Sculpt / Paint / Manage, tool tiles with a one-sentence "how", brush Size (log slider, shown in units and cells),
    Strength, Falloff; `[` / `]` resize, Shift inverts (Raise digs, Lower builds, Hole fills).
  * Brush circle laid on the ground (outer + falloff ring, orange when inverted), layer outlines (all faint, picked one
    bright, hovered one white), previews for the expand strip, the new layer footprint and the ramp.
  * Paint swatches: the disc level's ground textures (kept even when painted away), named by colour ("Grass", "Sand",
    "Grey stone 2"; textures have no names on the disc) with their share of the ground; strength below 100% scatters.
  * Manage: picked-layer card (Original / Edited / New, size, height range), expand compass + cells, new layer size.
  * Footer: "The game sees ground edits only in a play test or an exported mod" and the changed-layer count.
  * Pointer handling is a capture listener on the viewport box: left drags without Alt go to the brush; Alt orbit, middle
    pan and right-button fly still work. Esc cancels a stroke.
* Help: guide `landscape` ("Shape and paint the ground") appended at the end of `editor-core/src/guides.ts`;
  `docs/reference/editor-guide.md` regenerated. Shortcut sheet: Shift+L and `[ / ]`.

## Checked (static `vite build`, private Playwright context on an ephemeral port, the PAL ISO, Surdana)

Driver and screenshots: `build/scratch/landscape-ui/drive.py`, `shots/` (not committed).

* Landscape mode opens with the panel and the brush ring on the ground; a 3 s Raise drag builds a hill; frame times
  during the stroke: median 16.7 ms, p95 16.7 ms, max 16.8 ms (60 fps, headless SwiftShader).
* Paint (Sand) over grass and rock; Pick layer (Layer 5096, 14 x 9); Expand east by 4 = 18 x 9, Ctrl+Z = 14 x 9,
  Ctrl+Y = 18 x 9.
* Autosave, reload the page, open Surdana, Restore: "27 layers changed", hill and sand patch back.
* Tests: editor-core `terrain-edit.test.ts` (16), app `landscape.test.ts` (3: colour names, palette, chunk rebuild);
  whole suite 85 files / 816 tests green; `tsc` clean for app and editor-core.

## Open

* No live preview of the in-game look: the PS2 draws terrain with per-chunk blend weights (section 10.1); the editor
  draws one texture per cell. The panel says so in plain words.
* Undo of a big stroke keeps sparse arrays per stroke; a session of very large strokes grows memory linearly (no cap).
* Brush on stacked layers (bridges, roofs) picks the layer nearest the ray hit; "Blend" can reach a stacked layer that
  touches the ground at the same height.
* Texture names are colour words; a user rename (stored in the project) would help once the palette gets long.

## Follow-up: objects ride the ground, new-layer visible list, blended paint preview, end-to-end proof

### Code

* **Objects move with the ground** (`editor-core/src/terrain-edit.ts`): `TerrainStroke` takes `StrokeOptions`
  (`carryObjects`, `carryTolerance` = `CARRY_TOLERANCE` 64, `carrySunk` = `CARRY_SUNK` 512, `skipObject`, `onObjects`).
  On the first sculpt or ramp dab, `groundedObjects` picks the objects that stand on the stroke's layers: at most 64 above
  or 512 below the surface under them (Surdana's birches stand about 220 *below* the surface, tents about 90; with a
  symmetric 64 they would have been left inside the hill), and whose nearest terrain layer is one of the stroke's (a roof or
  bridge layer above keeps its objects; flying and stacked objects are further up). Each dab moves them by the surface
  change under them; `finish()` puts the moves into the stroke's `TerrainEditCommand` (`lifts`), so one undo / redo moves
  ground and objects together, and `cancel()` restores both. Paint strokes never carry. UI: "Move objects with the ground"
  under the sculpt tools (default on, saved with the landscape settings); locked objects are skipped; the view redraws
  live through the new `LevelView.objectsMoved()`.
* **`makeLayer` visible list**: the new layer's own index first, then the template's links without duplicates (same rule as
  formats `linkVisibleLists`, which still fixes older documents on export).
* **Blended ground preview** (`app/src/scene/terrainlive.ts`): `cellBlends` uses the writer's weight rule, now exported from
  formats as `textureCountAt` (`weightByte` calls it): per vertex and texture, the number of the four cells around it that use
  the texture. The cell's own texture is drawn opaque as before; every other texture of its 3x3 neighbourhood is drawn over
  it as a transparent overlay mesh (per-vertex alpha `count / (own + counts drawn so far)`, fixed per-texture order through
  negative `renderOrder`, polygon offset, no depth write), which composites to exactly `count / total` per vertex like the
  PS2 splat. A repaint marks one extra cell around the touched rectangle so the neighbours' overlays rebuild.
  `EditableTerrain.blend = false` gives the old one-texture-per-cell look. Surdana overview: draw calls 101 -> 323 in the
  paint close-up, still 60 fps in headless SwiftShader.
* Tests: editor-core `terrain-edit.test.ts` +4 (carry: grounded / flying / sunk / under-ground / roof / locked objects, one
  undo step; carrying off; cancel; ramp; visible list), app `landscape.test.ts` +3 (`cellBlends` weights, three-texture
  compositing, overlay meshes appear on a repaint and go on cancel). Whole suite 85 files / 828 tests; `tsc` clean for app,
  editor-core and formats.

### End-to-end through the real UI (private, `build/scratch/terrain-e2e/`, not committed)

`drive.py` (static `vite build` in `dist/`, Playwright private context on an ephemeral port, the PAL ISO): opens Surdana,
hides the other objects, frames Birch Tree 01 #9478 (438315, 171243, 351312; ground 171462), Landscape mode, Raise with
"Move objects with the ground" ticked (default), size 19,200, strength 60 %, a 3 s drag over the birch. The birch went
171243 -> 175363 with the ground 171462 -> 175582 (its 219 sink kept); Ctrl+Z put both back, Ctrl+Y both up again. Paint
"Sand 2" (ref 0x9025b) from a top-down camera on the hill's east slope; `03b` / `03c` compare the blended preview with one
texture per cell. Manage: pick layer 305, Expand south by 4 (42 x 30 -> 42 x 34). Export through Ctrl+E (the Export dialog,
browser download): "14 change(s) in LEVEL1NA.LVL, 2 terrain layer(s) edited" (the objects are the birch and other ground
objects carried by the stroke) and the known warning "layer 305: its slot overlaps layers of group 38 that it is never drawn
with". `export/my-campaign-mod.zip` (mod.toml + 0x0001 and 0x0020 overrides) unpacked into `mods/` with a `mods.ini`.

Game (`play_run.py --level 1a`, `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`, `DRAKAN_GAME=d2`, `DRAKAN_FAST_START=1`,
`DRAKAN_MODS=<abs mods>`), runs in `runs/`:

| Run | Pose | Result |
| --- | --- | --- |
| `e2e-hill2` | 440815 176458 351612 1.451 (on the sand patch, on the hill, facing the birch) | the mod loads (2 entries); Rynn stands at y 176617 (old ground about 171500) for the whole run on the pale sand, which blends into the grass at its edge; the birch trunk rises from the hill in front of her (on the old ground it would be 5,000 below). |
| `e2e-hill` (2nd run, same pose) | same | placed, then dead by frame 150 ("Press Enter to load the last saved game"); frame 0 shows the pre-placement camera in the level-start water. Same pose in the first build and in `e2e-hill2` stood fine: looks like the known playtest race with Surdana's opening, not the terrain (1 of 3 runs at that pose). |
| `e2e-extend` | 439296 173288 361472 3.142 (middle of the new 4-row strip of layer 305) | Rynn stands on the new grass strip (y 173803) over the valley with the cliff ahead. |
| `e2e-view1571`, `e2e-viewm1571`, `e2e-hill-wide` | east / south of the hill | yaw check (1.571 faces -x); the wide views ended up against slopes, not useful. |

Note for drivers: aim paint from above. From the oblique camera the ray of a point behind the hill's crest hits the near
slope, and the first paint pass landed 5 cells away from the intended spot.

### Still open

* Carry rule is height-only: an object standing on another object within 64 of the ground (a bottle on a low crate) is
  carried too; objects with no model (logic volumes, sounds) placed on the ground move as well (they are ground-relative in
  practice). Rotation is not aligned to the new slope (Unreal does not by default either).
* The blended preview counts only cells inside the layer (the writer's border rule for neighbour layers is not modelled), and
  the right-triangle ref of per-triangle cells is not blended.
* The in-game death in one of three runs at the hill pose is unexplained (no log line); worth a few more runs if it recurs.
