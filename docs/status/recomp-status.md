# Recompiled Drakan: status

Latest combined build (17 October 2026): see [integration.md](../journal/2026-10-17/integration.md) for merged revisions, actual test scope, deployed executable hash and unresolved issues. The earlier section summaries below are historical.

(Latest: section 4e - black 3D world solved (soft-double libm ABI), FMV noise solved (libipu table addresses), UI prompts merged, guest-time HLE latency, measured VU1 bottleneck; before that 4d - mouse + keyboard input, memory-card warning skip; 4c - SPU2 audio, GS merge, title menu and Level 0 reached.)

(Second iteration: see section 4 - the RSRC services are now served by the HLE library and the game runs its main loop; the text below describes the first iteration.)

State: the recompiled game links into a native Windows x64 executable (`build/recomp_out/bin/drakan.exe`), starts at
the ELF entry point 0x100008, runs crt0, the C++ static constructors, `main`, the IOP-reboot sequence, language
selection and the SIF/RPC bring-up, creates the EE threads and then **blocks on the first RSRCIOP request**
(`sid 0x1201 fno 0`, open of `cdrom0:\English/Drakan 2.rrc`). Nothing else is missing before that point: no unknown
syscall, no unknown SIF RPC, no missing function, no crash. Everything beyond is the job of the RSRCIOP HLE library.

Work is on branch `drakan` of `tools/PS2Recomp` (commits `f440d16` recompiler, `54cd959` runtime; base `eb8688b`).

## 1. What changed (drakan branch)

### Recompiler (`ps2xRecomp`)
| Change | Where | Notes |
|---|---|---|
| `functions_csv` config key | `src/lib/drakan_support.cpp` (`loadFunctionsCsv`) | Replaces the heuristic discovery with `analysis/functions.csv` (12,173 functions, bypasses analyzer/Ghidra-map merging). Ghidra only lists the entry chunk of non-contiguous functions, so the 4,988 gaps between functions (178,908 bytes) are **absorbed into the preceding function**. Without this the very first run died on a branch to a chunk outside every function (0x4acb18). |
| Real jump tables | `drakan_support.cpp` (`buildJumpTargets`), `control_flow_analyzer.cpp` | For every `jr rX` (X != ra): recover the table base from the instruction pattern (lui/addiu/ori constant chains, `addu base,idx`, `lw rX,off(..)`), read the table words from the ELF, keep 4-aligned targets inside the function, union with Ghidra's case targets (`jump_tables_csv`). Emitted as `switch (jumpTarget) { case 0x..: goto label_..; }` (existing emitter code). Unresolved `jr` = tail call through the function table. |
| No fallback explosion | `indirect_fallback = false` (default) | The "every instruction of a function with an unresolved JR/JALR is an entry point" promotion is now opt-in. JALR needs only its return point (jal+8), which was already queued. |
| Batched output | `functions_per_file = 150` | N functions per translation unit, generated on all cores, files rewritten only when their text changed (so incremental builds recompile only changed TUs). |
| Function table | `function_table_emitter.cpp` | Constant-initialised `{address, fn}` array + 6-line loop, instead of a ~70k-statement constructor. |
| Leaner code | `function_emitter.cpp`, `control_flow_emitter.cpp` | `ctx->pc` stores, `in_delay_slot`/`branch_pc` stores only where a runtime call can observe them (`always_write_pc = true` restores the old output); `emit_comments = false` drops the per-instruction disassembly comments. |

### Runtime (`ps2xRuntime`)
| Change | Where |
|---|---|
| `PS2X_GENERATED_DIR` / `PS2X_GENERATED_OPT` CMake options: generated code is built as static library `ps2_generated` (no `/GL`+LTCG, `/MP`, `/bigobj`), exe renamed `drakan.exe`, 256 MB stack | `ps2xRuntime/CMakeLists.txt` |
| Tail calls (`J`, `JR` to another function) are nested host calls instead of unwinding to the scheduler | `ps2_runtime.cpp` `dispatchGuestBranch` |
| `src/lib/drakan_overrides.cpp`: game override matched by ELF name + entry 0x100008. Skips `LoadIopModules` (0x101710, returns 0), stubs `sceSifLoadModuleBuffer` (0x4c3798) with fake ids, binds SIDs 0x1201/0x1202/0x1203 and logs every call (sid, fno, mode, send/recv address+size, end function, first 96 send bytes), replies with a zeroed buffer | new file |
| `PS2Runtime::HleRpcHooks {canBind, handle}` + `setHleRpcHooks()`: the plug point for the real RSRCIOP/sound/file services (consulted before the IOP emulator in `canBindIopRpc`/`handleIopRpc`) | `ps2_runtime.h`, `ps2_runtime.cpp` |
| Diagnostics (env vars): `PS2X_WATCHDOG=<s>` prints live pc/ra/thread table every s seconds, `PS2X_MAX_SECONDS=<n>` stops the run, `PS2X_TRACE_CALLS=<n>` logs the first n guest calls/jumps, `PS2X_HLE_FAIL_OPEN=1` answers RSRC open with -1 | `ps2_runtime.cpp`, `EeScheduler.cpp`, `drakan_overrides.cpp` |

IOP: `ReplaceIop` (0x101db8) needs no override, the runtime's `sceCd*`/`sceSifRebootIop`/`sceSifSyncIop` stubs already
return success and the game proceeds. Only the module-bundle load had to be skipped (the bundle `ENGLISH\LEVELS.RRC`
is not in the extracted tree and would need decrypt+inflate; the HLE plan does not need any of the 7 IRXs).

### Result numbers (SCES_500.06)
| | previous evaluation (heuristic, unmodified) | now |
|---|---|---|
| functions | 8,287 | 12,173 (+1 synthesized) |
| unresolved JR/JALR functions / fallback entries | 6,896 / 1,418,946 | 0 / 0 |
| resumable entry points | 728,650 | 68,023 |
| non-`$ra` `jr` | n/a | 210: **181 ELF-verified tables**, 1 Ghidra-only, 28 unresolved -> tail-call dispatch |
| generated output | 363 MB, 8,299 files | **161 MB, 85 files** (82 TUs + headers + table); 107 MB with `emit_comments = false` (3.30 M / 2.31 M lines) |
| recompiler run time | 17 s | 3 s (10 s incl. rebuilding the recompiler) |
| decode failures / warnings | 0 / n/a | 0 / 0 |

`.gcc_except_table` is not an exception table. It holds 21 code pointers in 0x4c7dc8..0x4c8004 (a libc switch table,
handled by the jump-table pass). There is no `.eh_frame` section, so the game is built without C++ exceptions; no
unwinding support is needed.

## 2. Build and run (exact commands)

Prerequisites: VS 2022 Community (bundled CMake), network on the very first configure (FetchContent). Everything lives in
`decomp/build/recomp_out/`; the recompiler itself is the pre-existing `build/ps2recomp_try/b` tree.

```powershell
# one-shot: rebuild recompiler -> regenerate C++ -> rebuild drakan.exe   (flags: -SkipRecompilerBuild -SkipGen)
cd "C:\Games\Drakan - The Ancients Gates\decomp\build\recomp_out"
.\rebuild.ps1

# configure (done once; reuses the already fetched dependencies of the earlier trial)
$cm = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$root = "C:/Games/Drakan - The Ancients Gates/decomp"
& $cm -S "$root/tools/PS2Recomp" -B "$root/build/recomp_out/rt" -G "Visual Studio 17 2022" -A x64 `
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF `
  -DPS2X_ENABLE_DEBUG_UI=OFF -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_SCCACHE=OFF `
  "-DPS2X_GENERATED_DIR=$root/build/recomp_out/gen" "-DFETCHCONTENT_BASE_DIR=$root/build/ps2recomp_try/rt/_deps"
# faster, slower-running iteration build:  add  -DPS2X_GENERATED_OPT=/Od

# recompile only (3 s)
cd build\recomp_out; ..\ps2recomp_try\b\ps2xRecomp\Release\ps2_recomp.exe config.toml

# run (cwd = bin; the ELF directory is the cdrom0: root, mc0 is created next to it)
cd bin; $env:PS2X_WATCHDOG=5; $env:PS2X_MAX_SECONDS=20
.\drakan.exe "..\..\..\iso\SCES_500.06"
```

`build/recomp_out/config.toml` is the earlier analyzer config (SDK stub bindings `name@addr`, 202 of them, MMIO hints)
with these keys added under `[general]`:
`functions_csv`, `jump_tables_csv`, `functions_per_file = 150`, `indirect_fallback = false`
(optional: `emit_comments`, `always_write_pc`). The `[performance]` table is ignored by this version.

### Times (8 cores / 16 threads, MSVC 14.44, Release)
| step | time |
|---|---|
| recompile (12k functions -> 161 MB C++) | 3 s |
| full build of `drakan.exe` at `/O2` (compile of 82 TUs ~6 min wall on 16 threads, then link) | 8 min 15 s |
| same compile at `/O1` | 7 min (not faster) |
| same compile at `/Od` (per-TU numbers from `bench_compile.ps1`) | 65 s |
| runtime-only change (a .cpp in ps2_runtime) | 20-40 s |
| runtime header change (`ps2_runtime.h` ...) | full rebuild, ~8 min (every generated TU includes it) |

The heaviest single TU needs 43 s alone (about 3.5x that when all 16 hyperthreads compile at once), the cost is the
optimizer on the macro/lambda-heavy code. Output exe is 106 MB.

## 3. Run log up to the first blocking point

```
Loading segment: 0x100000 - 0x5b85fc (filesz: 0x4567d0, memsz: 0x4b85fc)
[game_overrides] applying 'Drakan: The Ancients' Gates (SCES-500.06)'
ELF file loaded successfully. Entry point: 0x100008 ... Starting execution at address 0x100008
[drakan] LoadIopModules (0x101710) skipped: IOP modules are HLE'd, returning success
[hle-rpc #0] sid=0x1201 fno=0 mode=0x0 send=0x1fbfac0/268 recv=0x1fbfac0/8 endfn=0x4af488
           send[0..16]= 00 00 00 00 00 00 00 00 63 64 72 6f 6d 30 3a 5c | text= ........cdrom0:\English/Drakan 2.rrc
[watchdog t=5.0s] live thread=6 pc=0x4bc088 ra=0x1d7a24
    thread 1 status=2 wait=2 waitId=23 pc=0x4bc1a8 ra=0x137bfc entry=0x100008 prio=7   (main, WaitSema 23)
    thread 2..5 entry=0x1f11e8 prio 1,6,5,4   (RPC server threads for 0x1301/0x1302/0x1303 + 0x1401, parked in RpcLoop)
    thread 6 status=2 wait=1 entry=0x1d78f0 prio=2                                         (SleepThread)
[SifRegisterRpc] sid=0x1401 sd=0x56bcb8 ... sid=0x1301 sd=0x5b5880  sid=0x1302 sd=0x5b58c4  sid=0x1303 sd=0x5b5908
```
Full log: `build/recomp_out/run_first_blocker.log`. Call order up to the block (from `PS2X_TRACE_CALLS`): crt0 -> `0x4bc3e0`
-> main `0x101400` -> static ctors (`0x4c4358`, including the sin/cos table builders that call `sinf` thousands of
times) -> `0x4af9b0` -> ReplaceIop `0x101db8` (sceCdInit, sceSifRebootIop, sceSifSyncIop, ...) -> SelectLanguage
`0x1015d8` -> LoadIopModules `0x101710` (hook) -> game init / resource client (EE threads for servers 0x1301-0x1303 are
created) -> File::Open("cdrom0:\English/Drakan 2.rrc") through the RSRC client (RPC 0x1201).

**Diagnosis.** The blocker is by design, not a recompiler or runtime fault. The game opens its resource container through
RSRCIOP service 0x1201 (File Command, fno 0 = open; request = 8 zero bytes + path, 268-byte buffer; reply = 8 bytes
`{handle,size}`) and then waits (main thread on semaphore 23, resource thread asleep) for the answer and for the EE-side
completion servers 0x1301/0x1302. With `PS2X_HLE_FAIL_OPEN=1` (reply -1) the main thread takes the game's own
"cannot open" path and exits cleanly through `exit` (0x1000e0), which also shows that the recompiled control flow,
stubs, syscalls and the scheduler behave consistently up to this point.

Note the path is `English/Drakan 2.rrc` (forward slash), not the `LEVELS.RRC` named in docs/reference/iop-and-io.md; the open goes
through the same File Command RPC.

## 4. Update: RSRCIOP HLE wired in, boot past file I/O (second iteration)

State now: the game boots through crt0, static constructors, main, IOP "reboot", the three RSRC RPC services (served by
`decomp/runtime/hle` from the ISO), thread/server creation, ~19 file opens and 24 resource batches (Interface + music sets),
then settles in its main loop at 60 Hz: it clears the screen with black full-screen sprites every frame, flips
DISPFB1/2 between 0x1000 and 0x1080, polls the memory card (GetInfo / GetDir x5, every ~2 frames) and reads the pad every frame.
Window opens, stays black. No further resource request arrives (60 s observed).

What was added
* **HLE wiring** (`ps2xRuntime/src/lib/drakan_overrides.cpp`, CMake target `drakan::hle`): `IEeMemory` over RDRAM, `IEeRpcPoster` that posts to the
  EE-registered servers 0x1301/0x1302/0x1303 by running their handler on a dedicated high-priority guest dispatcher thread
  (`EeScheduler::postGuestCall`, thread-safe; `HleCall` invocations get their own thread), `NullSpu2` (logs + keeps a 2 MiB RAM image),
  0x1202 on one worker thread (sequential, end function run via `postGuestCall` when the batch is done), 0x1201/0x1203 inline.
  `sceSifRpcLoop` now parks the server threads forever (as the real one), instead of returning and ending them.
* **Config**: `DRAKAN_ISO` (default `C:\Games\Drakan - The Ancients Gates\Drakan - The Ancients' Gates.iso`, a directory works too),
  `DRAKAN_LANG` (`\ENGLISH`), `DRAKAN_RES_DELIVERY=direct|rpc1301` (default direct), `DRAKAN_HLE_LOG=<n lines>` (default 400, logs every open,
  resource request, RPC and EE post), `DRAKAN_RES_FAILOK=1` (experiment: report failed resource requests as OK).
* **Recompiler**: code addresses that are only reachable as pointers (callbacks, SIF end functions such as 0x4af488, vtable/data
  words, lui/addiu pairs) are now registered as extra entry points (`scan_code_pointers`, 2,017 found). Without this the SIF end
  function `0x4af488` (inside an absorbed gap) was not callable and the first RPC never completed.
* **GS capture** (`PS2X_GS_CAPTURE=<frames>`): see `docs/reference/gs-capture.md`. Diagnostics: `PS2X_PEEK=<hexaddr,...>` prints guest words in the watchdog.

Findings that differ from docs/reference/iop-and-io.md / the HLE README (all measured, adapter compensates, library untouched)
1. **Open record size**: some opens send a short record (8 + strlen + 1 bytes, e.g. 57-58) and ask for only 4 reply bytes. The adapter pads
   the request to 0x10c and copies only the requested reply length.
2. **File-not-found status must be -1**, not -2. With -2 the game retries the whole open group forever and leaks handles (>30 opens);
   with -1 it continues (the COMMON sets have no `.ssd` and `PS2 Music.adb`/`.vdb` do not exist; the level `.SSD` files do exist and open; see `docs/reference/sound-debug.md` section 2).
3. **Resource wire entry has `id` at +8 and `type` at +0x0a** (doc/library have them the other way round): requests decode as
   (0x40, id, ...) for textures, 0x302 sound descriptors, 0x305 lip-sync tracks of speech samples, 0x400 for XDB text only after swapping. Adapter swaps.
4. **`sub == -1` means "any instance"** (request `sub` is 0xFFFF, index subs are 0..29); adapter resolves it to the first index entry with
   that (type,id) using `FileService::get()`. The library's exact match would report "not found" for every request.
5. Type-0 destination is always the 0x80 "EE allocates" path (dest field 0): the 0x1303 alloc call returns a pointer and the reply word is
   used. `Delivery::Direct` works (24 batches complete, completions arrive through 0x1302 in order, failures as status -1 for the missing
   type-0x305 sound entries); `Delivery::Rpc1301` stalls: the game's 0x1301 handler (0x1f0110) does not return within 20 s.
6. Handlers posted to the EE servers can block (WaitSema). They must not run on the interrupted thread (self-deadlock on the game's
   semaphore 0x1f06d0 -> 0x4bc1a0); hence the dispatcher-thread change in the scheduler.

pad / memory card / sound driver: nothing needed. `scePad*` stubs already return a connected DualShock in analog mode and map the keyboard
(arrows, Z/X/C/V, Enter = Start, WASD = left stick) and gamepads through raylib; the pad is polled every frame. `sceMc*` reports an
8 MB formatted card; `sceMcGetDir` of a wildcard in a missing directory now returns 0 entries instead of NoEntry (-4) (no visible
difference). `sceSdRemote` is acked by the runtime stub; the 0x1203 sound batches reach the null SPU2 (e.g. setParam 0x980 = 0x3fff).

Graphics path (what the existing runtime does): the game writes PMODE=0x8023, SMODE2=1, DISPFB1/2, DISPLAY1/2 (640x512 interlaced) and submits
GIF DMA chains on channel 2 (PATH3, PACKED A+D). The software GS processes them and presents through raylib (window "PS2-Recomp |
SCES_500.06"), but the window is black because the only thing drawn is black sprites; no PATH1/PATH2 traffic, no VU1 microprogram run
and no texture upload (IMAGE) has occurred yet. Captures: `build/captures/gscap_*.bin` (600-2000 frames, 2.4-8k packets).

Performance: the main loop runs in real time (600 vsync frames recorded in 10 s, pad read each frame). The /Od build uses about 1.2 CPU
cores; the /O2 build uses about 0.6 core (11 CPU-s over 18 s wall) for the same idle state, so the loop has plenty of headroom.
The call overhead (`dispatchGuestBranch` + `checkpointDue`) is visible in traces (~12k calls/s with tracing on) but nothing is CPU bound yet.

Current blocker: after the Interface/Music sets are loaded the game neither requests more resources nor draws anything but black.
Not diagnosed: candidates are (a) a state machine waiting for a hardware condition the runtime reports differently (GIF_STAT/VIF1_STAT/
D_STAT polled by the flip thread 0x1d78f0, `sceCd*`/disc-ready, VBlank counters), (b) a missing completion for the last batch
(0x305 lip-sync entries, which only exist for speech samples), (c) the intro FMV path (IPU/MPEG not exercised so far, no
`sceIpu*` call seen). Next step: identify the game-state variable the main loop (Engine_Tick 0x135c40) is waiting on, e.g. with
`PS2X_PEEK` plus Ghidra's names in `analysis/input/names.csv`.

Build note: `-DPS2X_GENERATED_OPT=/Od` builds the 82 generated TUs in about 65 s (1 min 40 s for the whole exe) and is what the
iterations above used; a runtime-only change relinks in 5-20 s.

## 4b. Update 3: black screen solved, logo FMV plays through the emulated IPU

Screenshots: `build/screenshots/logo_movie_frame_red_splat.png` (red splat frame of the Sony/Surreal logo movie) and
`logo_movie_surreal.png` (Surreal logo, fading) plus `after_logo_*.png` (final state). They are 640x500 crops of the raylib window.

Root causes of the black screen, in the order they were found
1. **COP0 Count never advanced** (the recompiled `mfc0 Count` read a constant). The game's ms timer (0x4af730, used for fades, movie pacing and
   timeouts) was frozen. `mfc0 Count` now compiles to `runtime->readEeCount(ctx)` (wall clock x 294.912 MHz).
2. **The first screen waits for the pad**: Cross (X key / gamepad A) is the "continue" button, Start (Enter) does nothing there. Pressing it makes the
   game create the movie player (header request `0342/4`, frames `0343/4`). Headless test aid: `DRAKAN_AUTOPRESS=<ms>` taps Cross every period
   (3 s after start), `DRAKAN_AUTOSTART=1` adds Start. Everything in the log below used it.
3. **Resource delivery** (see `runtime/hle/README.md`, "Measured corrections"): the 1202 call must complete immediately, type-0 requests with the
   alloc flag need the allocated address stored at the request's `dest`, `Delivery::Rpc1301` is the default now. Direct delivery had been writing
   movie/text data to guest address 0.
4. A deadlock-free model for IOP->EE posts (dispatcher threads, section 4) and parking `sceSifRpcLoop` server threads.

IPU (register/DMA level, `ps2xRuntime/src/lib/ps2_ipu.cpp`, hooked from `ps2_memory.cpp` at the IPU register block, `write128` to IN_FIFO and the DMA
start of channels 3 and 4): IPU_CMD/CTRL/BP/TOP, BCLR, SETIQ, SETVQ, SETTH, FDEC (result readable through IPU_CMD/TOP), IDEC. IDEC decodes the whole
frame at once with `runtime/fmv` (`fmv::Idec`, RGB32, alpha 0x80) until the next start code; BUSY stays set until the fromIPU DMA (ch3) drained the last
macroblock; toIPU DMA (ch4) is a copy into the bit buffer; fromIPU DMA waits (STR stays set) until a decoded frame is available. The game's own player
(0x177800 / 0x1767a0 / 0x1776e0 / 0x177650, libipu 0x4bbaf0) runs unchanged: `Initializing IPU library`, sceIpuInit sequence (BCLR, SETIQ x2, SETVQ,
SETTH, BCLR), then per frame FDEC 32 / FDEC 0 / FDEC 8 / IDEC 0x10010000, all 264 frames of the logo movie decode as 1024 macroblocks with flags 0x20,
and the frames are uploaded through the game's own GS image path (65,536 qwords per frame, visible in `gs_capture`). Trace with `PS2X_TRACE_IPU=1`.
Not implemented (never issued by the game): BDEC, VDEC, CSC, PACK, RGB16 output, dithering.

State after the logo: the movie ends on its last (almost black) frame and the game then loads `INTERFCE.XDB` text entries 117-121 and stops
requesting resources; the screen stays on that frame, the main loop keeps running at 60 Hz and the sound system keeps issuing volume/KON/KOFF batches.
Main menu not reached. Likely cause (not verified): the game waits for a notification that only the real IOP sound service produces. The sound
thread registers an EE RPC server **SID 0x1310** (`sd=0x56bd88`) that nothing ever calls, and the movie's soundtrack (sound set 79: 0x302/0x304, 133 KB
of ADPCM uploaded to SPU2 RAM at 0x15040, KON on core 1 voice 23) never "finishes" because the null SPU2 has no playback. The SPU2/sound agent's
`ISpu2` implementation (and `SoundService`) will need to post 0x1310 events (voice end / transfer done) through `IEeRpcPoster`. A profile
(`PS2X_PROFILE=<s>`, needs `always_write_pc = true` in the recompiler config) shows the main loop in its normal frame path, not in a hardware spin.

Missing `.ssd` / type 0x305: the game asks for `0305/<id>` sound-set entries in the same batch as `0302` and opens `*.ssd` for every resource set; neither
exists on the disc (the SDB has only 0x300/0x301/0x302/0x304/0x307). Open returns -1 and the batch entries complete with status -1; the game
carries on (movie frames and texts load after such batches). Reporting those completions as success (status 0) made no difference, so an error
completion is what the real IOP does or at least is harmless; no completion at all was not tried (the IOP structure in docs/reference/iop-and-io.md 3.2 posts one per
entry).

Performance: the logo movie (512x512, 264 frames) decodes and uploads in real time at /Od (decode ~2-4 ms/frame); the main loop stays at 60 Hz.

Tools added: `PS2X_PEEK` supports `*addr:off` (word at [word at addr]+off) and IO registers, the watchdog prints semaphores, `PS2X_PROFILE`,
`PS2X_TRACE_IPU`, `DRAKAN_AUTOPRESS`/`DRAKAN_AUTOSTART`.

## 4c. Update 4: SPU2/sdrdrv wired in, GS merge, the real blocker found, title menu and Level 0 reached

Screenshots (`build/screenshots/`, taken with `PS2X_GS=d3d11`, `PS2X_GS_SHOT`): `title.png` (title/menu screen while the music set still
loads, "Loading..." bar), `menu.png` (title menu: Load Game / New Game / Options / Credits), `level0_throne_room.png` (Level 0 - Intro, 3D scene
rendered by the game; reached with `DRAKAN_AUTOPRESS`: Cross on the menu starts "New Game").

### What was done
1. **`drakan-gs` merged into `drakan`** (clean merge; worktree `tools/PS2Recomp-gs` removed, branch kept). `PS2X_GS=sw` (default) and
   `PS2X_GS=d3d11` both play the logo movie, reach the menu and load Level 0.
2. **Real SPU2 in the runtime** (`drakan_overrides.cpp`, CMake target `drakan::spu2`, library `runtime/spu2` linked as `drakan_spu2`):
   * `spu2::Spu2` is the `hle::ISpu2` (through a thin logging forwarder `SpuTap`): 0x1203 sdBatch commands and the SPU-destination resource uploads
     go to it. `sdInit(0)` at start.
   * **sdrdrv** (SID 0x80000701): the EE stub `sceSdRemote` (0x4b91d8) is replaced by a native hook that packs the register arguments into the
     stub's 0x40-byte buffer (word0 = pointer slot, words 1..6 = a2,a3,t0..t3, fno = a1; effect-attr commands OR the core into the fno) and calls
     `spu2::SdrServer::call`; the resolver maps buffer addresses to RDRAM. The reply is stored in the stub's static buffer (0x56df00) and returned in v0.
     The game only uses 0x8000 (Init) and 0x8040 (GetSwitch) there (the sound player talks to the IOP through 0x1203). The sdrdrv callback server
     0x80000704 is never registered by the game (no `sceSdRemoteCallbackInit` call), but the pump thread would post SPU2 events to it
     (`{code, arg}`, codes 0xb/0xc/0xd as in dispatcher 0x4b94d0) through `EeRpcPoster` if it ever were.
   * **Audio**: `spu2::AudioOutput` (WASAPI shared mode, 48 kHz float, auto-convert). `DRAKAN_AUDIO=0` (or a failing device) runs the SPU2 on a wall-clock
     driven silent render thread instead, because the game polls voice positions to detect the end of sounds. Verified numerically (not audibly):
     with `DRAKAN_SND_LOG=1` the pump logs the output peak and the playing voices (movie soundtrack peak ~15000 for 10 s, menu music, level music loop on
     core 0 voice 23).
3. **SID 0x1310 solved** (see docs/reference/iop-and-io.md 3.4): it is not an event notification but RSRCIOP's *voice position snapshot*: every 500 ms the IOP sound thread
   (RSRCIOP 0x4690) runs `ProcBatchEx(GETADDR NAX, voice mask 0xffffff)` for core 0 and core 1 and sends the 48 words (core0 v0..23, core1 v0..23 = current NAX
   byte address of each voice) as a 192-byte `CallRpc(fno 0, blocking)` to the EE server 0x1310 (handler 0x1e6208 copies them to 0x574760 under semaphore 5; the
   game's voice manager 0x1e62e0 reads them to see which voices are still running). The runtime's pump thread does exactly this (`soundPump`).
   It was NOT the cause of the logo-screen stall.
4. **The real blocker was the 0x1301 header** (not sound): for requests with the 0x80 "EE allocates" flag the IOP sets **header word 0 = reply word 2 of the
   0x1303 call (offset of the allocated buffer from the 0x1301 receive buffer 0x574880)**, not 0x10. EE handler 0x1f0110 computes `data = buf + word0` and builds
   a stream reader over it (flag "owns data" when word0 >= 0x14, so the loader frees the buffer afterwards). With word0 = 0x10 the texture parser (0x1bf838) read garbage,
   rejected the two INTERFCE textures 0x40/96 and 0x40/97, the font/text object waited for them forever (loop at 0x43e640 -> 0x288658) and the screen stayed on the
   last movie frame. Fixed in `runtime/hle/src/ResourceService.cpp`. Disassembly: RSRCIOP 0x35a0-0x35f8, EE 0x1f0110.
5. **File table has 64 slots**, not 32 (RSRCIOP 0x23b8 allocates 0x40 entries of 0x11c bytes): with 32 the level load ran out of handles (-7) and silently hung.
6. **Completion latency**: the real IOP needs milliseconds before a batch completes; with instant completions the 1301/1302 handler could preempt the requester
   between its request submission and its own reference counting, the resource object was destroyed under it and the game crashed later (stale pointer writes in
   0x1e69e8 etc., `[guest-branch:missing-target]`, ~40 % of runs). The 0x1202 worker now waits until `submit + 2 ms` (`DRAKAN_RES_LATENCY_US`, then default 2000) before
   processing a batch: 0 crashes in 30 consecutive runs. Default now 50 (2026-10-22, see item 7 and
   `docs/journal/2026-10-22/options-black-screen.md`).
7. **Scheduler fidelity** (runtime, `EeScheduler`): (a) `checkpointDue` no longer preempts / dispatches interrupts or events while the running thread has EE
   interrupts disabled (`DI`, Status.EIE clear; bounded to ~13 ms of EE time); (b) HLE-posted handlers run at the priority of the game's real server thread
   (0x1303: 4, 0x1302: 5, 0x1301: 6, others 3) instead of all at priority 1 (`postGuestCall(..., priority)`).
8. Recompiler: every generated function reports (`[bad-resume]`) if it is entered mid-function without a resume label (silent restart from the prologue).
   None seen.

### New diagnostics / options (all env vars)
`DRAKAN_AUDIO=0` (no WASAPI), `DRAKAN_SND_LOG=1|2` (1: per-500 ms voice snapshot with output peak; 2: also KON/KOFF/SSA/ramWrite with timestamps),
`DRAKAN_SND_PUMP=0` (no 0x1310/0x80000704 pump), `DRAKAN_RES_LATENCY_US`, `DRAKAN_RES_NOFAIL=1` (experiment: do not report failed requests; the game then stalls),
`DRAKAN_TRACE_FN=addr,addr,...` + `DRAKAN_TRACE_N` (log args/return of up to 8 guest functions, with thread id), `PS2X_TRACE_FROM=<s>` (delay `PS2X_TRACE_CALLS`),
`PS2X_TRACE_WAKE=1` (log WakeupThread), `DRAKAN_AUTOPRESS_UNTIL=<ms>` (stop auto-tapping, e.g. to rest on the menu), the HLE log now also shows file close (fno 1).
Handy for reading game code: a whole-program Ghidra decompile (`analysis/ghidra_scripts/InputDecompAll.java`) plus `analysis/scripts/iop_eedis.py` for an EE disassembly.

### Current state
Logo movie (video + SPU2 soundtrack) -> title menu with music -> New Game -> Level 0 loads (World Common, Mountain, System, FX, NPC ... sets) and renders the
intro scene. The /Od build runs the 3D scene at roughly 20 fps; an /O2 build is needed for real-time. Known visual issue (gsr only, not investigated): a 1-pixel
dark vertical seam at x~340 on the 2D menu. Audio is produced and sent to WASAPI; it has not been listened to by a human yet (levels were checked via the peak meter).
Open (corrected in section 10): 0x305 = lip-sync tracks exist only for speech samples (status -1 for the others is normal), the COMMON sets have no `.ssd` (level `.SSD` files exist), `0210/0214/0208` MOD entries likewise optional.

## 4d. Update 5: mouse + keyboard input, memory-card warning skip

(Build note: all runs of this section used an `/O2` build in `build/recomp_out/rt_o2` (configure with the command of section 2 plus `-B .../rt_o2`, `PS2X_GENERATED_OPT` defaults to `/O2`);
it writes the same `build/recomp_out/bin/drakan.exe`. Runtime-only changes relink in ~5 s. Copy the exe to `drakan_in.exe` for tests so other agents' `taskkill /IM drakan.exe` does not hit it.)

### Input (details: `docs/reference/input.md` section 11, `runtime/input/README.md`)
* `runtime/input` (module `drakan_input`) is linked into the runtime (CMake target `drakan::input`, define `PS2X_HAVE_DRAKAN_INPUT`); glue `ps2xRuntime/src/lib/drakan_input_glue.cpp` (installed from `applyDrakanOverrides`)
  replaces libpad (`scePadInit/PortOpen/GetState/SetMainMode/SetActDirect/Read`) and wraps `PadMgr_Update` (0x1e3970) and `Camera_UpdateLook` (0x44e4a8) with after-hooks.
  `DRAKAN_INPUT=0` restores the stock keyboard stubs. Config file `drakan_input.ini` next to the exe (created with defaults when missing; delete it after updating to get new defaults).
* Raw Input from the module's own message-only window, cursor clipped/hidden through window subclassing, raylib's Esc exit key disabled. Verified with **real injected keyboard/mouse events**
  (W walked ~85 units/frame, 800 mouse counts turned 1.6 rad, Space jumped, `ClipCursor` = client rect) and with scripted input.
* New runtime plumbing: guest calls from hooks run on a copy of the context (`pc = entry`, `$ra = 0`) under a thread-local no-preemption guard (`EeScheduler.cpp`: `ps2xNoPreemptEnter/Leave`, checked first in `checkpointDue`).
* Verified: yaw_sign -1, pitch_sign **+1** (default changed), FPR calling convention for `FUN_0013ee90`, `0x4e8c48` = (0,1,0,0), W forward / A,D turn (scheme A, invert-Y off by default), pad -> command map
  (Triangle 0x4011 jump, Cross 0x4012 crouch, Square 0x4006 attack, Circle 0x4007 magic/parry, L1 0x4009, L2 0x400c, R1 0x403a, R2 0x400b, R3 0x400a, Select 0x4030).
  The game re-centres the camera pitch itself (the module holds the mouse pitch 1200 ms). Rynn-vs-Arokh callers of `Player_GatherInput` not determined (no dragon reached).
* Menus: keyboard navigation works with the defaults (arrows/WASD, Enter/Space = confirm, Esc = back: Credits opened and left again). The mouse cursor feature does **not** work in the game
  (menus ignore the cursor position; a click acts as Cross on the highlighted item) so `[menu] cursor/clicks` stay off; `MOUSE1 = CROSS` in `[menu_bindings]` is the useful click.
* Default bindings now follow the game's Standard scheme (Space jump, Ctrl/C crouch, LMB attack, RMB magic/parry, E use/Arokh, Q quick inventory, F/MMB targeting, R next target, Tab inventory/map, Esc pause).
* Test mode: `DRAKAN_INPUT_SCRIPT`, `DRAKAN_INPUT_AUTOSTART`, `DRAKAN_INPUT_LOG`, `DRAKAN_INPUT_LOGFILE`, `DRAKAN_WINDOW_POS`, `DRAKAN_SHOT_DIR` (see the module README); scripts and runner in `build/input_tests/`
  (`run.sh`, `t*.txt`, `m*.txt`); screenshots `build/screenshots/input_ingame_*.png`.
* Findings while testing: (1) skipping the Level 0 intro cutscene (Start) leads to a level whose 3D world renders black (HUD fine, player controllable) - not diagnosed; the intro cutscene itself
  runs at ~2-3 fps in the D3D11/O2 build (draw cost), too slow to wait out. (2) The game intermittently sits in a memory-card/level-transition loop (`sceMcGetInfo/Sync` spam, no frames) until Cross is pressed;
  `DRAKAN_AUTOPRESS` taps by wall clock for that reason.

### Memory-card warning screen ("WARNING! ... Press X to continue")
Shown first at boot (the texts are `INTERFCE.XDB` strings; the screen is a property-table driven UI object, no direct code reference was found), waits for Cross.
New option **`skip_memcard_warning`** in `drakan.ini` next to the exe (`[general]`, created with `skip_memcard_warning = 1`; env `DRAKAN_SKIP_MEMCARD_WARNING=0|1` overrides): the glue taps Cross (120 ms every 400 ms, from t = 2 s)
until the game requests the logo movie header (resource type 0x342, seen by the RSRC HLE hook in `drakan_overrides.cpp`) - that request is the first thing the game does after the screen is dismissed.
The memory-card emulation (`mc0/BESCES-50006*` saves) is untouched. Verified: log lines `[input] skipping the memory card warning` / `[drakan] logo movie requested`, the logo movie then starts without any key press.
Set the option to 0 to see the original screen.

## 4e. Update 6: the black 3D world, the FMV noise, UI merge, VU1 is the bottleneck

Screenshots (`build/screenshots/`): `world_*.png` (3D world in gameplay, incl. mouse-look up/down), `fmv_before_*.png` / `fmv_after_*.png` (logo movie before/after).
(Build/run: `/O2` build in `build/recomp_out/rt_o2`, exe copied to a private name, scripts in `build/input_tests/`: `bld_bw.ps1`, `run_bw.sh`, `boots2.sh`.)

### 0. `drakan-ui` merged
Clean merge of `drakan-ui` (keyboard/mouse prompt layer, `runtime/ui`, `DRAKAN_UI_PROMPTS` default on) into `drakan`; worktree `PS2Recomp-ui` removed (branch kept), `patches/ps2recomp-ui/` deleted. `DRAKAN_PADSEQ` (a test aid of that branch) is kept: it only ORs
extra buttons into the stock libpad stub, which the input module's pad replacement bypasses. Verified: `[drakan] ui: prompts keyboard/mouse ...` at start, menu navigation and the scripted gameplay input still work.

### 1. Black 3D world: root cause = wrong ABI of the bound double-precision libm stubs
After the intro skip every 3D vertex came out of VU1 as `x=0x8000, y=0|0xffff, ADC=1` (all degenerate), colours/Q/fog/textures were fine. Bisected through the chain: GS capture (all PATH1 packets valid, vertices
clamped) -> VU1 dump (`PS2X_VU1_DUMP`) -> the view-projection rows in VU data qw0-2 had x/y/z lanes ~1e36 while the w lane (view direction) was sane -> source of the rows found in RDRAM (`PS2X_VU1_DUMP_SCAN`) ->
camera object +0x78 (rewritten every frame by `FUN_001480e0` = template copy scaled by `*(0x5565c8)+0x48/+0x4c`) -> those two scale factors are written by `FUN_00132d68` (`SetViewport`/projection set-up), which was fed
`[+0x10c] = 64 / tan(fov/2)` computed by `FUN_00133fc8` as `fptodp -> tan -> dpdiv -> dptofp`.
**The game's libm is the PS2 SDK one with the soft-double ABI (a double travels in the 64-bit GPR a0 and the result comes back in v0).** `config.toml` binds `tan@0x4B23A0`, `atan`, `atan2`, `exp`, `ceil`, `floor`, `fabs` to runtime stubs
written for float/FPR arguments (`f12 -> f0`), so `tan` returned nothing in v0 (the argument double stayed there): `64/x` instead of `64/tan(x)` - plausible for small angles, `+Inf` (0x7f800000 = 2^128 on the PS2) when the argument was 0, and therefore a
projection scale of -2^122. The menu/intro looked fine because the field of view there is small.
Fix: `Kernel/Stubs/LibC.cpp` - `tan/atan/atan2/exp/ceil/floor/fabs` now follow the soft-double ABI (`dblArg`/`dblRet`). The float ones (`sinf`, `cosf`, `fmodf`, `powf`, ...) were already right. Result: the 3D world renders (screenshots).
New debugging aids (all env vars): `PS2X_VU1_DUMP="<after s>,<count>,<dir>"` (+`PS2X_VU1_DUMP_BAD=1` only packets with degenerate first vertex, `PS2X_VU1_DUMP_SCAN=1` finds the VU qw0-2 in RDRAM, `PS2X_VU1_DUMP_ONLYERR`), `PS2X_VU1_CHECK=<dir>` (first run after which the GIF tag templates in VU memory are overwritten, with
an instruction ring), `PS2X_VU1_TRACE_FLAG=<hex>`, `PS2X_VU1_DEBUG_TAG`, `PS2X_VU1_STATS=1`, `PS2X_VIF_LOG=<n>` (UNPACKs into the constant block), `PS2X_WATCH_W=<lo>,<hi>[,<max>]` (guest write watch via `ps2TraceGuestWrite`, also `PS2X_WATCH_RING=1` = last 512 hits
flushed at a bad VU dump), `PS2X_WATCH_POLL=<addr>` (reports changes of one guest word from any writer incl. HLE/DMA), `PS2X_GS_CAPTURE_FROM=<s>` (delayed GS capture start), `PS2X_IPU_DUMP=<dir>`, `PS2X_TRACE_IPU=1` (now also SETIQ matrices and FIFO pushes).
Open (rendering quality, not diagnosed): the default view of Level 0 (`world_neutral.png`: Rynn on the grass, terrain, sky) is right, but once the camera pitches/yaws the polygons that cross the frustum come out as large wedges and stretched textures
(`world_lookdown/lookup/lookup2/turned.png`) - the VU1 clipper path (`0x15f8-0x26b8`, Sutherland-Hodgman with CLIP/FCGET/DIV) or its inputs (FP clamp/denormal/Q-latency semantics of the interpreter) or vertices outside the GS guard band are the suspects; the GS capture plus `PS2X_VU1_TRACE_FLAG`
are the tools (a capture from the failing view was not obtained: the START-driven intro skip needed to reach the player kills the main thread in many runs, see 3). The sw/d3d11 comparison was not possible (`shot` of the raylib window is blank white).
Open (VU1 side, seen in some runs after the fix): in a few percent of the batches with the grid/warp flag (`[7].x & 0x3000`) the microprogram writes outside its buffers and the data memory is trashed; the next XGKICK then reports `[VU1 reserved lower] ... 0xfffffff8`
(GIF tag with FLG=3 from garbage) and the VU1 run is stopped. The world still renders, the affected batch is dropped. The batch inputs look sane at program start (`[43] = 10x11 grid, base 66`), not yet explained.

**Update (VU1 investigation, docs/reference/vu1-correctness.md):** the wedges, the stretched textures and the grid/warp overflow (`[VU1 reserved lower] ... 0xfffffff8`) were *not* VU1 bugs: a VIF1 DMA with CHCR.DIR=0 (GS local->host read-back, 128x128 image, 16 per run) was parsed as a VIF packet and its garbage `STCYCL` poisoned `CYCLE` (skipping-write UNPACKs scattered the vertex data). Fix: `patches/vu1_fix.patch`; an independent reference simulator (`runtime/vu1ref/`) agrees bit-exactly with the VU1 interpreter on 40k captured runs.

### 2. FMV noise/fringing: root cause = `sceIpuInit` stub with another game's table addresses
`Kernel/Stubs/IPU.cpp` replaces the game's `sceIpuInit` (bound by signature) and hard-coded `IQVAL_BASE = 0x1721e0`/`VQVAL_BASE = 0x172230` (the game the stub was written for). In Drakan that address is *code*, so SETIQ loaded MIPS opcodes as the intra/non-intra quantiser
matrices (`33 32 36 2 48 0 ...`) and every dequantised AC coefficient was scaled wrongly (blocky noise, chroma fringes, mean error 1 LSB, peaks 80+). Real tables: `0x517000` (matrices) and `0x517050` (CLUT) (docs/reference/fmv.md).
Fix: `ps2_stubs::setIpuInitTables(iq, vq, setD4ChcrHelper)`; `drakan_overrides.cpp` sets `0x517000 / 0x517050` and disables the (wrong-game) `SetD4_CHCR` helper call. Verified with `PS2X_IPU_DUMP`: decoded frame 100 of the logo movie vs the `fmv_test`/FFmpeg reference frame 99: **bit exact (max diff 0)**
(before: 35 dB, peaks 84). Both movies (logo, intro) go through the same path. `fmv_before_*.png` vs `fmv_after_*.png`.

### 3. Level-load race (null `JALR` at 0x12c4d8) and stalls
* `FUN_0012c3f0` (13 callers) is a resource-object state setter that calls a virtual method through `[obj+0x18]`; the crash is that field being null = the object being used before it is constructed / after it was destroyed - a completion overtaking the requester.
  The old protection was a *wall-clock* sleep of 2 ms in the 0x1202 worker (host dependent). It is now a **guest-time** delay: `EeScheduler::postAfterGuestCycles(cycles, fn)` (new `EeEventType::DelayedCall`, a cycle-deadline event without host pacing: deterministic, independent of host speed/scheduling, skipped at once while the guest is idle,
  and the form that also works for a single-threaded cooperative scheduler). `DRAKAN_RES_LATENCY_US` (default 50 since 2026-10-22, was 2000) is converted to EE cycles (294.912 MHz).
  "Skipped at once while idle" does not hold in the uncapped menus: the guest clock runs at a few percent of real time
  there, so 2000 µs took 30-200 ms wall per batch (`docs/journal/2026-10-22/options-black-screen.md`).
* **The "stall until Cross is pressed" is (mostly) an artifact of the test aid `DRAKAN_AUTOPRESS`**, which taps START *and* CROSS every few seconds. START opens the in-game pause menu ("PAUSED: Save Game / Load Game / Options / Resume / Quit", screenshot at GS frame 1000
  of an autopress run), CROSS then selects its highlighted first entry "Save Game" -> the memory-card dialog, which polls `sceMcGetInfo/GetDir/Sync` forever until a button answers it (the `[MC]` spam without new frames). With only CROSS tapped (`DRAKAN_AUTOPRESS_NOSTART=1`) 5/5 boots were fine; without any autopress 20/20.
* The same START+CROSS taps are also what produced the intermittent **main-thread death** seen in ~80 % of autopress boots (and the lead's "null JALR at 0x12c4d8" level-load crash): the main thread jumps through a null virtual slot and ends (`Game thread returned. PC=0x0` only shows at shutdown; the display thread keeps flipping, hence "no
  frames"). Captured with the new `PS2X_TRACE_RING=1` (last 512 control transfers flushed when the main thread ends): the movie player decode step (`FUN_001767a0` at 0x176a40) calls its completion callback `[obj+0xb8]` (thunk 0x440610) on its listener object `[obj+0xbc]` (0xd60b80), whose vtable (`[+0x98]`) already belongs to a destroyed/base state (slot +0x7c = 0): the listener
  (a menu/pause screen object) is destroyed while the movie player still reports to it. It needs START (pause menu) pressed while a movie/stream is in flight; timing dependent (logging to stderr hides it). Not fixed - the guest-time latency removes the *completion overtakes requester* family, but this is a
  lifetime race between the pause menu and the movie player (open item, reproduce with `DRAKAN_AUTOPRESS=4000 PS2X_TRACE_RING=1 PS2X_MAX_SECONDS=60`; tell-tale: `[trace-ring] ... 0x440624 -> 0x0`).
  **SOLVED, see `docs/journal/2026-10-17/crash-listener.md`**: not a game lifetime race but a runtime bug (a callee unwound by a scheduler checkpoint at its own entry pc was treated as returned in `dispatchGuestBranch`; also the root cause of the null JALR at 0x12c4d8 and the pc=0/1/2 deaths). Fix: `patches/listener_fix.patch`.
* Other findings on the way: with the old IPU tables the logo movie was decoded with garbage quantisers (section 2) - not the cause of the hang.
* Boots without any autopress/START: see section 5.

### 4. Performance finding (details in `docs/journal/2026-10-17/perf-notes-intro.md`)
The VU1 interpreter is 50-80 % of the wall time at ~9 Mcycle/s (113 ns/cycle, ~470 runs per game frame): gameplay is 2-3 fps and the level streaming (one loader step per game frame, 213 MB `LEVEL0RO.VDB`) takes minutes. GS/D3D11 is not the limit. VU1 AOT/threaded code is the one change that fixes
intro speed, "Reloading..." and the stall impression.

### 5. Boot tests (no autopress, no key presses except the ones a player makes)
* With `DRAKAN_AUTOPRESS=4000` (START+CROSS every 4 s): 4 of 5 boots end with the dead main thread above (display thread alive at 60 Hz, game main loop gone); older build 2 of 4.
* `build/input_tests/boots2.sh 10 n1` (script `nb.txt`: Cross once on the title menu = New Game, nothing else): **10/10** boots went through the logo movie, menu, New Game and into the Level 0 resource load (identical resource sequence each time, 265 `LEVEL0RO` HLE lines), no crash, no hang.
* `build/input_tests/boots3.sh 10 p1` (script `nb2.txt`: Cross on the menu, then Start taps to skip the intro, `sync play 10`): **10/10** boots reached controllable gameplay (~190 s each at the current VU1 speed), no `DRAKAN_AUTOPRESS`/`DRAKAN_INPUT_AUTOSTART`.
* The load itself is slow (see 4); a run that is "still on the menu" 60 s after New Game is loading, not hung (the `Loading...` bar of `fmv_after_1300.png`).

## 5. Merged build: drakan = ux + perf + settings overlay (play folder `dist/DrakanPC/`)

Branch `drakan` of tools/PS2Recomp now contains `drakan-ux` (D3D11 default, drakan.ini, latched present/pacing, flicker/blur/fullscreen/audio fixes), `drakan-perf`
(emitter fast paths, VU1 AOT microcode, async GS worker, `PS2X_PERF`) and the wiring of `runtime/settings`. Worktrees `PS2Recomp-ux` / `PS2Recomp-perf` removed, branches kept.
Build dir `build/recomp_merged/` (`setup.ps1` configure, `gen.ps1` recompile, `build.ps1`; /O2 full build 6.5 min, runtime-only relink seconds; exe in `build/recomp_merged/bin`).

### Merge notes
* `gs_gsr_bridge.cpp`: perf's record queue + worker kept; ux's `tickWindow` (syncSize + `presentLatched`), hostTick pacing (`waitForPresent` / 60 Hz sleep), fullscreen, `[gsr-pacing]` kept.
  `rmtx` is now a `recursive_mutex` (lock order rmtx, then gsr; the overlay runs inside `presentLatched`). Window-mode / size requests from the overlay are queued and executed by `hostTick`
  outside the present. The child window forwards mouse messages to the host window (the overlay listens there).
* `ps2_runtime.cpp`: `PS2X_TRACE_RING` ring now lives in `traceTransfer` (enabled by `g_ps2DispatchTrace`). `ps2_vu1_core.cpp`: debug aids (VU dump/ring/trace) kept; the ring/trace only run in the generic (non-AOT) loop, use `DRAKAN_VU_AOT=0`.
* `LibC.cpp` soft-double libm fix kept. Vblank host deadlines are clamped to now - 2 frames (`EeScheduler.cpp`) so a paused game (overlay) does not catch up with a vblank burst.

### New in this build
* **Settings overlay** (F1, gamepad Back+LB+RB hold, Options > "PC Settings"): `src/lib/drakan_settings_host.cpp` (`DrakanSettingsHost`) on gsr live setters, `AudioOutput`, `drakan_input::reloadConfig`; pause gate in the `PadMgr_Update`
  hook (`drakan_settings_host::isOpen()`), `setMenuActive` while open. Options hook on `0x4424e0` (widget 0x2345), XDB 156 renamed through a programmatic `XdbTextOverride` in `drakan_overrides.cpp`.
  Restart (language / prompt style) relaunches the exe (`DRAKAN_WAIT_PID` makes the new instance wait for the old one).
* drakan.ini keys: `[general] iso, elf, ui_prompts, language, settings_ui_scale`; `[video] window_mode, frame_limit, anisotropic, texture_upscale, texture_upscale_filter, mip_bias, hd_textures (alias texture_replace; hd_textures wins),
  texture_dump, texture_dir, texture_budget_mb` (GSR_* env vars still override). `frame_limit` caps host presents only.
* ELF optional: without an extracted `SCES_500.06` the exe extracts it from the ISO to `%LOCALAPPDATA%\DrakanPC`.

### Smoke test (RTX 5080, internal scale 2, 1280x960)
logo movie / menu sharp and stable; F1 opens/closes, Options > PC Settings opens it (game held: flips 0, resumes without burst), borderless fullscreen applied live with the keep/revert prompt, settings written back to drakan.ini;
New Game -> Level 0 intro renders the 3D scene; audio dump RMS 1400-5700 (audible); Level 0 ~23-24 flips2/s (46-48 vblank/s), EE thread 100 % (VU1 ~66 Mcycles/s).

### Known issues (others are working on these)
* VU1 clipping wedge artifacts when the camera pitches / yaws; grid/warp batch overflow (VU memory trashed in a few batches); START intro-skip kills the main thread (movie listener null).
* Performance ~23-25 fps in 3D (target 30): VU1 interpreter/AOT is the bound (see docs/reference/perf.md).
* Overlay: static restart asterisks on render scale / texture upscaling; `mip_bias = auto` shows as 0.00 in the UI; prompts regenerate only at the next level load / restart.

## 5b. Fixes branch `drakan-fixes` (narration loop, popups, title)

* **Narration looping on repeat.** Reproduced: any time the EE stops while the SPU2 keeps rendering (F1 overlay open during the New Game book narration; measured with `DRAKAN_AUDIO_DUMP`, the
  envelope repeats with a 14 s period) a streamed voice replays its last chunk. Fix: `spu2::AudioOutput::setPaused` + a guest heartbeat watchdog (vblank counter, 250 ms): the SPU2 is frozen and the output
  silent while the guest is held or stalled, then resumes seamlessly (spu2_test [11]). Verified in the game with `DRAKAN_DEBUG_HOLD=34,15` (EE held 15 s: 15 s of silence, no repeat), the F1 overlay and
  `pause_when_unfocused`. Window state: raylib `FLAG_WINDOW_ALWAYS_RUN` (otherwise `PollInputEvents` blocks while minimised), no present while minimised, game keeps full speed unfocused (default).
  `drakan.ini [general] pause_when_unfocused = 0|1` (env `DRAKAN_PAUSE_UNFOCUSED`): 1 holds the guest at the next PadMgr_Update and pauses the audio. Not reproduced: a loop caused purely by focus loss
  (covering / minimising the window kept 60 flips/s and continuous audio on the unfixed build); the generic stall protection covers every cause (blocked console write, hung load, overlay).
* **Popups.** Enter / keypad Enter = CROSS in the gameplay layer (config migration for old files), automatic menu layer while a GUI screen is active (`0x4fb60c`, id at `+0x18`; the quit confirmation is id 1),
  left click = Cross there.
* **Console window = root-cause candidate for "unfocused" loops.** The old exe was a console-subsystem app printing to its console; a click / selection in a console with QuickEdit blocks every writer to it until the
  selection ends, so the EE thread (which logs to stderr) froze while the SPU2 kept looping. (Could not drive a real console selection from a script, so the freeze itself is argued from the documented console
  behaviour, not measured.) Now: GUI subsystem (`/SUBSYSTEM:WINDOWS`, CMake `PS2X_DRAKAN_GUI_SUBSYSTEM`), `drakan_log.cpp` routes stdout / stderr through an in-process pipe -> bounded 8 MB queue (drops oldest)
  -> writer thread -> `drakan.log` (+ `drakan.1.log` for the previous run), the original stderr if a script redirected it (test scripts keep working), and a console only with `[general] console = 1` /
  `DRAKAN_CONSOLE=1` (AllocConsole, QuickEdit off). Logging can no longer block the game threads.
* **"MISSING FUNCTION" at 46 s with START mashing** (`pc = ra = 0xa21624a1`, a0 = 0x3c030007 (an instruction word), a1 = 0xfffffffc; log line "[memcpy] size clamp from 0xfffffffc"): not a function-boundary gap. The
  address is garbage (odd, KSEG1): a memcpy with a negative length (-4) smashed the stack / return address (callers 0x10523c/0x10525c loop <- 0x103828 <- 0x140e74). Nothing to register; the cause is an upstream
  bad size (probably during the skipped intro cut-scene / level start with START and Cross held); not investigated further.
* Window title "Drakan: The Ancients' Gates", application icon (`src/res/drakan.rc`, GLFW_ICON).

## 6. Next steps (older list, still valid where not superseded above)
1. **RSRCIOP HLE** (other agent, `decomp/runtime/hle/`): implement `PS2Runtime::HleRpcHooks` (`canBind(sid)`, `handle(...)`)
   in place of the logging stub in `drakan_overrides.cpp`. The hook sees `RpcRequest {sid, function, mode, send{addr,size},
   receive{addr,size}, endFunction, endParameter}`; write the reply into guest RAM at `receive.address` and return
   `RpcResult{handled=true}`. The runtime then runs the game's end function (`0x4af488` -> `iSignalSema`) as a guest
   invocation. Calling the EE servers 0x1301/0x1302/0x1303 (registered at the `sd=` addresses above) from the HLE side
   needs a "run guest function on server thread" helper (see `SifCallRpc`/`GuestInvocation` in
   `Kernel/Syscalls/RPC.cpp` and `EeScheduler::invokeCurrent`); not wired yet.
2. The cdrom0: root is the directory of the ELF (`iso/`); the extracted tree has no `ENGLISH/`. Either extract the disc
   next to the ELF or let the HLE file layer read the ISO directly.
3. Pad (`scePad*`), memory card (`sceMc*`) and `sceSdRemote` already have runtime stubs bound by address; expect to
   replace them per docs/reference/iop-and-io.md 7.3 once the game gets that far.
4. Not yet validated: the 202 signature-matched SDK/libc bindings (`config.toml` `stubs`, incl. host `malloc`/`memcpy`/
   `sprintf`/libm) come from the old analyzer and were only exercised by boot. A wrong one will show up as odd data, not
   as a crash. The 28 unresolved `jr` sites go through `dispatchGuestBranch`; an unknown target is logged by
   `[guest-branch:missing-target]` (none seen so far).
5. Performance (not measured, no game loop yet): every call still goes through `dispatchGuestBranch` -> function-table
   lookup -> `EeScheduler::checkpointDue` (timer + IOP cycle accounting), GPRs are `__m128i` in memory, memory accesses are
   per-access lambdas with a special-address check. Candidate work, in order: inline checkpoint fast path (counter, slow
   path every N), direct calls for functions that are not hooked, plain `uint64_t` GPR file, inline RDRAM fast path.
6. Compile time: split TUs by instruction count rather than function count (a few TUs are 3x the average), keep `/Od` for
   bring-up iterations, `/O2` for release.
7. Overlays: the six `.DVP.overlay` sections are 2 KB each at address 0 (VU microcode, not EE code). Nothing to recompile.
8. Left as is: `sceSifLoadModuleBuffer` stub returns fake ids 100+; `mc0`/`mc1` directories are created in the working
   directory.

## 7. Rendering bug hunt (menu highlight, blob shadows, inventory)

Reproduced from boot captures (`PS2X_GS_CAPTURE` from boot, 3-7 GB for a world + inventory run; scripted input `build/input_tests/g*.txt`, `rung.sh`) and replayed with `gsr_replay`.

* **Blob shadows (fixed, gsr)**: not a readback problem (the game's VIF1 DIR=0 readbacks are the 16 x 128x128 screen grabs behind the pause menu, no shadow involvement). The shadow is rendered
  into a 128x128 CT32 target at FBP 0x1f2 (clear + 10854-vertex caster silhouette with DATE), then sampled as a texture by a few projective quads (CLAMP). `findRTForTexture` returned the *first*
  matching render target in creation order. Targets are allocated with >= 512 rows, so an older 128-wide target (FBP 0x1df) covers the same memory at offset (64,288): the shadow map was
  sampled from stale memory (alpha 140 on the whole clamped area -> huge uniform dark polygons with stripes). Now the target whose base is exactly the TBP wins, then one that was drawn to in
  the texture rows, then the most recently used. Regression test `rtOverlapTest` in `gsr_test_synthetic` (42 checks, fails without the fix).
  Before/after: `build/screenshots/fix_shadow_before.png` / `fix_shadow_after.png` (same capture frame).
* **Menu highlight ("New Game", "OK")**: investigated, renderer is faithful. The highlight is a 128x128 T8 heat map the EE regenerates every frame (a fire cellular automaton, 4-neighbour
  average minus noise: `f_0x4a0018`, uses PEXTUB/PEXTLB/PADDH/QFSRV/PPACB, all checked against PS2 semantics) uploaded with a fresh 256-entry CLUT, drawn additively (ALPHA 0x48, palette
  alpha 0xFA so the blend factor is 1.95, shader path, COLCLAMP 0 wrap). Decoded texture == raw index data through the palette (pixel exact, diff 0), no CLUT/cache staleness at scale 1
  without upscaling. The 4-neighbour average decouples even/odd pixels, which gives the checkerboard speckle; on a CRT / interlaced field blur this looks like flames, 1:1 on a PC it looks
  noisy. Not changed. (An optional cosmetic smoothing of this draw would be a separate feature.)
* **Inventory / weapon preview smear, red square behind the orb, missing panel backgrounds**: could not be reproduced (a new game has no weapons; the Equipment tutorial popup appears but the
  game draws only the popup and z-clears, no preview). The most likely common cause is the same stale-overlapping-render-target sampling as the shadows (smear = old frames, square = stale
  rows); unverified. Needs a save with items.
* Debug aids added: `GSR_XFERLOG=1` (every GS transfer start), `GSR_RTDUMP=<draw>[,..]` + `GSR_RTDUMP_DIR`/`GSR_RTDUMP_ROWS` (render target + alpha PNG right after a draw),
  `gsr_replay --upload <hex dbp>` (raw bytes of every host->local upload to a block) and `--fb <fbp> <fbw> <psm> <w> <h>`; runtime `PS2X_GIFDMA_LOG=<n>`.

## 8. Round 3: widescreen, subtitles, HD textures, menu highlight (branch `drakan`, patches 0022+)

* **Merged** `drakan-fixes` (GUI subsystem exe + `drakan.log`, pause / stall audio safety net, Enter / click confirm + screen-manager menu layer, window title / icon); worktree `PS2Recomp-fixes` removed, branch kept.
  Applied `patches/memcpy_fix.patch` (cvt.w.s truncates toward zero + saturates; LibC mem* drops sizes above RAM): header-only, full recompile of the generated code, no regeneration.
* **Widescreen** (`docs/reference/widescreen.md` section 9): `[video] aspect = 4:3 | 16:9 | 21:9 | auto | native | stretch` drives the anamorphic projection hook and the presenter from one place, live from F1; 4:3 pillarbox
  for title, FMV and loading (state machine in `drakan_widescreen.cpp`); `hud_aspect_correct` de-stretches UI sprites in gsr (default on).
* **Narration subtitles** (`docs/reference/subtitles.md` section 8): SRT files in `subtitles/`, trigger through the new `ResourceService` request observer, SPU2-frame clock, ImGui HUD layer; names resolved (Jasaad, Surdana, Mala-Shae).
* **HD textures** (`docs/reference/hd-textures.md` section 7): volatile (animated) slot detection, deflate PNG dumps, `.a0` marker, prompt textures excluded.
* **Smooth menu highlight** `[video] smooth_menu_highlight = 1` (default): the fire highlight (128x128 T8 heat map in a volatile slot, drawn as ALPHA = Cs*As + Cd sprite) and the CT16 outline overlay drawn in the same frame get a
  separable [1 2 1] blur at decode time (`gsr::Renderer::setSmoothMenuHighlight`, env `GSR_SMOOTHHL`); before / after in `build/screenshots/r3_menu_highlight_before_after.png`.
* New settings: `[video] hud_aspect_correct`, `smooth_menu_highlight`, `[subtitles] narration`, `size`; F1 rows added (Display, Gameplay). The settings library gained `Aspect::Ratio21x9 / Auto`, `SubtitleSettings`, `HudLayer`.
* Test aids: `GSR_WINSHOT=<prefix> GSR_WINSHOT_AT=<frame,...>` (presented window incl. overlay as PNG), `GSR_HLDUMP=<dir>`, `DRAKAN_WS_LOG=1`, `DRAKAN_NO_PROMPT_EXCLUDE=1`, `DRAKAN_SUB_OFFSET_MS`.
* Not reachable here: the castle gate of Level 1 (no save game, a fresh game ends in Level 0); gameplay / shadows were checked in Level 0.

## 9. Round 5: merge of win + ctl + geo + inv, regeneration, smoke test (branch `drakan`, patches 0027+)

* **Merged** (no textual conflicts): `drakan-geo` (SQRT.S takes its source from the FT field, was FS = $f0 for all 329 uses; VU0 macro VDIV / VRSQRT / VSQRT PS2 semantics, VFTOI saturation: emitter change, code regenerated with `build/recomp_merged/gen.ps1`),
  `drakan-win` (native Win32 top-level window, FPS meter, quit-to-desktop), `drakan-ctl` (controls v2), `drakan-inv` (HUD clusters, `hud_scale`, 32:9 clamp). Worktrees removed, branches kept; `drakan-snd` not merged.
* Full /O2 build clean; unit tests: gsr_test_synthetic 47, swizzle 92, texup 19, input_test 632, settings_test 201, ui_test 56, spu2_test 89, hle_test 509, override_test 18, all pass. `gsr_test_live` needs a recent GS capture
  (the only capture in `build/captures` is a black frame: 8 of 34 checks fail on "baseline frame is black", unrelated to this merge).
* **Fixed in gsr (`runtime/gs/src/d3d11_backend.cpp`)** - found while looking up with the mouse at 16:9 (`build/screenshots/r5_g_mouseup_3s.png`): (1) the sky dome (large triangle draw with ZTST ALWAYS) was treated as a UI quad,
  squeezed into a 4:3 rectangle and merged the HUD clusters into one centred page; large non-sprite draws (area > 20 % of the frame) are no longer UI. (2) the HP / mana orb gem samples memory that aliases a render target while the camera looks
  at the sky, and the "UI never samples a render target" rule left the gem unmapped (wide and shifted off its ring); render-target sampling now excludes only draws larger than 30 % of the frame. Debug: `GSR_HUDLOG=1` lists the clusters per frame.
* **Defaults for the HD pack** (requested by the HD texture work): `texture_upscale = auto`, `anisotropic = 16`, `texture_budget_mb = 0` (auto) in `drakan_config.cpp`, `gs_gsr_bridge.cpp` (0 no longer clamped to 64 MB) and the settings library
  (`Types.h`, `Store.cpp` reads / writes "auto", Menu "Texture upscaling" has Auto / Off / 2x / 4x). With the pack: `[texup] exit: replaced=57 upscaled=36` after a short Level 0 + pause + quit run (`GSR_TEXUP_LOG=1`).
* **Smoke test** (real SendInput, 1600x900, 16:9, `build/recomp_merged/t/*.ps1`, `build/screenshots/r5_*.png`): logo, title menu hover highlights rows, left click activates (rows are full-width widgets of the game, so a click left / right of a row at the
  same height activates it; clicks above / below rows do nothing), right click goes back, cursor hidden in gameplay; New Game -> Level 0: the braid hangs naturally (`r5_level0_a.png`); mouse up looks up (cam pitch +0.44 held for 3 s, no snap back), down -0.63;
  A / D strafe and return; HUD orbs intact; F2 simple / detailed FPS counter (`r5_fps_detailed.png`), F1 opens (screen 23); Load Game shows "Reading save data..."; pause -> Quit -> Yes exits the process.
* **Inventory (SELECT)**: the backdrop is a clean stone panel, no strips or wedge, and the Rynn portrait is lit and natural (`r5_inventory*.png`); the fix of the broken `sqrt` removed the inventory problem of section 7.
* Open: a pale light wedge in the top right of the sky at 16:9 when looking up (sun glow quad, probably stretched by the widescreen hook; not seen at 4:3). No smoke / particle effects were visible in Level 0. No crashes in about 12 boots.

## 10. Sound audit: why positional SFX were silent (branch `drakan-snd`, `docs/reference/sound-debug.md`)

* **Root cause = the SQRT.S operand bug** (translated from `fs` instead of `ft`; fixed independently by `drakan-geo` commit 997ee7a, already merged in section 9, and found here from the sound side):
  the distance gain of every positional sound came out 0, so footsteps, attack grunts / swishes, NPC idles and birds were started but played at volume 0. Same bug: Rynn not drawn, inventory half-empty.
  **Any build made before the regeneration of the generated sources has it** (check: `DRAKAN_SND_DBG=1`, `[setgain]` gain=0 for a sound inside its range).
* Verified on the merged tree (`scripts/harness/snd_test_run.sh` + `scripts/harness/recipes/snd_attack_test.txt`): after equipping the dagger, 8 swings key on "Short Swish 011/02/03/04" (RESOURES.SDB 450-453) and "Rynn Attack Long 01 / Short 02-04" (SYSTEM.SDB) with
  volumes 0x1a19 .. 0x375b; the audio dump shows a transient at every swing (RMS x4..x8); footsteps ("Grass Short", "Dirt Gravelly") at 0x0d0c .. 0x3fff; birds / gusts attenuated by distance.
* The stereo flag of the sound manager was never set without a settings load (everything mono although Options > Audio > Stereo Sound showed on): `soundPump` now keeps it equal to the option (`DRAKAN_STEREO_SYNC=0` disables).
* Checked and fine: SPU2 voice allocation / ADSR / uploads / lifetimes, 0x1203 batches (no unknown or unhandled command), distance culling, listener tracking, 0x305/.ssd handling (see docs/reference/sound-debug.md section 2).
* New diagnostics: `DRAKAN_SND_LOG=3|4|5`, `DRAKAN_SND_DBG=1`, `DRAKAN_TEST_SUB`, `DRAKAN_SUB_LOG`; tools `analysis/scripts/snd_*.py`, `sub_*.py`.
* Narration subtitles re-timed and re-synchronised (key-on based clock): `docs/reference/subtitles.md` section 9.
