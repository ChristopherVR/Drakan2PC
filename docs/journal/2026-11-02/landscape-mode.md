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
