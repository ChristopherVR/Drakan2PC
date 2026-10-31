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

## Pipeline (`formats/src/ootfpipeline.ts`, later the same day)

`convertOotfLevels(src, pcPaths, ctx, opts)` is the one entry point (CLI `editor/scripts/ootf-convert.ts` and `scripts/harness/ootf_level_run.py`
call it). Stages, each wrapped so a failure is reported in `result.stages` / `warnings` and the level keeps what the earlier stages made:
textures (terrain, `ootfterrain.ts`) -> models (`OotfAssetBuilder`) -> class plan (`planClassMapping` with `modelFor`, `substituteEngineId`,
`rewriteText`) -> sounds (used sounds only, one `OOTFCOM.SDB`) -> per level `convertPcLevel` (`resolveClass`, `mapResource`, `objectIdMap`) ->
sequences (convert twice: object ids first, then refs) -> singletons (Rynn / Camera / Level Properties cloned from the template, Level
Properties params from the PC class) -> music triggers -> campaign exits -> texts. Shared container `files/ENGLISH/COMMON/OOTFCOM/OOTFCOM.{MOD,TXD,DB,SDB}`,
level header gets a dependency to it; model refs of cloned classes need the owning ODB's `.DB` to list it (`depIndexFor`).
`--only a,b` (CLI) switches stages on for bisecting; `--tex-first/--tex-max/--tex-cap/--tex-common/--tex-disc` are terrain-texture probes;
`ootf_level_run.py --convert-arg=--only --convert-arg=textures,singletons` passes them.

### Bisect results (Relic, template LEVEL1NA, runs in `build/ootf-run/<name>/run`)

| Run | Converter stages | Result |
| --- | --- | --- |
| p3 | singletons only (fallback terrain texture) | loads, world runs, rock terrain, Rynn walks |
| p7 | terrain refs = 12 of the template's own textures (`--tex-disc`) | loads, textured terrain (blob path with many textures is fine) |
| p2, p4-p9 | new terrain textures via splat TXD (db 24) or a new `OOTFCOM` dependency (index 25 / 7) | World 0.0 fps (stall). Fixed by the terrain session: append to `PROCTEES.TXD`, refs db 9, texture header +40 = 45 (`ootf-terrain.md`) |
| p1, p10-p12 | new ODB class records (`new/` + 0x0103 override) | **Intro fails to load** (`afterLevelLoad ok=0 result=-6`) |
| p13 | 1 new class in SYSTEM.ODB | loads |
| p15, K=2, `0,2` | 1 or 2 new classes in RESOURES.ODB | loads (RESOURES.ODB 331946 -> 332326 bytes) |
| K=3 variants (3 records, +644 bytes of inflated 0x0103, 3 different templates, different groups) and every larger set | 3 or more new records in RESOURES.ODB | **Intro fails to load** |
| patched `.DB` files (`RESOURES.DB` gets a dependency to a new container) | | Intro stops while reading `Resources.odb` (p10); not needed any more |

So the engine tolerates about 2 appended class records per ODB (about +420 bytes of inflated field lists; 3 records / +644 bytes fail).
Not the group, not a repeated template, not the number of distinct classes. The cause is unknown (a fixed-size guest buffer for the ODB
or its field lists is the likely one); `OotfPipelineOptions.maxNewClasses` / CLI `--max-classes N` keeps only the N classes with the
most objects. Ways forward: find the buffer (guest ODB loader), spread the clones over several ODBs (1-2 each; the other ODBs were
not tried beyond SYSTEM / FX with 1), or ship a replacement ODB that drops classes the level does not need.

### Models without `.DB` edits (run m3, works)

New models and their textures are appended to the **host ODB's own MOD / TXD** (`RESOURES.MOD` / `.TXD`, whole-file replacements in the pack;
`ps2.hostMod/hostTxd`, ids past the host maxima, `0x0202` last-model counter updated) and the cloned classes (`modelOdbFile`) live in
`RESOURES.ODB`, so the model ref is `db 0`: no dependency, no `.DB`. Relic with `--max-classes 2` (BrickWall2 / BrickWall4): level loads,
24 world entities (was 8), the engine requests our `RESOURES.MOD 0200/0203/0204` entries and `RESOURES.TXD 0040/66xx`, converted
terrain textures render (Proc Textures route), Rynn walks. Screenshots: `build/ootf-run/m3/run/s_start.png`, `s_walk1.png`
(the walls are not in view of the start). Without 0x0211 the engine logs `0211/<id> not found` and carries on.

### Open

* The ODB growth limit (above) caps how many scenery / item classes a level gets: 2 per ODB.
* Sounds (SDB into a host SDB: the converted SDB is written as `RESOURES.SDB.new`, not merged), sequences (new SSD entries) and the
  campaign exits were wired but not run in the game; `sounds` is off by default (`opt.sounds === true`).
* Two clones of the same template, groups, and a per-record `texts` rewrite were ruled out / not needed as causes; music, texts and
  campaign stages are in the pipeline and compile but were not exercised in game.
* Intro Level with the same options (`m4`: 38 of 102 objects, 36 models, 87 textures = 3.7 MB added to `RESOURES.TXD`, 2 new classes) did **not**
  start: `Level 0 - Intro` loaded, the converted level never did (`FAIL`, no world sample). Not bisected (host TXD size, the 2 clones'
  ODBs, or the Intro's objects); Relic is the only converted level verified with models. Ruined Village not tried.
