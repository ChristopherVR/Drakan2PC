# DLSS 5 neural rendering at render resolution (2026-10-23)

Goal: make DLSS 5 neural rendering (NR) much cheaper with the user's settings (DLSS SR Quality + NR + ray water +
ray reflections, scale 4, 3840x2160 borderless, RTX 5080), as a user-selectable setting, and measure it.

## What the NR API allows (bundle sl.dlss_nr 2.13.0 / nvngx_dlssnr 310.8.0)

There is no public NR header; the facts below come from the plugin strings and a standalone timing probe that drives
`slEvaluateFeature(1004)` with synthetic inputs (throwaway, `build/nr-timing/`, not committed).

- The plugin has a *performance mode* (`DLSSNR.ScalingRatio`, `PerfQualityValue`, `setDLSSNRScalingRatio`, NGX log
  text "requested resolution %ux%u (network %ux%u)") and per-resource subrects. The bridge uses mode 6 (DLAA, 1:1).
- Mode 5 is rejected (`DLSS-NR performance mode 5 is not supported`), mode 4 fails the NGX scaling-ratio callback.
- Modes 1, 2, 3 with exact render-size input textures and an output-size output texture fail
  (`NGX evaluate feature failed 0xbad00005`, invalid parameter). The same happens for mode 6.
- With all textures output-sized and a reduced *input subrect* (modes 3 and 6), evaluation succeeds but NR only
  writes the input rectangle 1:1 (2560x1440 in the top-left of a 3840x2160 output; the rest stays zero). It does not
  upscale.
- Mode 3 with full-size input costs exactly what mode 6 costs (12.47 vs 12.46 ms at 3840x2160): no internal
  reduced-resolution network is selected.

So this bundle offers no working "NR at a scale + upscale" path; NR cost follows the number of pixels it is fed.
Probe GPU times (timestamps around `slEvaluateFeature`, synthetic content, median of 40 frames, idle GPU):

| NR input = output | GPU ms |
| --- | --- |
| 3840x2160 | 12.46 |
| 2560x1440 | 5.81 |
| 1920x1080 | 3.90 |
| 960x544 | 2.26 |

**Jitter.** A period-8 sinusoid rendered with Halton raster jitter shows the NR output keeps the input's sample
positions (output phase follows the jitter to within ~0.05 px; amplitude 0.26 of 0.30 kept). `jitterOffset` in the
constants makes no measurable difference (bit-identical results with the true jitter and with zero). NR does not
resolve jitter like DLAA, so a jittered NR output is a valid DLSS SR input.

## Implementation

`[video] dlss_neural_resolution = output | render` (env `DRAKAN_DLSS_NEURAL_RESOLUTION`, F1 → Video → NVIDIA DLSS →
*Neural rendering resolution*, live; default `output`).

- `render`: in `finalizeNeuralWorld` NR evaluates on the render-size prepared guides (color after the ray scene,
  reverse-Z depth, model motion, raster jitter) into the guides' output texture, then DLSS SR resolves that NR image.
  `sr_combined::Inputs::alphaRender` supplies the GS alpha from the pre-NR color, so the vendor output alpha is never
  used. Composition takes the SR color (NR is inside it); the NR on/off matched-compare capture is skipped in this mode.
- A failed render-size NR evaluation still presents plain SR for that frame and resets both histories, like an
  output-mode failure.
- Switching modes discards temporal worlds and resets NR, SR and motion histories.
- With SR off or DLAA, render size equals output size; `render` only changes the order (NR before DLAA).
- `GSR_NR_GPU_TIMING=1` prints `[nr-gpu]` D3D12 GPU time of each NR evaluation (average/max over 240), read back
  without extra CPU waits.

## Measurements (in game)

Surdana, standing still (`build/perf-review/still.txt` plus screenshots at 36 s / 38 s), `GSR_TEXREPLACE=0`,
`PS2X_PERF=1`, `GSR_NR_GPU_TIMING=1`. With scale 4 the world target is 2048x2048; SR Quality renders 1364x1364.

NR GPU time (`[nr-gpu]`, D3D12 timestamps around `slEvaluateFeature`, averaged over each run). It is very stable
across 13 runs per mode, including runs where the machine was busy:

| Mode | NR input | NR GPU ms (per-run averages) |
| --- | --- | --- |
| output | 2048x2048 (SR output) | 6.80 – 6.89, mean 6.85 |
| render | 1364x1364 (SR input) | 3.66 – 3.73, mean 3.68 |

So render mode removes **~3.2 ms of GPU work per frame** (−46 % of NR). The in-game ratio is smaller than the probe's
pixel ratio because NR has a fixed per-evaluation cost (the 960x544 probe still takes 2.26 ms).

World FPS (`summarize.py`, 22 s still window), 3 interleaved batches; runs flagged by a tasklist watcher for a second
`drakan.exe`, `cl.exe`/`link.exe`/`msbuild.exe`, `ffmpeg.exe` or `realesrgan` were discarded:

| Mode | Runs | Mean | Median | Best run | Best 2 s window |
| --- | --- | --- | --- | --- | --- |
| output | 11 | 61.1 | 57.9 | 79.7 | 81.2 |
| render | 12 | 63.0 | 63.4 | 90.0 | 108.4 |
| main build (output) | 3 | 65.0 | 59.0 | 77.9 | 84.8 |

FPS is **not conclusive**: "clean" runs of the same mode range from 43 to 90 FPS, and FPS follows the EE/VU1 rate
(`vu1Mpairs/s`, `ee_thread_cpu`) rather than the mode, i.e. the shared machine's CPU load dominated. Only the best
runs/windows reflect the GPU-bound case: there, render mode reaches 90 FPS average / 108 FPS peak windows where output
mode never exceeded 81–85, consistent with the 3.2 ms saving (84 FPS = 11.9 ms → ~8.7 ms ≈ 115 FPS if purely GPU-bound).
A quiet-machine, longer A/B is still needed for a firm FPS number.

## Image quality

Screenshots (local only, `build/nr-res/`): `ab1/output_0/still38.png`, `ab1/render_1/still38.png`; 1:1 crops
`build/nr-res/cmp1/zoom_trunk.png`, `zoom_tent.png`, `zoom_grass.png` (left output, right render).

- Static scene, 1:1: no new artifact class in render mode (no seams, alpha/HUD errors, colour shifts or ghosting in the
  stills). Render mode looks very close to output mode; bark and grass have slightly coarser, higher-contrast grain
  (NR adds its detail at 1364² and SR magnifies it 1.5x) and the tent fabric pattern is a little less fine.
- Numbers on the central world region (excludes HUD/FPS overlay): gradient RMS 22.6–22.9 (render) vs 22.0–22.3
  (output) ×10⁻³, Laplacian RMS 28.1–28.3 vs 27.5–28.0, mean luminance +1.2 % in render mode. Output vs render MAE
  1.8–3.4/255, the same order as run-to-run differences of one mode (0.7–2.7/255; idle animation, wind, camera drift).
- Static temporal stability (still36 vs still38, far terrain band): MAE 0.13–0.82/255 in render mode vs 0.26–0.49 in
  output mode for runs without camera drift; outliers (2–6/255) occur in both modes and are idle-camera drift.
- Not assessed: motion, disocclusion and fast camera turns. NR now sees 1364² inputs and its output is resampled by
  SR, so NR's own temporal behaviour under motion is filtered through SR history; check in play before changing the
  default.

## Recommendation

Keep `output` as the default for now and offer `render` as the performance option. Render mode halves the NR GPU
cost (−3.2 ms/frame) with stills that are very close; the FPS gain could not be confirmed on the shared machine, and
moving scenes have not been compared. If a quiet-machine A/B confirms the FPS gain and a motion check shows no new
artifacts, `render` is a reasonable default for SR Quality/Balanced/Performance.
