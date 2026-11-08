# OotF terrain draws pink in the distance (2026-11-08)

User report: "Some textures in the map show as pink in OotF." Levels and surfaces were not named.

Runs are private under `build/ootf-run/pink/` (the user's pack `dist/DrakanPC/mods/ootf-levels.zip`, build `321463ed808626e8`)
and `build/ootf-run/pinkfix/` (the same 23 levels converted with the fix). Every run used `ootf_level_run.py --campaign
--no-convert` from `dist/DrakanPC` (its `drakan.ini`: D3D11, HD textures on), a direct start (`DRAKAN_START_LEVEL`) and god
mode, holding `build/ootf-run/game.lock`. Pink was counted per screenshot as pixels with R and B above 150 and G below 0.6 of
both. Nothing from either game is quoted here.

## Where

A 13-shot look-around (12 turns of 30 degrees, up, down) in Intro, Ruined Village, Wartok Canyons, Islands and Volcanic:
only Wartok Canyons showed pink, on the cliffs and peaks in the distance (the start view: 3,997 to 4,277 pink pixels per
shot at 960 x 540). The pink areas are flat, shaded only by the lighting, with no texture detail, like all distant terrain
of the converted levels (the far cliffs next to them were flat tan).

| Wartok start view | pink pixels |
| --- | --- |
| HD textures on (`a00`, `a01`) | 4,277 + 662 |
| HD textures off (`GSR_TEXREPLACE=0`) | 3,991 + 299 (+ 173 up, 161 down) |
| HD off, three shots of the still start view | 3,997 each |
| palette HD off (`GSR_PALETTE_HD=0`), LOD bias -12, ray passes off | 3,996 / 3,995 / 3,996 per shot |

So not the HD pack, the palette-HD path, a mip level or the ray passes.

## Root cause

* `GSR_SURFACE_PICK` on a pink pixel of the still start view (epoch 600, inside the 357 to 842 window of that view) named the
  front triangle: a PSMT8 64 x 64 texture at TBP 12288, vertex colour about (124, 120, 95), fog 211.
* A temporary decode dump (not committed) wrote every decoded texture with more than 20 % pink: in Wartok, 64 x 64 PSMT8
  textures with (248, 100, 248) blocks on a 4-texel grid. The first one is byte for byte texture 3 of the level's
  `OOTF03TS.TXD`, which the converter copied from the template's `LEVEL1TS.TXD` (all 23 levels shipped the same 1,723,010
  byte file).
* What that TXD is: texture n of `LEVEL1TS.TXD` is the **far texture of terrain group `seq` n** of `LEVEL1NA.LVL`. 50 groups,
  50 textures (ids 1..50), and every texture is `pool x pool` of its group (64 / 128 / 256). Placing each cell's texture mean
  at `x_lo + 4 cx`, `z_lo + 4 cz` (stored row order) matches the disc (8,577 sampled cells: chroma error 0.035; 0.12 to 0.15 for
  the flipped placements; brightness ratio 0.97 to 1.01, so no lighting is baked). Texels no layer uses hold the magenta filler
  (seq 3: 56 x 60 used of 64 x 64, the pattern of the dumped texture). The engine draws distant chunks with these textures.
* Converted levels have their own groups and slots (`planTerrainSlots`, all pool 256 in Wartok), so their far terrain sampled
  Surdana's baked colours everywhere and the filler wherever a converted slot lay in Surdana's unused pool area. A census of the
  user's pack (each drawn cell's far texel under the template TXD): 12 to 35 % of drawn cells per level land on magenta
  (Wartok 17.9 %, Islands 14.0 %, Volcanic 22.2 %, Rift Crystal 30.7 %, Ruined Village 15.0 %, Intro 14.8 %; every campaign level
  and every arena except Rendezvous). Whether it shows depends on the view distance, which is why only Wartok's start view
  showed it in these runs.

Not the cause: every converted model, terrain and TXD texture decodes; no `[hle] ... not found` texture resource; no converted
terrain palette has a pink entry; no cell refers to the splat dependency (all use Proc Textures, db 9). The animated frame
lists of formats `8dcc627` are not in the user's pack (built 15:03, the commit is 15:44); in a pack with them every own-TXD
model texture ref resolves (RESOURES 7,278 of them, up from 5,139) and the runs below show no pink.

## Fix (formats `3e7d62f`)

`buildTerrainPoolTxd` (`ootfterrain.ts`) writes the level's own `_splats` TXD: per group `seq` a `pool x pool` texture
(id = seq) with each drawn cell's texture mean in its 4 x 4 block at the blob slot, holes, invisible cells and spare texels in
the group's mean colour (no filler colour to bleed into), 8 bpp, header as the disc's far textures (flags 0, +40 = 0,
+44 = 256.0). The slots come from `planTerrainSlots` over the same layer list `ps2BlobBuilder` uses.
`TerrainTextureBuilder.meanOfRef` gives the means (converted textures from their source pixels, the template's own entries
decoded once). `convertOotfLevels` ships it per level as `splats.TXD`; the template's `.DB` stays. Three Islands groups hold one
layer wider than the 256 pool and get a 512 texture (the disc never has one); the Islands load and draw.

Tests (`ootfterrain.test.ts`, synthetic): one texture per group at the blob slots, 4 texels per cell in stored order, holes and
spare texels in the group mean, never the magenta filler, the disc header; `meanOfRef` for a template entry.

## Verification

The fixed pack (`pinkfix`, 23 levels): the census finds 0 drawn cells on magenta in every level. In game:

| run | before | after |
| --- | --- | --- |
| Intro, Ruined Village, Islands, Volcanic, 13 shots each, HD on | 0 | 0 |
| Wartok, 13 shots, HD on | 4,939 | 0 |
| Wartok, 13 shots, HD off | 4,624 | 0 |
| Wartok, still start view, 3 shots, HD off | 11,991 | 0 |
| Volcanic, 13 shots, HD off | not run | 0 |
| decoded textures with more than 20 % magenta after the Wartok load | 3 (its far textures) | 0 |

(The 128 x 128 Drakan 2 tile textures with filler that the decode dump also lists come before the converted level, at the same
addresses in both runs, and are never in a shot.) `build/ootf-run/pinkfix/wartok_before_after.png`: the pink peaks and the
pink-tinted cliff of the start view are grey-brown rock, with snow on the far peaks where the PC terrain has it.

## For the player

Regenerate the pack (`editor/scripts/ootf-convert.ts --campaign 1`, the editor, or the harness). The HD textures do not need
regenerating: the new far textures are flat colour blocks the game draws natively; `ootf_hd_pack.py` on the new zip only adds
upscaled copies of them.

## Open

* Distant converted terrain is now the right colour but still flat (one colour per cell), as on the disc.
* The 512 far textures of the three wide Islands groups load, but their far drawing was not looked at from a distance.
