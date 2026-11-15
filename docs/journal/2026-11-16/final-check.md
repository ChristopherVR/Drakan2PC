# Final check of the 15 November evening build (2026-11-16)

One verification pass over the build in `dist/DrakanPC` (`drakan.exe` 19:40, md5 `a36f26f5...`, the same file as
`build/final-game/bin/drakan.exe`; HEAD `32e920e0`; pack `ootf-levels.zip` 19:41, md5 `21bd8a9c...`). No source was
changed. Scratch, play folder, tools, runs and pictures: `build/review/final-1116/` (private, not tracked). Nothing from
either game is quoted here.

## Set-up

* Play folder `build/review/final-1116/play`: a full copy of `dist/DrakanPC` (with `mods/`: the OotF pack, the
  character motion pack, `mods.ini`; without the save folders). `drakan.ini` is the owner's, unchanged except `elf =`,
  which points at a private ELF copy (`build/review/final-1116/elf`). So: borderless 3840 x 2160, `skip_intro_videos =
  0`, `world_rate = uncapped`, `vsync = 0`, `frame_limit = 0`, HD textures on, `texture_upscale = auto`, DLSS off,
  `view_distance` 1, `far_scenery` 1, `music_volume = 13`. HD textures were switched off per run with
  `GSR_TEXREPLACE=0` (the textures look visibly softer in those runs).
* `play/drakan_old.exe` is the 12:47 exe of `ootf-final-check.md` (md5 `480f68cc...`, built before `deb41a6a`), used
  only for the before picture of item 4.
* Drivers: `tools/opening.py` (new; boot with **no autopress**: the logo plays untouched, then a Cross press on the title
  with the cursor on New Game, then no input through the movie, the Intro and the scenes; threads for screen and
  position, the ponytail cloth (`hairprobe.py` logic), the music categories (`ootf-music/tools/probe.py`) and log lines
  with wall-clock time; `--tavern` is the staged doorway walk of `probe.py`), `tools/wdrive.py` (the `ootf-distance2`
  driver plus `--game` and a `--hair` probe thread), `tools/run.sh` (the ponytail runner), `tools/fps.py`,
  `tools/msum.py`, `tools/scan621.mts`, `tools/sheet2.py`.
* An earlier driver, `ootf-final/tools/final.py` and the others, taps Start + Cross every 3 s
  (`DRAKAN_AUTOPRESS`). Those taps skip the OotF intro movie (any of Start, Cross, Circle, Triangle after 1 s). So
  earlier opening runs did not play the movie in full.
* Another session's game (from `dist/DrakanPC`, started 20:15, later restarted) ran from the middle of the check on.
  Runs `ootf_a`, `d2movie` and the runtime tests were before it; every other run had it beside it (one game of mine at a
  time). The `dist/DrakanPC` files newer than the start marker are that game's `drakan.log` and an `mc0_ootf` save. My
  runs log and save into their own run folders.

## Results

| # | Check | Result | Evidence (under `build/review/final-1116/`) |
| --- | --- | --- | --- |
| 1 | Logo and New Game intro movies at normal speed, not skipped | **Pass** | `runs/d2movie`, `runs/ootf_a`, `runs/ootf_a2` (`loglines.tsv`, `events.jsonl`) |
| 2 | OotF Ruined Village opening, no skips | **Pass** | `runs/ootf_b` (frames), `runs/ootf_a2` (music, tavern), `ev/b_*.jpg` |
| 3 | Distance view from the ridge west of the Human Bridge | **Pass** | `runs/dist_on`, `runs/dist_farland0`, `ev/dist_*.jpg` |
| 4 | Live upscale alpha fix, HD textures off | **Pass** (Inquisitor's Lair, before / after); Ruined Village clean | `ev/il_621_old_top_new_bottom.jpg`, `ev/il_old_621.jpg`, `ev/il_new_621.jpg`, `ev/rv_hd0_top_hd1_bottom.jpg` |
| 5 | Ponytail walking at uncapped | **Pass** (walking); running still lifts | `runs/d2_on*.csv`, `runs/d2_off*.csv`, `runs/rv_walk*/hair.csv`, `runs/ootf_a*/hair_rv.csv` |
| 6 | Stability; FPS in 1.2a | **Pass**; 1.2a 82 fps mean | all `runs/*/stdout*.log`; `tools/fps.py` |
| 7 | Runtime tests | **Pass**: 202 of 202 (6 skipped); `vertex_records_webgpu_d3d12` passed (221.8 s) | `rt-test.log` |

### 1. Movies

| Movie | Measured | Expected |
| --- | --- | --- |
| Logo `interfce_4`, three boots | `started` to `264 HD frames shown`: 10.53, 10.49, 10.53 s | 264 frames at 25 fps = 10.56 s |
| Drakan 2 New Game intro `level0ro_6` | 20.97 -> 143.22 s = 122.25 s, `3058 HD frames shown, 0 fell back` | 3059 frames, mkv 122.36 s |
| OotF New Game intro `ootf_intro.mkv` | `cover removed (next level loaded) after 41.4 s` (both runs); no `skipped` line | mkv 41.45 s |

The logo screen (30) ran from 3.2 to 14.9 s with no input. The OotF movie in `ootf_a` (machine otherwise idle): 8395
presents, 17 later than 25 ms, 30 skipped pictures. In `ootf_a2`, with the other game on the GPU: 400 late and 538
skipped pictures, so it was choppy, but it kept the same 41.4 s length (it runs on the audio clock). The Intro level's
exit fired 41.1 s after the movie started, 0.3 s before the movie ends. The cover stays until the next level has
loaded, so nothing of the movie was cut visibly.

### 2. Ruined Village opening

* **Models, mouths.** Delon in 1.1 during "I hope his reward..." (`ev/b_11_delon_mouth.jpg`, frames `f0012` to
  `f0027`): a round head, and the lower lip and chin change from frame to frame (open in `f0014`, `f0016`, `f0021`,
  `f0024`, shut in `f0018`, `f0027`). Julian in the 1.3 close-up, 21 to 25 s (`ev/b_13_julian_mouth_zoom.jpg`): his chin
  drops under the moustache in `f0206` and `f0208` and closes in `f0210` and `f0216`. The overview sheets
  (`ev/b_11_delon_overview.jpg`, `b_12a.jpg`, `b_13_overview.jpg`, `b_13_rest.jpg`) show no torn or stretched polygons.
* **Door.** The old man's door is shut in every 1.3 frame where it is in view (16.8 to 18.2 s, `ev/b_13_door.jpg`).
* **Subtitles.** "Find Arokh." and "Arokh." at 1.3 + 76.6 to 78.3 s (`ev/b_13_subtitles.jpg`).
* **Music** (`ootf_a2`, category 2 from RAM `music.jsonl` via `tools/msum.py`, log lines, and the dump with
  `ootf-music/tools/amp.py`):

| Driver time | RAM / log | Dump (ncc) |
| --- | --- | --- |
| 62.9 (level start) | `0xe0008` alone in the music category | track 8 0.17 to 0.24 (under speech, volume 13) |
| 74.6 (1.1) | `track e0006 starts now (fade in 7.00 s), e0008 fades out (3.00 s left)`; 8 gone at 77.7 | track 6 0.2 to 0.6 from about 120 |
| 125.6, 179.7 | `scene end: the scene's music plays on`, `scene start: the playing music goes on` | |
| 184.0 (1.3) | `scene stop: category 2, fade 9.00 s`; 6's attenuation rises linearly from 0.02 to 0.91 over 9 s (184 -> 193); the category is empty from 194.1 | none afterwards |
| 294.7 (tavern doorway) | `0xe0002` starts (normal start, the category was empty) | **track 2 0.58 to 0.88** (296 to 320) |
| 325.6 (street) | `track e0008 starts now (fade in 3.00 s), e0002 fades out` | track 8 0.31 to 0.43 |

  `ootf_a` (no tavern) has the same timeline up to the stop (stop 179.9, empty from 189.1).

### 3. Distance view

`runs/spotplan.txt` is the plan of `ootf-distance2/tools/spot2.sh`: placements at (222000, 30000, 221000) with headings
-1.5708 / -1.35 / -1.15, and the floating-house spot (226000, 30000, 210000). It was run with far land on (default) and
with `DRAKAN_FAR_LAND=0` (`ev/dist_on_top_farland0_bottom.jpg`, `ev/dist_a1_on_left_off_right.jpg`). With far land
off, the bank behind the bridge is missing in stair steps and the house floats. With far land on, the bank, the cliff
over the river and the hills behind it are drawn, and the house stands on its hill. No sky holes, no striped bands: the
sky is a flat colour.

### 4. Live upscale, HD textures off

* **Inquisitor's Lair.** The 621 cells are in layers 0 to 3, x 590848 to 615424, z 369664 to 396288 (342 cells,
  `tools/scan621.mts` over `build/ootf/ps2/data/LEVEL1IR.LVL`). The same placed plan (`runs/ilplan2.txt`: (603435,
  127400, 384393) at four headings with the camera tilted up, plus (600000, 127400, 376000)) was run with
  `GSR_TEXREPLACE=0`. The old exe (`runs/il_old_621`) shows large black blocky patches on the rock ceilings and walls
  in nearly every frame. The new exe (`runs/il_new_621`) shows none (`ev/il_621_old_top_new_bottom.jpg`). The start area
  of the level has no 621 and shows nothing with either exe (`runs/il_old_hd0`).
* **Ruined Village** (`runs/rv_hd0`, tree views 60 and 1845 of `ootf-final`): no patches with HD off. This is the
  shipped combination: the pack's opaque copies of 621 / 535 would hide the patches even without the renderer fix, so
  it does not prove the fix on its own.

### 5. Ponytail

Median angle from vertical (`summ.py` bins by ground speed; no screenshots in these runs):

| Run | stand | slow | walk 700-1500 | run > 1500 |
| --- | --- | --- | --- | --- |
| Surdana on: `d2_on`, `d2_on2`, `d2_on3` (93 to 107 fps) | 16 / 15 / 16 | 34 / 42 / 24 | **64 / 33 / 34** (>60: 60 / 12 / 22 %) | 68 / 70 / 72 |
| Surdana off (`DRAKAN_PONYTAIL=0`): `d2_off`, `d2_off2` | 15 / 77 | 73 / 75 | 82 / 85 (>60: 99 / 97 %) | 86 / 87 |
| Ruined Village gameplay on / off: `rv_walk`, `rv_walk_off` (118 / 102 fps) | 14 / 16 | 23 / 76 | **47** / 84 (>60: 23 / 97 %) | 75 / 92 |
| Ruined Village scenes on: `ootf_a`, `ootf_a2` (1.1 walk is the slow bin) | 17 / 21 | 38 / 43 (>60: 5 / 14 %) | (cuts) | |

The tail hangs when walking; standing is unchanged. The first Surdana run (walk 64) is the spread the journal reports.
Running still lifts it to about 70 degrees (`ponytail-lift.md` lists that). `d2_off2`'s standing median of 77 (with the fix off)
was not looked into.

### 6. Stability and frame rate

Across the 18 game runs: no crash, no `[watchdog]`, no VU1 report, no device hang or removal, no texture budget miss.
Every run exited with code 0. Each run with audio ends with the known `[hle] post(sid=0x1310) ... did not return within
20 s` lines after `[run] shutdown`. Frame rate from `[frame-uncapped]` (`ootf_a`, no screenshots, nothing else on the
machine): 1.1 99 fps mean (min 2 s window 72), **1.2a crowd scene 82 fps mean (min 48, median 83)**, 1.3 103. With the
other game running (`ootf_a2`): 34 / 29 / 25.

### 7. Runtime tests

`cmake --preset runtime-tests && cmake --build --preset runtime-tests --parallel 8 && ctest --preset runtime-tests`
before any game was started: 100 % of 202 passed, 6 skipped (data-dependent), 736 s.
`vertex_records_webgpu_d3d12` passed in 221.8 s, so it was not rerun.

## Regressions

None found.

## Not checked

* Tears at every frame of every scene: the scene sheets were looked at, about one frame in three.
* The 1.2a frame rate of the shot run (`ootf_b`), which is disturbed by the 4K screenshots and the other game.
