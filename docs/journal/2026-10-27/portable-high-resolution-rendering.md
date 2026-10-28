# Portable high-resolution rendering

The parity target includes actual scene sampling at higher resolution, with HD
texture detail, shared modern UI and Windows preserved. Sharper final scaling
alone cannot meet it.

The portable ordered raster shader now accepts odd integer presentation scales
(3x and 5x). It shades separate fractional positions inside every native pixel,
including sprite UVs. The centre sample is exactly the native position and is the
only sample committed to guest color/depth VRAM. This keeps texture feedback and
CPU-visible memory independent of the presentation resolution.

Initial native tests on both the default WSL adapter and RTX 5080 pass all 192
procedural full-VRAM comparisons, including the new 3x centre-sample comparisons.
Retaining the first and second halves of command groups produces the same scaled
surface as one combined run. Tests also pass with DRAKAN_GPU_SCALE=3 through the
integrated deferred backend. These are procedural checks, not a claim of game
visual or performance parity.

The presentation cache holds two bounded color targets, retains matching sample
data across dispatches and tracks native-pixel color/depth validity. CPU fragments
invalidate their bounds; direct writes, transfers, clears and aliased targets
invalidate matching storage. The cache is separate from guest VRAM and loss of
detail falls back to native pixels. High-resolution scanouts use their actual
packed row pitch instead of the legacy 640-pixel pitch. Dual-CRT composition
retains per-sample alpha, background and source origins; its native centres match
the reference. Whole-surface invalidation is lazy, and complete replacements or
unchanged cache bounds reuse owned storage instead of copying a retained surface.

Native GS submission now scopes nearest rounding independently of the EE's
MXCSR rounding control. Previously the real world's toward-zero control rejected
otherwise supported GPU batches. The fixture explicitly checks GPU dispatch and
all VRAM bytes under all four incoming modes and verifies control restoration.
The GPU path also batches with one CPU raster worker; CPU replay stays serial
when there are fewer than two workers.

The current native GPU suite passes 192 procedural fixtures (1,021 dispatches),
including exact native centres, retained/sparse work, same-value CPU overwrites,
resident atlas eviction, failure recovery and dual-CRT composition. The CPU tile
suite passes serial/tiled comparisons under four modes (726 dispatches), and the
ordered asynchronous suite passes. Browser WebGPU passes 610 recorded procedural
jobs, comparing every color/depth output word.

A real browser Surdana 3x window measured only 7.51 uploads/s and game/wall time
0.7515 over 114.71 seconds, with Call of Duty left running as requested. The
scene has visibly finer geometry, but this fails the performance target and the
private GPU path remains opt-in. This is not a minimum-performance or full-game
fidelity result. Exact receipts are private under build/.

An isolated 61,426-draw, 1536-square real-frame replay compared cooperative
workgroup command caches against the original shader. All output words matched,
but the resident-atlas baseline averaged 23.74 ms versus 24.67 ms for the shared
cache. Full-atlas uploads in an initial benchmark added substantial unrelated
cost, so those timings must not be treated as shader-only performance. A second
experiment conservatively culled about 40% of bin references with identical
output, but its small timing difference did not establish a useful speedup.
Neither experiment was promoted to production. These are dispatch timings,
not completed gameplay frame rates.

The portable CPU line and point paths ignored texture sampling entirely. They
now sample UV or homogeneous STQ, apply the texture-combine state, and pass its
alpha to the existing fragment tests. New procedural checks verify colors,
transparent-texel rejection and a nonuniform-Q line whose texel sequence differs
from affine interpolation. The Linux deployed gameplay check still shows the white
HUD outlines, so this correctness fix does not resolve that visual defect. Native
Windows and Wasm line/tile and asynchronous renderer checks pass. Windows also
rebuilt and passed the deployed gameplay smoke test (35 seconds, exit zero).

The browser GPU return path now delivers pixels directly to Wasm and wakes its
waiting renderer without waiting for a UI message callback. A fresh private
four-byte token arbitrates copying versus cancellation before any Wasm output or
completion address is touched. A cancelled job cannot access reused caller memory;
if copying wins first, the caller waits for its completion before releasing memory.
Input copies remain owned by the GPU worker and return to the UI's transfer pool.
This removes the former full-image GPU-worker-to-UI-to-Wasm copy. Node tests run
the shipped worker with controlled completion, cancellation, failure and legacy
delivery. Real WebGPU passes all 610 procedural jobs through both direct and
legacy delivery, comparing every color/depth output word. Actual game pacing
with this new bridge still needs measurement. An additional real-WebGPU check
blocked the page's UI event loop while waiting on the shared completion word:
the correct result arrived in 0.69 ms with no UI callback having run, confirming
that result delivery no longer depends on that callback.

The deployed direct-return bridge measured 7.13 fps over 30.00 seconds in
Surdana (game/wall 0.7133, no page errors or compute failures). Removing that
return callback alone did not recover gameplay pacing.

A subsequent shared request queue removes the per-job UI submission callback
as well. Its control storage has process lifetime, and a mutex serializes callers.
The worker claims Ready requests, owns input copies before entering asynchronous
device work, and claims Writing before accessing caller output. The caller may
cancel Ready/Async states, but retains allocations during the synchronous Reading
and Writing sections. A cancelled/failed compute instance stops dispatching.
The worker refreshes its shared WebAssembly memory buffer after growth. Owned
input allocations are reused in the GPU worker rather than transferred via the UI.
Startup alone creates the worker on the UI, allowing it to start independently
of the blocked pthread. The existing player exit handler terminates that worker.

Queue cancellation before input copying, cancellation during device promises,
failure, memory growth and direct/legacy delivery pass the shipped-worker Node
checks. All 610 real-WebGPU jobs also match through the queue. A separate browser
check dispatched and verified 30 jobs in 86.86 ms while its UI event loop stayed
blocked throughout, with zero UI callbacks. These establish protocol correctness,
not gameplay performance; deployed queued-game validation follows.

The queue's worker-error handler also waits for any Reading/Writing ownership to
end before failing the request and terminating the worker. Termination itself is
not used as a memory-ownership fence: the HTML Standard runs its
[termination algorithm](https://html.spec.whatwg.org/multipage/workers.html#terminate-a-worker)
in parallel with the worker. Node checks execute the actual embedded error
handler against every queue state and verify that live copies keep their claim.
Publishing a new request uses compare-exchange so it cannot overwrite a concurrent
terminal failure. Startup failure cancels the idle request as well.

The first deployed shared-queue game window measured 9.40 fps over 30.01 seconds
in Surdana (game/wall 0.9390), with no page errors or compute failures. This still
fails the pacing gate. Host load was not controlled, so the difference from the
7.13 fps bridge run is not presented as an isolated causal speedup.

Draw packets subsequently shrink from 80 to 64 words (320 to 256 bytes). Each
vertex retains its exact uint32 depth and packed byte RGBA instead of expanding
those values into six floats. Positions and STQ/fog remain f32. A zero winding
encodes the existing degenerate-triangle rejection; valid winding and inverse
area retain the original bits. Shader unpacking produces the same exact f32
colors and integer depth. Native tests pass all 192 full-VRAM fixtures and the
browser passes all 610 jobs with independently converted compact packets. This
reduces command packing/copy/upload traffic by 20%; a gameplay speedup is not yet
established by those correctness checks.

The remaining presentation blur had a direct Windows mismatch. GSR's progressive
display policy disables the second circuit for libgs' constant-alpha blend of
two nearby reads from the same framebuffer. Portable completed-frame scanout
previously kept that blend, mixing rows a native line apart even at higher scene
resolution. The portable host now applies the same framebuffer/format/pitch,
horizontal-origin, width, vertical-distance and PMODE checks. Its default matches
Windows, while the existing PC Anti-flicker setting now works live and restores
the blend when enabled. This changes only the copied host request; registers and
VRAM remain unchanged. CPU tests compare both settings against independent
single-source/dual-source output and preserve unrelated composition cases.

The sprite path also expanded truly empty rectangles into one-pixel spans.
Submission now discards rectangles with equal original X or Y endpoints; direct
GPU packets carry the same empty-coverage flag. Genuine nonempty subpixel
rectangles retain their prior coverage. CPU tests cover serial/tiled submission;
GPU tests verify unchanged native VRAM and retained high-resolution samples for
all three empty shapes. This is a candidate cause of the visible HUD borders,
pending deployed capture verification.

The updated native suite passes 192 fixture groups and 1,024 GPU dispatches.
All 615 newly recorded native compact jobs match every color/depth output word
when replayed through the browser's shared queue, including empty sprites.
Windows and Wasm CPU suites also pass the new anti-flicker and sprite checks.

WSL's default Mesa D3D12 adapter on this host is AMD Radeon integrated graphics,
despite the host also having an NVIDIA GeForce RTX 5080. Setting
MESA_D3D12_DEFAULT_ADAPTER_NAME=NVIDIA for a private test selected the RTX 5080 and
passed the same GPU fixtures. This is a process-local testing override, not a
machine-wide change. Mesa documents the substring selection mechanism in its
[D3D12 driver documentation](https://docs.mesa3d.org/drivers/d3d12.html); Microsoft's
[WSLg GPU selection guide](https://github.com/microsoft/wslg/wiki/GPU-selection-in-WSLg)
also describes the integrated-GPU default. Native Linux hardware normally uses
its own Vulkan/OpenGL drivers; this finding must not be generalized to it or to
the browser's adapter without checking those separately.
