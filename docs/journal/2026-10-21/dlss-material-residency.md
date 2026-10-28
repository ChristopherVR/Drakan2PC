# Ray material residency experiment, 21 October 2026

Rejected; not deployed. The installed world-cache build remains
`1ca391ce9cd11a97f6f6179034b07df8ad6066678990de4eb5ccfadf9de26e89`.

The experiment staged triangle attributes and changed texture slots into a
D3D12 DEFAULT-heap buffer before ray dispatch. It retained the existing fences
and CPU completion wait. This follows Microsoft's recommendation to consider
[DEFAULT memory for repeated GPU reads](https://learn.microsoft.com/en-us/windows/desktop/api/d3d12/ne-d3d12-d3d12_heap_type),
but the additional copies and memory did not improve this measured workload.

Evidence is under `build/dlss-material-residency-20261021`:

- Private candidate: `f3602bd25a8ebb352a671fff8488010b8d20f2c6b0e42e3688cc6a68056632a1`.
- Upload and local-memory hardware tests produced byte-identical readbacks:
  150 frames, 4,915,200 bytes, SHA256
  `9a6404970624e2960defd1aa5780746757d544fb66ea339d9ad3556324306ed4`.
- Six focused ray, motion, guide, presentation and scene tests passed.
- River, pond and camp movement routes each recorded 349 shadowed frames,
  independent model motion, finite SR/neural output and exact presentation.
  All save checks passed. These checks do not establish universal visual quality.
- Paired fixed-view pond runs with validation readbacks disabled measured
  29.22 world updates/s for the installed build and 28.41 for the candidate
  (-2.78%). RTX 5080 / Ryzen 7 9800X3D, 1920x1080 presentation, scale 3,
  Quality SR, neural rendering, water, reflections and shadows enabled.
  These are the final five two-second samples, not frame-time percentile data.
- No competing game/compiler overlap or tracked external CPU activity was
  detected. The process monitor is not an exhaustive system-load measurement.

The source was restored from `ray_water.before.cpp`; the optional binary
readback capture in the hardware test was retained for future comparisons.
No release/settings/save files were changed by this experiment.

The baseline CPU profile averaged 2.68 ms for world triangle packing, 3.72 ms
for the ray completion wait, 10.86 ms inclusive for the ray scene, and 5.23 ms
for neural evaluation. These scopes overlap; do not sum them. The completion
wait includes prior queued work and is not an isolated GPU ray-tracing timing.
Full-feature performance remains below a 60 FPS target and the goal is unmet.

Additional mode-isolation runs observed 36.18 world updates/s with Quality SR
alone and 38.58 with SR/neural rendering off (RTX effects retained). Both are
marked non-comparable in `mode-costs.json`: the broader monitor caught external
CPU activity, plus compiler overlap in the native run. Do not attribute the
difference to SR or use these as a clean neural-overhead estimate.

After restoring the source, the ray test target was rebuilt and all six focused
tests passed again. `rejection.json` verifies the restored source and unchanged
installed executable hashes. The other optimization chat was still active on
texture-processing overhead when checked; its work was not deployed here.
