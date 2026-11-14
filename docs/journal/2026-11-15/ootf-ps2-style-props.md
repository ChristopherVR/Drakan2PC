# Order of the Flame: new PS2-style models for props with no Drakan 2 model (round 8)

Owner request: for Order of the Flame props that still have no Drakan 2 model, "rebuild / remake them into PS2 equivalent
ones", offered in the converter as an option like the Drakan 2 model table rows. Props of this brief: Alwarren's magic
flame ring, the talon torches, the Succubus pillars and ruins, the light beams. Converter only (submodule
`editor/packages/formats`: new `ootfpropremake.ts`, rows in `ootfps2model.ts`, one dispatch in `ootfassets.ts`, one stage
and a morph guard in `ootfpipeline.ts`). Formats `e48eef6` (first committed as `357a92f`, which was lost with the local formats repository at 07:13 and
recreated from the scratch copy onto the recreated voices commit `484f8e0`; the recreation also keeps turning corners only
on open borders, so a crease inside a talon horn no longer kinks it). Formats `30349ee` then keeps the borders between two textures
sharp. Scratch, packs, runs and frames are under `build/review/ps2-models-new/` (not
tracked); nothing from either game is quoted here.

## 1. What the props are

From the PC data (`tools/pcmdl.ts`, `tools/where.ts`) and an offline textured render of each mesh (`tools/dump.ts`,
`tools/render.py`, scratch only):

| Prop | Classes (PC model) | Placed | Shape |
| --- | --- | --- | --- |
| Talon torch | Talon Torch 01 / 02, FangTorch01, FangTorch2 (FangTorch01 / 02), Talon Torch Death Mage (FangTorch03), Talon Torch w/o light, islandfang, Landscape Spike2 (IslandFang01, island fang02) | 70 + 2 + 22 + 9 in Volcanic, 4 + 2 + 18 + 8 in Succubus Lair | a curved horn of few facets on a flat slab, a bowl hanging on chains; no flame, emitter or dynamic light (engine classes 20, 43, 133) stands within 2,500 units of any of the 105 placed in the PC data (`tools/near.ts` over every anchor), so no fire was added |
| Succubus pillar | Succubus Pillar (SuckPillar01) | 42, Volcanic (not in Succubus Lair) | octagonal column with carved bands |
| Succubus ruins | Evil Ruin Straight / Entrance / End / Left Turn / Right Turn / Arch / Ceiling / Right Open, Entrance 01, Left Turn 01, Arch 01, Arch to Rooms, Straight w lite 01 / 02 ... (EvilRuin*) | about 300 in Succubus Lair, Volcanic, Islands, Islands Night and three Multiplayer levels | tiling corridor modules (walls, gothic vaults, carved arches) |
| Light beam | Light Beam | 32 in six Multiplayer levels | an open tube of 16 translucent cards over each pick-up pad |
| Magic flame ring | Alwarren Magic Flame (engine class 43 emitter, damage 1,000 hp/s, damage effect "Small Fire Effect") | 93 in Alwarren | particle fountains; PC flags without the visible bit |

Drakan 2 has no model of the same kind for any of them: its columns (LEVEL6EN, LEVEL2RE, WORLDCON) are sand-coloured
desert or castle columns of other proportions, its light cones (LEVEL2RE glow morphs, FX light rays) are other shapes,
and it has no talon or Succubus architecture.

## 2. What changed

**New drawings built at convert time from the OotF mesh** (`ootfpropremake.ts`), drawn through the same path as the
refined bodies and palms (`OotfAssetBuilder.refineDrawing`): the OotF vertex, polygon and bounds records stay, the new
vertices are appended and the new strip display list replaces the OotF one, so the engine collides with exactly the OotF
shape (`tools/cmpmod.ts`, pack `on2` against `off1`: polygon (0x0204) and bounds (0x0207) records byte-identical and the
OotF vertices first for EvilRuinStraight01, EvilRuinEntrance02, EvilRuinEnd01, IslandFang01, FangTorch01, SuckPillar01,
Light Beam).

* `smooth` (talons, pillar): two steps of Loop subdivision. The earlier `pn` / `round` refinements keep every original
  corner in place and changed nothing visible on these props (offline renders, `m/refine1.png`); Loop moves the corners,
  so the octagonal pillar becomes round and the faceted horn a smooth curve. Edges sharper than 50 degrees, open borders
  and corners where a border turns stay sharp; UVs are split linearly, so the OotF textures and layout stay. About
  1,800 to 2,700 triangles per model instead of 80 to 170.
* `module` (ruins): the same with every open border fixed (split straight), so a remade piece meets its neighbour, remade
  or not, without a gap.
* `beam`: the light beam rebuilt as two nested shafts of 32 sides, the beam and a halo at 1.5 times its radius that narrows
  to the beam at the top, with the OotF beam texture and its V gradient. A first version with three shafts drawn from both
  sides was far brighter and yellow (`evidence/relic_beam_first_version_too_bright.png`); the PS2 draws both sides of
  every polygon anyway.
* Models that a Morph Plug (engine class 99) also shows as a frame are left alone: morphing blends vertices one to one, and
  the first pack (`on1`) tore the Succubus Lair start corridor into spikes and coloured blocks at 14 fps
  (`evidence/succubus_ruins_morph_tear_on1.png`; "Straight w lite 01 / 02" and "Left Turn 01 w lite" share their models
  with "(M)" morph classes). The guard covers every props row, Drakan 2 models included; the conversion warns.

**The magic flame rings, shown harmless** (`harmlessShow`, stage `harmless fires`): the flames of the class that start on
and that no object of the level links to get the visible flag and object parameters `Damage (hp/s)` 0 and `Damage Effect`
0; a flame is never shown when the damage parameter cannot be set. That is 52 of the 93: the rings round all three
barrier crystals (17 each) and a single flame; the fountain flames and the pulsing groups have senders and keep their PC
flags. On the PC these rings burn from the start and nothing puts them out; burning with damage in the PS2 engine they
would block the way to a crystal for good, so the shown fire hurts no one, which is what the shipped pack does today (the
fire does not run there at all). The particles are the OotF emitter's own; no Drakan 2 particle replaces them.

**Option.** 30 props rows (category Props, `refine` `smooth` / `module` / `beam`) and one effects row ("Magic flame rings
round the barrier crystals"), all off by default (the round's rule: the shipped behaviour stays the default). The editor's
import dialog lists them with the other rows; `--ps2-model-map` turns them on with `{"ootf": "<class>", "ps2": "(new
PS2-style model)"}` or `"(OotF fire shown, harmless)"`, and a remake row also takes `"refine"`. With all rows off the pack
is byte-identical, every file, to one converted from the formats commit before this change (`packs/base` from the voices
commit against `packs/off2` from the rebuilt change, same script and inputs). With all rows on (`packs/on5`, converted
from the landed `e48eef6` in the main checkout), only `RESOURES.MOD`, the Alwarren LVL and `mod.toml` differ from the off
pack.

## 3. Evidence (owner's ini, `play/` = copy of `baseline-play` with the dist exe 5fdf3a1a, private ELF)

Packs: `packs/off1` (formats with this change, every new row off) and `packs/on4` / `on3` (every new row on;
`on3` before the damage effect parameter, otherwise the same), full campaign packs with `DRAKAN_TERRAIN_CHUNKS=wide` and the
subtitle files. Direct level starts on an empty card, god mode unless stated, the same placements for both packs
(`tools/look.py`, `tools/spin.py`).

| Prop | Level | Frames | Seen |
| --- | --- | --- | --- |
| Talon torch, pillars | Volcanic, torch 2513, pillars 2680 to 2689, a talon by the lava | `evidence/volcanic_talon_pillar_zoom_pairs.png` (crops brightened), `evidence/volcanic_pairs.png` | the horn shades smoothly and is rounder; the pillars are rounder with soft shading; places and sizes unchanged. Up to `e48eef6` the horn's carved band edge dipped into a V notch where smoothing crossed the texture border (`evidence/volcanic_talon_pillar_zoom_pairs_e48eef6.png`); with `30349ee` the band edge is a smooth line again (`evidence/volcanic_talon_notch_fix_pairs.png`, `e48eef6` left, `30349ee` right, packs `on5` / `on6`) |
| Ruins | Succubus Lair start corridor (no placement), three headings | `evidence/succubus_ruins_pairs.png` | vaults and arches curve a little more smoothly; no gaps at the piece borders; frame rate as before |
| Light beam | Relic, pads 265 and 267 | `evidence/relic_beam_pairs.png` | a rounder, softer beam with a halo at the pad, a little brighter than the OotF tube |
| Flame rings | Alwarren, the three barrier crystals | `evidence/alwarren_rings_pairs.png` | fire on every skull of each ring and a plume above it, where nothing showed before |

Played with real input, god mode off (`tools/ringwalk.py`): placed 2,600 units from the ring centre, W into the ring, then
standing in it.

* Ring 2 (crystal 1457), 32 s standing in the fire (runs `rw4_off1`, `rw4_on4`, `evidence/ring2_walk_orbs.png`,
  `evidence/ring2_standing_in_fire.png`): the health orb stays full and red in both packs. At 1,000 hp/s she would be dead
  in a fraction of a second.
* Ring 1 (crystal 250), 32 s (runs `rw3_off1`, `rw3_on4`, `evidence/ring1_walk_orbs.png`): the orb stays full in both; in
  the runs with the fire it turned purple and blue for a few seconds while the lizard creatures of that room fought her.
  Standing still outside the ring, 2,600 units away, for 32 s (runs `rs_off1`, `rs_on4`, `runs/rs_orbs.png`), the
  creatures come and the orb flashes blue in both packs, with the fire and without: the colour comes from the creatures,
  not from the ring (`evidence/ring1_outside_orbs.png`).

Collision: identical by construction (records above); no walk into a talon, pillar or ruin wall was played.

## 4. Cost

`scripts/perf/scene_ab_bench.py` (turn lock, owner's ini, 3 interleaved pairs, `bench/vo-dense`): Volcanic, Rynn at
269310 145400 353533 facing talon torch 2513 with the carved pillars behind (`runs/bench_pose.png`), all rows off (`off1`)
against on (`on4`). Another agent's game was running before or during 5 of the 6 runs (the tool's census), so these are
not quiet-machine numbers.

| | off | on |
| --- | --- | --- |
| frame ms, median (range) | 16.67 (16.32 to 18.73) | 16.66 (15.78 to 16.67) |
| pairs off to on | 16.32 to 16.66, 16.67 to 15.78, 18.73 to 16.67 | |
| VU1 worker cpu ms per world frame | 4.64 (3.67 to 6.01) | 6.95 (6.30 to 7.81) |
| GS worker cpu ms per world frame | 7.06 (4.88 to 7.92) | 9.97 (9.22 to 10.09) |
| GPU world ms | 9.03 (5.88 to 13.00) | 8.32 (5.59 to 8.99) |

The frame stays at the 60 fps of the owner's vsync in both; the denser models cost the vertex and GS workers about 2 to
3 ms per world frame at this pose (about 15 times the triangles per remade model, roughly 50 % more worker time). Whether
the frame time changes is not established: vsync caps it and the runs were not on a quiet machine. Not measured: the
beams and the rings. From the overlay of the placed views (one frame each, not a measurement): Relic beam views unchanged;
standing in an Alwarren ring 223 fps without the fire against 115 fps with it (4.5 against 8.7 ms), the fire of 17
emitters filling a 4K screen; the ring row stays off by default.

## 5. Props left unchanged, with the reason

* The talon torch bowls carry no fire: no flame object is placed at any talon torch in the PC data.
* The three ruin pieces that are also Morph Plug frames ("Straight w lite 01 / 02", "Left Turn 01 w lite") and the "(M)"
  morph classes themselves: a remade frame tears the morph.
* "Beam o Light Tower" (Rift World, one placed, another model): not in this brief's set; left.
* The ring flames keep the OotF particles: swapping an emitter's particle textures for Drakan 2 fire textures is not a
  field the converter can point at a disc class (the emitter names its own textures), so it was not done.
* No Drakan 2 texture is used on the remakes (not tried in game): the disc's column and wall stones are sand or grey
  castle stone with their own layouts, while the OotF Succubus stone is dark green with carved bands laid out for these
  meshes; the remakes keep the OotF textures and their layout.

## 6. Not verified

* Walking into the remade talons, pillars and ruins (collision records are byte-identical, not played).
* Islands, Islands Night, Volcanic's ruin corridors and the Multiplayer ruins were not looked at, nor the beams of the
  other five Multiplayer levels; Talon Torch 02, FangTorch2 and Landscape Spike2 not seen in game.
* The pulsing and fountain flame groups (left as they are).
* Drakan 2 is untouched (converter only); no Drakan 2 scene was played.
