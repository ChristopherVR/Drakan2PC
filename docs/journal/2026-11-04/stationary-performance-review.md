# Stationary-view performance review

Investigated the report of inconsistent FPS while looking at a fixed area. Runs use
`scripts/harness/play_run.py` from `dist/DrakanPC`, installed scale 4 / DLSS Quality /
water / reflections / ray shadows, uncapped and vsync off, with audio enabled.
The harness uses private cards and native-options copies; player settings were not
changed. Logs, recipes, screenshots and build/test receipts are under
`build/perf-still-review/` and `build/harness/perf-*` (untracked).

## Findings and changes

- The current scene no longer exhibits the old every-32-frame texture-refresh
  stall. The initial fixed-camera Surdana run measured 123.5 FPS, p99 9.77 ms,
  six gaps over 12 ms in approximately 26 seconds. Ravenshold measured 161.2 FPS,
  p99 7.50 ms and one gap over 12 ms in approximately 16.5 seconds. Those rates
  describe different workloads, not a single global FPS ceiling.
- EE samples during longer frames mostly wait in `GsrBridge::commitDisplay`.
  Renderer profiles attribute substantial CPU time to ray-scene material/normal
  preparation. Steady-state GPU readbacks were absent in the sampled still view.
- Ray model attributes used shared-pointer equality as their cache check. A new
  immutable capture containing identical triangles unnecessarily rebuilt welded
  normals and attributes; WebGPU also rebuilt its BVH. Both paths now verify
  identity, topology, resource ID and all triangle bytes before reusing a snapshot.
  Hash equality alone is insufficient. D3D12 retains material-generation and
  texture-base invalidation; WebGPU now also invalidates on material alpha-flag
  changes while retaining its unchanged geometry BVH.
- `packNormal` made two `lround` calls per corner. Its bounded positive
  quantization now adds 0.5 in double and truncates. Adding in float would be
  incorrect immediately below a half-integer. A 20-million-normal alternating
  microbenchmark measured old 367.65/372.81 ms, new 338.02/340.13 ms: approximately
  8.4% less time for that operation, not an 8.4% game FPS claim.
- Smooth faces meeting at a welded vertex frequently produce identical corner
  normals. `packCornerNormals` quantizes repeated values once within each weld
  group, then restores triangle-corner order. Hard edges, distinct normal values
  and stale-weld fallback retain independent packing. Both ray backends use it.
- `GSR_RAY_MODEL_REUSE=0` restores pointer-based snapshot reuse and independent
  corner packing for same-executable comparisons. The bounded quantizer remains
  enabled in both cases.

## Validation and measurement limits

`ray_trace`, `scene_geometry`, `ray_water` and `ray_water_lost_signal` passed.
The tests cover distinct snapshots with identical geometry, changes to UVs,
materials and topology, colliding identities, pixel-identical recaptures,
material alpha-flag invalidation and opaque restoration. Normal tests compare
against the original implementation across all four rounding modes, including
non-finite and zero normals, shuffled weld order, repeated normals and stale
topology (1.8 million comparisons). Repository guard passed.

An intermediate deployment measured Surdana at 135.1 FPS, p99 7.89 ms, maximum
9.78 ms and zero gaps over 12 ms in approximately 16.5 measured seconds.
Its model-attribute stage averaged 0.902 ms/frame. This is a useful playability
check, not a controlled estimate of the change's FPS gain.

Other sessions changed renderer files and launched additional game processes
during this investigation. They were left intact. An earlier same-executable
off/on pair was visibly contaminated (130.4 versus 110.1 FPS, with the latter
showing long `Present` waits). It does not establish a gain or regression from
snapshot reuse. No universal FPS floor, cross-level equality or elimination of
all user-reported drops is established by these tests.

## Final same-executable check

Executable SHA-256:
`6fe38b076682c00135640cd843ae25fa3a9135c73cb21571b7826ab9129dd402`.
Both runs use the same wall-clock recipe, installed graphics settings, audio,
and `DRAKAN_FRAME_AUDIT=2` / `GSR_PROFILE=2`. The summarizer excludes the first
three seconds after the start screenshot and the final half second.

| Surdana fixed view | Reuse off | Reuse on |
| --- | ---: | ---: |
| Measured frames | 2072 | 2159 |
| World presents/s | 125.76 | 130.75 |
| Mean interval | 7.951 ms | 7.648 ms |
| Interval standard deviation | 0.501 ms | 0.367 ms |
| p99 interval | 8.957 ms | 8.463 ms |
| Maximum interval | 9.616 ms | 13.799 ms |
| Gaps over 12 ms | 0 | 1 |
| Model attributes per call | 1.059 ms | 1.030 ms |

This pair observed approximately 4% more throughput and a lower p99, but did not
eliminate every outlier. Background game activity was present during this session;
the receipts are descriptive, not proof of an isolated 4% causal gain. The bounded
quantizer is enabled in both runs; this pair toggles only snapshot/normal reuse.

The final Ravenshold run (`perf-raven-final`) stood still, walked backwards for
three seconds and turned the camera, with audio and all installed effects on.
`GSR_RAY_CPU_CACHE_CHECK=1` compared world/material packing against complete
repacking through at least 3360 ray passes with zero mismatches. It completed
both renderer screenshots and a clean scripted quit. Its extra validation work
makes it a correctness run, not an FPS benchmark.
