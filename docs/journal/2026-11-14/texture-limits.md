# Texture memory and texture quality limits (round 6)

Brief: find the PS2-era texture limits that cost quality or cause stalls (guest texture cache, host cache and eviction,
VRAM upload thrash, mip levels, anisotropic filtering, palette and colour depth, texture size, HD lookup misses), measure
them and lift them from the host side, each behind a `drakan.ini` setting with the old behaviour one setting away.

Everything was run from a private play folder (`build/review/limits-textures/play`: a copy of
`build/review/baseline-play`, the owner's `drakan.ini` with 4K, DLSS quality, HD textures, `texture_budget_mb = 6144`,
anisotropic 16; the current `ootf-levels.zip` and HD textures of `dist/DrakanPC` from 14 November). Before = the dist exe
of 14 November 09:55 (`drakan_before.exe`); after = `9b57acae` (`bc849484` plus the corrected miss counter and the
tap rule of section 4), where each new setting is set back by its environment variable inside one exe, so the A/B pairs
below differ in that variable only. That exe was built from the main tree while another agent's uncommitted model lights
edits (`drakan_model_lights.cpp`, `ModelLights.h`) were in it; the env pairs are unaffected, before / after exe
comparisons are not used for any conclusion. Runs: `scripts/harness/play_run.py` (New Game on an empty card, autopress through
the intro, then a recipe of mouse turns and shots); nothing below was played by hand. Runs, recipes, sheets:
`build/review/limits-textures/` (`runs/`, `recipes/`, `bench/`).

## 1. What was looked at, and what was not a limit

| Candidate | Finding |
| --- | --- |
| Host texture cache budget | Already PC sized: `texture_budget_mb` 6144 in the owner's ini (auto = 40 % of VRAM). Peak use in these runs 0.3 to 1.3 GB; nothing was ever evicted for the budget (counter below). Not a limit. |
| Anisotropic filtering | Already 16 taps in the owner's ini (shader footprint path). Not a limit by itself, but it worked on the wrong texel scale for HD copies (section 4). |
| Maximum texture size, colour depth of converted textures | `ootftex.ts` never shrinks: the PC art is at most 256 px and every size is kept or enlarged (journal 2026-11-12 `ootf-texture-quality.md`). Large textures go 8 bpp in the guest (PSNR median 47 dB), but with HD textures on the pack image is made from the lossless PC pixels (`.hdsrc`), so the shown texture is true colour at the PC's own resolution (checked: `build/hd/index.json`, which indexed `build/ootf/mods/ootf-levels.zip` with its `.zip.hdsrc` of 1144 sources, has 1111 entries made from a version 2 lossless source; the dist log's "lossless sources 0" counts only entries newly switched to a source in that run). Nothing to lift on the converter side; making the guest copies 32 bpp would cost 4 times the guest texture memory for an invisible gain. |
| HD upscale factor cap | `pickFactor` caps one texture at 4096 px and 1/24 of the budget: with 6 GB that is never reached by 256 px sources. Not a limit. |

## 2. The game's own texture memory (`game_texture_cache_kb`)

The engine's texture manager (`0x4db280`) keeps at most 0x566666 bytes (5.4 MiB) of textures in a level; when a load
would overflow it may only recycle a buffer of the same size (`drakan_texture_budget.h`). Before this round only a
campaign game (Order of the Flame chosen on the card) got 8 MiB; a converted level loaded any other way (a direct start,
the editor's play test, a multiplayer map from a pack) ran as Drakan 2 with the native budget, which is how the round 3
Wartok Canyons direct start sat on the loading screen.

Measured with the counter added this round (`[texbudget]` lines: loads whose request did not fit the budget in force;
`0x1bf3e0` is also called when a load fits, 81 times in Drakan 2's intro with nothing missing). Same exe, same pack,
only `DRAKAN_TEX_CACHE_KB` changed, Wartok Canyons direct start as the editor's Play does it (`DRAKAN_GAME=d2`,
`DRAKAN_START_LEVEL_FROM=Level 0 - Intro`), recipe `turnback.txt` (no movement, two turns), 4K, DLSS quality:

| Wartok Canyons direct start | native (old default here) | `auto` = 8 MiB (new default) |
| --- | --- | --- |
| texture loads that did not fit, about 45 s | more than 14,000 (last log line; logged every 1000) (cache full at 5.65 of 5.66 MB the whole time) | 0 |
| texture slots the host saw change content ("volatile", never HD-replaced) | 337 | 101 |
| host texture cache peak | 1286 MB | 519 MB |
| first shot under control (as observed; the two runs reached different game states, so not all of it need be textures) | river drawn with a stray "T" texture, sky and water planes streaked, Arokh not drawn (only his shadow), no HUD | correct |

Picture: `runs/wartok_budget_a.jpg` (left native, right auto). A second pair of runs at frame 600 of a plain direct
start (`wartok-native`, `wartok-auto`) showed the same: 614 against 85 volatile slots, 116 against 15 live upscales.
The level did load in both; the round 5 terrain fixes ended the loading-screen hang that round 3 saw.

Drakan 2 guard scene (Surdana from New Game, same recipe): 0 loads missed the native budget in the intro or in 60 s
of Surdana, and a run with 8192 KiB looks the same (`runs/surdana_budget_f.jpg`, mean difference 1.9 of 255, Rynn's
idle animation). The disc levels fit the native size, so `auto` leaves them alone.

Change: `[video] game_texture_cache_kb` (env `DRAKAN_TEX_CACHE_KB`, same values as before plus words): `auto` (default)
= 8 MiB in a campaign game **and in any level whose name is one of the packs' levels**, also when a direct start
replaces the level at the file layer (`packLevel` in `drakan_texture_budget_policy.h`); disc levels keep the native size.
`native` = never raised (the old `0`); a number of KiB (1024 to 16384) for every level. The 8 MiB default is unchanged
because the guest heap is shared with the dynamic guest memory work of this round; 16 MiB is one setting away.

## 3. HD copies dropped after 4 seconds (`hd_texture_keep_s`)

The GS has 4 MB of VRAM, so the game streams textures through it all the time and the host decodes them again by
content. The HD or upscaled copy of a texture is found by content hash, so it survives that, but the cache swept every
copy not drawn for 240 renderer frames (4 s at 60 fps) whatever the budget. Turning away for longer and back meant the
native texture until a worker had decoded the pack PNG again (10 to 26 ms per 4x image).

Surdana, same recipe (`recipes/turnback.txt`: look, turn away 10 s, turn back, twice), hd mips off in both (exe `bc849484`; the keep code is the same in `9b57acae`):

| | keep 0 (old 240 frames) | keep 60 s |
| --- | --- | --- |
| HD copies dropped for age | 150 | 0 |
| copies rebuilt after a drop | 62 | 0 |
| texture draws shown native while the HD copy was made | 4562 | 2716 (only first sightings) |
| host cache at exit / peak | 106 / 303 MB | 290 / 305 MB |

Change: `[video] hd_texture_keep_s` (env `GSR_TEXKEEP`), seconds since the last draw, 4 to 600, default 60; `0` = the
old 240 frame rule. The budget LRU still applies on top. Counters in the `GSR_TEXUP_LOG=1` lines (`[texup] frame ...`
every 1200 frames and at exit).

## 4. HD copies sampled 8 texels per pixel (`hd_texture_mips`)

The shader picks the mip level from the GS LOD (the game's K and L, minus log2 of the internal scale), then samples
level l of the bound texture. An HD or upscaled copy is N times larger (4x or 8x), and its level l is N times the game's
level l, so every distance drew from a level N times too fine: a distant ground texture at 8x was point or bilinear
sampled about 8 texels per pixel, which shows as shimmer and sparkle in motion (DLSS's jitter turns it into frame to
frame noise at a still camera). The copy's chain also stopped at the game's last level.

Change: `[video] hd_texture_mips` (env `GSR_HDMIPS`), default 1. For a mip-mapped draw (MMIN 2 to 5, MXL above 0, not
a sprite or render target) of a copy with factor N, the level is chosen on the copy's own texels (GS LOD + log2 N, and
the anisotropic footprint level likewise), clamped to the game's last level + log2 N, for which the copy now gets the
extra levels; the magnification / minification filter still follows the game's own level. The anisotropic taps still
start only where the game's texture is minified (`9b57acae`; the first version, `bc849484`, also started them on the
copy's minification, which adds taps on near ground). Textures without game mip levels, sprites, the HUD and render
targets are unchanged, and with the setting off or without HD the shader evaluates the old expressions (`hdShift = 0`;
by construction, not by a replay). On `9b57acae` the renderer tests `synthetic`, `synthetic_webgpu`,
`synthetic_webgpu_vulkan`, `rt_sampling_pixels`, `draw_join`, `draw_state_cache`, `rhi_texture_draw_data`,
`texture_decode`, `gs_hd_texture` and `texture_budget_policy` pass; the full 193-test run passed on `bc849484`.

Frame to frame mean absolute difference (of 255) of two consecutive frames at a still camera, i.e. what DLSS's jitter
makes of undersampled texture; 4K, DLSS quality, HD uploads synchronous (`GSR_TEXUP_SYNC=1`), foliage wind off
(`recipes/pose.txt`, `tools/flicker.py`), exe `9b57acae`:

| Surdana start pose (Drakan 2) | hd mips off | on |
| --- | --- | --- |
| far hills, first pose / pitched down | 0.063 / 0.092 | 0.047 / 0.065 |
| far ground | 0.151 / 0.161 | 0.094 / 0.103 |
| mid ground | 0.084 / 0.064 | 0.072 / 0.034 |
| near ground | 0.174 / 0.167 | 0.083 / 0.035 |

With foliage wind on and the first shader version the far ground went from 0.585 to 0.153. Pictures (left off, right
on): `runs/mips2_surdana_c_crop.jpg`, `runs/mips_surdana_crop.jpg`, `runs/mips_surdana_ground.jpg`: the same detail,
calmer distant texture, no visible blur. Wartok Canyons direct start (`runs/mips_wartok_a.jpg`): no measurable
difference either way (far ground 0.137 against 0.162 with Arokh moving in that region, other regions equal); the
converted ground is mostly the terrain layer path and 128 px sources, so there is less to gain there.

## 5. Frame time

`scripts/perf/scene_ab_bench.py` with the shared turn lock, scene `surdana` (Level 1A start pose, the owner's ini),
variants old (`GSR_HDMIPS=0,GSR_TEXKEEP=0`) and new (`1,60`), 3 interleaved repeats, exe `9b57acae`
(`bench/surdana-tex`). At a still pose the keep time changes nothing, so this measures `hd_texture_mips` only.

| Run | other activity during the run | frame ms (median) | 1 % / 0.1 % low ms | GPU world ms | GPU total ms |
| --- | --- | --- | --- | --- | --- |
| old-0 | another game, GPU at 99 % before | 10.82 | 22.1 / 30.0 | 1.01 | 11.78 |
| new-0 | another game | 9.88 | 21.1 / 25.8 | 1.04 | 10.98 |
| new-1 | another game, 9 compiler processes (my runtime test build) | 9.65 | 19.3 / 22.3 | 0.86 | 9.99 |
| old-1 | quiet | 6.50 | 9.8 / 11.2 | 0.39 | 6.68 |
| old-2 | quiet | 6.83 | 10.7 / 12.5 | 0.50 | 7.01 |
| new-2 | quiet | 6.83 | 11.3 / 17.2 (one 28 ms frame) | 0.44 | 6.94 |

Only one new run had a quiet machine; it sits inside the old range for the median, the GPU world pass and the GPU total.
Its 0.1 % low is one 28 ms frame. The first shader version (`bc849484`, taps on the copy's minification) measured
+28 % in a busy pair (GPU world 0.67 to 0.91 ms), which is why `9b57acae` keeps the old tap rule. No cost was measured
for the default; a longer quiet benchmark would make that firmer.

The game texture memory raise costs nothing measurable on the host (guest heap only) and halves the host texture cache
peak in Wartok Canyons (section 2); the keep time trades about 190 MB of resident video memory in Surdana (290 against
106 MB at exit, same 305 MB peak) for no rebuilds.

## 6. Settings and old behaviour

| Setting (`[video]`) | Default | Old behaviour |
| --- | --- | --- |
| `game_texture_cache_kb` | `auto` | `auto` gave pack levels outside a campaign the native size; set `native` for the disc size everywhere |
| `hd_texture_keep_s` | 60 | 0 |
| `hd_texture_mips` | 1 | 0 |

## Not verified

* No Order of the Flame campaign game (`d1`, New Game from the card) was played on the new exe; the campaign kept its
  8 MiB before and the rule did not change for it.
* The editor's Play and multiplayer maps from a pack are covered by the level-name rule only (the play test rewrites the
  level path so the load carries the pack level's name; netplay maps the name); only the `DRAKAN_START_LEVEL` direct start
  was run.
* Nothing was hand-played: every run is `play_run.py` with autopress and scripted mouse turns, no walking or fighting.
* The WebGPU renderer was not run in the game (the WGSL change mirrors the HLSL one; the WebGPU synthetic tests ran).
* `material_detail` (off in the owner's ini) was not combined with `hd_texture_mips`; its maps keep their own full chain.
* 16 MiB of game texture memory was not re-evaluated after `748e9cde` let the game heap grow; 8 MiB already gave 0 misses.
* Frame time: one quiet pair only (section 5).
* No settings menu rows: the three settings are `drakan.ini` keys only.
