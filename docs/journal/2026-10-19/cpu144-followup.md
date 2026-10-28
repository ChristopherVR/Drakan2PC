# CPU performance follow-up, 19 October 2026

The starting installed binary is `EBB54534B8246C425A9946D3D69C24544163DA6606930FC6C721153149CCBE58`. The target is at least 144 raw world updates per second. This follow-up uses the existing fixed encounter, audio enabled, 4x internal scale, a 3840x2160 window target, uncapped simulation, and no frame generation, VSync or frame cap. Optional enhanced effects are measured separately with the saved DLSS quality, neural rendering, modern water, ray reflections and ray shadows enabled.

## Profile evidence

Fresh sampling of the starting binary places 82.2% of the busiest raw worker's executable samples inside vector execution. Packet assembly accounts for 10.3%, compiled block 47 for 5.3%, the run loop for 5.0%, block 19 for 4.0%, generic memory stores for 3.7%, and PATH1 progress for 2.5%. The earlier submission-barrier problem is no longer dominant.

With enhanced effects enabled, the busiest renderer thread spends substantial time assembling ray geometry and waiting for neural/ray interop. Resident terrain matching accounts for 5.4% of executable samples, triangle-vector insertion for 4.7%, and resident terrain projection/export for 2.1%. These are sampling fractions, not a prediction of FPS gains.

## Retained changes

Resident terrain caches retain immutable triangle edges, determinant, normal and length, preserving the original order of matching candidates and arithmetic. The cache is invalidated on replacement and floating-point rounding/denormal mode changes. Projected terrain attributes are retained until a winning material observation, geometry replacement or floating-point mode change. The renderer reuses its ray-scene vector capacity between frames. Dynamic triangles are still appended each frame.

Vector SQ, ISW and SQI stores inline the original memory-store logic into compiled instruction pairs. Constant destination masks allow the compiler to remove unused lane writes and avoid spilling vector registers for generic helper calls. Due overlapping PATH1 qwords still transfer before memory is changed; store retirement cycles and wrapping behavior are retained.

## Evidence

`build/cpu144-20261019` contains source snapshots, matching binary/map files, profiles, isolated save copies, benchmark manifests and validation logs. The resident cache has passed 1,163,264 exact comparisons against the previous implementation across four host rounding modes, evolving materials and scene replacement.

The first clean enhanced comparison was 25.366 FPS before and 27.370 FPS with the terrain cache. This preliminary result predates the final candidate and is not an achievement of the raw 144 FPS target.

## Final installed result

Executable SHA-256: `6a784a43ccb0a626996d7c7d0eca75b3f8dc9fe7cc98ca6bff153994f7ff3f7f`. Backup: `C:\Games\Drakan - The Ancients Gates\decomp\build\release_backups\cpu144-followup-20261019-050556`.

Clean final raw averages: **90.015 -> 90.958 FPS**. The small difference is within observed run variation; no substantial raw gain is established. The earlier separate candidate ranged from 90.984 to 93.100 FPS; those results are not the final installed artifact. **The 144 FPS goal is not achieved.**

Clean enhanced averages with unchanged settings: **25.278 -> 27.324 FPS (+8.09%)**. Profile, screenshot and reference-check runs are excluded from the averages. Selected runs have no overlapping game/compiler processes and unchanged saves.

The final binary passed 327,680 live vector runs with zero mismatches; the store path matched the previous runtime in 32,768 synthetic cases. One unrestricted synthetic PATH1/reference discrepancy also occurs in the previous runtime and remains unresolved. The terrain cache matched 1,163,264 reference results across four rounding modes, changing materials and scene replacement. Final enhanced rendering was inspected.

Final sampling places 81.9% of the busiest worker samples inside vector execution: packet assembly 9.9%, run-loop work 6.0%, block 47 5.0%, block 19 4.0%. Generic stores no longer appear as a separate hot function, but their inlined work remains. Larger vector-kernel and pipeline-bookkeeping changes are needed for 144/200 raw FPS. Settings, launchers and original-30 controls are unchanged. Full receipts are in `docs/journal/2026-10-19/data/cpu144-followup-20261019.json` and `build/cpu144-20261019/deployment.json`.
