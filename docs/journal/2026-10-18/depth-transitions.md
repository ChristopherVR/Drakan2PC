# SR depth transitions — 18 October 2026

## Reproduced defect and correction

A locked Equipment view reproduced the flat blue-gray holes under the vendor canopy with SR Quality and DLSS 5 enabled. Single-frame GPU readbacks show the holes in prepared color **before SR**, as well as after SR and NR. A separate full-resolution NR-only run does not show those holes. These observations rule out the NR evaluation as the source of this particular defect.

The explicit world path resized color when entering reduced rendering but retained depth in full-resolution coordinates. The inventory preview clears only the right side of its depth target. Reduced world draws could therefore test against retained UI depth. Native HUD draws also inherited depth still in reduced coordinates after the world resolve.

The renderer now snapshots and resamples depth with nearest taps at both transitions: full to reduced before world draws, then reduced to full before native UI. The latter uses the same jitter mapping as the combined pipeline depth guide. Backdrop-copy failure restores the original depth; reduced-world recovery also restores full-size depth. This preserves partial clears rather than clearing the entire depth buffer.

The reproduced gray holes disappear with the correction, and the Equipment grid lines return while SR and NR remain enabled. This is evidence for this scene and defect, not a claim that all sky, smoke, hair or shadow issues are fixed.

## Bounded diagnostic evidence

Private local runs (raw color/depth/motion and game screenshots are excluded from publication):

| Run | Result |
| --- | --- |
| `build/t/sky-stages-01` | SR Quality + NR; holes before SR; normal quit, exit 0, 102.39 s |
| `build/t/sky-stages-nronly-01` | NR only; no corresponding holes in prepared color; normal quit, exit 0, 62.34 s |
| `build/t/sky-stages-fixed-01` | Depth correction; holes absent and grid restored; normal quit, exit 0, 62.58 s |

All original, fixture and private-copy save hashes remain unchanged. These times include diagnostics and are not performance benchmarks. The NR-only helper accidentally invoked unrelated category validators after normal exit; those validators failed because the short locked-view recipe contains no category RAM dumps. Its render and completion evidence are valid; no category-test pass is claimed for that run.

At GS frame 2500, the original 1364×1364 prepared-color ROI x880..1249/y120..349 contains 16,387 pixels with byte RGB(92,104,133), the flat hole color. The fixed ROI contains zero. Camera animation/pose varies slightly between boots; this count characterizes the visible defect, not a whole-image golden comparison. The supporting synthetic test isolates the coordinate error independently.

`GSR_TEMPORAL_DUMP_DIR` plus `GSR_TEMPORAL_DUMP_FRAME` opt into one-frame captures. The destination directory must exist. Files contain four little-endian uint32 values (width, height, DXGI format, bytes/pixel), followed by tightly packed rows. Stages include entry/world/prepared color, prepared depth/motion, SR color and NR color. Absent these environment variables, no capture allocation or GPU readback occurs.

## Regression coverage

The asset-free `stretch_depth` WARP test compiles the production shader. Three cases cover even and odd dimensions, partial preview clears, positive/negative jitter, edge clamps, untouched exterior pixels and source-snapshot preservation. The no-resize control reproduces stale-depth occlusion; the corrected transitions restore visibility. All three passed.

Combined executable checks and deployment identity are recorded in the final follow-up receipt.
