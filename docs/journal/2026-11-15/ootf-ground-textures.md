# OotF ground textures: the stitched look, Drakan 2 ground, bridges, burnt houses, mountains (2026-11-15)

Owner's request (Order of the Flame, Ruined Village first): the ground looks stitched together instead of flowing; can the
Drakan 2 ground textures of the player's disc replace the OotF ones; the bridges, some burnt houses and the mountains look
old. Scratch, not committed: `build/review/ootf-textures/` (`probe/` data probes and contact sheets, `packs/` conversions,
`runs/` game runs, `ev/` cited pictures, `play/` private play folder).

## 1. Why the ground looks stitched

The PC paints nearly every terrain cell with its own tile, picked at random among up to six variants of a surface (Ruined
Village: six brown rock-plate variants on 17,000 of its 20,700 ground cells, six grass variants, then transition tiles), and
draws every triangle with hard edges, so on the PC the variants meet like floor tiles. The PS2 engine never draws a cell
with its own texture alone: each texture is a splat pass whose weight at a vertex is how many of the four cells round it use
that texture (`docs/reference/level-format.md` 10.1), so a cell's texture fades into all eight neighbours. With random
variants a texture reaches full weight only where all eight neighbours share it:

| Level | Cells drawn at full weight (all 8 neighbours same texture) | Textures per 3 x 3 window |
| --- | --- | --- |
| Disc Surdana (LEVEL1NA) | 41.5 % | 1.50 |
| Disc Kragmor (LEVEL2RE) | 13.9 % | 1.88 |
| Ruined Village, converted OotF ground | 0.6 % | 4.70 |
| Wartok Canyons, converted OotF ground | 1.8 % | 4.97 |
| Ruined Village, Drakan 2 ground (this change, final pack `d2g`) | 54.1 % | 1.21 |
| Wartok Canyons, Drakan 2 ground | 35.2 % | 1.48 |
| Intro, OotF ground / Drakan 2 ground | 17.3 % / 83.1 % | 3.78 / 1.07 |
| Grimstone Mines, OotF / Drakan 2 | 8.0 % / 25.1 % | 3.99 / 1.42 |
| Grotto, OotF / Drakan 2 | 23.1 % / 37.5 % | 2.98 / 1.28 |
| Islands (not mapped), both packs | 17.4 % | 4.28 |

(`probe/fullw.ts` over the packs.) Every cell is therefore a half-blend of four to nine different pictures, all aligned to
the 4 m cell grid, with the far colour map showing through: a patchwork of squares with straight seams, darker and lighter
cells, the "stitched" look (`ev/d2a_village.jpg` left, `ev/b_vs_d2b_ruined756.jpg` left: straight diagonal and square patch
edges on flat ground).

Checked and not the cause: per-cell rotation (only 13 of 2,386 Ruined Village transition cells and 7 % of variant cells are
rotated or flipped on the PC; the variants are interchangeable); the 8-textures-per-chunk repaint (`limitWindowTextures`
changes about 1 % of cells, the chunk repaint is off and the pack has wide chunks); HD seams (a uniform field of one OotF
variant would not show the patchwork; the images are wrap padded, `ootf-texture-quality.md`). Not separately measured in
game: HD off at the same spot.

## 2. Drakan 2 ground in place of OotF ground (default on)

Whole OotF ground families map onto one Drakan 2 `Proc Textures` entry each (formats `DEFAULT_PS2_TERRAIN_FAMILIES`,
`ootfps2tex.ts`): a family is a surface's variants plus every transition tile whose larger part is that surface (pixel
majority against the family colours; grass tiles with a thin earth trail, 60 % green or more, stay grass, because one PS2
cell of earth blended into its neighbours draws the trail two to three times as wide as the PC; snow on rock and rocky
tracks in snow join the snow or the track from 35 %). The cells then name the disc's own entry in the template's
`PROCTEES.TXD` (no copy, no new texture id), so it draws with its own material (tiling, fade distance, foot-step sounds,
noise pass), its disc HD image and the template's texture order. The PS2 engine then blends only where the surface really
changes, as on the disc. Mountain World only (Intro, Ruined Village, Wartok Canyons, Grimstone Mines, Grotto: container
`Mountain World Common TD`, 244 of its textures, 103,673 cells over the campaign); Islands, Volcanic and Rift World keep
their OotF ground (not reviewed).

| OotF family (Mountain World Common TD ids) | Drakan 2 ground (PROCTEES id, material) | Why |
| --- | --- | --- |
| Grass: meadow variants 2-7, grass-majority grass/rock edges, grass tiles with thin trails, dark grass 361-365, 408 (48 tiles) | 537 "Grass", tiling 1 | meadow grass with blades. "Mossy Green" (593), nearest in colour, read as mossy rock in the Ruined Village dusk (`ev/tri_start_ground.jpg`, middle) |
| Brown rock plates: variants 9-14 and 222-241, 395-397, 411-412, 431, rock-majority edges to grass and snow (93 tiles) | 621 "Brown Rock Outdoor", tiling 4 x 4 cells | weathered cracked rock for the ground and mountain sides; "Mud" (13719) matched the colour but drew slopes as dirt (`ev/tri2_bridge147.jpg`, right) |
| Snow: 16, 466, snow-majority edges, rock tiles with 35 % snow or more (26 tiles) | 563 "Blue Snow", tiling 2 | the snow of Surdana, Ravenshold, the Tundra |
| Dark earth paths: 120-123, 206-209, wide path edges 127-145, 156, 159, 168-171 (30 tiles) | 13719 "Mud", tiling 2 | dark wet earth, nearly the OotF path colour |
| Grey rocky tracks through snow: 272-335 (47 tiles) | 535 "Rock - Gray (Outdoor)", tiling 4 | the grey mountain rock of the disc's snow levels |
| Kept OotF: 1 (placeholder), 270, burnt black ground 367-375, brick and stone floors 399, 410-418, 456-465, lava 404, mine crystals 432-435 | | no fitting Drakan 2 ground |

Switches: `--ps2-terrain 0` keeps the OotF ground (`Ps2TextureSwapOptions.terrain`), `--ps2-textures 0` turns off every
Drakan 2 texture. Rows of `--ps2-texture-map` that name `WORLDCON/PROCTEES/PROCTEES.TXD#<id>` edit ground textures (add or
redirect a tile); `"ps2": null` on a family member keeps that OotF tile. Example used for the comparison packs:
`{"ootf": "Mountain World/Mountain World Common/Mountain World Common TD#2", "ps2": "WORLDCON/PROCTEES/PROCTEES.TXD#537"}`.
A base-TXD id missing from the template falls back to the converted OotF tile. Far colour maps take a Drakan 2 entry's
picture shrunk to one cell, like a converted tile (a first version used its mean colour, and distant Wartok peaks drew as an
even bright tan, `ev/wcg_wc_e.jpg`).

Side effect: far fewer textures per chunk. The in-run frame counters (owner's ini, other agents' games on the GPU, not a
quiet-machine benchmark) went from 65 to 92 and 81 to 114 fps at two Ruined Village spots and from 51 to 86 fps at the
Wartok Canyons start. The pack shrinks (178.1 to 173.5 MB, 244 fewer converted ground textures).

## 3. Bridges, burnt houses, mountains (object textures, default on)

Rows of `DEFAULT_PS2_TEXTURE_SUBSTITUTIONS` (byte-for-byte disc copies, square to square, so the existing aspect and HD rules
hold). The Ruined Village bridges that still looked old are the arched stone bridges (`Human Bridge`, `Broken Human
Bridge`); the rope bridges were redrawn in round 8.

| OotF texture (Mountain World Common unless named) | Drakan 2 texture | Used on |
| --- | --- | --- |
| #5334, #5335 soot-black brick | WORLDCON.TXD#355, fire-blackened stone block wall | burnt houses (SmallHouseBurnt01, House Ruined) |
| #5327 stone block wall | WORLDCON.TXD#444, dressed stone blocks, 256 px | village house walls, stone bridges |
| #5351 stone with a dirty base | WORLDCON.TXD#469, damp mossy stone blocks, 256 px | stone bridge sides |
| #5350, #5354, #5355 cobbles | LEVEL1ON.TXD#690, dark Surdana cobbles | stone bridge walkways |
| #3193, Ruined Village #3193 and #4582 brown rock | LEVEL2RE.TXD#497, tan weathered rock, 128 px | cliffs, Village Rocks, Dragon Falls rocks, Arch Hall rocks, flat walls: matches the Drakan 2 ground rock |

Tried and left out of the defaults because it was never seen on screen: #5337 charred planks to WORLDCON.TXD#328 (dark scorched planks); add it with `{"ootf": "Mountain World/Mountain World Common/Mountain World Common#5337", "ps2": "WORLDCON/WORLDCON/WORLDCON.TXD#328"}` in `--ps2-texture-map`. Kept OotF (no Drakan 2 picture with the same layout): the half-burnt plank and thatch pictures (#5336, #5340, #5200,
#5201), charred log ends (#5333, #5213), the stone with a grass foot (#5352, #5353).

Mountains: their slopes are the brown rock family above, now Drakan 2 rock tiled over 4 x 4 cells (and the rock models'
texture follows). What else could be done: the far colour map still holds 4 texels per cell (`terrain_distance` 2 keeps the
ground textures twice as far, about twice the terrain passes, now much cheaper); `terrain_cliffs` (side projection on steep
faces) and `terrain_detail` are off by default; for OotF pictures that stay, HD regeneration is already the Real-ESRGAN
path of `ootf-texture-quality.md`.

## 4. Evidence (in game)

Play folder `build/review/ootf-textures/play`: `build/final-game/bin` (exe of 15 November 11:21), the owner's
`dist/DrakanPC/*.ini` with the `elf =` line on a private ELF copy, the dist HD folder hard-linked (new images only added;
dist files untouched, checked by time stamps) plus the HD step (`hd_pack.py modextract` + `upscale`, private work dir) for
each pack's new far maps. Deviations: shots at 1280 x 960 (the ini's window size), audio off, god mode, a direct level
start on an empty card, `place` with fixed yaw (placed, not played). Packs: full 23-level campaign conversions from one
formats tree with `DRAKAN_TERRAIN_CHUNKS=wide` and the subtitle files: `before` = `--ps2-terrain 0` (old ground), `d2b` to
`d2g` = Drakan 2 ground variants (`d2f` final ground plus object rows, `d2g` = `d2f` plus the five rock/snow tiles moved to
snow, seen only in Wartok), `d2h` = the final tree (far maps from the Drakan 2 pictures) still with the plank row that was then
taken out of the defaults; `d2h` is the pack kept in `packs/`).

| Picture (before left) | Seen |
| --- | --- |
| `ev/b_vs_d2b_ruined756.jpg`, `ev/tri_ruined756.jpg` | Ruined Village by the ruined house: the square patchwork and straight patch seams are gone; one continuous grass field |
| `ev/tri_start_ground.jpg` | start, looking down: OotF grass, Mossy Green, Drakan 2 Grass (chosen) |
| `ev/tri2_mountains_n.jpg`, `ev/b_vs_d2b_mountains_n.jpg` | village square: the PC's earth square now reads as a mud square with a grass edge; mountain behind in Drakan 2 rock |
| `ev/final_bridge147.jpg` | stone bridge: block-built sides and cobbles instead of a smooth brown smear; the river bank is rock |
| `ev/final_burnt377w.jpg`, `ev/final_burnt5.jpg` | burnt houses: the corner block and walls show stone blocks, the burnt wall is soot black |
| `ev/h_gr_all.jpg` (pack `d2h`) | Grotto cave from Arokh: the floor's grid of squares is gone (one snow floor), walls and ceiling are Drakan 2 rock instead of dark brown blur |
| `ev/h_rocks689.jpg` (pack `d2h`) | Ruined Village falls: rock models and rock ground in the same tan Drakan 2 rock |
| `ev/h_wc_e.jpg` (pack `d2h`) | Wartok with the far maps taking the Drakan 2 pictures: unchanged from `d2g` at this distance; the left peak is near-pass rock, an even tan |
| `ev/wc_all_pairs.jpg`, `ev/wcg_wc_e.jpg` | Wartok Canyons in daylight from Arokh: grass, rock and slopes blend without seams; the peak's snow streaks of the OotF rock tiles (10 to 30 % snow) become plain rock and the distant peak is a bright, even tan from the far colour map; the snow fields stay snow |

Not seen: Grotto, Grimstone Mines, the Intro; the rock models' new texture close up (the views were too dark); played
movement and foot-step sounds on the new materials; a frame-time benchmark on a quiet machine.

## 5. Code (formats submodule and converter)

* `ootfps2tex.ts`: `DEFAULT_PS2_TERRAIN_FAMILIES`, `ps2TerrainRows`, `effectiveTerrainSubstitutions`, `PS2_TERRAIN_TXD`,
  `Ps2TextureSwapOptions.terrain`; the object rows above.
* `ootfterrain.ts`: `TerrainTextureOptions.native` (cells use the base TXD id instead of a converted tile),
  `TerrainTextureReport.nativeCells`.
* `ootfpipeline.ts`: passes the ground map to the terrain builder and logs `drakan 2 ground: <cells> cells`.
* `editor/scripts/ootf-convert.ts`: `--ps2-terrain 0|1`.
* Tests (synthetic): `test/ootfps2tex.test.ts` (family table rules, switches, edits), `test/ootfterrain.test.ts` (base
  entries used instead of converting, fallback, report, far-map rule).

## 6. Open

* `--ps2-terrain 0` keeps the stitched look. A fix inside the OotF art would bake a family's variants into one 4 x 4 cell
  tile (256 px) with a tiling-4 material; not built.
* Islands, Volcanic and Rift World ground families (345, 125 and 52 textures) are not mapped.
* The burnt black ground (367-375) stays OotF and meets Drakan 2 grass with a soft seam.
* The editor's import dialog lists only object rows, not the ground families.
* Needs a regenerated pack, the HD step for it (new far maps) and a new game; the exe does not change.

## 7. Black patches with HD textures off: opaque copies of the Drakan 2 rock (2026-11-15 afternoon)

Follow-up to `ootf-final-check.md` item 9b. Scratch, not committed: `build/review/ootf-opaque/` (`play/` private play
folder: `build/final-game/bin` of 12:47, the owner's ini with `elf =` on a private ELF copy, `textures/` hard-linked from
dist with only new files added; `packs/`, `runs/`, `ev/`, `tools/`, `hdwork/` + `hdout/` the private HD step).

**How Drakan 2 draws 621: the same way, and with the same fault.** 621 "Brown Rock Outdoor" (material 92) and 535 "Rock -
Gray (Outdoor)" (material 16) are 32 bpp with alpha selector 7 and feature bit 0x02; 621's raw alpha is 0 on 59 % of
the texels and never above 0x7f; 535's raw alpha is 2 to 0x7e (no alpha-0 texel, 14 % below 0x10). Both keep a brown or grey colour under alpha 0 (621: mean
130, 104, 68 at alpha 0). The disc uses them as ground: 535 on 29,416 cells of LEVEL1NA and in every level, 621 in LEVEL1IR
(Inquisitor's Lair, 342 cells, cave floor and ceilings), LEVEL0RO (155), LEVEL1ED (74), LEVEL5NE (32), LEVEL4LD (12)
(`tools/scan621.ts`). The
converter already names the disc entry itself, so there is no other material or flag to copy. Inquisitor's Lair from the
disc alone (OotF pack off) with HD textures off shows the same black, blocky patches on the 621 ceilings
(`ev/disc_ir_hd0_texup.jpg` left, `runs/d2ir_hd0`, `runs/d2ir_hd1` for HD on, where they are gone).

**Cause: the live texture upscale, not the texture.** With `GSR_TEXUP=1` (`texture_upscale = 1`, live upscale off) and HD off,
the same disc view has no patches (`ev/disc_ir_hd0_texup.jpg` right); that is the one measurement. The rest is code
reading, not traced per draw: the owner's `texture_upscale = auto` runs (inferred: the default filter picks smooth for
this picture, not xBRZ) `smoothUpscale` (`runtime/gs/src/gs_texup.cpp`, Catmull-Rom on alpha-premultiplied colour) on native textures without
an HD image. Its un-premultiply sets RGB to 0 wherever the interpolated alpha is near 0 (`inv = 0`), so the upscaled
621 is black under its transparent texels (and darkened under 535's low-alpha ones). The splat pass draws the colour without texture alpha (only the noise pass
uses it), so those texels are drawn black. This is gap 2 of `docs/reference/hd-textures.md`, in the live filter. The
HD image keeps the colour under alpha 0 (`hd_pack.py` bleeds it). It has the same alpha as the disc entry (mean 39,
93 % of the texels below 128), not opaque as `ootf-final-check.md` inferred, and draws fine.

**Fix (converter, formats).** `TerrainTextureBuilder` gives a `native` ground entry that is drawn with texture alpha and is
not fully opaque an opaque copy: header byte-identical (size, selector, feature byte, material at +40), colours
unchanged, every alpha 0x80, under the next free id of the pack's `PROCTEES.TXD`, once per pack; the cells and the far
maps use the copy (`opaqueTextureCopy`, `TerrainTextureOptions.nativeOpaque`, default on; dev knob
`DRAKAN_TERRAIN_NATIVE_OPAQUE=0`; report `nativeOpaque`, log `drakan 2 ground, opaque copies: 621 -> 18208, 535 -> 18212`).
The disc entries stay byte-identical (checked in the Ruined Village pack: every base entry unchanged, the level's
splat TXD unchanged against the old pack), because Drakan 2 levels draw them while the pack is on. It applies to
`--ps2-texture-map` ground rows as well. With HD on the look stays the same: the converter hands the copy's HD source
(`--hd`, `<pack>.hdsrc`) as the disc entry's own picture with its alpha, so `hd_pack.py modextract` + `upscale`
make the copy's HD image from the same source (needs `--hd`, as the play pack build uses). For 621 (`edba3646d79f303b`) and 535 (`12860babfc7bc797`) the result is
pixel-identical to the disc images `7cce34f95c72d01c` and `6701dd7f02aeed37`, and the runtime uses an exact-hash image's
alpha as it is (code reading, `gs_texture_cache.inl`; the run log does not name the hash used). Without that source, the RGB alias would give the disc image with the copy's alpha (opaque rock).
Ruined Village uses only 621; Wartok Canyons uses both.

| Picture (before left, after right) | Seen |
| --- | --- |
| `ev/rv_hd0_before_after.jpg` (`runs/rv_before_hd0b`, `rv_after_hd0`) | Ruined Village, HD off, tree 60 and the two 1845 views of item 9b: the black patches on the rock slope and the brown ground are gone; rock and ground are brown rock |
| `ev/rv_hd1_before_after.jpg` (`runs/rv_before_hd1`, `rv_after_hd1`) | the same views with HD on: no visible difference |
| `ev/wc_hd0_before_after.jpg` (`runs/wc_before_hd0`, `wc_after_hd0`) | Wartok Canyons, HD off, by a snow field with 535 and 621 cells (321422, 176469), four directions: black patches on the cliffs and dark specks on the rocky snow before, none after |

Deviations: placed views (direct start, god mode, audio off, `place` with fixed yaw), DLSS not loaded (the bin copy
has no `nvngx_dlss.dll`; DLSS plays no part, item 9b). The "before" packs are `ootf-final/packs/rv_t1` (formats
`da38fdb`) and `packs/wc_before` (this tree with the knob at 0). Not run: Intro, Grimstone Mines, Grotto (same table),
OotF with `texture_upscale = 1`.

Open:

* The runtime fault stays: `smoothUpscale` blackens the colour under alpha 0 for every alpha texture without an HD image,
  so disc levels show it with HD off (Inquisitor's Lair ceilings; 535, on most disc ground, was not looked at on the disc). A fix there (upscale the colour unpremultiplied where alpha is 0, as `hd_pack.py`
  does) would make the copies unnecessary. With HD off, the copies then draw opaque rock where the disc rock lets the
  layer below through.
* Splat passes draw in texture order; the copies have ids past the template's, like converted OotF tiles. No effect was
  seen.
* The play pack needs a reconversion and the HD step (two new images); the exe does not change.
