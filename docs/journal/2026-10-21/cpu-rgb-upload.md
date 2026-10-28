# RGB24 uploads and VU flag retirement — 2026-10-21

Consistent 144+ raw FPS remains unachieved. This continuation produced two private candidates, correctness tests, and interleaved movement/combat runs. Neither candidate has replaced the installed game or root renderer/runtime sources.

## Renderer change

The `rgb-upload` candidate extends the preceding `source-reuse` renderer with byte ownership for the complete 64×64 CT24 uploads seen in combat traces. It accepts only x=y=0, buffer width 1, and a nonwrapping 64-block span. RGB bytes become CPU-owned while every fourth storage byte remains GPU-owned. All overlapping dirty aliases are checked before ownership changes. Unsupported formats, sizes, and aliases keep the existing conservative path.

Covered CT24 texture reads can decode CPU RGB without materializing the GPU alpha bytes. CPU reads needing those bytes and later GPU render-target reuse still perform the existing merge. A full replacement cancels obsolete ownership. Deferred IMAGE buffering now counts three bytes per CT24 pixel, including fragmented submissions.

The new RGB suite passed 3,620,864 GPU pixel comparisons, 26 exact 64 KiB VRAM comparisons including alpha, and 16,384 decoded texture pixels at scales 1 and 4. It covers fragmented transfers, CPU observation mid-transfer, partial/full replacement, raw-pointer exposure, subsequent GPU drawing, repeated uploads, and mixed ownership. Existing T8 tests passed another 3,620,864 GPU pixels and 26 exact VRAM ranges. Source reuse tests passed 23,040 exact mip pixels and 13 format/CLUT assertions.

## VU change

The `rgb-vu` executable adds an inline readiness check before selected flag-retirement calls. Empty FIFOs and results whose ready cycle has not arrived skip the out-of-line call. Ready results use the original FIFO retirement implementation. The original entry point and ABI remain available to unchanged objects.

Only `ps2_vu1_core.obj` and `ps2_vu1_native_kernels.obj` were rebuilt; 11 other objects were verified byte-identical to the frozen packed-cache-fusion baseline. An initial full regeneration was stopped because it needlessly rebuilt unrelated generated blocks. No build was running during the recorded gameplay runs. This is a small pipeline cleanup, not a new parallel geometry implementation.

Validation passed:

- 114,688 fused-transform state/data/packet comparisons.
- 114,688 fused-packet state/data/packet comparisons.
- 1,800,000 block-boundary and future flag-state comparisons.
- 8,192 timed DIV/SQRT/RSQRT, batched-flag, and dependent-tail comparisons.
- 100,000 busy-pipeline retirements, including short blocks and ring wrap.

The earlier thread-CPU evidence still identifies VU execution as the largest measured consumer. Its sampled generic blocks 209 and 212 start at microcode PCs 1460 and 1499 (byte PCs 0x2DA0 and 0x2ED8), with 16 and 11 instruction pairs respectively. Those generated blocks remain unchanged. Their arithmetic and scheduling work is a more substantial remaining target than this guard; wall-sample counts must not be treated as CPU-time percentages.

## Gameplay validation and limits

All six diagnostic runs checked 32 distinct GPU texture resources with zero mismatches. Final five profiling windows had zero readback maps for both candidates and controls. The RGB-only combat run exercised 26,981 sub-block deferrals and 108,478 covered texture reads over 542 latches. These counters include already CPU-owned blocks; they are not counts of maps eliminated.

Crucially, the fresh control also had zero maps in its final window. This route therefore did not reproduce the historical CT24 stall reliably enough to quantify a gameplay map reduction. The targeted renderer tests prove the new no-readback behavior with preserved alpha; live counters prove path coverage. Neither proves an FPS improvement by itself. The completed control trace still includes a CT16 32×32 upload outside this extension.

The combined combat diagnostic's final interval window had mean 17.117 ms, p95 22.116 ms, p99 23.887 ms, and maximum 24.613 ms. All 117 sampled intervals exceeded the 6.944 ms budget for 144 FPS. These are instrumented world-update intervals, not raw presentation latency.

Routes use the existing Level1A Surdana movement and hostile-combat fixtures. They remain proxies for the reported city approach; the exact user viewpoint has not been visually confirmed. Wall-clock input timing means varying contention changes simulation progress. The raw runs disable profiling, texture validation, frame generation, vsync, and the FPS cap. Their averages do not establish frame-time consistency, and external CPU contention prevents attributing small differences to the changes.

## Final raw comparison

| Candidate | Movement raw FPS | Combat raw FPS |
|---|---:|---:|
| Control (source-reuse) | 82.90 | 57.86 |
| RGB24 ownership | 84.73 | 59.06 |
| RGB24 + VU guard | 81.13 | 59.19 |

All six raw runs were externally contended (4.76�4.98 average CPU cores used
by other processes). No compiler or other benchmark launched by this task ran
alongside them. These are single interleaved runs on wall-clock routes, so the
small differences do not establish a causal speedup. All save hashes remained
unchanged, and every run exited normally through the input script.

The VU guard has no demonstrated combat benefit over RGB-only and its movement
result is lower. Keep it experimental rather than making it the default. RGB24
ownership is correctness-validated but needs a reproducible CT24-stall route and
broader gameplay validation before promotion. The primary remaining work is the
hot geometry blocks and submission path, not further reductions in already-zero
sampled map counts. Neither 144+ raw FPS nor consistent frame times were reached.

## Artifacts

- `analysis/experiments/rgb-upload-20261021/`: renderer patch, VU guard patch, RGB test, and application instructions. Both patches were applied in isolated fixtures and checked against the built sources.
- `docs/journal/2026-10-21/data/rgb-upload-20261021.json`: exact binary hashes, test logs, diagnostic counters/intervals, raw measurements, save checks, and CPU-contention evidence.
- Private executables: `build/texture-gameplay-20261021/rgb-upload/drakan.exe` and `rgb-vu/drakan.exe`.
- Reproduction scripts in that lab: `run-rgb-diagnostics.ps1`, `run-rgbvu-diagnostics.ps1`, `run-rgb-raw.ps1`, and `archive-rgb.py`. Use fresh run names.
- VU source/build/test records: `build/vu-flagguard-20261021/`. `object-provenance.json` records unchanged versus rebuilt objects.
