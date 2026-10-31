# Converted OotF levels in the real game (2026-10-31)

First in-game test of `ootf-import.md`. Result: **converted levels load, render with the engine's own terrain textures,
Rynn stands on the converted collision terrain and walks.** Screenshots (private, not in the repo): `build/ootf-run/shots/`
and `build/ootf-run/<name>/run/`.

## Repeatable command sequence

```
python scripts/harness/ootf_level_run.py --ootf build/ootf/cab_disc "Multiplayer/Ground/Relic/Relic.lvl" --name relic
python scripts/harness/ootf_level_run.py --ootf build/ootf/cab_disc "Mountain World/Intro Level/Intro.lvl" --name intro
```

It extracts the template files (and splats) from the ISO into `build/odbx/data`, converts with `editor/scripts/ootf-convert.ts`
into `build/ootf-run/<name>/mods/ootf-levels.zip` + `mods.ini`, then runs `play_run.py` with `DRAKAN_MODS=<that mods dir>`
(`dist/DrakanPC/mods` is not touched) and `DRAKAN_START_LEVEL=<title>`. Output: `stdout_1.log`, `s_start/s_walk1/s_walk2.png`.
For a manual install put the zip in `dist/DrakanPC/mods/`, set `ootf-levels = on` in `mods/mods.ini`, restart, pick the level.

Harness notes: `DRAKAN_START_LEVEL` replaces **Level 1A - Surdana**, so the intro cut scene plays first (about 20 s of game
time). `DRAKAN_START_LEVEL_FROM=Level 0 - Intro` (used by arena_tour) does NOT work for a template-based custom level (the
Intro level loads the level, the guest never opens the new level's files). The player's drakan.ini is 4K / DLSS, which made a
run take minutes; the script lowers it with env overrides (`PS2X_GS_SCALE=1`, `DRAKAN_FULLSCREEN=0`, window 960x540,
`DRAKAN_DLSS=off`, `DRAKAN_HD_MOVIES=0`, `GSR_TEXUP=off`). A run is about 40 s.

## What the engine accepts / needs (found by bisecting on a LEVEL1NA clone)

| Part | Result |
| --- | --- |
| Clone of LEVEL1NA with the same bytes under a new name | loads **only with the splat files** `<first 6 of mangled>TS.DB/.TXD` in the pack. Without them: `guest-branch:missing-target` (indirect call to 0) right after the load, world never runs. Fix: `buildOotfPack(..., { splats })`, CLI `--splats` (default: the `--ps2` dir) |
| Converted header (0x0000) + template terrain | loads |
| Converted object list (0x0020) | loads (Rynn, Camera and any mapped object are spawned) |
| Converted terrain 0x0001 with the generated inline blob + converted layer groups 0x0002 | loads and draws, **if** `seq` is the same for all layers and `level_tag` is the template's (0x18 for LEVEL1NA) |
| template `0x0003`, `0x0025`, dependency table | kept from the template, engine happy |

Blob findings (test bed: Surdana's own terrain re-written with regenerated blobs, `build/ootf-run/shots/`):

* `seq` unique per layer (what `ps2BlobFor` did) with converted terrain: **the world stops** (HUD only, `World 0.0 fps`, the
  guest stays alive). With one common `seq` (1) the same terrain runs at hundreds of fps.
* `level_tag` 0x16 (old default) drew the ground as a flat blue-grey and only some layers; 0x18 draws the proper textured
  ground on Surdana (small pink patches remain: texture priority order is still first-seen, not the real one).
* Overlapping slot ranges from solo layers (`0..4w-1`) are fine.
* Normals are the central-difference ones; no problem visible.

## Other defects found and fixed

* The PC start (`Rynn`) hangs high above the ground (Relic: 17000 units). The PS2 engine lets her fall and the fall kills
  her ("Press Enter to load the last saved game"). `convertPcLevel` now puts every `Rynn` at terrain height + 400.
* `ps2BlobFor` is now `ps2BlobBuilder(0x18)`; `buildOotfPack` swaps in the template's real tag (`ps2LevelTag`) when given
  `ps2BlobFor`.

## Levels tried

| Level | Objects kept | Result |
| --- | --- | --- |
| Multiplayer/Ground/Relic | 3 of 152 | loads, rock terrain (fallback texture), Rynn alive, walks and climbs |
| Multiplayer/Air/Arena of Death | 9 of 22 | loads, 41 layers, walks |
| Mountain World/Intro Level/Intro | 12 of 102 | loads, 18 layers, walks, no crash |

Still wrong / open: every cell uses the single fallback texture (no PC -> PS2 texture mapping yet, that is ootftex); almost
all objects are dropped (no PS2 class), so levels are empty terrain plus Rynn; sky/fog come from the template; the editor
dialog (`ootfimport.ts`) cannot ship the splat files because `readLevelFile` only returns the LVL (needs an RPC for sibling
files); Rynn's start is the PC `Rynn` object, a level without one has no player; the texture order in the blob is
first-seen.
