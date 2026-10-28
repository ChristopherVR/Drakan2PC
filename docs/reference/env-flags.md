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
| `GSR_RAY_REFLECTIONS` | unset or not-0 | on | DXR reflections in the water pass. |
| `GSR_RAY_SHADOWS` | not-0 | off | Ray-traced shadows. |
| `GSR_RAY_CPU_CACHE` | first-char != `0` | on | CPU cache of packed ray textures. |
| `GSR_RAY_CPU_CACHE_CHECK` | first-char == `1` | off | Verify the CPU cache against a fresh pack. |
| `GSR_SURFACE_ASYNC` | first-char != `0` | on | Reconstruct ray-scene surface triangles and prepare model motion on the surface builder thread (`0`: on the GS worker, same results). |
| `GSR_RAY_WORLD_CACHE` | first-char != `0` | on | Incremental world triangle cache. |
| `GSR_RAY_PACKED_TEXTURES` | first-char != `0` | on | 32-bit packed ray textures. |
| `GSR_WATER_COVERAGE_DEBUG` | atoi (uint) | 0 | Water coverage debug mode passed to the shader. |
| `GSR_WATER_TIME` | strtof | unset | Fix the water clock (finite, abs < 1e6). |
| `GSR_RAY_SCENE_DUMP` | string | unset | Ray scene dump path (world epoch 200 only). |
| `GSR_WATER_LOG` | presence | off | Log water passes. |

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
| `GSR_MATCHED_COMPARE_EFFECT` | `water`/`models`/`shadows`/other | unset | Which effect is toggled off. |
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
| `DRAKAN_PAUSE_UNFOCUSED` / `DRAKAN_RESUME_ON_CLOSE` | `[general] pause_when_unfocused` / `resume_on_close` | off / off |
| `DRAKAN_SUBTITLES` | `[subtitles] narration` | on |
| `PS2X_GS` | `[video] renderer` | `d3d11` |
| `PS2X_GS_SCALE` | `[video] scale` (internal resolution) | `auto` |
| `DRAKAN_WINDOW_WIDTH` / `DRAKAN_WINDOW_HEIGHT` | `[video] window_width` / `window_height` | 1280 / 960 |
| `DRAKAN_FULLSCREEN` / `DRAKAN_VSYNC` / `DRAKAN_FRAME_LIMIT` | `[video] fullscreen` / `vsync` / `frame_limit` | off / on / 0 |
| `DRAKAN_WS_ASPECT` (`PS2X_ASPECT` legacy) / `DRAKAN_FOV` | `[video] aspect` / `fov` | `4:3` / 80 |
| `DRAKAN_HUD_ASPECT` / `DRAKAN_HUD_SCALE` / `DRAKAN_SMOOTH_HIGHLIGHT` | `[video] hud_aspect_correct` / `hud_scale` / `smooth_menu_highlight` | on / `auto` / on |
| `DRAKAN_FILTER` / `DRAKAN_DEINTERLACE` / `DRAKAN_FLICKER_FILTER` / `DRAKAN_SHARP_PRESENT` | `[video] filter` / `deinterlace` / `flicker_filter` / `sharp_present` | `game` / `weave` / off / on |
| `DRAKAN_WORLD_RATE` | `[video] world_rate` | unset |
| `DRAKAN_TICK_SNAP` | `[video] tick_snap` | on (with vsync: engine tick time in whole display refreshes, see perf.md) |
| `DRAKAN_SHOW_FPS` / `DRAKAN_FPS_POSITION` | `[video] show_fps` / `fps_position` | 0 / `top-left` |
| `DRAKAN_DLSS` / `DRAKAN_DLSS_PRESET` / `DRAKAN_DLSS_SHARPNESS` | `[video] dlss` / `dlss_preset` / `dlss_sharpness` | `off` / `auto` / 0.0 |
| `DRAKAN_DLSS_NEURAL` / `DRAKAN_DLSS_NEURAL_DIR` | `[video] dlss_neural` / `dlss_neural_runtime` | off / empty |
| `DRAKAN_DLSS_NEURAL_RESOLUTION` | `[video] dlss_neural_resolution` (`output` \| `render`; see `dlss.md` "Neural rendering resolution") | `output` |
| `GSR_MODERN_WATER` / `GSR_RAY_REFLECTIONS` / `GSR_RAY_SHADOWS` | `[video] modern_water` / `ray_reflections` / `ray_shadows` | see the water table above |
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
| `DRAKAN_INPUT_LOG`, `DRAKAN_INPUT_LOGFILE`, `DRAKAN_INPUT_LOGDUMP` | Log pad input to stderr / a file / dump the log. |
| `DRAKAN_WINDOW_POS` | Place the window at a fixed position (test aid). |
| `DRAKAN_SHOT_DIR`, `DRAKAN_OVERLAY_SHOT_DIR` | Directory for screenshots / settings-overlay screenshots. |
| `DRAKAN_SKIP_MEMCARD_WARNING` | Skip the no-memory-card warning screen. |
| `DRAKAN_MEMCARD_DIR` | Memory card 1 folder (card 2 = sibling `mc1`) instead of `<elf dir>/mc0`; an empty folder boots without saves (`docs/reference/input.md` 13). |
| `DRAKAN_START_LEVEL`, `DRAKAN_START_LEVEL_FROM` | New Game loads this level (long name, e.g. `Level 4A - Ravenshold`) in place of `Level 1A - Surdana` (or of `_FROM`): HLE file-open redirect (`runtime/hle` `FileService::setLevelRedirect`). |
| `DRAKAN_TEST_LEVEL_SAVE` | `1`: every save screen opens in level-change mode (save screen mode 1, request `{1, "", no callback}` through the ctor `0x4442e0`), so a harness reaches it from the pause menu's Save Game (`play_run.py --level-save`). The slot click prepares the folder but writes no `.rsg` (the level exit's callback does that). |
| `DRAKAN_LOG_DIR` | Folder for `drakan.log` / `drakan.1.log` instead of the exe folder (test runs). |
| `DRAKAN_PARRY_AUDIT`, `DRAKAN_COMBAT_AUDIT`, `DRAKAN_COMBAT_CALL_TRACE` | Combat audit log lines used by the harnesses (`scripts/harness/`). |
| `DRAKAN_DEBUG_OPEN_SHOP` | `<n>[,<n>...]`: once Rynn has been under control for 150 frames, open the level's n-th shop (class 0x100ac entity) through the game's `33be50`; the next number opens after the shop closed. Logs every shop found. Surdana: 1 blacksmith, 2 Jade's Magic Emporium, 3 potions (`docs/reference/mouse-menus.md` 11). |
| `DRAKAN_DEBUG_SKILL_POINTS` | Unspent skill points written once in gameplay (test aid for the Skills page). |
| `DRAKAN_DEBUG_TAB_HIDE` / `DRAKAN_DEBUG_PAUSE_HIDE` | `1`: the host Tab / strip / map screen, or the host pause menu, never draws, so the EE watchdog's native fallback runs after 90 frames and 2.5 s (test aid, `mouse-menus.md` sections 13 / 14). |
| `DRAKAN_DEBUG_TITLE_HIDE` / `DRAKAN_DEBUG_SLOTS_HIDE` | `1`: the modern title menu, or the modern Save / Load screen, never draws, so the EE watchdog brings the native page back (test aid, `mouse-menus.md` section 15). |
| `DRAKAN_DEBUG_PAUSE_DRAWLOG` | `1`: logs the hidden screen draws of the modern pause (`2bd1b0` entry / return for the owned Options / Audio / Controller screens, the box the host answers, and the pause when a timer message is due; test aid, `mouse-menus.md` section 14). |
| `DRAKAN_NATIVE_OPTIONS_INI` | Path of `drakan_native_options.ini` (the game's own options, saved whenever they change; default next to the exe). `scripts/harness/play_run.py` points it into the run folder, so test runs that change options never write the player's file. |
| `DRAKAN_DEBUG_TAB_PEEK` | `1`: the host Tab / strip / map screen runs (input, grid and map targets, watchdog) but draws nothing, so screenshots show the native frame its icon and map crops are taken from (test aid, `mouse-menus.md` section 12). |
| `DRAKAN_NO_ACTIVATE` | `1`: the window is shown behind the other windows and never activated, and the scripted `shot` skips its desktop BMP copy (only the renderer PNG is written); `scripts/harness/play_run.py` sets it, so a test run does not take the foreground. |
| `DRAKAN_FRAME_AUDIT`, `DRAKAN_RUNTIME_PROFILE`, `DRAKAN_DEBUG_HOLD`, `DRAKAN_F1_ALWAYS` | Frame-rate audit, runtime profile, pause-gating debug aids. `DRAKAN_FRAME_AUDIT=2` adds a per-frame `[ft]` timeline on one steady-clock axis (microseconds): `P` present tick (+ shown world serial), `F` EE display flip, `V` guest vblank (+ lateness), `U` engine update (+ `dt` ms), `W` GS record over 8 ms (+ type, duration), `H` present tick over 8 ms (lock / present split). |
| `DRAKAN_HLE_LOG`, `DRAKAN_RES_DELIVERY`, `DRAKAN_RES_LATENCY_US`, `DRAKAN_RES_NOFAIL` | Resource-service HLE log level and delivery / latency / failure behaviour. |
| `DRAKAN_SND_LOG`, `DRAKAN_SND_DBG`, `DRAKAN_SND_PUMP`, `DRAKAN_STEREO_SYNC`, `DRAKAN_AUDIO_DUMP`, `DRAKAN_AUDIO_STALL_MS` | Sound HLE diagnostics, audio dump, stall threshold. |
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
| `PS2X_IPU_DUMP`, `PS2X_IPU_DUMP_N`, `PS2X_TRACE_IPU` | Dump the first N decoded IPU movie frames as raw RGBA; trace the IPU. |
| `PS2X_PERF`, `PS2X_PROFILE`, `PS2X_HOSTPROF`, `PS2X_HOSTPROF_DELAY` | Per-second perf line, profiler, host RIP sampling (`docs/reference/perf.md`). |
| `PS2X_TRACE_CALLS`, `PS2X_TRACE_FROM`, `PS2X_TRACE_PCS`, `PS2X_TRACE_RING`, `PS2X_TRACE_SCHED`, `PS2X_TRACE_WAKE`, `PS2X_PEEK`, `PS2X_WATCH_POLL`, `PS2X_WATCH_RING`, `PS2X_WATCH_W` | Guest call / scheduler / wakeup tracing and guest-memory word watches. |
| `PS2X_WATCHDOG`, `PS2X_MAX_SECONDS`, `PS2X_FTZ`, `PS2X_FUZZ_UNWIND`, `PS2X_UNWIND_FIX`, `PS2X_RPCLOOP_RETURNS` | Watchdog / run-time limit, flush-to-zero, unwinding and RPC-loop behaviour switches (`ps2_runtime.cpp`, `Kernel/`). |
