# OotF terrain textures and shading on the PS2 engine (2026-10-31)

Code: `editor/packages/formats/src/ootfterrain.ts` (`TerrainTextureBuilder`, `convertTerrainTextures`, `ps2CellFlags`,
`findTerrainDbIndex`, `TERRAIN_TXD_PATH`), `ootftex.ts` (`Ps2TextureOptions.slot`), tests `test/ootfterrain.test.ts`.
Runs: `build/ootf-run/terr0..terr13/` (private screenshots), harness `scripts/harness/ootf_level_run.py`.

## How the engine binds a terrain texture (verified in game, Relic converted onto the LEVEL1NA template)

A cell ref is `u16 id, u16 db`; db indexes the LVL dependency table and the engine looks the id up in that dependency's
TXD. Experiments (all with the same 23 Relic textures):

| Route | Result |
| --- | --- |
| refs into `<level>_splats` (db 24, the shipped `LEVEL1TS.TXD` with textures appended) | world freezes (World 0.0 fps, no terrain); the same TXD with fallback refs is harmless |
| new dependency (db 25 or free index 12) with an `OOTFCOM` container (+ a copied ODB) | `afterLevelLoad result=-4`, `guest-branch:missing-target` (JALR to 0) |
| refs into `Proc Textures` (db 9) with new ids, mod ships a replacement `WORLDCON/PROCTEES/PROCTEES.TXD` = original + appended textures | works |

So: **append to PROCTEES.TXD and use db 9** (`findTerrainDbIndex(header)`, `TERRAIN_TXD_PATH`). One TXD per pack,
shared by all levels (`TerrainTextureBuilder` keeps the ids stable across `addLevel` calls). The replacement is 3.6 MB
plus the new textures; the mod loader logs `replaced ENGLISH\WORLDCON\PROCTEES\PROCTEES.TXD`.

### The texture header word at +40

Converted textures froze the world until header `+40` (u32) was changed. On the disc: 0 in every splat texture; in
Proc Textures 1..113 (rising with the id, equal for neighbouring ids, so probably a family number) and 199646 on a few.
Not derivable. Tried on converted terrain: clone of a disc texture (+40 = 45) works; 114+ (unique counter) freezes; 1 runs
but draws flat dark olive (no texture detail); **45 renders the real textures**. `TERRAIN_TEXTURE_SLOT = 45`. Header
+44 (f32, 1 / 4 / 10 / 256 on the disc) is left at 4.0 and drew fine. 16 bpp 128x128 was not tried in isolation (the disc has
16 bpp terrain textures only at 64x64); terrain textures are always encoded 8 bpp (`mode: 'indexed'`), max size 128.

## API

```ts
// formats (import by path; ootf* modules are not in index.ts)
const b = await TerrainTextureBuilder.create(src /* PcFileSource */, procTxdBytes, findTerrainDbIndex(tplHeader)!, { maxTextures?, maxSize?, firstId? });
const mapTexture = await b.addLevel(pc);            // ConvertOptions.mapTexture for that level (PC ref -> 9<<16 | id)
files[TERRAIN_TXD_PATH] = b.build();                // original PROCTEES.TXD entries + the converted ones
```

`convertOotfLevels` already does this when `ps2.procTxd` is given (`ootf-convert.ts` reads `PROCTEES.TXD` from `--ps2`;
the harness extracts it from the ISO). Without `procTxd` it falls back to the old splat route, which does not work.
`opt.terrain` forwards `TerrainTextureOptions`. Dev env knobs: `DRAKAN_TERRAIN_TEX_COUNT`, `_TEX_MAX`, `_SLOT`, `_NOREF=1`.
Reduction strategy: `maxTextures` keeps the most used N per level; the rest map to the nearest kept texture by mean colour.
Not needed for Relic (23 textures); no engine limit on count was hit (the biggest tested pack TXD was 3.9 MB).

## Cell flags (PS2) = diagonal bit + per-cell opacity

Surdana: the high byte of the u16 cell flags is 0xff on 95 % of cells and ramps 0x18 / 0x98 / 0xd8 / 0xf8 / 0xff over the
3 to 4 cells at a layer border (and 0x6b / 0xd6 next to edges): per-cell opacity used to fade a layer into the layer under
it. Bits 2 and 3 (0x04 / 0x08) come in runs (both set about half of the time) and do not follow neighbour textures or UV
patterns; unknown. PC flags only have bit 0, so `ps2CellFlags` writes `0xff00 | bit0`. Wired in the pipeline as
`terrainShading` (default on).

## Not done / open

* Geometry checks beyond "it draws" (diagonal choice, UV orientation, normals vs PC light, flat layers, water, holes) were
  not compared against a stock level in game; the UV order is copied unchanged and the visuals looked plausible.
* Blob exactness (slot ranges, texture order, border counts) not attempted: the engine draws regenerated blobs fine.
* The full `convertOotfLevels` pipeline (RESOURES/SYSTEM `.DB` patches, `OOTFCOM` dependency) currently stopped the Intro level
  from loading in my runs; terrain runs used `--only textures`.
