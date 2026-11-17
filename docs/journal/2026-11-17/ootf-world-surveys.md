# Drakan 2 textures and models for the OotF world: three surveys (2026-11-17)

Owner request: make the converted OotF levels look like Drakan 2 (mountain and rock textures, ground, buildings, props).
Three subagents surveyed the 13 campaign levels against the disc in parallel; each wrote candidate rows in the converter's
edit-file formats with side-by-side pictures and a trial conversion, and the lead merged and checked them in game.
Working files (game data, not tracked): `build/ws-tex/`, `build/ws-ground/`, `build/ws-props/`.

## Building, rock and wood textures (`ootfps2world.ts`, `WORLD_TEXTURE_SUBSTITUTIONS`, 69 rows)

* Ranked by polygon area times object scale over every level mesh and placed model; candidates from disc contact sheets.
* The best evidence: Drakan 2 ships remodelled copies of many OotF kits under the same names (hallway kit, pillars,
  Brick01 to 17, MArchway01b, the boulder, rope bridges); the disc texture each draws in the same slot is the disc's own
  choice for that surface. Exact copies of a picture across OotF containers each get the same mapping.
* 84 proposed: 38 high, 31 medium, 15 low confidence. Merged: the 69 high and medium ones. Left out: the low ones (the teal
  ruin rock and the evil knotwork band have no Drakan 2 counterpart; grey would change the evil ruins' tint).
* Limits met: one OotF texture maps to one disc texture whatever model draws it (`Tropical Common#5367` is drawn by the
  hallway and the ruin wall kits); arched doorways, ragged wall tops, doors and shutters have new UVs in the remakes and need
  a model swap instead; an object texture row also reaches terrain cells that draw the same texture.

## Ground (`ootfps2world.ts`, `WORLD_TERRAIN_FAMILIES`, 11 families)

* The Tropical World, Volcano World and Rift World ground containers had no family at all: Islands, Islands Night,
  Alwarren, Rift Crystal, Succubus Lair, Volcanic and Rift World drew 0 % Drakan 2 ground. With the families: 64 to 93 % (the
  rest is liquids and tiles kept on purpose: lava edges, glowing veins, kill tiles, placeholders).
* Transition tiles join the surface covering most of their pixels (8 px block votes against sample tiles).
* Rock: `K2-Rock - Indoor` (PROCTEES 6317, tiling 1): dark, no visible repeat from above, sharp on steep faces; the tiling-4
  outdoor rock would smear on steep cells (the steep-keep set is fixed to 621 and 535).
* Trial: Islands, Alwarren and Rift World 107,050 cells on Drakan 2 ground, exactly the predicted count, 0 on fallback.
* Open: the Drakan 2 rock, grass and floor are 1.6 to 1.8 times as bright as the OotF tiles (sand, tan and earth within
  10 %); the existing Mountain World brown rock family (621, tiling 4) could be A/B tested against 560 "Rust Rock".

## Props and dungeon pieces (`ootfps2worldprops.ts`, `WORLD_PROP_SUBSTITUTIONS`, 57 rows)

* WORLDCON carries a PS2 remake of the whole OotF dungeon kit ("(H) PS2" classes, same grid size and origin): 31 kit rows
  pass the size check unchanged and cover about 1,116 placed pieces (Rift Crystal, Islands, Alwarren, mines). Also the
  Troll Crate (176 placed, a remake under the same model number), iron-bound crates, mine cart wheels, cocoons, bed,
  anvil, columns and boulders (same mesh, disc textures), and with `fit` the chest and lid and the tables.
* The four kit classes that show two OotF models (Ceiling, Corner, Left Open, Right Open) get a row per model.
* Left out: Full Pillar and Boulder Damage Player (another class shows the same OotF model in Ruined Village, and a model
  is drawn one way per pack), the chair and the cart (low confidence).
* No match: hanging torches, jugs, mine carts, bookshelves, benches, banners, gravestones, doors and gates (the Drakan 2
  ones fit their own rocky plugs), and the multi-part OotF houses and towers (the Drakan 2 buildings are single models of
  other plans). Edit files cannot carry `pcModel` or `fit` yet (`ps2ModelSwapFromEdits`).

## In game (harness settings, `ootf_level_run.py --campaign`, with the authored models)

| Run | What the frames show |
| --- | --- |
| `world-isl` (flight out of the arrival cave) | the island mountains in Drakan 2 rock with grass patches, no OotF tile grid; 16 Drakan 2 textures used in the level, 0 rejected |
| `world-volc` | the start storeroom; ground reads, nothing new broken (the blue patches were in earlier Volcanic runs) |
| `world-rc` (Rift Crystal, 31 Drakan 2 models used) | the remade dungeon walls and carved archway columns around the start pool; no holes or missing pieces; deeper corridors too dark to judge in these frames |

Not judged yet: the corridor joins and the uneven remake floors against the flat OotF collision, the brightness of the
new rock in daylight levels, and the player's own `drakan.ini`.
