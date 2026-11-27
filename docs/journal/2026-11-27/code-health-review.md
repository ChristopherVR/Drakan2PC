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

Recompiler (`ps2xRecomp`), not changed yet: these alter generated code and need a full rebuild plus play testing.
- With `indirect_fallback = false` the jump-table detector never runs; a switch missing from `jumptables.csv` only
  fails when that case is reached in play (thread made dormant).
- `J` to a known function is a direct C++ call, bypassing the function table, so runtime overrides (e.g. the
  `0x4bc080` replacement) are skipped on tail jumps.
- `ctx->pc` is stored only when the emitted text contains `runtime->`; READ/WRITE macros hide it, so MMIO hooks
  keyed on pc can see a stale pc.
- `ADD32_OV`/`SUB32_OV`, DADD/DSUB/DADDI(U) use signed overflow (UB under clang/wasm; MSVC happens to be safe).
- Stale `ps2_recomp_NNNN.cpp` files are never deleted while CMake globs them; output writes are unchecked.
- `findContainingExternalFunction` is a linear scan per branch target per pass (about 7e8 iterations a pass).

## Readability hotspots (refactor plan, not started)

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
  validators: one `GuestRam` / `resolveGuestSpan` header.
- Raw guest struct offsets (`+0x4b0`, `+0xfc`, ...) and guest PCs (`0x437170`, ...) repeated as literals: a
  `guest_layout.h` with named constants.
- Three `exeDir` variants with different encodings (UTF-8 vs ANSI) and a dozen `lower`/`trim` helpers: one utility.
- `drakan_input.ini` parsed twice with different validation (`input/Config.cpp`, `settings/Store.cpp`): one schema.

## Verification

- `runtime-tests-asan`: 185 tests, no AddressSanitizer reports (the timing test `precise_sleep` only fails while a
  parallel build saturates the CPU; it passes on an idle machine).
- Incremental game build clean; `scripts/harness/lifecycle_run.py` boot, save_surdana and restore phases pass (11/11)
  on `dist/DrakanPC`.
- Kept on purpose: guest `sprintf`/`vsprintf` still cut at 255 characters (the cap guards guest stack buffers; lifting
  it needs a gameplay check). `world::Reader` keeps its inclusive record bound (the native allocator checks inclusively).
- Not yet checked in real play: multiplayer under load, co-op object replays, long sessions with many guest threads.
