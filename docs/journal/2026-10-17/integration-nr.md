# Neural rendering and Quick Inventory integration — 17 October 2026

## Changes

The native runtime includes the Quick Inventory fix, the continuous D3D11/D3D12
NR bridge, true engine camera capture, ordered world completion before HUD
callbacks, GPU guide conversion and a separate live DLSS 5 switch in F1 Video.
The signed user-supplied NR bundle is installed locally in `dist/DrakanPC/nr`.
No vendor binaries are tracked or published.

Runtime source commit: `4897134`. Reproducible tool changes are in fork patches
0042–0051; the renderer, guide implementation and settings live in the outer
repository. Public Streamline headers are fetched from the pinned official
commit with `tools/PS2Recomp/nr_prototype/fetch-public-headers.ps1`.

Quick Inventory's keyboard callback used a synchronous nested guest call which
cannot yield at its native completion loop. Keyboard input now reaches the native
pad handler, including live controller schemes, release and ownership changes.

NR uses feature 1004 with the user's signed SL 2.13 / NGX 310.8 bundle. The private
options layout is inferred and pinned to those exact binary hashes; it is not an
official NR SDK header. The public manual-hooking route upgrades D3D12 presentation;
no private Present callback, driver patch or caller spoof is used.

## Verification

- Settings: 205 checks, zero failures. Quick Inventory bridge: 10 checks, zero failures.
- Final guide sources: WARP checks passed, with no D3D11 debug warnings/errors;
  formats, known camera translation/yaw, static and jitter-only zero motion,
  perspective depths, finite/HDR guards, large world coordinates, resize,
  context restoration and preservation of all 256 destination alpha values.
- Continuous isolated backend: 72 neural evaluations, 96 presents, 96 latency
  waits, repeated toggles, resize, output preservation and same-window reinitialization.
- `build/t/nr-deployment-combined`: actual game world, inventory equip sequence,
  attacks, held keyboard Quick Inventory/release, native quick selection, repeated
  NR off/on transitions. Script quit at frame 1340, normal exit. Four successful
  Short Swish attack events occurred at 45.366, 47.365, 49.365 and 55.032 seconds.
  Final input was neutral. User-save hashes matched.
- Actual game output readback: **12,531,930 / 12,582,912 RGB channels changed** in
  the first 2048x2048 neural frame, **zero nonfinite RGB values**. Evaluation
  completed on the GPU before output was composed. The camera-far unset-value
  warning from the earlier candidate was corrected.
- Earlier combined captures at `build/t/nr-combined-equipped/window_*.png` were
  inspected: the scene and native HUD were visible without a black/corrupt output.
  This is not a controlled image-quality comparison or a speed benchmark.
- The renderer also builds with both NVIDIA modules disabled (`build/nr_off_check`).

The default-off and missing-bundle smoke results and final executable identity
are recorded with deployment in `dist/DrakanPC/BUILD_INFO.json`. Tests use private
ELF/memory-card copies. Audio events/SPU activity are instrumented; no listening
review or live-target damage/parry/combo verification is claimed.

## Deployed build

`dist/DrakanPC/drakan.exe` SHA256:
`1081C498705511A1CAE4547C1996B42212529A60E128C2479B70EF5BD2733568`.
The final candidate recorded **197 successful NR evaluations, zero failed
evaluations**, all four scheduled off/on transitions and normal shutdown.
Default-off direct Quick Inventory quit at frame 649; the absent-bundle boot
quit at frame 180. All three runs used this exact executable hash and exited 0.

The previous exe/configuration/receipt was backed up to
`build/release_backups/20261017-182723-nr`. Both existing config files remained
byte-identical after deployment. The runtime bundle's copied hashes were checked.
The deployment changed no saves or texture packs.

## Use and limits

F1 in gameplay → Video → NVIDIA DLSS → **DLSS 5 neural rendering**. Off is the
default; existing configs and saves are preserved. The key is
`[video] dlss_neural = 0|1`; the bundle defaults to `<exe>/nr` and can be selected
with `dlss_neural_runtime` / `DRAKAN_DLSS_NEURAL_DIR`.

Motion guides cover camera movement, not independent animation/particles.
Fast objects and transparency can show temporal artifacts. SR pauses while NR
is enabled because the earlier SR guide/alpha path is not suitable for simultaneous
use. No Frame Generation or Ray Reconstruction is added. Enemy damage, parry,
combo timing, bow/magic, Arokh, every scene, long runs and device-loss recovery
are not fully verified. Existing ShowCursor/LNK4088 linker warnings remain.

The standard C++ camera packet is separate from the Windows/NVIDIA implementation;
the future browser renderer can leave NR unavailable. No browser build is claimed.
