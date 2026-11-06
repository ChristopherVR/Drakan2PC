# Issue 5: converted invisible walls drew as black boxes

Follows [Volcanic route](../2026-11-06/ootf-volcanic-route.md) ("Still open" 4). Probes and runs are private under
`build/harness/ootf-invis/` (`clearobjs.mts` lists the PC objects, `ps2clear.mts` / `ps2flags.mts` / `ps2users.mts` survey
the disc, `run.py` + `recipes/` the game runs). Nothing from either game is quoted here.

## How each engine makes a model invisible

* **PC.** No flag: the "invisiblewall" model (a unit box, 6 quads) and its kin ("invisible key lock", the Kill Box,
  Bounding Box, Invisible Wall, death trigger, Air Current and Powerup Mover classes) use one texture whose every pixel
  has alpha 0 (64 x 64, alpha channel, 4 bit alpha). 119 placed objects in the 13 campaign levels draw only such textures.
* **PS2 disc.** Two forms, both in WORLDCON: "invisiblewall" (796, 12 triangles, display list, texture 401: 16 bpp
  1:5:5:5, flags word 1, every alpha bit 0) is used by shootable rfl 17 targets (Rope Target, Bouquet Target, Force Crystal
  Target, Dummy Building); "invisiblewall(no_polies)" (1124, no polygons, bounds only) by static walls, grab boxes and
  damage boxes. The null models of RESOURES use the same 16 bpp clear texture (6028).
* **Converter before the fix.** The clear texture was written 8 bpp, flags word 0, palette alpha 0. On the disc no 8 bpp
  texture with transparent pixels has flags word 0 (they carry 8, a few 1 or 4); in game it drew as a black box.

## Fix (formats `7553750`)

`encodePs2Texture` writes a wholly transparent texture in the disc's 16 bpp flags-word-1 form (forced 8 bpp, used for
skinned models and fast-draw weapons, keeps the palette with flags word 8). Models keep their polygons, draw list and
bounds, so collision is unchanged; this matters for 2366, which the arrow has to hit (shootable like the disc's targets,
so the polygon form, not the no-polygon one). Tests: "writes a wholly transparent texture like the disc invisiblewall
..." and "keeps the polygons, draw list and collision of an invisible wall ..." in `ootftex.test.ts`.

A full 23-level pack from formats `918de7e` with and without the change differs only in three TXD files (FX, RESOURES,
PROCTEES; 21 textures to 16 bpp, one 8 bpp texture to flags word 8); every LVL, MOD and ODB is byte-identical, so the
arrow hit on 2366 checked in `vr11` (same converter, same MOD) still holds.

## In game (`dist/DrakanPC/drakan.exe`, direct starts, god mode, each run on the new pack and the base pack)

| Run | What | Base pack | New pack |
| --- | --- | --- | --- |
| `vol-*` | Volcanic, placed by Bounding Box 2509 (same converted model as 2366) at the cage, looking at it | the whole view is black | the cage and Arokh behind it are drawn |
| `suc-*` | Succubus Lair, placed 2,100 units from Invisible Wall 3483, W held 250 ticks | black box over the right half of the view | the scenery behind is drawn |
| `suc-*` | same walk: where Rynn stops | z 175428 | z 175095 (the near face of the wall at about 174890, taking one model unit as 1,024), with the same sideways slide; she does not pass it in either |
| `suc-*` | Invisible Wall 3474 (doorway) | black panel in the door | doorway drawn; stop position 152179 / 152178 |

Not shown: 2366 itself after the breath. The quicksave taken after the cave-mouth dismount (`card-sanctum`) stalls in its
load on both packs (watchdog, no change of ours involved), and the full route was not replayed. The Volcanic walk into
2509 gave no usable stop position (the dumps did not follow the input), so blocking is shown in Succubus Lair only.
Possible follow-up: 43 converted 8 bpp textures with holes (cut-outs) also carry flags word 0; if that is what drops
their alpha, their transparent parts draw black too.
