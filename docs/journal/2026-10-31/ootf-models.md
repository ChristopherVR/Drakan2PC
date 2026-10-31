# OotF static models and textures -> PS2 formats (2026-10-31)

Code: `editor/packages/formats/src/{ootfcontainer,ootfmodel,ootftex,ootfassets}.ts`, tests `test/ootfmodel.test.ts`,
`test/ootftex.test.ts` (synthetic fixtures only). Not exported from `index.ts` (import by path). Skinned models (0x0208) are
skipped and reported (`report.skipped`, "skinned model"); `ootfanim.ts` owns them.

## Verified against the PAL disc (l1on.mod + system.mod, 295 static models)

* **0x0200, 0x0203, 0x0207 re-written byte for byte: 295 of 295.** 0x0204: 271 of 295; the 24 others differ in one byte, the
  `u16 0` alignment pad, which is garbage (nonzero) in those records. PS2 -> neutral -> PS2 is therefore lossless apart from that pad.
* 0x0200: name area is `n >= chars, n % 4 == 2` (same rule as XDB/ODB names, `HangingDummyWire` has 16 chars and n = 18); the
  second u32 after the shading word is 0 on 252 models and 2 on 13 (kept as `reserved`).
* 0x0203: 4th byte of each normal is 0x7f, but 0 on 2877 of 117556 vertices (kept as `normalW`).
* **Normal sign**: with a Newell normal of the polygon winding, 147064 of 147064 vertices of the disc have the opposite sign
  (PS2 front faces are clockwise); `computeNormals` defaults to flipped and agrees with the stored normals on 147063 of 147064.
* **0x0207 = 8 zero bytes + the PC 0x0207 record** (PC bounding_info: sphere, OBB with `center` = minimum corner and the axes
  scaled by the box size, `u16 num_shapes, u16 is_obb`, hierarchy, shapes; for OBB shapes `u16 n + n x u16` polygon list).
  Decoded on the disc: cube (154 bytes = 8 + 64 + 4 + 4 + 48 + 2 + 24), 880 (96 bytes, one sphere shape). The 8 prefix bytes are
  zero on 244 of 265 models (the others are mostly the models with 0x0208).
* PS2 containers: header `SRSC, 0x0101, indexOffset, count, 0`, payloads 8-aligned, index to EOF (`writePs2Srsc`). MOD has
  0x0202 (`u16 groups, u16 last group id, u16 last model id`), 0x0402 = 0x11b, groups 0x0201, models with `sub` = group.
  TXD has 0x0402 = 0x20a.
* **PS2 TXD header** (64 bytes): w, h, stride, bpp(u32), flags word at +16, `ffffffff` at +20, fps byte at +36, alpha flag
  byte 0x02 at +37, 0x80 at +38 for bpp > 8, f32 at +44 (4.0 or 256.0, no pattern found), u32 1 at +56. Flags word: 8 bpp 0
  (opaque or colour key), 4 / 8 (graded alpha), 16 bpp 1, 32 bpp 7, 24 bpp 0. 8 bpp palette alpha is 0x7f for opaque (201 of 207
  palettes). Sizes on the disc: 16..256 powers of two; bpp 8 (most), 16, 24, 32.
* **PC TXD record**: `w, h, rowSpacing, u16 bpp, u16 alphaBpp, 2 ignored, u32 colourKey (0xffffffff none), 3 refs, u8 fps,
  u8 flags, u16 mip number, ref material, u32 usage, u32 compression, u32 compressedSize` = 54 bytes, then the zlib (or raw)
  data; `54 + compressedSize == record size` held on the textures checked. Palette = 0x0030 `u16 count, (r,g,b,flags)`.
  16 bpp layouts per OpenDrakan (565, 1555, 4444, 3:3:2:8). Install-wide the PC sizes are 16..256, bpp 8 and 16 only.
* **Real data run** (all 29 PC .mod files, each container converted on its own so shared textures repeat): 3121 models
  converted, 99 skinned skipped, 1 failed (3240-style empty detectors now convert as zero-polygon models); 3737 textures, 52.6 MB.
  Ruined Village alone: 266 models, 324 textures (232 at 8 bpp, 92 at 16 bpp, 5.4 MB TXD, 1.7 MB MOD); every output model parses
  with the existing `parseModel` and every texture with `decodeTxd`.

## Guessed / untested

* **0x0211 (PS2 display list copy) is NOT written.** 261 of 265 models in LEVEL1ON have it; the 4 without are detector boxes.
  Whether the engine draws a model without it is unknown (no in-game test). What I found: small models start with u32 words
  `1, 1, 0, <vertex count>, 1, 0, 1`, then `u16 0, u32 triangle count, u32 0x40`, then triangle index triples as 1-based u8 +
  pad byte, zero fill, then a 0x40 marker and 32-byte rows of three (u, v) float pairs per triangle with the UVs shifted by an
  integer; larger models use a strip-like packing with different row layout and sizes of 622 / 1198 / 1218 / 1228 / 1774 ...
  bytes. Not decoded enough to generate. If the engine needs it this is the blocker for visible static models.
* Polygon flags: PC flags have editor bits in the high byte (0x8000 always, 0x1000..0x4000, 0x0100..0x0400) and 0x02 never
  occurs, so PS2 flags are 0, or 2 (double sided) for polygons whose texture has transparency (leaf cards). Guess.
* Flat shading (0x01) occurs in no disc model, so it is turned into smooth (96 of 269 Ruined Village models). Guess.
* LOD links (0x0209) are dropped; quads are split into triangles; UV/row order is copied unchanged (PC rows are bottom-up like PS2;
  inferred from OpenDrakan which uploads unflipped).
* Colour key transparency: key colour -> alpha 0. 32 bpp PC byte order is BGRA per OpenDrakan (none in the OotF data).
* Animated textures: frames (consecutive ids, PC flag 0x20) get consecutive PS2 ids and the 0x20 flag in the PS2 flag byte; the
  PS2 meaning of that byte is unverified.
* The texture `material` ref and the f32 at header +44 are left at 0 / 4.0.
* PS2 memory: nothing limits the size of a level TXD (Ruined Village 5.4 MB, install average 1.8 MB per container).

## API

```ts
const b = new OotfAssetBuilder(src /* PcFileSource */, { texture: { mode: 'auto' | 'indexed' } });
await b.prepareLevelTextures(pcLevel);             // terrain textures -> b.mapTexture (ConvertOptions.mapTexture)
await b.registerOwner('Common/NPC/NPCs');          // reads NPCs.db so (db, id) refs of that ODB resolve
const ps2Id = await b.addModelRef(owner, pcDb, pcId);   // or addModel(containerBase, id); null = not converted
b.mapModelRef(pcDb, pcId, owner);                  // sync lookup, PS2 model ref (db 0 = the level's own MOD)
const { files, report } = b.build('files/ENGLISH/LEVELS/LEVEL9AB', 'LEVEL9AB'); // .MOD .TXD .DB
```

Everything a model needs is copied into the one TXD, so converted models use texture `db 0` and the `.DB` is
`version 1 / dependencies 0`. One builder across several levels with `build('files/ENGLISH/COMMON/OOTFCOMN', 'OOTFCOMN')`
gives a shared container (the LVL dependency table must then list it; not done here). Model ids start at 2 (group is 1),
texture ids at 1. Lower level: `parsePcModel`, `pcToNeutral`, `readPs2Neutral`, `writePs2ModelRecords`, `buildPs2Mod`,
`decodePcTexture`, `encodePs2Texture` (8 bpp when <= 256 colours or graded alpha, else 16 bpp 1:5:5:5; `indexed` forces
median-cut 8 bpp; sizes rounded up to powers of two, max 256).

## Gaps

0x0211; in-game test; the class (0x0102) side that points a class at the new model id; sharing textures between levels in a
common container (the builder deduplicates only within one instance); mip/alternate/bump textures; alpha-map textures
(`flags & 1`, rejected); terrain textures larger than 256.
