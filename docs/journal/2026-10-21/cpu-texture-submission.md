# Texture and geometry gameplay investigation — 21 October 2026

The 144+ raw FPS goal remains unmet. Nine raw gameplay measurements completed,
but every run was flagged for external CPU contention. No candidate was
deployed or made the default. The preceding texture-hash source improvements
remain in the working tree; uncapped and original/30 FPS controls are unchanged.

## What was tested

The baseline and candidate use the same frozen VU implementation and renderer
snapshot. The baseline reverses only the texture hash reuse/direct RGB lookup
changes. Both renderer libraries have neural rendering compiled out; their raw
configuration also disables DLSS, NR, frame generation and ray effects.
They use the existing audio-enabled, scale-4, uncapped gameplay fixture.
Because the shared renderer has developed since the earlier serial tests,
these builds must be compared with each other, not directly with old binaries.

| Private variant | Change | Validation |
| --- | --- | --- |
| Texture hash candidate | Reuse the decode hash; mask RGB while hashing instead of allocating a temporary image | Existing texture tests and gameplay completion |
| Exact texture blocks | Check only storage blocks read by each mip; retain conservative render-target coherence flushes | 5,400 exhaustive pixel-footprint comparisons, including indexed formats and wrapped memory |
| Geometry clock candidate | Skip diagnostic clock reads when VU statistics are disabled | Compiles and completes gameplay; no arithmetic, pipeline or packet changes |
| Native GPU upload | Upload native render-target rows and expand exact nearest-neighbour pixels on the GPU | 4,526,352 exact RGBA comparisons; scales 1–8, partial row offsets, resource growth/reuse, untouched rows and restored compute/RTV bindings |

The GPU path uses an intermediate RGBA8 texture and copies expanded rows into
the existing render target. It preserves compute shader, constant buffer,
resource views and UAV bindings. The existing CPU path remains the fallback
if GPU setup fails. It introduces no cross-frame work or reordered geometry.

## Measured work reduction

The final five gameplay profile windows measured:

| Diagnostic measurement per latched display | Texture-only control | Native GPU upload |
| --- | ---: | ---: |
| Render-target upload traffic | 7.994 MiB | 0.500 MiB |
| `syncRT` inclusive scope | 1.302 ms | 1.074 ms |
| Texture decode calls | 94.87 | 95.04 |
| Texture creation calls | 36.96 | 37.01 |

The traffic reduction matches the expected sixteenfold reduction at scale 4.
The timing scopes include contention and GPU waits and are not isolated CPU
execution times. They overlap other scopes and must not be added together or
treated as proof of an FPS gain. These two runs had the host RIP sampler off;
renderer phase profiling was on, so neither is a raw FPS benchmark.

The exact-block prototype's diagnostic also had about 37 texture creations
per display. It has not demonstrated a substantial reduction in this scene's
repeated decoding/creation. The geometry clock change likewise has no proven
gameplay speedup and remains private.

## Benchmark limits and evidence

The original texture ABBA comparison measured controls of 93.49 and 79.59 FPS,
and candidates of 67.31 and 79.31 FPS. All were contention-flagged. The GPU upload
pair measured 92.42 and 68.74 FPS, also contention-flagged. These values establish
neither an improvement nor a regression. Across the investigation external
processes sometimes consumed more than eleven CPU cores. The harness records
general system CPU activity rather than just competing games or compilers.

All twelve runs (nine raw, three diagnostic) completed their scripted gameplay
and passed original, source and private save checks. The private harness now
retains the process handle immediately after launch, fixing the missing-exit-code
failure seen in the previous profile. No installed executable, settings or saves
were changed by this investigation.

- [Machine-readable results](data/texture-submission-20261021.json)
- [Source-only experimental patches and hardware test](../../../analysis/experiments/texture-submission-20261021/README.md)
- Private binaries, complete logs, scripts and phase metrics:
  `build/texture-gameplay-20261021`

Each experimental patch was applied in an isolated fixture and its normalized
result matched the tested source. The GPU upload candidate is worth a clean
performance comparison; its established benefit is reduced CPU expansion and
upload traffic, not a verified FPS improvement. Geometry remains a separate
limiting path and speculative multicore geometry remains disabled by default.

The [renewed comparison and combined-row experiment](cpu-upload-coalescing.md)
adds buffer reuse and adjacent-row batching, plus a corrected active geometry
comparison. It recorded one clean 99.37 FPS control but no clean candidate pair;
the prototypes remain unpromoted.
