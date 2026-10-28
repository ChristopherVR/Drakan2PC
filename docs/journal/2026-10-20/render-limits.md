# Reflection coverage and remaining renderer limits — 2026-10-20

The sky observer now samples the whole viewport. Sky seen only through a lower
opening or a pitched camera can seed the retained reflection environment. The
previous observer sampled only the upper half. A hardware regression exercises
learning a lower opening, retaining it when hidden, and clearing it on level reset.

The observer loads and linearizes each probe once, accumulates all seven sky
statistics in one workgroup, and uses 28 KiB of shared memory. Per-water-pixel
environment evaluation reads the three contributing signed-axis lobes rather
than all six. Integer powers use multiplication. Directional cache ownership
and level-reset behavior remain intact.

The private candidate replaces only the water-renderer object in the previously
validated installed renderer library. It preserves the runtime, model motion
guides and menu implementation while concurrent CPU/VU work continues.
Build and deployment provenance lives in `build/render-limits-20261020`.

## Validation

All six focused tests pass: ray water, model motion, neural guides, temporal
presentation, scene geometry and scene models. The hardware water suite includes
lower-viewport sky, world-oriented sky retention, shallow water coverage,
subpixel reflection stability, alpha geometry and shadows.

Gameplay checks use private save copies and internal scripted input on the river,
pond and dragon-camp routes. Their raw neural output, independent model motion,
shadow activity, presentation and save hashes are recorded in `visual-metrics.json`.
The installed executable receives a separate smoke test.

Candidate SHA-256:
`9f7ea0d3acd973aacfe056a1c483249debbb8d14f5dca08c1875c1dcf63d7d41`.
The three routes produced 31960, 28664 and 31475 independent model-motion pixels.
Each retained native shadows over 349 world frames, with finite SR/NR output
and exact presentation checks. The isolated river-water capture was also
inspected: reflected banks remain visible; the coarse bank silhouette remains.
The previous executable and settings are in
`build/render-limits-20261020/deployment-backup`.
The installed smoke passed with finite neural output, exact presentation,
native shadows and unchanged save hashes; see `deployment.json`.

## Performance method

The earlier ABBA game comparison remains inconclusive: three of four runs had
detected competing game/compiler activity. Timed movement also changes the
workload when frame rate changes. These numbers cannot establish a speedup.

The new optional `gsr_test_ray_water --benchmark` uses a fixed 1024-square
synthetic scene with shadows and reflections, 20 warmup renders and 100 measured
renders. The render call waits for its D3D12 fence. It excludes debug-layer work
and output readback from timing. The same test object is linked against the
deployed and candidate renderer for comparison. This measures the synthetic
renderer workload, not full-game FPS. Process snapshots cannot exclude every
brief competing workload.

The synthetic ABBA samples measured baseline medians 0.740/0.735 ms and
candidate medians 0.762/0.773 ms. Another game process was present throughout.
These observations do not demonstrate a speedup; the change is justified by
reflection coverage, and clean performance attribution remains unresolved.

The final fixed-view gameplay pair measured 25.93 FPS baseline and 26.47 FPS
candidate at 1080p, Quality DLSS and neural rendering with reflections/shadows.
Both had detected competing work, so `performance.json` marks them noncomparable.

## Still unresolved

- Native terrain has a 2048-unit grid and authored triangle splits/holes.
  The sharp riverbank silhouette exists with modern water disabled. Shader
  blending cannot replace it with smooth geometry; no terrain remesh is shipped.
- Neural reconstruction consumes moving-model guides but does not replace
  original meshes or increase their polygon counts.
- The retained sky is a low-frequency directional approximation built from
  observed background pixels, not a complete environment map. Background
  classification is still heuristic.
- River, pond and camp checks are not whole-game validation or proof of
  artifact-free motion. Further areas and a quiet matched performance run remain.
