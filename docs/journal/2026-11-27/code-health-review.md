# Code health review: leaks, crashes, exceptions, stack, performance, readability

Date: 2026-11-27. A read-only review of all owned C++ (about 320k lines), split into six areas, followed by fixes.
Every finding below was checked against its callers. Status is as of this date; later commits may change it.

The same day the `*-asan` presets were added (`cmake/Sanitizers.cmake`, see BUILDING.md "Sanitizer builds"). The
first Windows ASan run of the 184 runtime tests failed only `gameui_settings_test`: `SettingsPages::activateCard`
read its `Item` after `closeCard()` had cleared `cardItems_`, the vector it pointed into. Fixed by copying the item.

## What held up

- Guest VRAM, VU data memory, VIF unpack, XGKICK and DMA chain walks are masked or capped everywhere they were traced.
- Network packet parsers (`runtime/net/src/Protocol.cpp`) bound every length and count and check floats are finite.
- Stack: guest calls nest as host calls, but generated frames are 40 to 144 bytes and the game thread has a 256 MB
  stack (`ps2xRuntime/CMakeLists.txt`), more than 1M guest frames. No large stack buffers in the hot code.
- Recompiled dispatch is a dense table; memory access is an inlined range test plus memcpy.

## Defects found (fixed unless marked)

Renderer (`runtime/gs`, `runtime/gpu`)
- `changeScale()` freed every render target but left `gridCapture.rt` pointing at one: heap corruption when the
  resolution changes while an item-grid screen is open (D3D11 and WebGPU). Fixed.
- Blocking `readTexture` (gpu Context and the RHI copy) stopped waiting on any device error while the map callback
  still held references into its stack frame: stack and heap corruption later. The callback now owns its state.
- PNG decoder: unbounded inflate output, 1 GB reserve from the header, unsynchronised lazy Huffman tables, and
  texture pool jobs without try/catch (a `bad_alloc` terminated the game). Fixed.
- `listDir` threw on non-ANSI file names in `textures/`. Fixed (skips such files).
- Frame-latency waitable handle never closed and kept after a failed swapchain re-create. Fixed.

Game glue and netplay
- Co-op: received object messages were replayed into the engine without the type allow-list the sender applies, so
  a modified client could post any engine message to any keyed object. Fixed, plus caps on both event queues and an
  atomic `g_host` set inside the handover lock.
- Server: chat/ready flood starves reliable gameplay messages; unbounded peer creation; Query reflection
  amplification; scoreboard rows missing lives/eliminated; pvp event queue unbounded and carried across matches; RPC
  timeout releases the server lock while the guest still reads the buffer. Fixed.
- HD movie index: `std::stoull` and a throwing directory iterator on player files. Fixed.

Guest runtime core
- EE invocation stack slots (64 in a 1 MB pool) are never returned; after about 64 distinct thread ids the game thread
  dies with "EE invocation stack space exhausted". Fixed.
- Guest-length writes through `getMemPtr()` (fioRead, sceMcRead, fread, GS copies) can run past the scratchpad or RAM;
  SIF allocates a guest-sized vector before validating; `publishSnapshot` rebuilds debug state on every sema op;
  `m_liveContext` dangles; `reset()` leaves host tasks queued. Fixed.

VU1
- The async VU1 worker ran jobs without try/catch (a throw terminated the game and left every sync waiting
  forever); batched jobs never returned their chain buffers; the batch pool could restart after shutdown and hang
  at exit. Fixed.

Libraries
- Mod zip entry sizes never checked against the archive (4 GB allocation in an unguarded worker thread); SPU2
  `procBatchEx` missing braces writes `rets[-1]` and builds the wrong voice bit; WASAPI fallback resampler drops its
  phase each period (buzz); XInput polls disconnected pads every frame; input sink thread can hang exit;
  `world::Reader` off by one; settings accept nan. Fixed.

Recompiler (`ps2xRecomp`), fixed in a second pass the same day (generated code diffed against the previous output;
every difference traced to one of these):
- With `indirect_fallback = false` the jump-table detector never ran; a switch missing from `jumptables.csv` only
  failed when that case was reached in play. It now always runs; this ELF has no new tables, and the 28 remaining
  unresolved non-`$ra` JR sites are printed as build warnings.
- `J` to a known function was a direct C++ call, bypassing the function table, so runtime overrides (e.g. the
  `0x4bc080` replacement) were skipped on tail jumps. Now calls through the table like `PS2_JAL` (56 sites).
- `ctx->pc` was stored only when the emitted text contained `runtime->`. Translators now flag statements that enter
  the runtime; run-time-address loads and stores use `READn_AT`/`WRITEn_AT`, which store pc only on the slow
  (MMIO/scratchpad) path. Plain RAM accesses cost the same.
- DADD/DSUB/DADDI(U) and ADD/SUB used signed overflow (UB under clang/wasm); now unsigned with sign-bit checks.
- Stale `ps2_recomp_NNNN.cpp` files are deleted and output writes are checked. Headers were written with CRLF, so the
  "unchanged" check never matched and every run recompiled all generated code; now LF.
- `findContainingExternalFunction` used a linear scan; now a sorted index (recompile 9.3 s to 2.2 s).

## Readability hotspots (refactored the same day)

All rows below and the cross-cutting items were done as behaviour-preserving `refactor(game)` commits, with any bug
found on the way in its own `fix(game)` commit: mod pack covers rewritten on every start (signed char compare), RPC
`run()` timeout freeing a buffer the guest still read, `exeDir` read with the ANSI API, `drakan_input.ini` opened by
a UTF-8 path as ANSI (controls reset in non-ANSI folders), `path::string()` throwing in CD/card/fio stubs, DMA QWC
read as 32 bits, and two wrapping bounds checks (VAG, DMA chain tags). Runtime library details:
`runtime-libs-cleanup.md`. Not split on purpose: `processVIF1Data` (hottest VIF path).

The worst places to understand, all with a clear split:

| Where | Size | Split |
| --- | --- | --- |
| `runtime/gs/src/gs_draw.inl` `draw()` | 1150 lines | debug hooks, resolveTarget/Depth, world phase, GUI rules, submitDraw |
| `runtime/gs/src/ray_water.cpp` `execute()` | 1150 lines | one function per existing `setStage` stage |
| `drakan_netplay.cpp` `runSession` | 872 lines | server options, client event visitor, per-subsystem pumps |
| `drakan_overrides.cpp` `applyDrakanOverrides` | 788 lines | table of `{addr, hook, &orig, flag}` |
| `ps2_memory.cpp` `writeIORegister` | 500 lines | per DMA channel handlers, named channel bases |
| `ps2_runtime.cpp` `PS2Runtime::run` | 475 lines | StallWatchdog, PcProfiler, shutdownRuntime |
| `ps2_recompiler.cpp` entry discovery | 440 lines | EntryDiscovery class over one FunctionIndex |
| `drakan_settings_host.cpp` | 3600 lines | one file per screen |

Cross-cutting duplication that causes real bugs:
- 17 copies of guest RAM readers (`rd32`, `ramPtr`, ...) in 11 files with different bounds rules, and five guest range
  validators: now `runtime/guest_access.h` (namespace `guest`) and `resolveGuestSpan` in `ps2_memory.h`.
- Raw guest struct offsets (`+0x4b0`, `+0xfc`, ...) and guest PCs (`0x437170`, ...) repeated as literals: now
  `drakan_guest_layout.h`; DMA/GIF/VIF register literals: `ps2_hw_registers.h`.
- Three `exeDir` variants and a dozen `lower`/`trim` helpers: `drakan_cfg::exeDirPath()`, `drakan_text.h` in the
  glue and the header-only `runtime/common` (`common/Text.h`, `common/Path.h`) in the libraries.
- `drakan_input.ini` parsed twice with different validation: one schema, `drakan::input_ini`
  (`input/IniSchema.h`). Now rejected: `directional_attack = none|0`, out-of-range `[advanced]` addresses, nan/inf.

## Verification

- `runtime-tests-asan`: 185 tests, no AddressSanitizer reports (the timing test `precise_sleep` only fails while a
  parallel build saturates the CPU; it passes on an idle machine).
- Incremental game build clean; `scripts/harness/lifecycle_run.py` boot, save_surdana and restore phases pass (11/11)
  on `dist/DrakanPC`.
- Kept on purpose: guest `sprintf`/`vsprintf` still cut at 255 characters (the cap guards guest stack buffers; lifting
  it needs a gameplay check). `world::Reader` keeps its inclusive record bound (the native allocator checks inclusively).
- Not yet checked in real play: multiplayer under load, co-op object replays, long sessions with many guest threads.

After the recompiler fixes and the refactors (drakan.exe SHA-256 `67B434EA...`, all generated code rebuilt):
- `runtime-tests-asan`: 187 of 188 pass, no AddressSanitizer reports. `vertex_records` exceeds its 900 s limit under
  ASan; the Release build passes (10050 checks) and takes the same time before and after this work (103 s vs 106 s,
  145 s vs 152 s, alternating runs).
- `lifecycle_run.py`, all phases (boot, save_surdana, save_4a, menu_save, restore): 15/15 checks pass.
- `play_run.py` screenshots in Surdana (world, HUD, inventory, map, pause menu, walking) look as before. In 2 of 7 runs
  the presentation froze for 7 to 13 s right after a 4K screenshot taken in the inventory ("host handoff timed out");
  older runs show the same warning, and the guest kept running (no stall report), so it is not tied to this work, but
  it is not explained either.
- Python tests: the 6 `test_build_drakan` errors fail the same way at the previous commit.
- Not built: `ps2xTest` (the J tests were updated to the function-table form). Not yet checked in real play:
  multiplayer and co-op, J tail jumps into overridden functions, a play folder under a non-ASCII path.
