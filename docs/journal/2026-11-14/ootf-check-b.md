# OotF play check B: Alwarren, Succubus Lair, Volcanic, Wartok Canyons (2026-11-14, round 5)

Brief: play the four levels that rounds 3 and 4 changed from data only (terrain half-hole cells, the 3 x 3 texture
window rule, water, corpse clips, armour requirements, the spells switch) and fix what is wrong. Scratch, tools and
every run: `build/review/ootf-check-b/` (private). Code: `editor/packages/formats/src/convert.ts`
(`limitChunkTextures`). Nothing from either game is quoted here.

## How it was played

* Play folder: a copy of `build/review/baseline-play` (the owner's `drakan.ini`: DLSS quality, HD textures, 1280 x 960)
  with the 14 November `dist` exe and DLLs, a private ELF copy, and the `dist` pack (`ootf-levels.zip`, formats `d937581`).
  Later runs swap in packs converted here (`packs/<tag>`), same exe and folder.
* Runs: `DRAKAN_START_LEVEL` direct starts, god mode, scripted live input through `tools/runf.sh` and the tour plan
  `plans/tour.py` (the round 3 plan driver over `live_drive.py`). Movement is W with mouse steering; a "visit" walks
  towards a spot and, when the walk does not get there, **places** Rynn 1,600 units short of it and walks in, then turns
  round in eight 45 degree steps with a shot each, then fights the nearest live creature (Rynn is unarmed in a direct
  start, so fights are fist fights). Exits: Rynn is placed beside the exit box and walks in; Enter answers the prompt and
  the save screen. F5, a short walk, F8. A guard kills the game 20 s after a `[stall]` line.
* One game instance of mine at a time; other agents had up to three games running, so frame rates in the shots are not
  measurements.

## What was wrong

### 1. Converted terrain draw data overflows the guest heap: hangs in Wartok Canyons and on the load into the Islands

Wartok Canyons hung in the lower canyon (about 96,000 to 132,000 / 427,000 to 448,000) in 4 of 4 runs on the `dist`
pack (`wc4`, `wc5`, `hd-dist`, and `hd-mbefore` on the pack from before the half-hole rule): the guest input boundary
stops, thread 1 loops in `FUN_001483e8` called from `FUN_001c72e8`. The stall dump (`dumps/`, RAM and registers) shows
the call chain from the stack: visible layer pass `FUN_00153e10` -> touch `FUN_0015f350` -> **block build
`FUN_00154060`** -> `FUN_001562f0` -> `FUN_001c72e8` -> `FUN_001483e8`. The layer being built (26 x 34 cells, 391 chunks
in the file) has a block whose page array pointer reads `0x10`: an allocation came back empty and the loop walks a count
read from address 0x10. The terrain LRU at that moment is intact (23 layers, none without pages) but holds 0x5eacf0
bytes against a budget of 0x1f3333: three times the terrain budget.

Cause: since the 8-texture chunk limit (formats `b4deb22`, 7 November) the blob builder cuts every chunk whose cells and
ring list more than 8 textures down to single cells. The PC paints nearly every cell with a texture of its own, so
converted layers have hundreds of chunks: Wartok Canyons up to 752 chunks with 5,491 texture entries in one layer,
the Islands 686, Alwarren 409 (the 14 disc levels: at most 25 chunks and 70 entries per layer). The engine builds a draw
page per chunk texture for every layer in view, so where many such layers are in view at once the guest heap runs out
while a page list is built. The load from Alwarren into the Islands hangs the same way (`al1`, at screen 14 right after
the exit, same `ra 0x1c7330`), and another agent's Islands water run (`ootf-water2/runs/is-lip-b`) shows the same stall
signature. The half-hole rule and the 3 x 3 window rule of rounds 3 and 4 did not cause it (the pack from before them
hangs too) but did not bound it either.

### 2. Not defects of these levels, or not mine to fix

* **Water** (for the water agent): flat, untextured looking water surfaces in all four levels. Wartok's river at the start
  is a flat pale blue sheet; Wartok at (190,464, 307,200) and a Succubus Lair pool under the hub floor at (168,000,
  167,900) draw as a flat lilac plane with Rynn's swim ripple on it (`wc-chunk/hE_t*`, `sl2/inv1_t0`); Alwarren's
  pools at (448,500, 508,700) show deep blue water on one half of the screen with a straight edge (`al1/hE_t*`).
* **Exit boxes next to arrivals**: walking north from the Succubus Lair arrival walks back into the exit to the Islands
  Night (prompt "the game must be saved"); that is the PC layout, the arrival lies next to that exit.
* **F8 position**: back at the exact saved spot in Succubus Lair and Wartok when nobody is near; 85 units off in Wartok
  and 520 units off in Alwarren where Wartoks / goblins were hitting Rynn during the 16 s after the load (knock-back,
  god mode). Not a save defect as far as these runs show.

## Fix (formats)

`limitChunkTextures` (`convert.ts`), run on every floor and ceiling after the 3 x 3 window rule: each disc-sized chunk
(the `ps2ChunkSplit` rectangles the blob builder writes, cells plus their ring) keeps at most 8 textures; the texture the
fewest cells of an over-full chunk use gives way, each of its cells taking the texture most of its eight neighbours use
among the chunk's other textures. Then no chunk is cut, and converted layers have the disc's chunk grid: Wartok Canyons'
largest layer 20 chunks / 144 entries (was 752 / 5,491), the Islands 14 / 96, Alwarren 12 / 96. Every 3 x 3 block lies
inside one chunk's rectangle, so the window rule's guarantee still holds. `DRAKAN_TERRAIN_CHUNKS=split` at conversion
keeps the old cut chunks. Changed drawn cells against the `dist` pack: Ruined Village 8 %, Wartok 16 %, Grimstone Mines
10 %, Grotto 7 %, Islands 21 %, Alwarren 27 %, Rift Crystal 11 %, Islands Night 21 %, Succubus Lair 8 %, Volcanic 12 %,
Rift World 11 % (a changed cell takes the texture of a neighbouring cell).

Test: `terrainblob.test.ts` "keeps every chunk of a layer within 8 textures so the blob cuts no chunk" (30 x 20 cells with
a texture each, holes and invisible cells untouched, every chunk at most 8, no cut chunk, fewer chunks than before).
`npm test` in `editor/packages/formats`: 61 files, 631 tests; `tsc` clean.

## Played

| Level | Played (real input) | Placed | Before (`dist` pack) | After (`chunk` pack) |
| --- | --- | --- | --- | --- |
| Wartok Canyons | dismount at the start; fights with two Wartoks (fists, 40 -> 28 and 29 -> 16 hp, both alive); W walks; Arokh flown with W / Space towards canyon waypoints (with straight-line steering he stops against the canyon walls, so the flight did not follow the route); the walk into the mine-mouth box; Enter on the prompt and the save screen; F5, walk, F8 | Rynn at six half-hole clusters (1,600 units short, then walked in); Rynn beside the mine-mouth box | **hang** in the lower canyon in 4 of 4 runs (`wc4`, `wc5`, `hd-dist`; `hd-mbefore` on the pack before rounds 3/4); start, caves and exit prompt otherwise fine (`wc1`, `wc3`) | no hang through all six spots, F5 / F8 back at the exact spot, exit taken, Grimstone Mines loads with control (`wc-chunk`); the same views look the same (`pair_wartok.png`) |
| Alwarren | start look; fights with yellow orcs and goblins at three spots (fists); F5, walk, F8 (520 units off while goblins hit her); the walk into exit box 153, Enter, save | Rynn at five half-hole clusters and three creature groups | long session (`al1`): exit taken, **hang** loading the Islands; short session (`alx-dist`): exit taken, **hang** loading the Islands | short session (`alx-chunk`): exit taken, the Islands load, Rynn walks and swims there |
| Succubus Lair | start look; walk east; fights at the orcs, the War Giant and two scavenger spots (fists, no damage dealt); F5, walk, F8 (exact) | Rynn at eight half-hole / invisible-half spots, at the War Giant and on the ship deck | no hang; the ship deck chain opens the exit prompt (`sl2/exit1`); a pool under the hub floor draws as a lilac plane (water) | not run on the new pack |
| Volcanic | start look; walk north; F5, walk, F8 (exact) | Rynn at six half-hole clusters (two landed on the volcano rim above them, one on lava) | not run | no hang; near the rift Rynn stands on an invisible floor (fully invisible PC layer, not a half cell) through which the lilac clear colour shows (`vol-chunk/hF_*`); exit not reached: `scripts/harness/ootf_volcanic_route.py` on this pack (harness window and ini, autopress for the first 60 s) carried the Lava Rune and then failed at the lock step (lava not drained), the harness flake already noted in `2026-11-12/ootf-volcanic-rift.md`; no hang (`vroute2`) |

## Not verified

* Corpses, armour requirements and the spells switch were not looked at on purpose: Rynn was unarmed and fought with her
  fists in direct starts, few creatures died (goblins lay flat where they fell in Alwarren, `al1/gob2_*`), no armour was
  picked up and no spell was cast.
* The War Giant of Succubus Lair and Alwarren's war giant were not killed; no subtitles were on screen in these runs.
* Walls near half cells were not walked into on purpose; the placements that put Rynn on top of rock (Wartok `hA`,
  Volcanic `hB`, `hC`, `hE`) are placement artefacts, not holes.
* Wartok Canyons was not flown along its real route (Arokh stops against the canyon walls with straight-line steering);
  the farm, the slaves and the Wartok camp were only seen from the air.
* The new pack was played once per level (Wartok, Alwarren to the Islands, Volcanic); Succubus Lair, Grimstone Mines,
  Grotto, the Islands beyond the arrival, Islands Night, Rift Crystal, Rift World and the Ruined Village were not played on
  it. The texture change (8 to 27 % of drawn cells take a neighbour's texture) was compared in four Wartok views only.
* The `dist` pack has the hang; the owner needs a pack converted with formats `e671be8` (and the exe is unchanged).
* Frame rate with the new chunks was not measured (no bench lock taken); fewer chunks should mean far fewer terrain
  passes (see `ootf-performance.md`), which also changes the baseline of the terrain-join work.
