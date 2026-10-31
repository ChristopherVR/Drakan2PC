# Terrain texture limit: 10 per chunk, not 8 per layer (2026-11-01)

Code: `editor/packages/formats/src/terrainblob.ts` (`MAX_CHUNK_TEXTURES`, chunk splitting), `convert.ts` (`MAX_LAYER_TEXTURES` now 0).
Test: `terrainblob.test.ts` ("cuts a chunk with more than 10 textures"). Runs: `build/ootf-run/tx0` (unlimited, old blob: hang),
`tx1` (unlimited, split chunks: PASS), `tx2` (layer cap 8 + split: PASS); helper `build/ootf-run/txrun.sh`.

## Where the limit lives

The terrain chunk record (0x4c bytes, vector at layer+0x10c, stride 0x4c in `FUN_0015c948`, `FUN_00165cb0`) holds `ref[10]` at
+0x14..0x3b, directly followed by the weight-array descriptor at +0x3c. `FUN_00165840` (chunk stream read, called from
`FUN_0015c948`) reads `ntex` and then `ntex` refs into `+0x14+4i` with no check. With more than 10, the refs overwrite
`vw/vh/n/ptr`; `FUN_00165940` then frees/allocates through the garbage pointer and the allocator free-list walk
(`FUN_004acd80`) never ends (or the guest takes an exception at 0x80000080 inside `FUN_004ac628`, ra 0x165a00, as in `tx0`).
The in-engine builder `FUN_00165f68` does check (`slti ntex, 10`), so the real limit is 10, and the earlier "8" was only the
maximum seen on the discs. Nothing limits the number of distinct textures per layer.

## Fix

Chunk rectangles (`x0 x1 z0 z1`) are plain fields of the file, not a fixed 12-cell grid, so the converter cuts a chunk whose
texture list (cells `x0-1..x1`, `z0-1..z1`) has more than 10 entries into two halves along the longer axis, recursively, and
writes the chunks sorted by (x0, z0). Shared border vertices are kept (a half is `[x0,m]`, `[m,x1]`, like neighbouring 12-cell
chunks). No engine patch was needed; patching would mean widening a 0x4c record used by ~10 functions of the recompiled code.
`MAX_LAYER_TEXTURES` is now 0 (unlimited); `maxTexturesPerLayer` / `DRAKAN_TERRAIN_LAYERTEX` still reduce a layer when wanted.
`TerrainBlobOptions.maxChunkTextures` (default 10, 0 = never split) is the chunk-level knob.

## In game (Ruined Village on LEVEL1NA, `--only textures,singletons`, `recipe_long.txt`)

* `tx1`, all textures: loads, Rynn walks, 437 entities; the cliffs show the extra texture variety. Frame rate 94 fps against 502 fps
  for `tx2` (cap 8) on a machine shared with other game runs: the extra chunks and splats cost something; use the layer cap for speed.
* `tx2`, layer cap 8 with the new chunk splitting: loads, same as before.
* Max chunk count per layer was not measured; a very mixed layer ends in 2x2-cell chunks.
