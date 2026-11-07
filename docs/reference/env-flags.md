# Environment flags of the gs renderer

All `GSR_*` variables (and the `DRAKAN_DLSS_*` / `DRAKAN_MOTION_*` ones the renderer reads) are parsed once,
on first use, into `gsr::RuntimeFlags` (`runtime/gs/src/runtime_flags.h`, accessor `gsr::runtimeFlags()`).
The struct documents each variable next to its field. The values are immutable afterwards, so changing the
environment of a running process has no effect; nothing in the repo does that. No flag is deliberately live.

Parse rules: **presence** = variable exists (even empty); **not-0** = exists and is not exactly `0`;
**first-char** = looks only at the first character; **atoi** = `atoi(v) != 0`; numbers use the C function named.

## Water, ray tracing

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_MODERN_WATER` | not-0 | off | Modern (ray-traced) water pass. |
| `GSR_RAY_REFLECTIONS` | not-0 | off | Ray-traced water and glossy opaque-material reflections (rough native textures keep their colour); independent of modern water. |
| `GSR_RAY_GI` | not-0 | off | Experimental ray-traced diffuse indirect illumination from nearby surfaces. |
| `GSR_RAY_SHADOWS` | not-0 | off | Ray-traced shadows. |
| `GSR_RAY_CPU_CACHE` | first-char != `0` | on | CPU cache of packed ray textures. |
| `GSR_RAY_UPLOAD_CACHE` | first-char != `0` | on | Skip byte-identical dynamic/native material attribute blocks in the ordered DXR material buffer. `0` restores full uploads for comparison. |
| `GSR_RAY_GPU_TIMING` | presence | off | Delayed DXR GPU timestamps: upload/AS build, sky reduction, opaque transport, composition and total; also logs material upload bytes. Reads only after the existing frame-slot fence. |
| `GSR_RAY_CPU_CACHE_CHECK` | first-char == `1` | off | Verify the CPU cache against a fresh pack. |
| `GSR_SURFACE_ASYNC` | first-char != `0` | on | Reconstruct current-world surfaces on a worker. `0` runs the same reconstruction inline. |
| `GSR_SURFACE_OVERLAP` | first-char != `0` | on | At a geometry join, finish independent D3D11 model preparation and help process queued surfaces in order. `0` leaves the whole queue to the worker for comparisons. |
| `GSR_RAY_WORLD_CACHE` | first-char != `0` | on | Incremental world triangle cache. |
| `GSR_RAY_PACKED_TEXTURES` | first-char != `0` | on | 32-bit packed ray textures. |
| `GSR_WATER_COVERAGE_DEBUG` | atoi (uint) | 0 | Water coverage debug mode passed to the shader. DXR `4`: the ray scene's receiver colour (texel times vertex colour), black without a receiver; `5`: the radiance that surface returns to a bounce ray; `6`: the incoming diffuse bounce light alone (white receivers, times four). 4 and 5 need ray shadows or a transport effect, 6 needs global illumination. |
| `GSR_WATER_TIME` | strtof | unset | Fix the water clock (finite, abs < 1e6). |
| `GSR_RAY_SCENE_DUMP` | string | unset | DXR scene dump path, plus `.models.csv` source metadata, at `GSR_RAY_SCENE_DUMP_EPOCH`. |
| `GSR_RAY_SCENE_DUMP_EPOCH` | positive integer | `200` | DXR dump world epoch. Pair with the same `GSR_TEMPORAL_DUMP_EPOCH`; early epochs can precede gameplay. Invalid/zero values keep the default. |
| `GSR_RAY_DIAGNOSTIC` | string | unset | `ao`: ambient occlusion only; `direct`: direct shadows only; `receiver`: land receiver confidence in grayscale. Requires ray shadows and native lights; unknown values preserve normal rendering. Recognized modes log an applied-mask receipt once after successful submission on each backend. |
| `DRAKAN_RAY_INCLUDE_SKY` | exact `1` | off | Replay the former sky-model inclusion in the ray scene for shadow comparisons. The `legacy-sky` case in `scripts/harness/shadow_review.py` enables this diagnostic. |
| `GSR_WATER_LOG` | presence | off | Log water passes (every `[ray-water]` pass line, not only the first three and slow ones). |
| `GSR_RAY_WATCHDOG_MS` | atoi | 3000 | Ray pass watchdog: a pass stuck this long on the GPU (7x as long on the CPU) is abandoned, the shared D3D11/D3D12 fences are released from the CPU and modern water/ray shadows turn off for the session. `0` = log stalls, never abandon. |
| `GSR_RAY_TEST_DROP_SIGNAL` | strtoull | unset | Test hook: the ray pass with this consumer fence value never signals D3D11, simulating a lost cross-API signal (the watchdog must recover). |

Fresh settings now default reflections off because they can run on opaque scenes without modern water. Explicit saved `ray_reflections=1` settings remain enabled. Global illumination also defaults off.

## Temporal upscaler / DLSS

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_UPSCALE` | `dlaa`/`quality`/`balanced`/`performance` (case-insens.), else 0 | config | Overrides the upscale mode. |
| `GSR_UPSCALE_PRESET` | `j`/`k`/`l`/`m`, else auto | config | Overrides the DLSS preset. |
| `GSR_UPSCALE_SHARP` | atof | config | Overrides sharpness. |
| `GSR_UPSCALE_TIMING` | presence | off | Print upscaler GPU timing. |
| `DRAKAN_DLSS_LOG` | presence | off | Backend log and NGX log callback. |
| `DRAKAN_DLSS_DIR` | string | unset | Extra folder searched for `nvngx_dlss.dll`. |
| `DRAKAN_DLSS_DEBUG` | `mv` = 1, other = 0 | unset | Motion-vector debug view. |
| `DRAKAN_DLSS_VALIDATE` | presence | off | Neural output validation readback. |
| `GSR_NR_GPU_TIMING` | presence | off | D3D12 GPU time of each NR evaluation; prints `[nr-gpu]` average/max every 240 evaluations. |
| `DRAKAN_DLSS_PRESENT_LOG` | presence | off | Log neural presents. |
| `DRAKAN_MOTION_VALIDATE` | presence | off | Object-motion residual validation. |
| `DRAKAN_MOTION_LOG` | presence | off | Per-epoch object-motion statistics. |
| `GSR_TEMPORAL_DUMP_DIR` / `_FRAME` / `_EPOCH` | string | unset | Single-frame pipeline stage dump (dir plus a frame or epoch, strtoull). |

## Comparison, screenshots, audits

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_MATCHED_COMPARE` | string | unset | Output prefix for on/off comparison images. |
| `GSR_MATCHED_COMPARE_EPOCH` | strtoull | unset | World epoch to compare. |
| `GSR_MATCHED_COMPARE_EFFECT` | `water`/`models`/`shadows`/`transport`/other | unset | Which effect is toggled off; `transport` compares diffuse GI and opaque reflections together, preserving water and shadows (SR off). |
| `GSR_WORLD_SHOT_AT` / `GSR_WORLD_SHOT` | strtoull / prefix | unset | Screenshot at a world epoch to `<prefix>.png`. |
| `GSR_WINSHOT`, `GSR_WINSHOT_AT`, `GSR_WINSHOT_PRESENT` | prefix / comma list (default `600`) / presence | unset | Window screenshots at frames (or present counts). |
| `GSR_SURFACE_PICK` (`x,y`), `GSR_SURFACE_PICK_EPOCH` | string / strtoull | unset / 70 | Report triangles under a raster pixel. |
| `GSR_SURFACE_AUDIT` | string (dir) | unset | Dump candidate transparent surface materials. |

## Textures

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_TEXUP` | `auto`/`off`/number | config | Texture upscale factor. |
| `GSR_TEXUP_FILTER` | `xbrz`/`smooth`/other=auto | config | Upscale filter. |
| `GSR_ANISO` | atoi | config | Anisotropy (clamped to 16). |
| `GSR_MIPBIAS` | `auto` or atof | config | Mip bias. |
| `GSR_TEXDUMP`, `GSR_TEXREPLACE`, `GSR_TEXUP_SYNC` | atoi | config | Dump / replace / synchronous upscaling. |
| `GSR_PALETTE_HD` | atoi | 1 | HD index maps for palette-only animated textures (`docs/reference/hd-textures.md` 9). |
| `GSR_PALETTE_HD_DUMP` | folder | unset | Writes the first 32 recoloured palette steps (`<hash>_native.png` / `_hd.png`). |
| `GSR_TEXDIR` | string | config | Texture pack folder. |
| `GSR_TEXBUDGET` | atoi MiB | config | Cache budget (<= 0 auto). |
| `GSR_TEXUP_LOG`, `GSR_TEXLOG`, `GSR_LODSTAT` | presence | off | Logging / LOD statistics. |
| `GSR_HLDUMP` | string (dir) | unset | Menu highlight debug dumps. |

## HUD / menu

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_HUDSCALE` | atof | config | HUD scale override. |
| `GSR_SMOOTHHL` | atoi | config | Smooth menu highlight. |
| `GSR_SPRITE_FOOTPRINT` | atoi | 1 | `0` restores legacy atlas coverage. |
| `GSR_HUDLOG`, `GSR_HUD_CANVAS_LOG`, `GSR_HUD_CORNER_LOG`, `GSR_SUBTITLE_LOG` | presence | off | HUD logging. |
| `GSR_HUD_CORNER_FROM` | strtoull | 0 | First frame of corner logging. |
| `GSR_INVENTORY_DRAWLOG` | strtoll, min 0 | off | Inventory draw log delay. |

## Draw / render-target debugging

| Variable | Type / rule | Default | Effect |
| --- | --- | --- | --- |
| `GSR_DRAWLOG` | `a` or `a-b` | off | Log draws of those vsync frames. |
| `GSR_DRAWLOGTBP` | strtol (base 0) | off | Log draws sampling this TBP. |
| `GSR_DRAWLOGV` | atoi | 3 | Vertices printed per logged draw. |
| `GSR_DRAWSTOP` | atoll | off | Execute only the first n draws. |
| `GSR_DRAWSKIP` | `a-b` | off | Skip draws a..b. |
| `GSR_RTDUMP` | comma list of draws | off | Dump target after those draws. |
| `GSR_RTDUMPD` | `a` or `a-b` | off | Dump target after every draw of those frames. |
| `GSR_RTDUMPF` | `a-b` | off | Dump all targets at the end of those frames. |
| `GSR_RTDUMP_ROWS`, `GSR_RTDUMP_DIR` | atoi / string | 0 / `.` | Rows read back / output directory. |
| `GSR_RTLIST` | presence | off | List targets in verbose draw logs. |
| `GSR_NATIVE_READBACK` | unset or not-0 | on | Native readback path. |
| `GSR_XFERLOG` | presence | off | Log every GS transfer start. |
| `GSR_SEAMFIX` | first-char != `0` | config | Sprite seam fix override. |
| `GSR_PROFILE` | strtod seconds | off | CPU profiler report interval (outside 2..60 uses 2). |

## Read outside the gs library (not centralised)

These `getenv` calls are not part of `gsr::RuntimeFlags`; each is read where it is used (all in `runtime/`):

| Variable | Where | Rule | Effect |
| --- | --- | --- | --- |
| `DRAKAN_MENU_MOUSE_LOG` | `input/MenuMouse.cpp` | presence | Logs every menu mouse press / release (cursor, view rectangle, hit test) to stderr. |
| `DRAKAN_OVERLAY_DEBUG` | `settings/Overlay.cpp` | presence (read once) | Logs every left click ImGui registers while the settings overlay is open. |
| `DRAKAN_UI_PROMPTS` | `ui/Prompts.cpp` | atoi != 0 | Force keyboard/mouse prompt glyphs on (`1`) or off (`0`). |
| `DRAKAN_INPUT_INI` | `ui/Prompts.cpp` | non-empty string | Path of `drakan_input.ini` used to look up binding names (default: next to the exe, then the working directory). |
| `DRAKAN_OVERRIDES` | `ui/Prompts.cpp` | non-empty string | Resource override folder (default: `overrides` next to the exe, then the working directory). |
| `DRAKAN_MODS` | `drakan_overrides.cpp` | non-empty string | Mod pack folder (default: `mods` next to the exe); see [modding.md](modding.md). |

The test programs read a few more (`DRAKAN_ISO`, `FMV_VDB`, `FMV_DUMP`, `FMV_VERBOSE`, `DRAKAN_DEMO_TXD`, `TEMP`); see the
skip reasons in the CMake test registrations. `GSR_UPSCALE` is read again by `ps2recomp/ps2xRuntime/src/lib/drakan_dlss.cpp`.

## Read in ps2recomp (`PS2X_*` / `DRAKAN_*`)

`ps2recomp/ps2xRuntime` (and `gsr_nr`) read about 180 further variables directly with `std::getenv` or through
`drakan_cfg::get*`. They were deliberately not refactored into one struct. The names below were collected from the
source; the meaning column is only filled where the code makes it plain. Parse rules vary per variable, check the file.

### Overrides of `drakan.ini` keys

`drakan_cfg::getBool/getString/getInt/getFloat(section, key, ENV, default)` (`drakan_config.cpp`): when the environment
variable exists it wins over the `drakan.ini` value of `[section] key`. `DRAKAN_INI` selects the ini file itself.

| Variable | `[section] key` | Default |
| --- | --- | --- |
| `DRAKAN_AUDIO` / `DRAKAN_AUDIO_DEVICE` / `DRAKAN_VOLUME` | `[audio] enabled` / `device` / `volume` (percent) | on / `default` / 100 |
| `DRAKAN_CONSOLE` | `[general] console` (open a console window) | off |
| `DRAKAN_ELF` | `[general] elf` (guest ELF path) | empty |
| `DRAKAN_LANG` | `[general] language` | `english` |
| `DRAKAN_NATIVE_FONT_ATLAS` | `[general] native_font_atlas` | on |
| `DRAKAN_QUIT_EXITS` / `DRAKAN_TITLE_QUIT` | `[general] quit_exits` / `title_quit` | on / on |
| `DRAKAN_SAVE_PAGES` | `[general] save_pages` (1..8) | 4 |
| `DRAKAN_CPU_AFFINITY` | `[general] cpu_affinity` | `auto` |
| `DRAKAN_PAUSE_UNFOCUSED` / `DRAKAN_RESUME_ON_CLOSE` | `[general] pause_when_unfocused` / `resume_on_close` | off / off |
| `DRAKAN_SUBTITLES` | `[subtitles] narration` | on |
| `PS2X_GS` | `[video] renderer` | `d3d11` |
| `PS2X_GS_SCALE` | `[video] scale` (internal resolution) | `auto` |
| `DRAKAN_WINDOW_WIDTH` / `DRAKAN_WINDOW_HEIGHT` | `[video] window_width` / `window_height` | 1280 / 960 |
| `DRAKAN_FULLSCREEN` / `DRAKAN_VSYNC` / `DRAKAN_FRAME_LIMIT` | `[video] fullscreen` / `vsync` / `frame_limit` | off / on / 0 |
| `DRAKAN_WS_ASPECT` (`PS2X_ASPECT` legacy) / `DRAKAN_FOV` | `[video] aspect` / `fov` | `4:3` / 80 |
| `DRAKAN_HUD_ASPECT` / `DRAKAN_HUD_SCALE` / `DRAKAN_SMOOTH_HIGHLIGHT` | `[video] hud_aspect_correct` / `hud_scale` / `smooth_menu_highlight` | on / `auto` / on |
| `DRAKAN_FILTER` / `DRAKAN_DEINTERLACE` / `DRAKAN_FLICKER_FILTER` / `DRAKAN_SHARP_PRESENT` | `[video] filter` / `deinterlace` / `flicker_filter` / `sharp_present` | `game` / `weave` / off / on |
| `DRAKAN_WORLD_RATE` | `[video] world_rate`; `uncapped` enables completion-driven world updates. For uncapped presentation also use `DRAKAN_FRAME_LIMIT=0` and `DRAKAN_VSYNC=0`. Positive display limits remain supported; after their deadline a late completed frame wakes presentation. See [cap/readback validation](../journal/2026-11-05/capped-completion-pacing.md) for tested behavior and remaining stalls. | unset |
| `DRAKAN_TICK_SNAP` | `[video] tick_snap` | on (with vsync: engine tick time in whole display refreshes, see perf.md) |
| `DRAKAN_SHOW_FPS` / `DRAKAN_FPS_POSITION` | `[video] show_fps` / `fps_position` | 0 / `top-left` |
| `DRAKAN_DLSS` / `DRAKAN_DLSS_PRESET` / `DRAKAN_DLSS_SHARPNESS` | `[video] dlss` / `dlss_preset` / `dlss_sharpness` | `off` / `auto` / 0.0 |
| `DRAKAN_DLSS_NEURAL` / `DRAKAN_DLSS_NEURAL_DIR` | `[video] dlss_neural` / `dlss_neural_runtime` | off / empty |
| `DRAKAN_DLSS_NEURAL_RESOLUTION` | `[video] dlss_neural_resolution` (`output` \| `render`; see `dlss.md` "Neural rendering resolution") | `output` |
| `GSR_MODERN_WATER` / `GSR_RAY_REFLECTIONS` / `GSR_RAY_SHADOWS` / `GSR_RAY_GI` | `[video] modern_water` / `ray_reflections` / `ray_shadows` / `ray_gi` | see the water table above |
| `DRAKAN_POLY_NEAR_CLIP` / `DRAKAN_POLY_NEAR_W` / `DRAKAN_POLY_NEAR_CLIP_LOG` | `[video] poly_near_clip`: clip the engine's projected fans (ribbons, beams) against the camera near plane and the GS coordinate window before they reach the GIF emitter (0 = original behaviour); near depth for the plane (default 8); log counters (every 16th clipped fan prints fans / clipped / dropped); with `PS2X_PERF=1` the `[perf]` line also shows `clip_fans/s`, `clip_us/s` (time inside the hook, original draw excluded, timer cost subtracted) and `clip_ee_pct` (of the EE thread CPU). PAL only | on / 8 / off |
| `DRAKAN_FOLIAGE_WIND` / `DRAKAN_FOLIAGE_STRENGTH` / `DRAKAN_FOLIAGE_LOG` | `[video] foliage_wind` / `foliage_wind_strength` (living foliage, `world-model.md` section 12); the log flag prints how many trees / plants were drawn every 5 s | off / 1.0 / off |
| `DRAKAN_MOUSE_AUDIT` / `DRAKAN_THREAD_BOOST` | per-packet and per-tick mouse log for `scripts/perf/mouse_audit_report.py`; `DRAKAN_THREAD_BOOST=0` disables the frame-thread priority / MMCSS / power hints (`mouse-microstutter.md`) | off / on |
| `GSR_BACKDROP_LOG` | prints the wide menu backdrop image chosen whenever it changes (D3D11) | off |
| `DRAKAN_HD_MOVIES` | `[video] hd_movies` (HD movie pack in `movies/`, `docs/reference/fmv.md` section 9) | on |
| `DRAKAN_INVENTORY_SCREEN` | `[general] inventory_screen` (`native` \| `modern`: the host-drawn Tab screen, `mouse-menus.md` section 13) | `native` |
| `DRAKAN_STRIP_SCREEN` / `DRAKAN_MAP_SCREEN` | `[general] strip_screen` / `map_screen` (`modern` \| `native`: the I strip and the M map as host-drawn screens, gameui `StripScreen` / `MapScreen`, or the older native-drawn views; `mouse-menus.md` section 12; read once at start) | `modern` |
| `DRAKAN_PAUSE_MENU` | `[general] pause_menu` (`native` \| `modern`: the host-drawn Esc menu, `mouse-menus.md` section 14; also F1 > Gameplay > Pause menu) | `native` |
| `DRAKAN_SAVE_MENU` / `DRAKAN_TITLE_MENU` | `[general] save_menu` / `title_menu` (`native` \| `modern`: the host-drawn Save / Load screens and title menu, `mouse-menus.md` section 15; also F1 > Gameplay and the modern Options > Gameplay page; a switch applies from the next opening, an open modern screen falls back to its native page) | `native` |

### Debugging and test aids (plain environment, no ini key)

| Variable | Meaning |
| --- | --- |
| `DRAKAN_ISO` | Disc image path for the runtime (`drakan_overrides.cpp`). |
| `DRAKAN_INPUT` | Input module on/off (`drakan_input_glue.cpp`); `DRAKAN_INPUT_INI` selects its ini; `DRAKAN_INPUT_MENU`, `DRAKAN_INPUT_SCREEN_LAYER`, `DRAKAN_INPUT_AUTOSTART`, `DRAKAN_INPUT_SKIP`, `DRAKAN_INPUT_DEBUGKEYS` tune it. |
| `DRAKAN_INPUT_SCRIPT` | Scripted input recipe (format in `docs/reference/input.md`). `DRAKAN_PADSEQ` presses DualShock buttons at given times; `DRAKAN_AUTOPRESS`, `DRAKAN_AUTOPRESS_UNTIL`, `DRAKAN_AUTOPRESS_NOSTART`, `DRAKAN_AUTOSTART` auto-press buttons for headless runs. |
| `DRAKAN_INPUT_SCRIPT_LENIENT` / `DRAKAN_AUTOPRESS_STOP_AFTER_LOAD` | `LENIENT=1`: a recipe line the parser rejects (no leading time, unknown command, bad arguments) is skipped; by default every rejected line is logged as `line N: ...` and the run exits with code 2 at load. `AUTOPRESS_STOP_AFTER_LOAD=1` (`play_run.py --keep-cutscene`): the autopress taps stop once the first level after the title has loaded, so a `--level` cut scene level plays out. |
| `DRAKAN_INPUT_LOG`, `DRAKAN_INPUT_LOGFILE`, `DRAKAN_INPUT_LOGDUMP` | Log pad input to stderr / a file / dump the log. |
| `DRAKAN_WINDOW_POS` | Place the window at a fixed position (test aid). |
| `DRAKAN_SHOT_DIR`, `DRAKAN_OVERLAY_SHOT_DIR` | Directory for screenshots / settings-overlay screenshots. |
| `DRAKAN_SKIP_MEMCARD_WARNING` | Skip the no-memory-card warning screen. |
| `DRAKAN_MEMCARD_DIR` | Memory card 1 folder (card 2 = sibling `mc1`) instead of `<elf dir>/mc0`; an empty folder boots without saves (`docs/reference/input.md` 13). |
| `DRAKAN_GAME` / `[general] game` | `ask` \| `d2` \| `d1`: which game the title menu runs; `d1` = Drakan: Order of the Flame (needs a pack with `campaign = "ootf"`, [modding.md](modding.md) "Campaign packs"): New Game opens the campaign start level, own save folder `<card>_ootf`. `ask` (default) shows the game card when a campaign is installed. |
| `DRAKAN_START_LEVEL`, `DRAKAN_START_LEVEL_FROM` | New Game loads this level (long name, e.g. `Level 4A - Ravenshold`) in place of `Level 1A - Surdana` (or of `_FROM`): HLE file-open redirect (`runtime/hle` `FileService::setLevelRedirect`). `_FROM=Level 0 - Intro` loads the level in the intro's slot: no movie, no opening cut scene; the play test then also rewrites New Game's level path so the engine knows the level by its real name (exits and arrivals work, `drakan_lifecycle::setLevelPathRewriter`). Works for disc levels, a mod pack's level copy and converted OotF levels (the editor's Play uses it for every level, `docs/journal/2026-11-02/level-copy-direct-play.md`). |
| `DRAKAN_PLAYTEST_POS` | `"x y z [yaw]"` (game units, yaw radians): once the player is under control she is moved there and held for a few seconds while the terrain streams in (`drakan_playtest.cpp`, set by the editor's "Play from here"). |
| `DRAKAN_PLAYTEST_CARD` | Editor desktop shell only (never reaches the game): `1` gives the game a fresh, empty memory card (`%TEMP%/drakan-editor-playtest/card<n>/mc0` as `DRAKAN_MEMCARD_DIR`). Every editor Play with "Skip title..." sets it: on the player's own card the title's first row is Continue, so the Cross taps loaded the latest save instead of the level. The editor also sets `DRAKAN_GAME=d2` so the game card is not shown. |
| `DRAKAN_FAST_START` | `1` (the editor's Play): the boot logo movie is not held to the PAL rate (runs at the uncapped engine tick until the first movie completes), the title presses New Game as soon as it is up (with `DRAKAN_START_LEVEL`), and the play-test placement starts after 60 boundaries instead of 240. Launch to level about 10 s instead of 18 s (`docs/journal/2026-11-02/editor-play-direct.md`). |
| `DRAKAN_PLAYTEST_GOD` | `1`: the player's hit points are kept at their maximum (`drakan_playtest.cpp`; the editor's "Player cannot die while testing"). |
| `DRAKAN_PLAYTEST_WATCH` | `class=<hex class id>` and/or `ref=<registry ref>` and/or `near=<units>` (plus `player=1` for Rynn's own line, `words=`, `data=`, `dataoff=<hex byte offset of the data window>`, `max=`, `every=`; `runtime/world/include/world/Watch.h`): once a second prints `[watch t] ref= h= cls= pos= yaw= \| e+<off>=<word> ... \| d+<off>=<word> ...` (the non-zero words of each matching object's record and of its instance data) to stderr, read only. Implies the headless world sampler. Door open state: `class=10013`, instance data `d+10` (0 closed, 1 or -1 open, `d+40` locked) and the entity's yaw (`e+20..`, a door swings about its origin) and `e+4` bit 8 while it moves, see `docs/journal/2026-11-07/door-proof.md`. | unset |
| `DRAKAN_PLAYTEST_HOLD` / `DRAKAN_PLAYTEST_SOFT` | Boundaries to hold the play-test position (default 240); `SOFT=1` pulls her back only beyond 400 units (so a mount can move her). Released when the controlled actor changes. |
| `DRAKAN_PLAYTEST_AUTOAIM` / `_AUTOAIM_RANGE` / `_AUTOAIM_PITCH` / `_ALT` / `_WAYPOINTS` / `_CLIMB` | Test aids for fights a time-based input script cannot win (`drakan_playtest.cpp`, `drakan_autoaim.h`). `AUTOAIM=<runtime class hex>`: every boundary the controlled actor is turned (yaw only, the `place` convention) to face the nearest living object of that engine class (hit points at data + 0x24c above 0), within `AUTOAIM_RANGE` units (default 20000); `0x1001e` = Ebon Dragon, `0x1003b` = Alwarren war giant. `ALT=<y>`: the controlled actor's height is held at y (once the waypoints, if any, are finished). `WAYPOINTS="x y z; x y z"` (+ `CLIMB`, units per boundary, default 25, `WP_RADIUS`, default 2500, and `WP_ACTIVATE`, xz distance from the first point at which steering starts (default always), `WP_HOLD`, boundaries to hold x / z over the last point so a held W stays there): an autopilot, the actor is turned toward the current point and its height moves toward the point's; the next point is taken within the radius in xz (a recipe only holds W). Never set by the game or the editor. |
| `DRAKAN_DRAGON_HP_PROBE` / `DRAKAN_DRAGON_HP_POKE` | Co-op research aids: log number-like words of Arokh's data every 3 s; write `hexoff:float[,..]` into it (`6b4:40` = dragon health 40). See `docs/journal/2026-11-01/coop-dragons-health-mount.md`. |
| `DRAKAN_TEST_LEVEL_SAVE` | `1`: every save screen opens in level-change mode (save screen mode 1, request `{1, "", no callback}` through the ctor `0x4442e0`), so a harness reaches it from the pause menu's Save Game (`play_run.py --level-save`). The slot click prepares the folder but writes no `.rsg` (the level exit's callback does that). |
| `DRAKAN_LOG_DIR` | Folder for `drakan.log` / `drakan.1.log` instead of the exe folder (test runs). |
| `DRAKAN_PARRY_AUDIT`, `DRAKAN_COMBAT_AUDIT`, `DRAKAN_COMBAT_CALL_TRACE` | Combat audit log lines used by the harnesses (`scripts/harness/`). |
| `DRAKAN_DEBUG_OPEN_SHOP` | `<n>[,<n>...]`: once Rynn has been under control for 150 frames, open the level's n-th shop (class 0x100ac entity) through the game's `33be50`; the next number opens after the shop closed. Logs every shop found. Surdana: 1 blacksmith, 2 Jade's Magic Emporium, 3 potions (`docs/reference/mouse-menus.md` 11). |
| `DRAKAN_DEBUG_GOLD` | `<n>`: Rynn's gold is set to n once she has been under control for 150 frames (stored like the skill points, `rotr8(*0x4fbae0 ^ *0x4fe86c)`); on the same frame as `DRAKAN_DEBUG_OPEN_SHOP`, so the shop shows it. Test aid for `scripts/harness/vendor_shop.py`. |
| `DRAKAN_DEBUG_SKILL_POINTS` | Unspent skill points written once in gameplay (test aid for the Skills page). |
| `DRAKAN_DEBUG_TAB_HIDE` / `DRAKAN_DEBUG_PAUSE_HIDE` | `1`: the host Tab / strip / map screen, or the host pause menu, never draws, so the EE watchdog's native fallback runs after 90 frames and 2.5 s (test aid, `mouse-menus.md` sections 13 / 14). |
| `DRAKAN_DEBUG_TITLE_HIDE` / `DRAKAN_DEBUG_SLOTS_HIDE` | `1`: the modern title menu, or the modern Save / Load screen, never draws, so the EE watchdog brings the native page back (test aid, `mouse-menus.md` section 15). |
| `DRAKAN_DEBUG_TITLE_PC_PAGES` | `0`: the modern title menu acts as if the modern PC Settings pages were missing, so PC Settings and the new mod packs card's Open Mods menu open the F1 overlay (its Mods page) instead (test aid for that fallback; the pages always exist in the Windows build). |
| `DRAKAN_DEBUG_PAUSE_DRAWLOG` | `1`: logs the hidden screen draws of the modern pause (`2bd1b0` entry / return for the owned Options / Audio / Controller screens, the box the host answers, and the pause when a timer message is due; test aid, `mouse-menus.md` section 14). |
| `DRAKAN_NATIVE_OPTIONS_INI` | Path of `drakan_native_options.ini` (the game's own options, saved whenever they change; default next to the exe). `scripts/harness/play_run.py` points it into the run folder, so test runs that change options never write the player's file. |
| `DRAKAN_DEBUG_TAB_PEEK` | `1`: the host Tab / strip / map screen runs (input, grid and map targets, watchdog) but draws nothing, so screenshots show the native frame its icon and map crops are taken from (test aid, `mouse-menus.md` section 12). |
| `DRAKAN_NO_ACTIVATE` | `1`: the window is shown behind the other windows and never activated, and the scripted `shot` skips its desktop BMP copy (only the renderer PNG is written); `scripts/harness/play_run.py` sets it, so a test run does not take the foreground. |
| `DRAKAN_FRAME_AUDIT`, `DRAKAN_RUNTIME_PROFILE`, `DRAKAN_DEBUG_HOLD`, `DRAKAN_F1_ALWAYS` | Frame-rate audit, runtime profile, pause-gating debug aids. `DRAKAN_FRAME_AUDIT=2` adds a per-frame `[ft]` timeline on one steady-clock axis (microseconds): `P` present tick (+ shown world serial), `F` EE display flip, `V` guest vblank (+ lateness), `U` engine update (+ `dt` ms), `W` GS record over 8 ms (+ type, duration), `H` present tick over 8 ms (lock / present split). `DRAKAN_FRAME_AUDIT=3` also samples the EE thread's RIP every ~0.5 ms (`X` lines; resolve with `scripts/perf/ee_samples.py <log> <drakan.map>` to see what the EE thread ran inside each long present gap). |
| `DRAKAN_HLE_LOG`, `DRAKAN_RES_DELIVERY`, `DRAKAN_RES_LATENCY_US`, `DRAKAN_RES_NOFAIL` | Resource-service HLE log level and delivery / latency / failure behaviour. |
| `DRAKAN_SND_LOG`, `DRAKAN_SND_DBG`, `DRAKAN_SND_PUMP`, `DRAKAN_STEREO_SYNC`, `DRAKAN_AUDIO_DUMP`, `DRAKAN_AUDIO_STALL_MS` | Sound HLE diagnostics, audio dump, stall threshold. |
| `DRAKAN_STOMP_PROBE` | Exactly `1`: install a PAL-only, read-only native cinematic readiness probe. Logs scene elapsed/pause/delta time and pending actor/resource ids with their caller and cache state, plus bounded RV-loading phase entry/return/resume/yield observations. Other regions or missing hook functions leave the probe off. It does not force readiness or change playback timing. Only completed readiness calls are inspected; calls yielding to another guest thread are not inferred. |
| `DRAKAN_STOMP_PROBE_LINES` | `atoi`, clamped1–4096, default512: maximum probe detail lines. A final suppression notice follows the limit. Scene state and repeated pending keys are sampled at most once per second; bookkeeping is bounded to512 keys. |
| `DRAKAN_TEX_CACHE_KB` | KiB, 1024-16384; `0` = off. PAL native texture cache budget (`drakan_texture_budget`): unset = 8 MiB while a campaign pack is played (the disc game keeps the native 0x566666 bytes), a number applies to every game. Raised each time the cache would overflow; without it Ruined Village's scene 1.2a waited 0 to over 110 s for textures ([journal](../journal/2026-11-06/issue-5-village-scene-timing.md)). | unset |
| `PS2X_WATCHDOG_DUMP_DIR` | Unset by default. Existing absolute directory for one forensic capture on the first silent-input stall (8 seconds), not ordinary periodic watchdog ticks. Writes a uniquely named32MiB guest RAM file and text containing all32 lower GPR words before/after RAM capture, live PC/thread and dispatch history. Guest execution is not suspended, so the capture is explicitly best-effort. Use an ignored private output folder such as an absolute `build/.../fault` directory. No guest memory or registers are changed. |
| `DRAKAN_TRACE_FN`, `DRAKAN_TRACE_N` | Trace calls of one guest function (decimal address), N hits. |
| `DRAKAN_SUB_LOG`, `DRAKAN_SUB_OFFSET_MS`, `DRAKAN_TEST_SUB`, `DRAKAN_TEST_SUB_AT` | Subtitle log level, timing offset and a synthetic test subtitle. |
| `DRAKAN_WS_LOG`, `DRAKAN_WS_TEST_SEQ`, `DRAKAN_INVENTORY_VIEWPORT_LOG`, `DRAKAN_INVENTORY_TEXT_ALL_SCREENS`, `DRAKAN_INVENTORY_TEXT_CAPTURE`, `DRAKAN_INVENTORY_TEXT_LIMIT`, `DRAKAN_INVENTORY_TEXT_LOG` | Widescreen / inventory typography logging and test sequences. |
| `DRAKAN_NR_TEST_SEQ`, `DRAKAN_DLSS_PRESENT_VERIFY`, `DRAKAN_DLSS_PRESENT_VERIFY_AT`, `DRAKAN_OBJECT_MOTION_DIAG`, `DRAKAN_OBJECT_MOTION_LOG`, `DRAKAN_CAMERA_RESET_DIAG` | DLSS / neural-rendering and object-motion diagnostics. |
| `DRAKAN_NATIVE_FONT_TTF`, `DRAKAN_NO_PROMPT_EXCLUDE`, `DRAKAN_UI_PROMPTS` | Native font atlas TTF path; UI prompt test aids. |
| `DRAKAN_CRASH_DUMP`, `DRAKAN_CRASH_TEST`, `DRAKAN_WAIT_PID` | Crash dump control, forced-crash test, wait for a process id before starting. |
| `DRAKAN_RENDER_QUEUE`, `PS2X_VU1_THREAD`, `PS2X_VU_SPIN` | Asynchronous VU1 / render queue and its spin policy (`vu/ps2_vu1_async.cpp`). |
| `DRAKAN_VU_AOT`, `DRAKAN_VU_BLOCKS`, `DRAKAN_VU_RECOVERY`, `DRAKAN_VU_STATS`, `DRAKAN_VU_TIMED_Q` | VU1 ahead-of-time microcode: enable, block table, recovery, statistics, timed Q register. |
| `DRAKAN_VU_FUNC` | `0` runs VU1 programs on the cycle-exact engine instead of the functional one (`vu/ps2_vu1_func.h`). |
| `DRAKAN_VU_LANES`, `DRAKAN_VU_LANES_STATS` | Functional engine: one-block loops run several iterations per SIMD step (`vu/aot/ps2_vu1_lanes.h`). Default: the widest the CPU supports (AVX-512 4 iterations, AVX2 2); `0` off, `2` forces AVX2. `_STATS=1` prints iterations / steps / dropped steps and each loop's eligibility at exit. |
| `DRAKAN_VU_BATCH` and `DRAKAN_VU_BATCH_*` (ADAPTIVE, DEPENDENCIES, LAZY_LOADS, NATIVE_PACKET, PREDICATES, PREDICT, PROFILE, REUSE_PROFILE, STATIC_REGS, STATIC_REGS_VERIFY, STREAM, THREADS, VERIFY) | VU1 batch executor features and their verification / profiling switches (`vu/ps2_vu1_batch.cpp`). |
| `DRAKAN_VU_NATIVE_KERNELS`, `DRAKAN_VU_NATIVE_LOOPS`, `DRAKAN_VU_NATIVE_LOOP_LIMIT`, `DRAKAN_VU_MATRIX_LOOP`, `DRAKAN_VU_TRANSFORM_KERNEL`, `DRAKAN_VU_TRACE` | Native (C++) replacements for hot VU1 loops and their selectors. |
| `DRAKAN_VUCHECK`, `DRAKAN_VUCHECK_VERBOSE`, `DRAKAN_VU_BENCH`, `DRAKAN_VU_BENCH_NOCHECK`, `DRAKAN_VU_RECORD`, `PS2X_VU1_CHECK`, `PS2X_VU1_CTXCHECK` | Compare native VU1 paths against the interpreter, benchmark them, record VU1 runs. |
| `PS2X_VU1_DUMP`, `PS2X_VU1_DUMP_BAD`, `PS2X_VU1_DUMP_ONLYERR`, `PS2X_VU1_DUMP_SCAN`, `PS2X_VU1_DEBUG_TAG`, `PS2X_VU1_STATS`, `PS2X_VU1_TRACE_FLAG`, `PS2X_VIF_LOG`, `PS2X_VIF_FAST_UNPACK`, `PS2X_DBG_VP`, `PS2X_GIFDMA_LOG` | VU1 / VIF1 / GIF DMA dumps, statistics and tracing. |
| `PS2X_GS_ASYNC`, `PS2X_GS_PACING`, `PS2X_SCENE_PACING`, `PS2X_GS_STRETCH`, `PS2X_GS_LOG`, `PS2X_GS_STATS`, `PS2X_GS_SHOT`, `PS2X_GS_SHOT_AT`, `PS2X_GS_DISPLOG`, `PS2X_GS_DISPLOG_FROM` | Renderer bridge (`gs/gs_gsr_bridge.cpp`): async mode, frame pacing mode, stretch to window, logs, statistics, screenshots. |
| `PS2X_GS_CAPTURE`, `PS2X_GS_CAPTURE_DIR`, `PS2X_GS_CAPTURE_FROM` | Record a GS capture (`gscap_*.bin`, input of the `live` gs test). |
| `PS2X_GS_CAPTURE_ROLL=<file>` | With `PS2X_GS_CAPTURE=N`: restart the capture every 8 frames until `<file>` exists, then record up to N frames (catches a rare bad frame). |
| `DRAKAN_VU_RING=<file>,<N>`, `DRAKAN_VU_TRIGGER=<file>` | Keep the last N VU1 runs (plus `.states`) and dump them when `<file>` appears; the GS creates `<file>` and `<file>.2` at the first perspective draw with a negative Q vertex. Replay with `vu1ref_ringscan`. |
| `PS2X_GIFDMA_FIND`, `PS2X_WATCH_RDRAM=<lo>,<hi>,<triggerFile>`, `PS2X_WATCH_VALUE` | Debug: log GIF DMA payloads that start with a PRIM 0x7d four vertex strip with the EE `$ra`; after `<triggerFile>` appears catch writers to an RDRAM range (page protection plus single step, 3 s) and print the host RIPs (resolve with `drakan.map`). |
| `PS2X_IPU_DUMP`, `PS2X_IPU_DUMP_N`, `PS2X_TRACE_IPU` | Dump the first N decoded IPU movie frames as raw RGBA; trace the IPU. |
| `GSR_GPU_COPY` | `0`: local-to-local transfers between two whole render targets (the game's frame-buffer copy) go through memory again instead of one GPU copy (D3D11). |
| `GSR_RB_SKIP`, `GSR_RB_VERIFY` | `GSR_RB_SKIP=0` turns off the GPU-readback avoidance (reads of blocks an applied upload already made identical, dirty-row trimming, frame-buffer-onto-itself copies) and restores the always-read-back behaviour. `GSR_RB_VERIFY=1` (D3D11) reads the target back on every skip and compares it with memory, logging `[rb-verify]` lines (`MISMATCH` = stale data would have been served). |
| `GSR_PARTIAL_UPLOAD` | D3D11: default on. `0` disables masked GPU updates for small unaligned image uploads. The default preserves untouched GPU bits and tracks the actual written prefix, including fragmented or abandoned transfers; exact readers can skip readback only for bits proven identical in CPU/GPU memory. Unsupported targets and larger transfers retain the readback fallback. |
| `GSR_RB_CLEAN_SYNC` | D3D11: default on. `0` disables retention of known-current CT32 blocks across CPU page-row refreshes. A row refresh makes its blocks current and preserves the proof for disjoint rows; GPU draws invalidate it through the target write version. |
| `GSR_RB_PREFETCH` | D3D11: default on. `0` disables early staging copies for recently read small scratch targets. Reuse requires matching write version, dirty rows and render scale; world/effects targets are excluded. This moves the copy ahead of unrelated GPU work without returning older data. |
| `GSR_MODEL_MOTION_UPLOAD` | D3D11 model motion guides use queued vertex-buffer updates by default. Set to `0` to compare the dynamic Map/Discard path. See [combat hitch measurements](../journal/2026-11-05/occasional-frame-hitches.md). |
| `GSR_RB_SOLID` | D3D11: default on. `0` disables exact CPU mirroring of bounded constant-color scratch sprite batches. Only clean CT32 targets with proven coverage/color and no overlapping dirty owner qualify; ordinary GPU drawing continues. Other draws retain readback. `GSR_RB_VERIFY=1` checks mirrored bytes against GPU output. See [validation](../journal/2026-11-05/solid-sprite-readback.md). |
| `GSR_RB_TRACE` | D3D11 diagnostic: presence logs up to 4096 read/write flush, readback and prefetch records with target geometry and VRAM block spans. Unset normally. |
| `GSR_RB_DRAW_TRACE` | D3D11 diagnostic: presence logs up to 64 recently read small-target draws and 64 reader rectangles to identify readback dependencies. Unset normally. |
| `GSR_RAY_ALPHA_FAST_PATH` | Both ray backends: default on. `0` restores interpolation/texture sampling even for constant alpha-test results. The default returns immediately for NEVER and for opaque ALWAYS materials; cutout tests and translucent alpha retain the original evaluation. |
| `GSR_RAY_ALPHA_COVERAGE` | Both ray backends: default on. Exact cached coverage of immutable native textures can prove a nonblended GREATER-0 cutout always passes when every texel has nonzero alpha. Original material classification stays intact; any zero-alpha texel retains sampling. `0` forces the original shader alpha evaluation for comparison. Replacement textures invalidate coverage; ad-hoc identity-zero inputs are rescanned. |
| `GSR_RT_UPLOAD_SPECIALIZE` | D3D11: default on. Use constant-scale exact CPU replication at 2x/4x/8x, a bulk copy at 1x, and doubling runs for duplicated rows. Other scales retain the generic path; `0` restores the variable-scale expansion for comparison. Pixel values, upload sizes and visual settings are unchanged. |
| `GSR_RAY_ACCEPTED_ALPHA` | Both ray backends: default on. After a hit has passed the material alpha test, its nonblended opacity is known to be one. Avoid sampling it again; blended materials and instance fades retain their opacity. `0` restores the second evaluation. |
| `GSR_RAY_ZERO_AMBIENT` | Both ray backends: default on. Skip ambient-occlusion queries only when all three sanitized ambient-light channels are exactly zero. Any positive channel retains AO. `0` traces the zero-contribution queries for comparison. |
| `GSR_RAY_PRUNE_REJECTED` | Both ray backends: default on. Omit an entirely reconstructed opaque frame batch from shadow, receiver and AO query masks when every triangle fails their existing geometry-eligibility test. Keep it in water-reflection queries; mixed batches retain per-hit filtering. `0` restores traversal for comparison. GPU timing logs include `rejected_frame_triangles`. |
| `PS2X_PERF`, `PS2X_PROFILE`, `PS2X_HOSTPROF`, `PS2X_HOSTPROF_DELAY` | Per-second perf line, profiler, host RIP sampling (`docs/reference/perf.md`). |
| `PS2X_TRACE_CALLS`, `PS2X_TRACE_FROM`, `PS2X_TRACE_PCS`, `PS2X_TRACE_RING`, `PS2X_TRACE_SCHED`, `PS2X_TRACE_WAKE`, `PS2X_PEEK`, `PS2X_WATCH_POLL`, `PS2X_WATCH_RING`, `PS2X_WATCH_W` | Guest call / scheduler / wakeup tracing and guest-memory word watches. |
| `PS2X_WATCHDOG`, `PS2X_MAX_SECONDS`, `PS2X_FTZ`, `PS2X_FUZZ_UNWIND`, `PS2X_UNWIND_FIX`, `PS2X_RPCLOOP_RETURNS` | Watchdog / run-time limit, flush-to-zero, unwinding and RPC-loop behaviour switches (`ps2_runtime.cpp`, `Kernel/`). |
| `DRAKAN_MC_TRACE` | `1`: one `[mc-trace]` stderr line per memory card call (`sceMcGetInfo/GetDir/Sync/Open/Read/Write/Close/Mkdir/Chdir`) with host thread, guest `ra` and the pending flag (`Kernel/Stubs/MemoryCard.cpp`); finds a guest card-polling loop that never completes. |
| `DRAKAN_TEST_FREEZE_AT` | frame number: the guest thread sleeps 12 s at that input-boundary frame once, to exercise the `[stall]` detector (`drakan_input_glue.cpp`). |
| `DRAKAN_DRAGON_PROBE`, `DRAKAN_DRAGON_RIDER_OFFSET`, `DRAKAN_DRAGON_RIDER_YAW`, `DRAKAN_DRAGON_TEST_OFFSET` | Co-op dragons: log the local rider's offset in the dragon frame; saddle offset `x,y,z` and heading (radians) of a mounted partner's body; draw a mounted partner that far off (test aid). `docs/journal/2026-10-31/coop-dragons.md`. |
| `DRAKAN_GHOST_ACTS` | Mirror-mode stand-in plays the clip of each listed activity (`idle,run,attack,hit,death,block ...`) for 3 s in turn (test aid, `scripts/harness/coop_standin.py`). |
| `DRAKAN_NATIVE_CHARACTER_SHADOWS` | Diagnostic: exact value `0` skips the native character projector before silhouette preparation; `1` or unset retains native projection and selected-avatar substitution. Use with `GSR_RAY_SHADOWS=0/1` to attribute overlapping shadows. This is an isolation switch, not automatic shadow replacement. Requires the normally enabled multiplayer hooks. |
| `DRAKAN_COOP_SPEW_PREEMPT` | Co-op chests: allow scheduler preemption inside the chest spew (default: the spew runs without it so its loot is keyed at once). |
| `DRAKAN_CHAR_PREVIEWS`, `DRAKAN_CHAR_PREVIEW_MAX`, `DRAKAN_CHAR_PREVIEW_LEFT` | Character studio: in a loaded level (needs `DRAKAN_GHOST=1`) spawn each co-op roster character (`all`, or hex class ids, comma separated) beside the standing player, turn it in 12 steps and write `<previews>/chars/<id hex4>.png` (turntable strips the co-op lobby plays). MAX = first N characters, LEFT = other side of the picture. `scripts/harness/char_previews.py`, `docs/journal/2026-11-01/map-vote-character-previews.md`. |
| `DRAKAN_TEXSCALE`, `DRAKAN_TEXSCALE_LOG`, `DRAKAN_TEXSCALE_BPP`, `DRAKAN_TEXSCALE_CHARGE`, `DRAKAN_TEXSCALE_DELAY_US` | Experimental native mip filter for 16 / 32 bpp surfaces (`drakan_texscale.cpp`, PAL 0x1c0f28). `DRAKAN_TEXSCALE=1` enables it (off by default: intermittent heap stalls in Islands Night); `_LOG=1` times the guest-path calls (`2` logs the lock step); `_BPP` bit mask of depths (1 = 16, 2 = 24, 4 = 32; default 5); `_CHARGE=0` drops the simulated-cycle charge; `_DELAY_US` busy wait per call (test aids). |

### Save reference audit

`DRAKAN_SAVE_REF_PROBE=1` installs a PAL-only, read-only observer for native save loading and slot-reference resolution. It logs static-range references with the caller, thread and current saved-record slot/ID, plus completion counters. `DRAKAN_SAVE_REF_PROBE_LINES=1..4096` bounds saved-record detail lines per load (default 512); non-record UID-map/global references have a separate 32-line budget, so their early volume cannot hide later record references. Deduplication and active record-scope storage are bounded. It forwards each guest call unchanged, retains thread/invocation-specific record scopes across yields, and does not infer a yielded call as a completed return. No hooks install by default or on non-PAL profiles. Intended for private save-format audits, not gameplay behavior changes.
