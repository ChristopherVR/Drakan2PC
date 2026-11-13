# OotF mountains and snow: pixelated, low-poly, not like the PC (2026-11-14, round 5)

Owner's report: in Order of the Flame, mountain and snow terrain textures sometimes look pixelated, low-poly and not
correct compared with the original PC engine. Scratch (not committed): `build/review/ootf-terrain-look/` (`probe/*.ts`
data probes, `packs/{before,slot55,after}` conversions, `runs/` game runs, `ev/` cited pictures).

## What was run

* Play folder `build/review/ootf-terrain-look/play`: a copy of `build/review/baseline-play` (owner's `drakan.ini`: DLSS
  quality, `hd_textures = 1`, anisotropic 16, texture upscale auto), the dist exe of 14 November and the dist HD texture
  folder, the `elf =` line on a private ELF copy. Deviations: a 3840 x 2160 window (client 3818 x 2032, internal scale
  auto = 4) instead of borderless fullscreen, audio off, god mode, a direct level start, `place` with a fixed yaw for
  the views (placed, not played). Other agents' games ran on the same GPU in every run.
* Packs: `before` = the converter at formats `4f2892d` (current main), `slot55` = the same with only
  `DRAKAN_TERRAIN_SLOT=55`, `after` = formats `33db378`. Full campaign conversions (23 levels), same subtitle inputs.
* Wartok Canyons, four views, two pitches each, per pack. An Islands tour was run too but the chosen spots put Arokh
  under the sea (view points from PC heights over the wrong cells), so it shows nothing about terrain and is not cited.
* PC reference: the PC game was **not** run. It is a 1999 Direct3D game that switches the display mode and needs its
  disc image mounted; on a desktop shared with five agents' game windows that was not safe. The comparison is against
  the PC data instead (`probe/pc_cells.ts`, `probe/uvpat.ts`).

## Causes found

### 1. Every converted ground texture was stretched over 2 x 2 cells

The PS2 engine takes the tiling of a ground texture from its material (texture header +40, a `PROCTEES.ODB` class):
"Base Tiling Size" is how many cells one tile spans (disc grass and dirt 1, snow 2, outdoor rock 4,
`docs/reference/graphics.md`). Converted textures carried 45 "Blue Snow" (the one value found to render on 31 October),
whose tiling is 2. On the PC every terrain cell of every campaign level maps its texture exactly once (UV span 0..1 on all
cells; 95 % in the plain orientation, the rest rotated or flipped). So the converted ground was drawn at twice the PC
size and half its texel density (64 px PC tile, enlarged to 128 px, over two cells), and the rock and snow edges painted
into the PC tiles landed across two cells instead of one.

Fix: material 55 "Patchy Snow" (tiling 1, the same splat fade of 30 cells and noise fade 30, no "solid colour base",
snow foot-steps like Blue Snow). Seen in game: `ev/w2_before_slot55.jpg` (Wartok, snow slope by the river, the only change
is the material: the tiles are half the size and the PC's rock and snow drawing becomes visible) and
`ev/w1_before_after.jpg` (matched pose on the canyon floor: rock grain twice as fine). Pass count is unchanged (same fade
distance), the texture pixels and their HD images are unchanged (the HD hash is over pixels, not the header).

### 2. Far terrain was a mosaic of flat cells

Beyond its splat fade distance (30 cells, the weight falls linearly from the camera) a chunk is drawn only from the
level's far colour map (`<level>TS.TXD`, 4 texels per cell). The converter wrote one flat colour per cell there. The disc's
maps are a small picture of the blended ground: only 9.2 % of the 4 x 4 cell blocks of `LEVEL1TS.TXD` are one colour
(`probe/tsblocks.ts`; picture `ev/disc_ts20.png` against the converted Wartok map `ev/conv03_ts1.png`). Order of the Flame
paints nearly every cell with a texture of its own, so distant slopes drew as hard flat squares and, on steep cells, flat
triangles of snow and rock, the "low-poly, pixelated" mountains.

Fix (`buildTerrainPoolTxd`): each texel holds the blend the near passes draw: the textures of the cells round each corner
of the cell weighted by how many of the corner's cells use them (the blob's vertex weights), interpolated across the cell,
each texture taken from its own picture shrunk to 4 x 4 (`cellTexelsOf`, `TerrainTextureBuilder.texelsOfRef`). New map:
`ev/after03_ts1.png`. In game: `ev/w2_peak_three.jpg` (Wartok, the peak behind the river: before and `slot55` show the
flat-triangle mosaic, after shows rock with snow streaks and soft transitions).

### 3. Lighting lines at layer borders

Converted levels are cut into hundreds of small layers (Wartok 441, about 9 x 9 cells), so about a third of their
vertices lie on a layer border. The converter computed a border vertex's normal from the layer's own heights with a
clamped neighbour, i.e. half the slope, so borders were lit flatter (brighter on shaded slopes) than the ground beside
them. Now (`groundBeyond`, `TerrainBlobOptions.heightBeyond`) the layer that holds the same border vertex at the same
height (within 64 units, so a cave floor or a ceiling never counts) gives the neighbouring height, however steep; with no
such layer the slope is one-sided. Disc rewrites keep the old rule. Unit tested; **its share of the in-game change is not
isolated** (the after run carries all three changes). The disc's own normals are not pure geometry (mean 9 to 16 degrees
from central differences inside layers, 21 to 34 at borders), so they are no reference for this.

## Checked and not the cause

* Texture sizes: no PC terrain texture is shrunk (`ootf-texture-quality.md`); filtering and anisotropy as for the disc.
* PC detail textures: the PC terrain materials (Rock, Snow, Grass) have no detail texture (class default 0); only "Stone
  Material" names one, on 1 to 4 % of cells. The PS2 materials have none either.
* PC cells with two textures (left and right triangle): 0.1 to 2.3 % of drawn cells per level; the PS2 keeps one per cell.
* HD pack: terrain tile images are made from the PC pixels (`ootf-texture-quality.md`); the far maps change pixels, so
  their HD images must be made again (the HD step does it for a new pack).

## What stays different from the PC (PS2 engine)

* The PS2 draws terrain as splats: a cell's texture fades into its neighbours from vertex weights; the PC draws each
  triangle with its own texture and hard edges, rotated per cell. Transition tiles (snow edge, path edge) therefore look
  softer and their rotation is not kept.
* Beyond about 20,000 units a ground texture has lost a third of its weight to the 4 texel per cell colour map; the PC
  draws the full texture to its view distance. `[video] terrain_distance` (1 to 4) pushes this out at a cost in passes.
* The geometry is the PC's own 2048-unit grid: facets on steep peaks are in the PC data too.
* The 8 textures per chunk limit (`limitWindowTextures`, `limitChunkTextures`, 8 to 27 % of cells repainted) still
  replaces some snow or rock cells with a neighbour's texture; not touched here (owned by the chunk work).

## What the owner needs

A rebuilt pack (`python scripts/buildtool/extras.py ootf ...` with formats `33db378`), then the HD step for the pack (the
far maps have new pixels; the tiles keep their images), and a new campaign game (packs do not load older saves). The exe
does not change.

## Not verified

* PC reference pictures of the same places (PC game not run).
* The normal change on its own in game; levels other than Wartok Canyons in game (Islands, Grotto, Alwarren snow);
  the after pictures with HD images of the new far maps (the runs used the live upscaler for them).
* Frame time (no measurement; the change adds no draws).
* Foot-step sounds on the new material (both materials are snow; not listened to).
