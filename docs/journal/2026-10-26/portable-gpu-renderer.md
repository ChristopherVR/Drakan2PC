# Portable GPU rasterization

The user requires GPU rendering, applicable graphics enhancements and original
speed on Linux/browser, while preserving Windows. Work continues from the
completed camera/field-presentation corrections. The user has asked to leave
Call of Duty running; measurements remain contended.

A new private compute component shares one WGSL ordered tile raster program
between Linux wgpu-native and the browser WebGPU worker. Native public headers do
not expose platform types. Existing Windows D3D11, DXR and vendor feature paths
remain separate. The pinned native release is v29.0.1.1, Linux x64 archive SHA
95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a.
The builder stages its shared runtime beside the Linux executable with an
origin-relative search path; the user does not install a separate wgpu SDK.

GPU invocation owns one native color/depth pixel and executes its tile's draws
in order. Decoded textures and existing HD replacement images remain immutable
through each dispatch. Native RGBA32/RGB24 and 32/24-bit depth are supported;
feedback, packed destinations, non-nearest host rounding, unsupported region
addressing and other unproven operations retain the CPU path. A failed dispatch
cannot write partially updated VRAM. The initial implementation reads/writes
native destination rectangles at batch boundaries. GPU residency/high-resolution
geometry and portable ray tracing are not complete.

A dedicated browser GPU worker handles device promises and map completion.
Blocked Wasm pthreads never block the browser UI. Each request owns input copies
and a separate shared result allocation, so a timed out job cannot retain pointers
into released guest memory. The renderer is initially private opt-in through
DRAKAN_GPU_RASTER=1 (browser ?gpu=1), with no new player controls.

Device discovery exposed WSL Vulkan llvmpipe and WSLg OpenGL D3D12 (AMD Radeon(TM)
Graphics). The latter is reported as unknown adapter type; hardware/unknown rank
ahead of explicit CPU adapters. A private DRAKAN_GPU_BACKEND=gl comparison exists.
Do not describe llvmpipe results as hardware acceleration.

Initial hardware comparisons detected fused arithmetic changing a color byte.
Runtime-zero bitwise rounding barriers prevent contraction across separate f32
steps; CPU-prepared triangle coefficients preserve coverage. Perspective sampling
also exposed permitted reciprocal error on the browser device. An integer-based,
correctly rounded reciprocal now preserves the host sampler's f32 reciprocal.
Sprite coordinate tables retain the CPU's fixed-UV rounding and STQ arithmetic.
Coverage retains the CPU tolerance of -1e-4; depth accumulation uses split integer
products rather than reducing 32-bit depth to one f32.

Validation so far:
- 144 procedural GPU fixtures compare all 4 MiB of VRAM against scalar CPU output
  on WSLg hardware, including perspective textures and full-width depth.
- The same 144 input/output jobs pass through the actual browser GPU worker.
- 48 fixtures additionally pass through the production queued GS adapter, with
  an assertion that GPU work actually completes (192 total jobs in that run).
- Existing HD texture tests pass with 10 verified GPU jobs, including subtexels,
  bilinear sampling/wrapping, palette-alpha aliases, recycling and exclusions.
- Full canonical browser/Linux/Windows deployment and live performance evaluation
  of the new renderer are pending. These procedural checks do not establish
  full-game fidelity, 1:1 performance, enhanced-resolution rendering or RT parity.


Live integration and PC prompts
------------------------------
The first browser deployment (45f8f114...) requested GPU rendering but completed
zero GPU jobs, so its 28.22448 fps / 22.002175 s Surdana receipt is explicitly CPU
fallback, not GPU evidence. Call of Duty had changed to sp26-cod and used 52.375
CPU seconds in 22.01983 wall seconds; it was left running. Strict pacing still fails.
The initial diagnostic suggested draw eligibility, so supported/unsupported runs
were split and the irrelevant RGBA32 DTHE restriction removed (pixel tests pass).

Browser D0713CB8F7D4013776419F1895B4DD2D55D316CAC756AFFCBEE590AA62704B38
exposed the real startup issue: a 30-second timeout on its first eligible job.
A separate nested-worker/Atomics.wait probe reproduced the startup deadlock.
The bridge is being moved to a UI-owned GPU worker: the UI copies/dispatches and
returns, then delivers completed results to the waiting game pthread. GPU waits
never run on the UI. Timeout cancellation is serialized with UI delivery before
releasing any Wasm output/status address.

The browser had explicitly set DRAKAN_UI_PROMPTS=0. That is removed. Portable
controls now inject physical samples through the shared drakan_input.ini binding
module, including menu/game layers and quick slots; camera ownership remains
independent so blur does not recenter pitch. Scheduled GUI calls use the guest
scheduler's postGuestCall dispatcher, matching the Windows glue's blocking-call
boundary. External pointer capture policy has focused shared-module tests.

Linux canonical executable 8e41a905778233b14d66950f6105092909fb4f01bc8b8e73cb71e9736eb28e41
stages libwgpu_native.so and its MIT/Apache license texts automatically. It boots
with hardware adapter D3D12 (AMD Radeon(TM) Graphics), reports keyboard/mouse
prompts and passes every live capture transition with Enter confirm and Escape
pause/resume, then exits normally. The first probe still used the old port's
Enter-as-Start mapping for pause; its corrected probe uses the Windows bindings.
Those input checks ran while another canonical build was active and are not
accepted performance receipts.

D071... live browser validation: canvas backing and viewport both 1251x516 at
DPR 1, rather than a CSS-rescaled 960x720 buffer. Enter confirms, Escape returns,
and the Controls page visibly shows keyboard/mouse caps instead of the console
pad diagram (build/browser-keyboard-controls.png). The native game layout remains;
no browser controls legend was added. Normal stop flushes without a save error.
This does not fix native-resolution geometry or establish full enhancement parity.


UI-owned bridge and first accepted GPU measurement
-------------------------------------------------
Browser 8bddeb646b61f5e03c7777667db368aec6156ebe1f54ab932c4256c55905301d
completes real GPU jobs. A separately compiled Wasm C++ smoke test also passes
through the production bridge, exits 0 and leaves zero pending jobs. The live
Surdana view renders correctly, but its 119.59276-second receipt records only
4.33973 completed frames/uploads per second, game/wall 0.43397, and 100% late
intervals. This fails the existing strict parity test. The default remains CPU.
Call of Duty remains running as requested; no owned compiler/native game ran
during this receipt. Uploads do not prove display scanout.

Profiling identifies roughly thirty synchronous batches per frame. A sampled
330.38-second window includes 44,917 readbacks (~2.74 ms each), 282 GB of buffer
writes, ~20.6 seconds inside writeBuffer and ~122.9 seconds awaiting mapAsync.
Repeated native texture decoding and UI/worker copies add CPU overhead.

The next implementation raises only the opt-in GPU batch limits (16,384 draws,
16M accumulated pixels) and reuses the byte/palette-validated source cache.
Decoded images have immutable shared leases so cache eviction cannot invalidate
an in-flight batch. CPU defaults remain 2,048 draws / 1M pixels. A lease-retention
test evicts more than 512 descriptors and verifies old image contents remain
valid. Oversized texture/bin storage batches fall back before device submission.
Native GPU pixel/HD checks pass, as do separately run CPU tile/async checks. The
CPU tile dispatch-count test intentionally requires the CPU path; running it
with the GPU environment override makes its CPU-only dispatch assertion fail.

GPU worker errors are prevented from bubbling into the browser's global fatal
error handler. Normal game exit now terminates the UI-owned GPU worker too.
Native-resolution geometry, full enhancement parity and RT are still unfinished.


Cached-batch browser 7e648ba17f1d662518dc9d55987134ee3d14fa092324e3b22d7657185a8fdf1f
runs about six frames per second while GPU jobs advance. After ~12,403 jobs its
UI transfer allocation fails with RangeError: Array buffer allocation failed;
the renderer safely falls back and CPU gameplay accelerates. The retained
parity-browser-gpu-cached.json crosses that transition and must not be reported
as GPU performance (its 27.7568 fps / 0.91434 game ratio fails parity anyway).
Idle camera pitch/yaw remain exactly zero through the measured interval.

The next bridge returns the four owned input ArrayBuffers from the GPU worker
into a bounded reusable pool, passing logical lengths separately from capacities.
It also reuses one private shared result allocation. Actual WebGPU tests run all
144 scalar-validated fixtures twice with deliberately oversized/reused buffers,
verify every expected output word, and assert transfer ownership is returned:
288 passed. Stop on the cached deployment leaves pending=0, worker absent and
no save error. GPU source leases are bounded and stale retained texture pages
fall back instead of being reused by VRAM-only cache keys.


Pooled browser fbe9d4350999d7a33c3201247cd1d42e5ddc752af02b072a343e4483d2ad69df
records 536 frames over 76.727945 seconds: 6.98572 fps, game/wall 0.69857 and
100% late intervals. GPU jobs advance 4,633 -> 14,833 throughout, exceeding the
previous allocation-failure point without fallback. The input capacities remain
3,191,296 / 297,728 / 12,582,912 / 2,097,152 bytes and result capacity 2,097,408.
Normal stop releases the worker, input/result pools and pending map with no save
error. GPU remains opt-in; synchronization/readback still dominates and residency
is required before high-resolution geometry can be a practical default.

Windows canonical rebuild 0285dc2ec7841b846e3c4bc62e5a3b56433f9e9e25ae95c2e2bd0d6b1396bd49
passes a fresh private-card Surdana smoke, takes two 3840x2160 screenshots and
exits 0 through the input recipe. Logs confirm D3D11 and keyboard/mouse prompts.
There are host-handoff timeout messages (latches 2, 5305, 5324); this is functional
smoke evidence, not performance parity. It overlaps a browser build. All five
focused Windows input/UI/CPU/HD checks and 118 Python tests pass. Repository audit
now passes with the pinned DLL hash present; no DLL was manually changed here.

High-DPI QA exposed a separate projection/viewport bug: DPR 2 canvas backing
2000x1300 still had a 1000x650 GL viewport, with a cropped menu. The next build
re-establishes the viewport and orthographic projection on the render thread;
canvas allocation alone is insufficient evidence that the whole image renders.


Frontend review follows the requested Claude frontend-design guidance: the game
art and in-game menus are the visual focus; the HTML shell only starts/resumes
playback and reports actionable failures. Existing subject-specific tokens stay
black #000000, dark slate #080c0e, aged gold #c3aa73, parchment #e1d6b8 and ember
#e7ad54, with Palatino-family serif type confined to the startup/failure shell.
No controls legend or second HTML game menu is introduced. The design critique
identified cropped rendering under DPR changes, which must be fixed beneath CSS.

The first viewport-only change still fails direct CDP visual capture: GLFW's
screen dimensions can revert to CSS units while the backing canvas is larger.
A raw CDP screenshot (which avoids screenshot helper metric overrides) proves
only the lower-left quarter was drawn. The next build retains the requested pixel
surface extent independently of the resize-event queue and applies that extent
to both viewport/projection and destination rectangle on the render thread.
Mouse/menu input remains in the existing logical coordinate system. A Wasm test
checks consuming a resize does not discard the persistent surface extent.


The retained physical extent fixes viewport/projection sizing but a second
DPR-2 capture still crops. The SDK's proxied offscreen framebuffer is separately
allocated: GLFW directly changes canvas attributes without resizing its color
and depth attachments. A live probe reallocating both attachments restores the
whole menu immediately (browser-fbo-probe.jpg). The implementation now calls
emscripten_set_canvas_element_size after each window-thread resize; this SDK API
also resizes the offscreen framebuffer. A re-armed resolution media query watches
DPR-only changes even when CSS bounds do not change. Production verification of
that final build follows; do not treat a backing-size check as visual proof.


Final browser 39ea93b1df94470de8a99fb8c227c99e6c8d9e9e9ad5727a1e358b1fa660c335
passes raw CDP capture with the complete menu after the SDK back-buffer fix.
At injected DPI changes 2 -> 1 -> 1.5 -> 2, canvas, depth attachment and viewport
all agree at 2000x1300 -> 1000x650 -> 1500x975 -> 2000x1300. CDP metric emulation
did not itself dispatch resolution/resize notifications consistently, so the
probe explicitly dispatches both change signals; this verifies handling rather
than claiming OS monitor-motion testing. browser-backbuffer-final.jpg is the
complete final capture. Browser screenshots through the convenience helper can
change emulation metrics mid-capture; use raw CDP for this DPI check.

Paced menu navigation reaches Controls with keyboard/mouse caps and no console
pad body (browser-controls-check.png). An earlier rapid transition plus DPI
probe captured a black transition screen; subsequent normal navigation renders
correctly, without another renderer change. Load confirmation sent before the
slot screen was ready was ignored by the transition suppression; confirming the
visible selected slot loads normally.

Live default-renderer camera test aims to pitch 0.03333333507180214, yaw 0;
blur and click recapture retain those exact values. Actual pointer lock is true
before and after recapture. Alt+Enter enters/exits fullscreen with matching
backing/viewport dimensions; Escape pauses and releases pointer lock. These are
functional checks during native builds, not accepted performance evidence.
The loopback server records successful HTTP 200 requests for real HD PNGs;
Linux logs confirm examples 128x128 -> 512x512 and 64x64 -> 256x256. Native-size
framebuffer rendering still limits their visible detail in the browser.


Default browser baseline before deferred GPU work
------------------------------------------------
Browser 39ea93b1... passes the existing strict checker for the recorded Surdana
save-1 view: 717 frames/uploads over 22.01648 s = 32.5665 Hz, game/wall 0.999342,
late frame intervals 1.534%, late uploads 2.789%, no invalid/error delta. No owned
compiler or native test game runs during that window; Call of Duty remains open.
This is one scene, not full-game parity and not proof of display scanout. Pitch
is 0.0314344 by the end after pause/resume versus the prior capture-test aim
0.0333333; no large camera jump occurs. Normal exit preserves browser saves.
Linux 133f6f93... passes all capture/fast-click/focus checks and exits 0; Windows
c71fa51a... also passes a fresh private-card D3D11 Surdana smoke and exits 0.

Deferred destination batching (new work, live performance pending)
---------------------------------------------------------------
The GPU path now retains immutable source images and ordered draw state for a
single destination across disjoint guest texture uploads. CPU-visible Flush,
readback, presentation, aliased transfers, CLUT loads, target feedback and CPU
fragment fallbacks resolve deferred work in order. Batches are bounded by 65,536
draws, 64M accumulated pixels and 96 MiB of retained source images. Failed compute
replays the owned images through the scalar renderer even after upload slots
have recycled, then disables further GPU attempts for that backend. CPU defaults
remain unchanged.

All 144 scalar-validated fixtures additionally test two appended groups, recycled
VRAM texture sources, successful compute and failed-compute CPU replay, comparing
all 4 MiB. Production integration tests verify 18 texture-upload scenarios merge
into one GPU job; feedback into a new target, packed CPU fallback, local-to-host
readback and CLUT-from-target reads preserve ordering and match scalar VRAM.
Native GPU/HD tests and CPU tile/async tests pass. Five Windows input/UI/host-pad/
CPU/HD checks pass after the private backend layout change. Canonical browser
build and live measurement of this optimization are pending.


Deferred browser 11e107bfc5ab99ae3af80097e596afe79ac59234ce082d8fbe4868dfa12b4b65
records 860 frames/uploads over 89.83225 s = 9.5734 Hz, game/wall 0.941867,
100% late intervals, no error delta. GPU jobs advance 2,538 -> 6,840 (~5/frame).
A main batch has 42,563 draws and ~75.9 MB of texture input. This still fails.
Diagnostic JS timing over 26.04 seconds attributes ~1.37 s to writeBuffer,
~1.53 s to UI copies and ~8.37 s to mapAsync; other preparation/fallback costs
remain. New C++ phase counters measure pack/read/compute/write separately.

The prolonged run with injected JS profiling later stops uploading (counter
2,602) while EE frames continue. Stop requests are not processed. Simulated GPU
worker failure correctly replays 42,563 immutable draws and CPU gameplay resumes,
but the window remains stuck. A debugger snapshot places its pthread in a sync
proxy for emscripten_get_gamepad_status, not in GPU work. Navigator reports eight
null gamepad slots. No frame-latch fix is assumed without further evidence.
The test explicitly flushes IDBFS successfully, then reloads the owned tab because
normal stop did not complete. Reproduction without JS profiling is pending.


C++ phase timing on the next browser build 0ff1d1dd... attributes about 280 ms
per two wall seconds to packing, 46 ms to VRAM reads, 1,200 ms to compute bridge
(including uploads/IPC/readback) and 20 ms to VRAM writes. A new run without JS
profiling maintains uploads to 2,353 of 2,382 completed flips and stops normally.
That is not yet a longer-than-failure-threshold soak and does not prove the prior
proxy hang resolved. No speculative frame-latch or gamepad workaround was applied.

Packing now counts tile references and unique immutable textures first, reserves
the complete texture allocation once, and fills flat ordered bin lists directly.
This avoids repeated growth/copy of a ~76 MB texture vector and thousands of small
per-tile allocations. Validation and staging map promises run concurrently in the
browser worker, eliminating a serial validation round trip. All native pixel,
recycling, failure-replay and boundary tests pass; 288 actual browser pooled
fixture jobs also pass. New canonical browser live timing is pending.


Packed browser 5b81378518ee1356cb18e350d7555ce81ef303f71e70b47a390cf8722f1224a3
records 352 frames/uploads over 22.012605 s = 15.99084 Hz, game/wall 1.001517,
100% late intervals and no error delta. It fails frame-rate/pacing parity even
though game speed is within 1%. The view is a later elapsed Surdana window with
fewer draws than the earlier capture, so do not attribute the entire fps change
to packing. Packing is ~7 ms/frame versus ~14 ms/frame in the earlier diagnostic.
GPU adapter metadata confirms NVIDIA Blackwell, fallback=false; a read-only GPU
engine sample shows Call of Duty using 40% of its 3D engine and it remains open.

Without JavaScript profiling interposition the run passes 5,235 unique uploads
and 5,248 completed flips, then stops normally with saved=true, pending=0 and no
GPU worker. This exceeds the earlier stalled run's 2,602-upload point. The exact
cause of the earlier proxied gamepad wait remains unproven; no speculative SDK
or frame-latch change was made. New private fence counters distinguish public
boundaries, source/palette feedback, destination/capacity changes, CPU fallback,
CPU reads/writes and transfers before selecting the next residency change.

The live fence diagnostic attributes roughly 40% of submissions to destination
changes and 60% to CPU fallbacks (about five submissions per frame); feedback,
CLUT, transfer and public-boundary counters remain zero in that Surdana window.
The production run stops normally after more than 26,000 completed GPU jobs.
CPU-fallback diagnostics now report the relevant state and queue eligibility.

Deferred compatible targets can now retain draws that enable or disable depth
without changing color/depth storage. Initial depth loading, hazard ranges and
final writeback use the union of all draws' depth usage. A late depth enable
cannot bypass color/depth alias checks. Forty-eight fixtures begin without
depth, then enable it; their integrated path must submit exactly one GPU job.
All 144 full-VRAM comparisons, immutable-source recycling, failed-device replay
and HD tests pass (`build/gpu-depth-unit-tests.log`). Browser timing is pending.

The canonical depth build deploys JS A62E32C8939092B2E3A273986695435C485EE7584270983A42ED7DC4001919F4.
Its live Surdana counters reduce destination fences to about 21 per two seconds,
while CPU fences remain about 63. Diagnostics identify native ST point sampling
and AA1-marked batches. Native ST point sampling now explicitly truncates toward
zero; HD replacements retain floor after scaling. Immutable native-source CPU
replay carries this distinction through scalar and tile samplers. Bounded region
clamp/repeat is accepted only when every source coordinate remains in the decoded
image. Outside-region addressing still forces a resolve.

The expanded suite adds 48 negative-ST, point-filtered, region-clamp/repeat cases.
All 192 full-VRAM GPU comparisons, late depth enable, recycled immutable sources,
failed-device replay, HD tests, four-rounding-mode CPU tile comparisons and async
ordering tests pass (`build/gpu-point-unit-tests.log`). Browser rebuild is pending.

The point/region browser build deploys JS 17B9281813FC18D0E9709DA342653E4D79C73CA17B42265DDE84F8A0406FBF76.
Live diagnostics show three jobs per frame: one target change, one CPU fallback,
one public boundary. The view is not a matched performance receipt. AA1-marked
batches still use the CPU; no unimplemented antialiasing behavior is assumed.

Immutable decoded/HD images now retain offsets in a per-compute-device 128 MiB
texture atlas. Sparse write ranges transfer only newly resident texels and
transient sprite-coordinate data. Weak ownership identities reject stale pointer
reuse; unowned sources upload every time. Capacity exhaustion resets the atlas and
uploads every source needed by that job. Failed compute clears residency before
the existing immutable CPU replay. Framebuffer readback remains per dispatch.

All 192 full-VRAM fixtures pass, including repeated owned-image jobs that must
omit the texture payload on the second dispatch. Thirty-five large HD-image jobs
cross the atlas capacity and reload the first image after eviction, checking
output pixels. The legacy native compute smoke and HD suite pass. The actual
browser worker passes 288 sparse-upload/reuse jobs with relocated offsets, plus
288 legacy pooled-buffer jobs. Logs: `build/gpu-resident-eviction-tests.log`,
`build/gpu-resident-unit-tests.log`, `build/gpu-browser-resident-results.json`.
The first unit script's final smoke command used the wrong executable path; the
subsequent eviction script runs the discovered `bin/gsr_test_portable_gpu` and
passes. Five Windows input/host-pad/CPU/HD/UI checks pass after the sampler change.
Canonical game rebuilds and live atlas performance remain pending.

Review found that strong atlas leases could prevent the separate 128 MiB HD
CPU pack cache from evicting images and loading new replacements. Residency now
keeps weak ownership identities: a hit requires a live matching image, expired
or recycled pointer values upload again, and GPU storage never pins CPU images.
The procedural reuse check also asserts unchanged image ownership counts.

Weak-residency pixel/eviction/HD tests pass. Additional HD cases verify bounded
region clamp/repeat bilinear taps and negative-ST point sampling, both scalar
and actual GPU (`build/gpu-hd-region-tests.log`, 12 GPU jobs).

Resident browser Wasm 8438e3264d2504b296b749a031431f7ec39873cba8083bda2625baa370aa2b66
records 393 frames/uploads over 22.01309 seconds = 17.8530 Hz, game/wall
1.000632, 100% late intervals, no error delta. It still fails frame/pacing parity.
No owned compiler or native game ran in this window; Call of Duty stayed open.
Main scene jobs report zero texture payload; aggregate payload is ~0.4 MiB per
two seconds versus ~1.7 GiB in the earlier diagnostic. Packing drops to ~3 ms
per frame in this view. Stop exits normally with pending=0 and no GPU worker.

The next iteration uses 4x4 triangle bins while retaining 8x8 workgroups. Legacy
wire jobs default to their original 8x8 bins. AA1-marked supported primitives
now stay on the GPU because the CPU reference ignores that flag; fixtures set
it explicitly and must still match all VRAM. This does not implement GS
antialiasing or assert improved visual fidelity for AA1. Validation is pending.

Fine-bin validation passes all 192 complete-VRAM, immutable replay and atlas
capacity fixtures, the expanded HD cases (12 GPU jobs), and the legacy native
compute smoke (`build/gpu-fine-unit-tests.log`). Browser WebGPU passes 768 pooled
and 768 sparse/resident jobs from 384 recorded procedural inputs; the result is
saved in `build/gpu-browser-fine-results.json`. Windows also passes the expanded
HD region/negative-ST test (`build/gpu-hd-region-windows-tests.log`). Live game
measurement and final native deployment are pending.

Fine-bin browser Wasm b9df561588558ae6e25b1d7f44cea472cff0982a70f9d4fa85157a3adb2ff224
records 375 frames/uploads over 22.00454 seconds = 17.0419 Hz, game/wall
1.000975, all intervals late. Main GPU time remains similar while bin traffic
and packing increase. The default returns to 8x8 bins; smaller bins do not
demonstrate a benefit here. AA1-marked batches now produce a source-feedback
fence instead of CPU fallback, keeping three jobs per frame. Stop is normal.

The next shader rejects destination-alpha/depth failures before texture
filtering, fog and color interpolation, and skips depth interpolation when
neither testing nor writing depth. Sources are immutable and guest texture-cache
reconciliation is performed separately, so skipping GPU texture reads has no
guest-visible side effects. All 192 full-VRAM fixtures, atlas capacity tests,
HD tests and native legacy smoke pass (`build/gpu-early-unit-tests.log`). Actual
browser WebGPU passes 1,824 legacy/fine-bin/sparse jobs with this shader
(`build/gpu-browser-early-results.json`). Live game build/measurement is pending.

Early-rejection browser Wasm 63db0482e9d6b8230bb736ff43e465807e54f0fa69374b49895dea10d2d6d55c
records 376 frames/uploads in 22.006905 seconds = 17.0855 Hz, game/wall 0.998141,
all intervals late, no error delta. GPU waits remain three per frame and dominate
the compute bridge. It does not establish a measurable frame-rate gain. Stop
completes normally.

Depth-storage compatibility now ignores ZBP/PSM values on draws that neither
test nor write depth. A later real depth user selects the depth allocation for
the complete run; hazard ranges, initial loads and writeback use that allocation.
Actual depth allocation changes still reject append and force a fence. Forty-eight
fixtures start with unused Z16 storage at base zero, then use Z32/Z24 at base 320;
their production path must produce one GPU job and match every byte of scalar
VRAM, including failed-device replay. The 192 pixel cases and expanded HD tests
pass (`build/gpu-depth-storage-tests.log`); final rejection regression and browser
build are pending.

The real-depth rejection regression passes (`build/gpu-depth-storage-final-tests.log`).
Browser Wasm dc424c6a330d033c45e1803c6a79772056eec2223032ffa265bafcad61f51454
records 431 frames/uploads over 22.01499 seconds = 19.5776 Hz, game/wall
0.998093, all intervals late, no error delta. It still submits three GPU jobs per
frame and fails frame/pacing parity. Contention and changing elapsed gameplay
prevent attributing the entire difference from earlier windows to this change.
The default CPU path remains unchanged and GPU remains opt-in.

The current GPU browser Controls screen is captured in
`build/browser-pc-controls-current.jpg` using raw CDP capture. It visibly shows
keyboard/mouse key caps without the original console-pad body. The game still
renders its interface and scene at native resolution, so this does not establish
Windows HD-renderer clarity. Normal stop again leaves pending=0 and no GPU worker.
Canonical Linux deployment succeeds; Linux GPU/CPU functional capture checks and
the refreshed Windows build are running. These concurrent checks are functional,
not clean performance measurements.

Final canonical deployments:
- Linux FE005B18D186ACAEAE40C02792840A3D1F333C9E1CD50810FB3E2F6F84C2A275
  (`build/gpu-resident-final-linux-build.log`). Actual GPU mode completes more
  than 5,888 jobs on WSLg AMD GL, renders gameplay, passes all eight cursor/
  focus/pause checks and exits normally (`build/linux-gpu-resident-capture-result.log`).
- Windows 8914D883E7270659ED55CBF2095713D67407E52CBDD44F0F9E49447D60CD4AC9
  (`build/gpu-resident-final-windows-build.log`). D3D11/HD gameplay renders at
  3840x2160 and exits normally (`build/gpu-resident-windows-smoke.log`,
  `build/harness/gpu-resident-final-20261026/shot1_20.png`).

Linux GPU mode still preserves CPU fallback, including inherited non-nearest
native rounding modes; completed GPU jobs do not mean every scene draw runs on
the GPU. The browser accepts its nearest-rounding path, but its latest recorded
19.58 fps window still fails parity. Native high-resolution geometry, portable
RT, framebuffer residency and matched full-game performance remain outstanding.
No shader approximation or relaxed performance threshold was introduced.

Formatting checks pass for the new GPU component and pixel/HD tests. The final
tracked-file guard passes (1,267 approved, zero prohibited) and git diff has no
whitespace errors. Browser trace artifacts were moved into build after the last
browser call. The current browser Controls capture confirms PC key caps; native
resolution still limits clarity.

The final Linux default-CPU capture also passes all eight transitions and exits
normally (`build/linux-cpu-resident-capture-result.log`). Its log confirms live
HD replacements including 256x256 -> 1024x1024 images, with keyboard/mouse prompt
generation active. Both native capture runs overlap other functional checks;
neither is a performance receipt. The owned GPU-fixture HTTP server was stopped;
the deployed browser game server remains available for the local player.
