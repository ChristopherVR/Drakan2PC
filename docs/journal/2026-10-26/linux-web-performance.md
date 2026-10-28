# Linux/browser full-speed performance work

The requested target is original game speed, with rendering correctness and Windows builds kept
working. The runtime's original world-cadence option is 30 Hz; a frame counter alone does not
establish world-clock or input/audio fidelity. Guest timing and game data remain unchanged.

## Baseline and first changes

The deployed eight-thread CPU renderer reached roughly 15–16 completed flips/s in the saved
Surdana scene. A ten-second native CPU-clock profile during steady gameplay attributed about
41.83% to DrawTriangle (including inlined sampling/interpolation), 12.23% to its sample-point
helper, 10.94% to WritePixel, 6.23% to GSMem::ReadTexture and 9% to libc.

Immutable texture batches can read their proven-disjoint source VRAM directly, then retain the
same last 8 KiB page at scope exit. Stale retained pages and feedback remain serial. The triangle
coverage helper computes four pixels with the original float operation association. Randomized
checks compare every weight bit and coverage decision across four native rounding modes and
Wasm nearest mode. Complete-VRAM serial/tiled comparisons still pass.

These first changes reached about 17 fps in the same native scene, insufficient for full speed.
Decoded-texel memoization within immutable scopes is the next measured candidate. Its key covers
texture base/stride/format, palette format/bank/mode and TEXA; palette loads and scope transitions
invalidate memoized values. The final sample is replayed before retaining the hardware cache page.
The production-backend comparison and explicit cache invalidation/retained-page checks cover it.

All private captures, profiler files and generated game output stay under ignored build folders.


## Early depth and work distribution

Decoded-texel reuse alone did not materially raise native throughput. Early depth rejection skips
color/texture interpolation for hidden triangle pixels in immutable scopes; a deferred final sample
preserves the original retained texture page. Tests cover all-rejected triangles in both windings,
a subsequent source overwrite and a draw that observes the retained bytes. Native comparisons pass
across four rounding modes; Wasm comparisons also pass. The native scene improved to about 18 fps,
while disabling decoded reuse measured roughly 17–18 fps.

The next candidate splits work into rectangles across both screen axes. Horizontal-only bands
concentrate foreground shading in too few tasks. Rectangle workers retain original primitive order
and record the actual last sampled pixel; cache reconciliation compares primitive, Y and X rather
than tile index. Coherent immutable tile caches initialize with invalid tags instead of copying
8 KiB for every task. Full-VRAM comparisons and retained-page tests cover the new partition.


## Host allocation correction

WSL reports four logical CPUs, arranged as two cores / two threads per core. The host has a
Ryzen 7 9800X3D (eight cores / sixteen logical CPUs); its browser reports sixteen logical CPUs.
The user's .wslconfig explicitly limits WSL to processors=4 and memory=16GB. Earlier native
"eight-thread" benchmark labels described the requested environment value; the worker pool also
caps itself at hardware_concurrency, so those Linux runs actually used at most four raster threads.
Native Windows unit tests exercise eight workers. Native timings are therefore measurements of
the restricted WSL allocation, rather than the full host's native Linux capacity.

No WSL configuration change or restart has been made. An optional request to test with eight CPUs
is pending; browser work proceeds independently on the host allocation. Initial rectangle timing
on the four-CPU WSL instance was roughly 17 fps. Conservative row bounds are being tested next;
the exact coverage decision remains unchanged at every retained pixel.


## Browser quad/palette and scheduling candidates

The first rectangular/row-bound browser build reached approximately 16 fps; combining immutable
four-texel reads, cached palette decoding and forced inlining reached approximately 17–18 fps.
The measured global engine delta sum tracked wall time in the audit. This is not sufficient evidence
of original-speed presentation: the 30 Hz frame target remains unmet.

Full per-coordinate texel memoization did not justify its table/lookup overhead. It was replaced
with a 256-entry per-palette color cache. Quad reads select the texture storage format once and
normalize each axis twice, rather than making four separately dispatched point calls. Serial and
stale/feedback paths retain their original read order and cache semantics. Tests pass on Windows,
Linux and Wasm for all production-backend texture formats and retained-page cases.

The next candidate adds explicit four-lane color/STQ/UV/fog interpolation and exact blend identities
that skip destination-color reads when the blend reduces to the source. Tests compare interpolation
bits and packed colors against scalar operations across rounding modes, and verify blend identities
against the full integer formula including alpha edge values.

Independent CSM1 palette reloads now retain per-draw color/cache snapshots in the pending batch.
Overlapping palette sources and CSM2 reloads still synchronize. Serial fallback replays snapshot
cache observations; parallel reconciliation orders real texture samples against palette-load events.
Tests cover a final palette reload, a later textured command with zero covered pixels, source
modification followed by a retained-page read, and palette reads from a pending destination. Windows
comparisons pass; Linux/Wasm and live timing follow before accepting the throughput result.


## Completed frames and ordered rendering

Software presentation now follows completed original graphics-worker flips, with a stale-clock
fallback for movies and other paths that do not publish those flips. A bounded, ordered CPU command
queue overlaps VIF production with rendering on non-Windows game builds. Readbacks, transfers,
FINISH and presentation retain completion fences; queued uploads own their source bytes. Tests
cover rounding capture, buffer lifetime, backpressure, reset/rebind, exceptions and shutdown.
Windows keeps its existing default backend selection. These changes reached approximately 19 fps
in the saved browser scene, still below the 30 Hz original-speed target.

Browser-wide V8 CPU sampling resolves Wasm symbols through the private build's symbol map. The
renderer thread remains busy and the producer spends most of its time blocked on the full queue.
Raster workers have substantial idle time. This points to renderer throughput and serial submission
costs rather than additional producer-wait removal. Sampling includes sleeping futex stacks, so
unfiltered sample totals must not be presented as CPU utilization.

The next candidate specializes the common RGBA32/Z32 textured MODULATE, source-alpha blend,
alpha-GREATER/KEEP and depth-GEQUAL fragment state. Integer SIMD preserves the existing per-channel
saturation, fog shifts, blend rounding and source alpha. Targeted comparisons caught an equality
boundary error in the initial alpha predicate; it was corrected before deployment. Randomized
math and complete-VRAM comparisons pass on Windows, including all native rounding modes.

PSMT8 uploads now process row/page spans with one row-table selection instead of a full swizzle
address and indirect write call per byte. Tests compare against the original point writer across
unaligned bases, odd/zero strides, page/VRAM wrapping, partial chunks and excess input bytes, and
check the final transfer cursor. A redundant successful tiling check is also removed. WebAssembly
checks and live measurement must pass before treating these candidates as performance improvements.


## Common fragments, row uploads, hazard checks and readback

The specialized fragment/P8 row-upload build sustained approximately 25 completed game flips per
second after warm-up in the same browser save. The following candidate added distinct-source hazard
checks and row/page-based 32-bit readback. A foreground run with per-frame audit logging measured
29.86 completed flips/s over 8.51 seconds: median 33.52 ms, p95 36.835 ms. This is near the 30 Hz
target, but does not establish headroom or whole-game parity. The textarea retains only 30,000
characters; audit level 2 includes V/U/F lines, so a snapshot contains fewer seconds than the
requested observation interval. Later receipts must collect the timestamps continuously.

Readback tests compare unaligned output and wrapped/randomized source rows against point reads.
Another candidate removes the snapshot copy inside the exclusively owned ordered renderer and
collapses identical CRT sources, where blending cannot change RGB and output alpha is normalized.
Distinct-address sources with identical bytes exercise the full blend reference; comparisons cover
four color formats, fixed/source alpha factors, interlace fields and nonzero display origins.
Windows and Wasm tests pass. Live validation of that presentation candidate is still pending.


## Presentation receipt and broader scene check

Browser Wasm SHA-256 0EB1F28CB83678F30CDD4B77AA9C5E7FBD2451A209A25989BED6005944EE31F4
measured 30.0198 completed flips/s over 19.9868 s (600 intervals), median 33.29 ms,
p95 36.086 ms, with per-frame audit logging. Walking forward for three seconds and checking the
nearby grass-heavy view measured 25.8208 flips/s over 19.9839 s (516 intervals), median 38.801 ms,
p95 41.209 ms. Movement and pause worked; this second result disproves a claim of consistent
original-speed performance based solely on the first stationary view. Private receipts and images
are under build/. The JS wrapper hash stays unchanged across these builds; use the Wasm hash to
identify the executable being measured.

The full canonical Windows ISO build linked/deployed and its executable booted through GSR,
then exited normally under PS2X_MAX_SECONDS (exit 0). Linux's complete 37-test run has no failures,
with hle_test and fmv_vdb skipped for missing test-disc fixtures.

The subsequent candidate pre-packs each immutable RGBA32/bank-zero palette once for shared tiled
lookups, retaining the general decoder for other palette formats/banks. The exclusively owned
ordered backend uses an unlocked internal submission entry; public calls remain locked. Tile state
is cache-line aligned. Windows and Wasm renderer comparisons pass. A bounded diagnostic
PS2X_CPU_RASTER_BATCH_AREA permits measuring larger compatible batches without changing pixels or
removing transfer/presentation/FINISH fences; the default remains 1 MiB of summed bounds area
until a live comparison supports changing it. Windows comparisons also pass at 8 MiB.


## Packed palettes, larger-batch comparison and native gameplay

With packed bank-zero RGBA32 palettes and owned/unlocked submission, the browser saved view
measured roughly 31.58 flips/s. A 20-second grass-view receipt at the default 1 MiB batch area
measured 27.5701 flips/s (551 intervals over 19.9854 s), median 36.179 ms, p95 38.645 ms.
At 8 MiB it measured 27.3043 flips/s (546 intervals over 19.9969 s), median 36.549 ms,
p95 38.955 ms. There is no demonstrated gain; keep the 1 MiB default. Do not benchmark with
PS2X_CPU_RASTER_STATS enabled: its extra synchronous browser logging materially lowers throughput.

The canonical Linux build (including the common fragment path for serial draws) ran from
its deployed play folder under Xvfb. On the unchanged four-CPU WSL allocation, the last 20 seconds
of the saved view measured 36.2254 flips/s (724 intervals over 19.9860 s), median 27.19 ms,
p95 33.153 ms. A separate run walked forward for three seconds: the final window measured
35.3003 flips/s (705 intervals over 19.9715 s), median 28.017 ms, p95 31.202 ms. Captures confirm
3D gameplay in both; both timed runs exited normally (0). This clears 30 Hz in the measured native
scenes without changing WSL allocation, game timing or pixel decisions.

The next browser candidate permits tiling when texture pages overlap an unused depth buffer;
color overlap remains a fence. It also specializes the common alpha-GREATER blend with depth
ALWAYS and masked/unmasked depth writes. Tests explicitly compare unused-depth aliasing and
framebuffer feedback against the general one-worker path. A missed helper parameter initially
broke compilation; it was corrected, and the Windows binaries were rebuilt successfully before
rerunning the new tests. Linux/Wasm rebuilds and the next live browser result are pending.

Native benchmark executable SHA-256: 8acdb147267c49f2648336ba1e30f50e48edd2be728dd38a5cd08a91eb5e1f0e.
The packed-palette browser comparison used Wasm SHA-256 ed1464c41afba405805213e7bf2f906a66d46ec690d9187ad65f61a679fd1514.
The latest expanded-fragment and unused-depth tests subsequently passed in the complete Linux suite
and both Wasm renderer checks; the browser game link is the remaining check for this candidate.


## Four-pixel perspective/depth candidate

Expanding the common serial fragment path and ignoring unused depth overlaps did not materially
improve this browser view: approximately 31.29 flips/s in the original saved view and 27.3157 flips/s
in the nearby grass view (546 intervals over 19.9885 s, median 36.555 ms, p95 39.014 ms).
The changes retain scalar parity and remove an unnecessary tiling restriction, but must not be
credited with a demonstrated gain in this scene.

The following candidate calculates perspective UV/fog across four adjacent pixels and Z in pairs
of doubles. Blocks with fewer than three covered pixels stay on the prior path. Depth comparisons
run first; packed perspective division is used only when at least three pixels survive, preserving
the benefit of early rejection. Pre-reading depth is safe only inside the proven disjoint tiled
scope. Failed pixels still record their final deferred texture-cache observation in original order.
The original per-pixel sampling entry delegates to a shared coordinate-based sampler; palette,
wrap, filter and retained-page logic remains common.

Randomized projected UV/fog bits and integer depth values match scalar arithmetic across all four
native rounding modes, including tiny/negative Q. The complete-VRAM tests pass on Windows, Linux
and Wasm. PS2X_CPU_RASTER_PROJECTED=0 permits a private same-executable comparison. Live browser
throughput is pending; native timing above predates this candidate.


## Rejected packed projection and low-overhead measurements

The projected/depth-quad candidate measured about 31.30 flips/s in the original view and
27.0013 flips/s in grass (540 intervals over 19.9991 s, median 37.011 ms, p95 39.451 ms).
It provides no demonstrated benefit here and adds complexity. The candidate helpers, tests,
coordinate-sampler split and projected-mode switch were removed; prior rendering remains.

A read-only drakan_web_completed_frames export now reads the tracked completed-flip revision.
It performs no guest-memory write, logging or pacing action. Counter/reset behavior is covered by
native frame-clock and Wasm input/export checks. This allows measuring the default runtime with
per-frame audit logging disabled. The candidate's complete Linux suite, Windows four-test renderer/
clock set, and five relevant Wasm checks pass. Default browser measurement is pending.


## Default-runtime counter and uniform-state shader

With audit logging disabled, the read-only counter measured 381 completed flips over 12.0050 s
in the saved view (31.7367/s), then 555 flips over 20.0935 s in grass (27.6209/s). Logging is not
the main remaining limitation. The default-runtime receipt is under build/; do not claim that
removing audit logging closes the browser gap.

The next candidate selects a common opaque PSMT8/RGBA32/Z32 state once per tiled triangle. The
shared draw/sampling implementations are instantiated with fixed feature choices, allowing the
compiler to remove uniform branches and the general texture-format switch from that pixel loop.
The original state remains the stable cache-observation source; no temporary state copy escapes.
Other modes and the one-thread diagnostic path retain the general implementation. Narrow rows
also skip construction of the conservative clip helper whose result would be the unchanged bounds.

A dedicated fixture forces the specialized state while varying texture alpha and alpha reference;
complete-VRAM comparisons pass on Windows across all four native rounding modes. Game-build GS
floating-point options now match the parity tests' numerical contract (source-scoped strict/rounding
flags), instead of inheriting Windows' general fast-math release option. Linux/Wasm and live browser
validation of this uniform-state candidate are pending.


## Uniform-state results and display blend candidate

The opaque specialization measured 405 flips over 12.0006 s in the saved view (33.7483/s),
then 588 over 20.0920 s in grass (29.2654/s). The clamped alpha/depth-masked specialization
measured 406 over 12.0001 s (33.8331/s), then 590 over 20.0874 s (29.3716/s). The opaque path
has a demonstrated gain; the second state adds little in this view. Its tests cover both windings,
texture-alpha modes and alpha references. Its unused Z interpolation is omitted only for the
validated depth-ALWAYS/ZMASK state. Other modes remain on the general path.

The next candidate vectorizes CRT composition using the exact signed, toward-zero divide by 255.
For |product| <= 65025, divide the magnitude using (n + 1 + ((n + 1) >> 8)) >> 8 and restore its sign.
Output alpha was already normalized to 255. Read the second CRT without retaining its unused alpha,
fill background alpha as 255, and write composed alpha as 255, avoiding the separate normalization
traversal. This does not write guest VRAM or change the source alpha used for blend factors.

Randomized packed RGB comparisons pass across four native rounding modes. An end-to-end fixture
compares different-size CRTs and distinct pixel values against the scalar formula, including fixed/
source alpha, background selection, AMOD and untouched regions outside the smaller CRT. The initial
fixture used dimensions below the existing 64-pixel display-validity threshold; it was adjusted to
valid unequal display sizes before comparison. Existing four-format/interlace tests also pass.
Linux/Wasm and live speed of this composition candidate are pending.


## Final browser acceptance and native rebuild

Display composition raised the browser saved view to 418 flips over 12.0034 s (34.8234/s),
and grass to 608 over 20.1019 s (30.2459/s). After a captured camera turn, a third view measured
870 over 20.0027 s (43.4942/s). Repeated mouse events changed and retained the camera angle;
movement and pause worked. A one-second audio check advanced both producer/consumer by roughly
48,000 frames with no added ring drops and peak 0.10565.

Saved to empty slot 3, whose main file is 1,278,979 bytes, then stopped successfully, restarted
and loaded it. The thumbnail and restored position were visible. Camera orientation resets on
load through the original behavior. The restored grass view measured 2,739 completed flips over
88.9919 seconds (30.7781/s). Private counter receipts and screenshots remain under build/.
Validated browser Wasm SHA-256: 7fc3d9319ec10d6db316d71fe368f1411cfc6493b45dbf11c97a09781304bc70.
Browser console errors/warnings were zero. New Game's original intro advanced through distinct
movie images; its log retained the Full/25 fps policy. Audio advanced at the expected rate, peak
0.47845, with ring-drop count zero. Stop/IDBFS flush completed normally afterward.

The full final Linux build ran again from dist/DrakanPC-linux, walking forward for three seconds.
The last window contained 749 intervals over 19.9795 s: 37.4885 flips/s, median 26.463 ms,
p95 29.132 ms. It exited normally (0). Native SHA-256:
c88df5d152993a9501f52425a1144b06c54a0b6d2b533adb4aff7115c6fc5afd.
WSL CPU/memory settings were not changed. The final complete Wasm suite (all nine checks), ten
focused Windows regressions, 106 Python checks and the repository guard passed. The final Windows
ISO build/launch check is still running at this entry.

Correction to earlier batch-limit shorthand: area thresholds count summed bounding-box **pixels**,
not bytes. "1 MiB/8 MiB" above meant 1,048,576 / 8,388,608 pixels; upload limits are separately
measured in bytes. The larger area limit showed no demonstrated throughput gain and is not default.

The 30 Hz target is met in these acceptance views. No full-game completion, every-region minimum,
PS2 hardware golden-image comparison or cross-browser/hardware performance guarantee is claimed.


Final Windows closure: the canonical incremental ISO build linked/deployed successfully. Its staged
GSR executable booted and exited normally (0) after the timed launch check; the renderer reported
8,130 host frames and 21,478 draws before normal stop. This verifies build/launch preservation,
not a new Windows whole-game performance claim. Final source formatting and whitespace checks,
tracked-file audit and local test-server cleanup complete the session. No game data was committed.


### Stricter original-speed measurement and sixteen-worker experiment

The follow-up treats average throughput as insufficient evidence for 1:1 play.
Added opt-in DRAKAN_FRAME_METRICS: completed-frame interval buckets, original
engine timestep sum, update count, invalid observations, unique software texture
uploads, copy failures and upload-interval buckets. It observes the existing
engine boundary and never changes RAM or pacing. Browser exports are read-only;
native snapshots are one line/second. Collection is disabled by default.
The receipt checker scripts/perf/software_frame_report.py rejects missing,
short or inconsistent measurements; it gates frame/upload throughput, deadline
misses and game-time/wall-time ratio independently. Synthetic regression cases
include fast rendering with stalled presentation and an incorrect simulation
clock. Physical scanout and full-game coverage remain separate requirements.

Raised the supported raster-worker ceiling from eight to sixteen, preserving the
existing eight-worker tile grid to avoid doubling primitive setup/culling work.
The preallocated browser pthread pool is 24, including the non-raster workers.
Windows's default renderer and native default raster cap stay unchanged; browser
hosts reporting sixteen logical CPUs now select sixteen raster workers.

Same Wasm 6512decbf9ef4d117ffef237f8c913591c64c3d2e6d69ca847d9e9201aede4d7,
same loaded Slot 3 grass scene, no compiler running, audit logging disabled:
- 8 workers, 29.88622 s: 30.01383 completed/uploaded fps; game/wall ratio
  1.00019343; 51.6165% of rendered intervals exceeded 33.333334 ms. FAIL on pacing.
- 16 workers, 30.00746 s: 37.75728 completed fps (25.8% faster), 31.42552 unique
  uploads/s; game/wall 1.00001803; no rendered interval exceeded 33.333334 ms.
- 16 workers, five seconds walking then stationary, 30.007905 s: 44.75487
  completed fps, 31.89160 uploads/s; game/wall 0.99973663; no late render interval.

These initial receipts predate upload-interval instrumentation. They establish
render headroom and correct observed game-clock speed, but do not by themselves
prove presentation cadence. The stricter checker now requires upload histograms;
final rebuilt-binary tests follow. All saves flushed normally after each run.
Focused Windows and Linux clock/tile/async checks pass; the Windows tile fixture
also passes full 4 MiB comparisons with sixteen workers. Browser's four selected
SDK checks and 110 Python checks passed before adding the upload-interval gate.


Upload-interval gate exposed another issue on the default sixteen-worker rebuild
(Wasm a770738b5bfb13f86e9f7387226a92bb0fc5dd9960ad41ffcaa7e2dd7a78fd42):
30.004505 s, 36.66116 completed fps, 31.79523 unique uploads/s, game/wall
1.00031649, no late render intervals, but 20.2306% late upload intervals. This
FAILS the full receipt gate despite clearing average throughput. Testing a
browser-only 120 Hz presentation poll limit next (diagnostic environment
DRAKAN_SOFTWARE_PRESENT_FPS=30..240); Windows/Linux's 60 Hz setting is unchanged.


The 120 Hz browser host poll limit fixes the observed delivery gap without
changing the guest clock. Wasm 04824710f2163f72573ad844f8ccfe976cb7eedf1110d9e91a18836f46ccf22a:
- Grass, 30.01536 s: 36.58127 completed and unique uploaded fps, game/wall
  1.00005467, 0.2732% late render intervals and 0.6375% late uploads. PASS.
- Walking five seconds then stationary, 30.00578 s: 43.62493 completed /
  41.75862 uploaded fps, game/wall 1.00020735, 0% late render intervals and
  3.4318% late uploads. PASS.
- Captured mouse-look reverse view, 30.00677 s: 60.05311 completed /
  48.78899 uploaded fps, game/wall 0.99994106, 0% late render intervals and
  2.1858% late uploads. PASS. Pointer lock remained captured and the screenshot
  confirmed the camera turn; saves flushed after stopping.

Full SDK suite passes (nine checks, tile parity explicitly configured for sixteen
workers). Full Linux runtime/policy suite passes: 35 executed, two fixture-dependent
skips. Windows's canonical incremental ISO build deploys and its staged GSR game
exits normally (0) after the 18-second boot probe (6,621 host frames / 16,952 draws).

The native receipt checker reads the same counters from log lines and refuses
short/malformed captures. Four-CPU WSL, native fa101b2467b1e1cb5d8985d39d2eea2e167eb7de476328edd79c64d50f921b17,
last 30.36495 s after walking: 37.14809 completed / 37.08222 uploaded fps,
game/wall 0.99930998, 0.9752% late rendered intervals but 12.4334% late uploads.
FAIL on presentation cadence. Applied the same 120 Hz limit to Linux; the Windows
branch retains its previous code. Native rebuild/probe is in progress here.


Linux 120 Hz, 4fed4fd07ffc1aa6e727b8f3ede1afccbcea4667550f84e19d799bdd2b6be568,
30.29021 s after walking: 37.56989 completed / 37.50386 uploaded fps,
game/wall 0.99986094, no late render intervals, 5.8099% late uploads. This
still narrowly FAILS the 5% upload gate; the threshold is unchanged.
A 240 Hz diagnostic run stayed in Load Game (screenshot confirmed) after a
short simulated confirm tap; it is not gameplay evidence and is excluded.
The probe now holds confirm for 150 ms and allows two seconds between dialogs.

Inspection found redundant GL draws even when UploadFrame correctly skipped
an unchanged game image. Linux/browser now retain the compositor's existing
image, poll input and briefly sleep instead of issuing another identical GL
paint. New images and debug UI still draw; Windows continues its original
host draw path. Measuring this with the 120 Hz cap next. This does not remove
GS fences or alter the guest timestep, shader arithmetic or resolution.


New-frame-only paints did not improve native pacing (12.5442% late uploads);
that experiment was reverted. Collapsing the preliminary CPU presentation fence
also did not improve it (12.5222% late uploads); it too was reverted. All original
GS barriers remain. The observed problem was when capture occurred relative to
the original flip, rather than the number of host draw calls or fences alone.

Linux/browser now capture the image at the original graphics-worker flip before
subsequent guest work, then publish completion. A mutex couples that image with
its revision until the window has copied it; the mutex is released before GL
calls. Stale/non-worker movie paths retain their existing capture fallback.
Windows retains its previous capture/presentation path. This also makes the
completed-frame metric describe an image that has actually been captured.

Native 76a383d30570d648edb9930b91cd5a48bba5a36b0a9da69344923f03220686dc,
30.13236 s after walking, four WSL CPUs: 36.80428 completed / 36.73791 unique
uploaded fps, game/wall 0.99952340, zero late render intervals and 0.63234% late
uploads. PASS. The game exited normally (0). Browser clock/input SDK checks pass;
browser's final whole-game rebuild/live measurements follow.


Final flip-capture Wasm a7ad3b6e9e943889eb62c3789e487eba5745d1797909afd1079e4abd1531a628:
- Grass, 30.01085 s: 36.15359 completed/uploaded fps, game/wall 0.99963850,
  0.2765% late render and upload intervals. PASS.
- Walking then stationary, 30.023015 s: 39.63626 completed/uploaded fps,
  game/wall 0.99906692, 0.0840% late renders and zero late uploads. PASS.
- Captured reverse camera, 30.01174 s: 60.00985 completed / 59.97653 uploaded
  fps, game/wall 0.99937563, no late render or upload intervals. PASS.
  Audio consumed 1,440,512 samples in that window, with no additional ring
  overflows and nonzero peak 0.0710144.
The final intro retained Full/25 fps, including 50/51 completed flips per roughly
2.0 s. Distinct book images advanced; audio consumed about 47,984 samples/s,
peak 0.472168, with ring overflow count zero throughout that movie check.
SHA-256/length comparisons of all twelve card files before and after New Game
were identical. Stop flushed normally after each check. Final Windows ISO build
and staged GSR boot exit normally (0); nine focused Windows checks pass. The
sixteen-worker Wasm VRAM fixture was also run on Windows Node, which reported
sixteen logical CPUs, so that comparison exercised the actual sixteen-worker
partition rather than the four-CPU WSL limit.

Shadowmire diagnostic redirection: initial attempts included transition time or
less than twenty seconds of settled gameplay, so they are excluded from qualified
performance evidence. A longer fresh-card New Game run confirmed the intro,
entered Shadowmire and captured a settled final 30.13895 s window: 46.28561
completed/uploaded fps, game/wall 0.99996841, no late render/upload intervals.
This is a diagnostic entry view, not proof of the normal story transition or
whole-level completion. The user card was isolated from those tests.

The user then explicitly approved eight WSL CPUs and restarting WSL. Changed
only processors=4 to processors=8 in ~/.wslconfig, preserving
memory=16GB, then ran wsl --shutdown and verified nproc=8. No WSL process was
running at the pre-change check. The Linux suite still passes (35 executed,
two fixture skips). The binary is unchanged, 76a383d3...:
- Surdana, 30.14185 s: 37.92069 completed / 37.75481 uploaded fps, game/wall
  1.00013757, 9.0989% late renders and 9.5782% late uploads. FAIL despite
  throughput above thirty. Further native worker-count comparisons follow.
- Shadowmire, 30.12929 s: 62.33138 completed/uploaded fps, game/wall
  0.99952584, 0.0532% late renders and no late uploads. PASS.


Balanced eight-CPU native policy and final player checks
-----------------------------------------------------

With eight WSL CPUs, four raster workers leave capacity for guest execution,
audio/resources and presentation. The default now uses half the available Linux
CPUs, bounded to four through eight; the browser still uses sixteen raster
workers on this sixteen-logical-CPU host. Guest timing and pixel math are unchanged.
Balanced native binary 37a5a507418190f3cf7cbd4d25a7151023977b0ebc3263abeed5c91cd69393b0:
- Surdana: 30.150807 s, 42.25426 completed / 42.15476 uploaded fps,
  game/wall 0.99997328, zero late renders, 0.07868% late uploads. PASS.
- Shadowmire diagnostic entry: 30.13772 s, 52.45918 completed/uploaded fps,
  game/wall 0.99971072, 1.328% late renders, 1.265% late uploads. PASS.

Current Linux player binary
373fe9d15c9fbdd418a905d1aab7129911dd3cae4e9ed58a6af618b341c2463c
also exited normally. Its final 30.116832 s Surdana window recorded 36.42481
completed / 36.35841 uploaded fps, game/wall 1.00017163, 0.6381% late renders
and 0.9132% late uploads. The strict gate passes, but another game was observed
running on the host; this is retained as a contended sample, not a clean comparison.
Receipt: build/parity-linux-latest8.json.

Final browser Wasm
8495eed987d2bdb134b30086d5114384256e3dd020c09068845d51139e8fc7e7
uses the game-only UI, actual menu-state bridge and serialized automatic
file-close save persistence. Its instrumented 30.012325 s Surdana sample recorded
31.2205 completed/uploaded fps, game/wall 1.00005585, 21.66% late renders and
18.14% late uploads: FAIL. Call of Duty was consuming approximately six logical
CPUs at the time. The user was asked to close it for an uncontended rerun; no
unrelated process was stopped. Earlier clean browser receipts remain scoped to
their recorded binaries/scenes and do not substitute for a final clean rerun.

Functional checks on the current browser build verified actual title, pause,
save and load screens without HTML chrome, gameplay mouse capture, automatic
pause/resume capture changes, and fullscreen. Saving into empty Slot 4 persisted
all four files to IndexedDB without a manual flush. Reloading the running page
and loading that slot succeeded. All twelve existing Slot 1-3 files retained
their hashes. The final Windows ISO build and staged GSR boot exit normally;
nine focused Windows checks and all 113 Python checks pass. Latest menu/input
and clock Wasm checks pass; prior complete nine-check SDK and actual sixteen-worker
VRAM comparisons are recorded above. See browser-player-ui.md for UI findings.


Enhancement scope correction
---------------------------

The user confirmed that the ports must support HD textures, ray tracing and
other enhancements where applicable. The software-renderer fallback is not the
final feature-parity target. They also explicitly instructed leaving Call of
Duty running; no host process was stopped and no clean timing claim is made
under that contention.

Inspection confirmed runtime/gs returned early on non-Windows before building
its already portable texture processing. Moved the existing gs_texup.cpp into
shared drakan::texture and linked the Windows renderer against that target.
Linux now builds the same core and runs its original tests; the Wasm SDK harness
also compiles and runs that exact source/test fixture. All thirty checks pass
on each platform, including generated-prompt protection, pixel upscaling,
content hashes, exact PNG alpha/colour round trips and mip generation. This
removes a build boundary; it does not yet integrate replacement sampling into
the software GS or implement browser pack delivery or a portable GPU/RT backend.
Those remain explicit work in docs/reference/portability.md.

The canonical incremental Windows ISO build also linked and deployed successfully
after the shared-library split (drakan.exe SHA-256
1035f6817dab3b4201e50b1683f83b3b47844a9a1ad78a00b685c496a1ab13eb).
All 113 Python checks still pass; this change does not alter pixel algorithms.


HD renderer integration (in progress)
------------------------------------

Added an asynchronous content-addressed TexturePack using the existing decoded
RGBA hashes and PNG/DDS decoder, RGB aliases and exclusions. Decoded image cache
is bounded at 128 MiB; queued source copies are bounded at 16 MiB. Images remain
original until a background worker completes replacement loading. Software GS
captures replacement selection on immutable queued draws and shares the selected
image with tile workers; it retains native sampling/cache observations, and
feedback/stale-cache cases keep the original path. Nearest and bilinear sampling
use replacement coordinates and GS wrap/clamp modes. Unsupported image dimensions
fall back to original rather than scaling UVs incorrectly.

The fixture renders HD subtexels through the real CPU GS and exercises aliases,
source-slot recycling, palette changes and exclusion. Initial native Windows,
Linux and Wasm fixture runs passed; later expanded fixture is being checked.
Existing complete-VRAM serial/tile and ordered-async comparisons pass. Added
loopback pack-index tests and startup indexing without a visible browser UI.
Browser images will load on demand via synchronous Emscripten Fetch on a dedicated
texture pthread (not on the UI/render thread); this follows the documented Fetch
pthread support: https://emscripten.org/docs/api_reference/fetch.html .

Canonical deployment now incrementally stages optional shared HD assets. The
initial PowerShell file-by-file staging was interrupted during the copy only;
replaced that loop with a bounded flat Python staging helper and restarted the
same canonical build, preserving partial assets for incremental verification.
No game data is added to tracked sources. Real-disc checks and performance
receipts follow; no HD performance or RT parity is claimed by these fixtures.


First real HD integration measurement
------------------------------------

Canonical Linux build 0d936f5d0bcc5fca151abb24a680deb6bb029c4a0809e000366f14dfc76b7c1e
staged 2,963 shared pack files. Native Surdana gameplay rendered and 62 replacement
loads were logged, including 256x256 to 1024x1024 images. Screenshot:
build/linux-speed-hd-on.png. The final 30.140169 s sample measured 14.86388 completed
and 14.83071 uploaded fps, game/wall 0.997307, all render/upload intervals late:
FAIL. Receipt build/parity-linux-hd-first.json. No unrelated process was stopped.
This result is not original-speed support and is not hidden by averaging.

The initial implementation decoded/hash-scanned every source at repeated lookup.
Added a 64 MiB / 128-entry decoded-source memo with byte comparisons over the
conservative physical source pages and the retained palette. Slot addresses alone
never validate content; recycled bytes and palette changes decode and rehash.
Non-indexed textures do not invalidate on unrelated CLUT updates. Content/RGB
hashes are computed only when decoding changes, then used for asynchronous pack
cache lookup. The expanded Linux HD/palette/subtexel fixture and serial/tiled/
ordered-async full-VRAM comparisons pass after this change. A new canonical
build and real-game timing run are in progress.


HD source reuse and worker balance
---------------------------------

The sampler arithmetic build d63d2b88... improved the measured scene to 26.45238
completed / 26.41919 uploaded fps; game/wall 0.99924937. It still failed pacing.
The first source memo overwrote the prior contents of a descriptor. Retaining
multiple byte/palette-validated contents per upload slot, with a sampled
fingerprint used only to reject candidates, improved binary 07ab70d775e4ab334d3df8dd9d968a7e548613827e3b52f23704f3704585114a:
- Four raster workers: 30.097590 s, 30.79981 completed / 30.73336 uploaded fps,
  game/wall 0.99948206, 35.7066% late renders, 37.1892% late uploads. FAIL.
- Six raster workers, same binary and eight WSL CPUs: 30.132078 s,
  34.44834 completed / 34.34878 uploaded fps, game/wall 1.00009700,
  2.8902% late renders, 4.6377% late uploads. PASS for this window.
Receipts build/parity-linux-hd-cycling.json and parity-linux-hd-six.json.

The source cache now retains up to 512 versions within its 64 MiB budget. The
regression fixture revisits an earlier upload slot, changes a byte not included
in the fingerprint, and changes the palette; only full byte/palette matches may
hit. Added opt-in DRAKAN_HD_PROFILE lookup timing. The sampler uses precomputed
power-of-two scales and wrapping and avoids decoding discarded native colors
inside proven immutable texture scopes, preserving the final native cache
observation. The original sprite fixed-UV rounding makes the tested bilinear
weight 5/8, not the initial test expectation; corrected the independent expected
pixel to 0x80ff009f. Linux, Windows and Wasm passed that HD fixture and renderer
comparisons before the final worker-selection change.

Native Linux with an HD pack on eight CPUs now selects six raster workers by
default, reserving two CPUs for the rest of the runtime; no-pack defaults and
explicit thread overrides are preserved. Larger native hosts remain capped at
eight raster workers. Shared pools are keyed by requested size. A canonical
Linux/browser build and default-policy validation are in progress. No full-game
HD or ray-tracing parity is implied by the Surdana receipt.

Browser deployment now references an existing shared HD pack in web-build.json,
as it already does for the ISO. The loopback launcher serves only hash-named
image requests from that directory and sends alias/exclusion metadata through
the pack index. This avoids a second multi-gigabyte copy. Added synthetic HTTP
checks for that transport and a staging regression for coarse DrvFS timestamps.


Browser HD streaming validation and contended timing
--------------------------------------------------

Wasm 9ac6e580ad3a061b818f12934db6e08ad1c7b12c4438ec986dbcb66d086e4b73
loaded the existing Windows pack from the external launcher folder, with actual
256x256 -> 1024x1024 and other replacements logged in title and Surdana gameplay.
No browser UI chrome was added; body text is empty during play. Screenshot
build/browser-hd-first-gameplay.png. A 30.002825 s grass sample measured
24.59768 completed/uploaded fps, game/wall 1.00037251, failing pacing.
Eight workers were slower: 22.68848 completed/uploaded fps over 20.010155 s,
game/wall 0.99879291. The default sixteen-worker policy is retained.

Opt-in profiling showed roughly 48-71 ms of source lookup per wall second in
settled gameplay, including full source validation and hashing on changes.
A three-second V8 sampling trace identified DrawTriangle as the main active
raster function; timed-wait samples are not interpreted as active CPU work.

An experimental vector wrap/clamp change preserved the synthetic image tests
(including repeat/clamp combinations and a fixed-UV bilinear oracle), but showed
no measured benefit. Browser Wasm f178b2f36df1237dd1c35e524a18cf5f7d3907c9bef2b1069188085fab70f808
measured 24.08918 completed / 24.05586 uploaded fps over 30.013480 s, game/wall
0.99928437. Native default six workers with that experiment measured 32.22145
completed / 32.15508 uploaded fps over 30.135206 s, game/wall 0.99889813,
9.6807% late renders and 14.7575% late uploads: FAIL. The experiment is reverted;
its correctness tests remain, rather than retaining unproven sampling complexity.

A host CPU sample during this session found sp23-cod consuming 582.2% CPU,
Chrome 512.9%. The user explicitly requested leaving Call of Duty running, and
that was honored. Looking only for the earlier sp22 process had missed its
successor. These are contended measurements, not clean-run comparisons.
A matched HD-disabled browser run on the same vector binary also failed:
28.64333 completed/uploaded fps over 20.004655 s, game/wall 1.00006727,
69.8080% late renders, 46.7714% late uploads. HD-on versus off under this load
still shows overhead, but neither establishes clean hardware capacity.

Current Windows canonical build linked/deployed after the experiment, executable
89185714467210fce9392b6f0af6050c930319b66be8aebdd8028f192cff6844.
The three affected Windows and Wasm renderer checks pass; Linux renderer checks
and all 117 Python HTTP/persistence/staging checks pass. Further GPU enhancement
work remains; the current CPU port does not provide hardware ray tracing.


Progressive scanout and local GLFW clock
---------------------------------------
Browser Wasm 222a5c5ab0ed885842f6361ea912e85dfeeea80a12b05af80110302ad06fca98
retains full completed-frame rows and removes synchronous UI proxying from GLFW
time reads/setters. HD Surdana grass, 39.25117 seconds: 21.32421 completed and
uploaded fps, game/wall 1.00019953, 100% late render/upload intervals. Strict gate:
FAIL; receipt build/parity-browser-progressive-clock.json. Call of Duty remained
running as requested; no own compiler or other test game ran during this window.
The visual and stall corrections do not establish 1:1 performance. Further portable
GPU rendering and applicable enhancements remain unfinished.
