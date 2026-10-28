# Water edges, terrain shadows and movement validation

## Changes

The river reflection cutoff returned during movement because the native sky
sphere could lag behind the current camera position. The strict camera-centred
classifier then admitted it as physical geometry. A captured ray scene confirmed
object `0xa5eca0` blocking the cliff at the cutoff; this address is diagnostic
only, not a production exclusion key.

`BackdropTracker` retains an initially proven background classification for the
same object generation, immutable mesh identity and transform basis. It resets
on level replacement and invalidates on object reuse, mesh/basis changes or
ineligible native flags/controller state. This also prevents the sky from
re-entering native neural geometry/motion guides. The initial large spherical
shell heuristic is unchanged; it is still not a native semantic sky-owner ID.

Terrain lighting selection now samples the actual split grid triangle at the
camera position, respects terrain holes and handles constant-height layers.
Previously it sampled just the top-left corner, so the slope movement run lost
its native lights and shadows for 25 frames. The revised run retained lights for
all 349 captured world frames.

The shadow receiver also fades smoothly where the native raster terrain LOD
and exported DXR terrain disagree in depth. This removes the hard shadow
hit/miss boundary while retaining full shadows on aligned receivers. Original
terrain silhouettes and coarse geometry remain; this is not a remesh of levels.

## Validation

Regression coverage includes camera/sky lag, object generation reuse, changed
meshes and bases, eligibility/level resets, both terrain diagonals, holes,
constant heights and invalid memory bounds. The hardware ray test verifies
continuous receiver-shadow fading and unchanged aligned/cutout/translucent
shadows, as well as reflection/depth/alpha handling. Six focused renderer tests
and the real RAM/ISO scene capture pass. The public runtime patch applies to a
clean upstream checkout.

The cave-water fixture placed the character on an upper slope rather than the
intended water view; it was excluded from claims about reflection coverage.
Accepted final checks use the snowy riverbank, open pond and dragon camp with
movement and camera turning. Visual captures and numerical receipts are in
`build/water-edges-20261019`. Performance measurements use separate runs without
readbacks/validation logging and reject overlapping game/compiler processes.

Build provenance: the menu-enabled deployed core and menu objects are retained;
the renderer and native scene capture are rebuilt. Concurrent experimental VU
runtime work is not implicitly included. Frozen object/library hashes are in
`candidate.json`. Subsequent edits to source files do not change those inputs.

Final gameplay, performance and installation results are recorded in
`visual-metrics.json`, `performance.json` and `deployment.json` after completion.

Final integrated candidate SHA-256: `d52cf5eebf34f44adfabcc64f2b9e623da9e4b35a851538c82153f4346f38228`.

A newer menu/inventory release (`3ab445e6...`) arrived during graphics testing.
Its five menu override objects were integrated, retaining byte-identical
renderer, generated/runtime libraries and scene-capture object from the verified
graphics candidate (`802c8714...`). The integrated build was then tested again
in all three areas; `integration.json` identifies the exact inputs.

| Area | Independent motion pixels | Frames with native shadows | SR/NR finite | Presented frame matches |
|---|---:|---:|---|---|
| river | 35134 | 350 | Yes | Exact |
| pond | 31716 | 349 | Yes | Exact |
| camp | 33289 | 349 | Yes | Exact |

These visual/numerical runs overlapped other work and are excluded from performance claims. User/source/private save hashes stayed unchanged.

## Performance samples

RTX 5080 / Ryzen 7 9800X3D, 1920x1080 output, native scale 3, Quality
DLSS, neural rendering and all ray effects enabled, VSync and frame cap off.
Separate 30-second scripted gameplay runs include movement and a camera turn.
Reported rates are weighted world-update FPS from the last five two-second
windows. Validation readbacks and per-frame logging are disabled. These are
short samples rather than a statistically established whole-game speedup.

| Area | Initial installed baseline | Graphics candidate | Notes |
|---|---:|---:|---|
| river | 28.94 | 33.76 | No overlapping game/compiler samples |
| pond | 19.45 | 20.12 | No overlapping game/compiler samples |
| camp | 21.87 | Excluded | Candidate attempts overlapped other work |

The initial baseline is `915d4299...`; the graphics candidate is `802c8714...`. The combined menu/graphics build receives a separate pond timing check against the newer `3ab445e6...` installation.

The newer installed baseline measured 21.18 FPS in its clean pond run. All three
final combined-build pond timing attempts were contended (17.25, 19.93 and
16.32 FPS under load), so they are excluded from clean FPS comparisons. The
camp candidate timing attempts were also contended. The measured graphics
renderer and scene-capture object are byte-identical in the combined build;
this provenance does not turn contended measurements into clean results.

## Deployment

Installed `dist/DrakanPC/drakan.exe`, SHA-256
`d52cf5eebf34f44adfabcc64f2b9e623da9e4b35a851538c82153f4346f38228`.
The prior menu/inventory build (`3ab445e6...`), its configuration and build
receipt are backed up in `build/water-edges-20261019/deployment-backup`.
Settings, launchers, artwork and user saves are preserved. Quality DLSS,
neural rendering, modern water, ray reflections and ray shadows remain enabled.
See `deployment.json` for the final installed smoke receipt.
