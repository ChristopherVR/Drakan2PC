# Linux and browser player implementation — 26 October 2026

## Scope

Continue the ISO-only native Linux and browser port while preserving the Windows build.
Game-derived outputs and diagnostics remain private under build/ and dist/.

## Implemented

- The canonical ISO builder now supports native x86-64 Linux and Emscripten output siblings
  inside build/final-game. Debian/Ubuntu bootstrap obtains missing public dependencies,
  checksum-pinned Python/PowerShell, Emscripten 4.0.23 and FFmpeg 7.1.1 automatically.
- Native Linux has portable executable paths, software GS presentation, ALSA audio with a
  WSLg PulseAudio fallback, and cards next to the deployed executable.
- IsoFs accepts byte sources. Native sources serialize shared FILE seeks; the browser reads
  bounded ranges through a loopback server and a per-worker cache, without preloading the ISO.
- Browser application/guest workers proxy DOM and graphics to the UI thread. Creating the
  context through the Emscripten C API before GLFW establishes TLS ownership required by
  asynchronous buffer uploads. Explicit offscreen framebuffer presentation fixes blank frames.
- Native Wasm exceptions replace legacy JS exception trampolines: the latter throw a TypeError
  when a guest memory call carries v128. A production-header regression covers vector calls
  and exceptions across the same boundary.
- VU arithmetic explicitly implements toward-zero rounding on Wasm, including tiny opposing
  addends and FTZ; native x86 keeps scoped MXCSR rounding.
- AudioWorklet consumes a shared PCM ring. Browser cards/configuration live in IndexedDB,
  with serialized periodic/manual/exit flushes. The local launcher provides isolation headers.
- Portable input publishes coherent samples, retains taps and accumulated mouse movement,
  and blocks held confirmations across GUI transitions. Browser DOM edges and pointer motion
  are retained independently of raylib polling; blur releases held input.

## Evidence so far

- The full native Linux ISO build linked and deployed through the canonical recipe. Live tests
  launched from dist/DrakanPC-linux, rendered the opening movie and gameplay, accepted WASD
  movement/pause, wrote a save and thumbnail, restarted, and loaded that save into gameplay.
- Linux runtime/policy CTest: 31 passed, two disc-dependent cases skipped.
- The Windows ISO build linked and deployed; focused portability regressions passed. Broader
  working-tree tests have unrelated failures and are not reported as entirely green.
- Wasm production tests: 150 million conversion/classification/lane-write comparisons;
  two million arithmetic comparisons with native hardware digest a0aa195d6d236fd4;
  vector arguments/results with native exceptions; retained keyboard/mouse/focus input tests.
- Browser platform probe: actual ISO lookup, colored/text rendering, keyboard event, audio
  worklet connection, IndexedDB flush and restoration, and clean runtime shutdown.
- The full browser game rendered its title menu and the opening Rynn/Arokh 3D cinematic.
  Full gameplay and game-created browser save/reload acceptance are still being checked.
- Software rendering in this WSL/browser test environment is slow (roughly 3–7 fps in the
  opening 3D scene). This is not an acceptable basis for claiming performance parity.

## Remaining acceptance

Continue full browser gameplay, actual audio signal, mouse control and game-created save/reload
checks; validate exit notification and the final input changes in all three deployed builds.
No full playthrough, hardware controller matrix, Firefox/Safari matrix, ARM Linux or GPU renderer
parity has been established.

## Browser acceptance and final input fixes

- A live full-game run reached free gameplay in Surdana, accepted WASD and pause/menu input,
  and reported a successful save. The browser created icon.sys (964 bytes), FolderIcon.ico
  (72,408), Level 1A - Surdana.rsg (172,412) and the main card file (1,284,099).
- The worklet produced nonzero game audio (measured peak 0.003448486328125 in one sample).
- Stop now explicitly notifies the browser before the application's _Exit; the UI reports
  "Session ended. Saves stored." The page then restarted, restored the card from IndexedDB,
  showed the Surdana thumbnail in Load Game, and loaded the save back into free gameplay.
- A transient sync XHR reset initially escaped as a JavaScript worker exception. The source now
  retries transport failures up to three times and returns a failed read without throwing.
  The HTTP launcher uses persistent HTTP/1.1 connections. Synthetic worker tests cover recovery,
  cache reuse, permanent failure, and rejection of a non-range full-file response; five real
  server tests cover bounded bytes, connection reuse and isolation checks.
- Pointer lock succeeds when the test browser is focused. Denied pointer lock / fullscreen
  requests are handled as recoverable UI actions, rather than terminating the game.
- Live guest inspection confirmed mouse motion reaching the original right-stick axes. The
  original position-mode camera recenters on release, which makes it unsuitable for a mouse.
  Temporarily selecting the game's rate mode retained the view after motion stopped. The
  portable camera hook applies that mode while a captured mouse owns the right stick, restores
  the original word after the call, and leaves physical controller / dragon behavior intact.
- Padman can sample faster than the engine consumes input. Portable PadMgr_Update now latches
  one coherent sample per engine input frame, and second-port probes cannot steal player-one
  taps/mouse movement. The production policy regression covers repeated reads within a frame.
- A native WSL software-renderer CPU sample resolved the largest costs to GS submission,
  texture sampling, texture address/CLUT reads and pixel writes. VU arithmetic was a small
  share. Captures and perf data stay private under build/ (perf's mmap output needed /tmp on WSL).

The final camera/input rebuild and live checks follow these observations; hardware GPU performance,
a full playthrough and the browser/controller matrix remain outside the established evidence.

## Final browser and regression results

- The rebuilt browser player accepted a 20 ms confirm from the restored Surdana slot, returned
  to free gameplay, and accepted a 20 ms pause tap. Pointer lock succeeded in the focused browser.
- Captured mouse movement changed the view; moving upward looked upward. The view stayed put
  across idle frames after motion stopped. The rate-mode hook is active only while the mouse
  owns the right stick, outside scripted input and the original dragon path.
- Stop again reported a completed save flush and a clean closed window in the game log.
- Latest Linux CTest: 31 passed / two skipped. Latest focused Windows regressions: six passed.
  Python suite: 106 passed. Wasm tests: all five production-header / worker cases passed.
- Canonical Linux, browser and Windows incremental builds linked and deployed with the final
  input/camera changes. The Linux bootstrap also checks runtime libraries and the amd64 ALSA
  plugin explicitly, avoiding a false missing-package result on multiarch installations.

Performance remains limited by the CPU GS renderer in these local tests. This is functional
ISO-only desktop play support, not a claim of a complete game playthrough or graphics/performance
parity with the Windows D3D11 player. Native final camera capture follows separately.


## Software renderer batching and parallel bands

The native profile identified the software GS, rather than VU arithmetic, as the dominant cost.
SIMD bilinear interpolation preserves the exact scalar float operation order and half-away channel
rounding. A private microbenchmark measured roughly 14.45 ns scalar versus 1.72 ns SIMD, though
whole-game speed improved only modestly from this isolated change.

Parallelizing individual large primitives was insufficient because most game primitives are small.
The backend now queues compatible immutable-texture draws and processes each horizontal band in
original draw order. Workers take small bands dynamically to reduce screen-coverage imbalance.
Transfers, actual CLUT reloads, texture flushes, presentation and public VRAM access are fences.
Feedback/wrapping/overlapping storage and stale retained pages fall back to serial rendering.
The final texture-cache state is selected from the last sampling primitive and highest sampled
band, preserving serial retained-page observations. Worker floating-point control is captured at
submission and restored after each dispatch. Shared worker lifetime outlives backend flushes.

The production-backend test compares all 4 MiB of VRAM against serial rendering, including small
batched primitives, overlapping blends, palette changes, depth/color formats, stale caches, source
feedback, read/write/transfer/reset/rebind fences, independent clients and destruction with pending
draws. Native tests cover all four MXCSR rounding modes. Wasm uses nearest rounding and has the
same backend test. Its standalone test needs an explicit 2 MiB stack; the game uses 16 MiB stacks.

The Linux suite now passes 33 tests with two data-dependent skips; eight focused Windows tests
pass. Python tests remain 106 passed. Live final browser performance validation follows separately.
The native saved Surdana scene improved from about 8–9 fps to about 13 fps with batching; balanced
bands and final browser timing are being measured. These figures describe this host and scene.


The balanced-band native capture rendered the saved Surdana scene correctly and held roughly
13.5–14.3 fps over the final stable two-second intervals, compared with roughly 7–8 fps before
this renderer work. All seven Wasm runtime checks pass. Eight focused Windows regressions pass;
the full Linux suite remains 33 passed / two skipped and the Python suite remains 106 passed.


The first rebuilt browser batch renderer restored the persisted card and loaded Surdana into free
3D gameplay. Stable intervals reached about 10 fps (previously about 3–7 on this host), and Stop
again flushed saves cleanly. A larger pool, capped at eight raster threads and hardware concurrency,
is being checked because this host exposes sixteen logical cores. The game preallocates sixteen
browser workers to cover raster, application, guest and sound threads without blocking worker creation.


The eight-thread native test reached approximately 15.8–16.0 fps in the same stationary saved
Surdana scene, with a correct gameplay capture and normal timed shutdown. Its complete-VRAM
parity test passed natively and in Wasm; eight focused Windows regression tests passed again.


## Final eight-thread deployment acceptance

- Canonical Linux, browser and Windows incremental builds linked and deployed successfully.
- The final browser player loaded the old persisted card, accepted movement, captured mouse look
  and a 20 ms pause/confirm tap, and retained the camera view after motion stopped.
- Stable Surdana gameplay intervals reached about 13–14 fps. Native reached about 16 fps in the
  corresponding stationary scene. These figures do not establish full-speed performance everywhere.
- Audio was nonzero again, with peak 0.01715087890625 in the sampled shared PCM ring.
- The game created Slot 2, including its thumbnail and 1,281,027-byte main save. Stop completed its
  IndexedDB flush; a page restart restored both slots. Load Game showed the new thumbnail and a
  20 ms confirm loaded Slot 2 into free gameplay at the saved position.
- The final browser session reported zero console errors and warnings. Its second Stop again
  reported "Session ended. Saves stored." Screenshots, logs and saves remain private/ignored.
- The repository source audit reports 1,267 approved tracked files and zero prohibited files.

This establishes local desktop ISO-to-build/play, audio, portable input and save/restart/load for
native x86-64 Linux and the tested browser. It does not establish a full playthrough, the physical
controller/browser hardware matrix, GPU parity or full-speed rendering across every level.
