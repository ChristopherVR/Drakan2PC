# Cheaper anisotropic filtering with the same picture (2026-11-17)

Follow-up of [perf-frame-limiter.md](../2026-11-16/perf-frame-limiter.md): at the owner's 4K settings (scale 8, render
target 5120x4096, GPU vertex path, modern water off) Surdana was GPU-bound at 5.26 to 5.33 ms and `anisotropic = 1`
took 17 % off. Goal: a cheaper filter whose picture is the same, measured as a number.

## 1. What the filter did and what changed

`sampleTexture` (`gs_shaders.h`, WGSL twin in `gs_shaders_wgsl.h`) emulates the GS texture unit in the pixel shader.
At `anisotropic = 16` a minified, mip-mapped, Q-driven pixel takes `ceil(major / minor)` taps (up to 16) along the
footprint's major axis at the level `log2(major / taps)`. The tap count was already adaptive. Each tap is one
`sampleMip`: one bilinear lookup per mip level (two for the trilinear modes), and every bilinear lookup was four
`Load`s, each through `wrapCN` (the GS wrap mode in logical texels, then the upscale factor N). So a trilinear tap was
8 `Load`s plus the wrap arithmetic, up to 128 `Load`s per pixel.

New (`hardware_texture_filter`, default on): `sampleLvl` hands each bilinear lookup to the texture unit
(`sampleLvlHw`, one `SampleLevel` at the integer level with a linear, mip-point, wrap sampler, `s4` / binding 18).
Tap positions, tap count, level choice and the trilinear blend between the two levels stay in the shader, unchanged.
Why the result is the same:

* Positions: the shader's bilinear lookup reads texels `floor(p - 0.5)` and `+1` with weights `frac(p - 0.5)` in the
  level's texels `p = uv * N / 2^level`; the texture unit does the same at `p / size` (sizes are powers of two, so the
  division is exact).
* REPEAT: the bound texture's level is the logical level times N, so wrapping the copy's texels is wrapping the
  logical cells; the sampler's wrap equals `wrapCN` mode 0.
* CLAMP and REGION_CLAMP: clamping both texel indices of the footprint to `[lo, hi]` is the same as clamping `p` to
  `[lo + 0.5, hi + 0.5 - 1]` first (the outer weight becomes 0 at the edge). Done per axis and per level in the
  shader.
* REGION_REPEAT (`(c & MINU) | MAXU`) has no sampler equivalent: those draws keep the four `Load`s. Point sampling
  keeps its single `Load`.
* The weights: the texture unit uses its own fixed-point fraction (8 bits on current GPUs), the shader a float. At a
  hard texel edge that is up to about half a step of 255 per lookup; after the 255 rounding of `sampleTexture`'s caller
  a texel channel differs by at most 1. Where the alpha sits exactly at an alpha-test threshold that one step can flip
  the test (single isolated pixels in foliage, section 3).

Also checked in the generated code: FXC keeps the hardware lookup and the four `Load`s in separate branches
(`if_nz` ... `sample_l` / `if_z` ... `ld`), so the old path is not executed alongside.

The old shader stays selectable live: `[video] hardware_texture_filter = 0` (settings store key, so
`--cycle video.hardware_texture_filter=0,1` switches it inside one run), env `GSR_HW_FILTER=0|1`. The flag is
`cb_d.x` bit 14 of the draw constants.

## 2. Ideas measured and dropped

A scratch GPU microbenchmark (`build/aniso-probe/microbench.cpp` of the worktree, not tracked) compiles the real
`kHlslMain` `PSMain`, draws an oblique ground plane over a whole 5120x4096 target with an 8x HD texture (2048x2048,
full chain, `hd_texture_mips`, trilinear, repeat), times it with GPU timestamps (10 draws, median of 7) and compares the
picture with the old shader. RTX 5080, nothing else on the GPU.

| variant | ms per 5120x4096 plane | picture against the old shader |
| --- | --- | --- |
| shader taps (old), aniso 16 | 3.47 | reference |
| **hardware bilinear taps (new), aniso 16** | **1.68** | max 1, mean 0.05, 4.6 % of pixels off by 1 |
| hardware taps issued in pairs (more fetches in flight) | 1.71 | same as new; no gain, dropped |
| hardware taps on one mip level (no trilinear blend) | 1.46 | 5.1 % of pixels off by more than 1, max 17: dropped |
| one tap at the anisotropic level (diagnostic) | 1.10 | not a filter (33 % off by more than 1) |
| hardware anisotropic sampler (`SampleGrad`, 16x) | 1.10 | 18 % of pixels off by more than 1, max 21: different picture, dropped |
| shader taps, aniso off | 1.34 | the owner's picture setting, not an option |
| hardware taps, aniso off | 1.08 | |

On this worst case the anisotropic overhead (aniso 16 minus aniso off) falls from 2.13 to 0.60 ms (-72 %), and the
isotropic lookups gain too (1.34 to 1.08 ms). What is left is the tap count itself: the cheaper forms that remain
(fewer taps, one level, the hardware's own footprint) all change the picture.

## 3. Picture

Exact comparison through `gsr_replay` (D3D11, scale 8, HD textures, aniso 16) of GS captures taken at the scene poses
(`PS2X_GS_CAPTURE` from boot, owner's ini with the CPU vertex path so the GIF stream is complete), both shaders on the
same frames, 4096x4096 GS frames:

| scene, frames | mean abs | pixels differing | > 2 | > 8 | p99 / p99.9 / p99.99 | max |
| --- | --- | --- | --- | --- | --- | --- |
| Surdana start, 1880 to 1920 (5) | 0.0098 | 2.76 % | 0.0010 % | 0.00097 % (162 px) | 1 / 1 / 1 | 126 |
| Wartok Canyons, 1570 to 1610 (5) | 0.0041 | 1.12 % | 0.0003 % | 0.00021 % | 1 / 1 / 1 | 87 |

Every difference above 2 is a single isolated pixel at an alpha-tested edge in tree foliage (map
`replay-surdana-diff-over-8-mask.png`): an alpha one step away from the threshold flips the test. The ground, roads,
walls and canyon faces at grazing angles differ by at most 1, the same in every frame (no shimmer: the per-frame
difference is the weight rounding, not a different footprint), and looked at side by side they are the same picture,
equally sharp. Evidence in `build/review/aniso-filter/` of the main checkout: `replay-*-sheet.png` (old | new |
difference x8, plus 1:1 crops of distant ground), the full frames `replay-*-shader.png` / `replay-*-hardware.png`,
`microbench-far-ground-sheet.png` (old | new | difference x64 | aniso off).

## 4. GPU time in the game

The machine was not in the state of the 16 November measurement: the session is shown through Remote Desktop, the 4K
screen hangs off the integrated AMD GPU and the RTX 5080 drives a 1080p screen. Uncapped frames were then bound by the
presentation (Present 6.5 ms per call, the same 9.7 to 10.1 ms frame with aniso 16 and aniso 1, borderless or a 1280x960
window on either screen), so frame time did not show GPU work. Measured instead: GPU busy time per frame at a fixed
60 fps cap (`frame_limit = 60`, otherwise the owner's ini with `vertex_processing = gpu` and `modern_water = 0`),
`nvidia-smi` utilisation sampled every 100 ms inside each 12 s window of the live toggle, busy ms = utilisation x
16.67 ms (`build/aniso-probe/gpu_util_bench.py`, not tracked). 3 repeats per scene, order rotated, no other game,
compiler or replay tool before or during a run except where noted.

| scene | old shader taps | new hardware taps | change |
| --- | --- | --- | --- |
| Surdana start | 9.19 ms (9.17 to 9.24) | 7.80 ms (7.68 to 7.84) | -1.39 ms, -15 % (3 of 3, ranges disjoint) |
| Wartok Canyons | 15.31 ms (14.85 to 16.33) | 13.16 ms (12.92 to 13.69) | -2.15 ms, -14 % (3 of 3) |

The canyon old-shader windows reached 92 to 98 % utilisation and fell to 52.7 and 58.4 fps in two runs, so the old
figure is a lower bound and the saving at least that. Its first run had a replay tool beside it (another agent).

Same method, Surdana, aniso 16 against 1 (2 runs per filter): old taps 9.17 / 9.76 against 8.04 / 8.24 ms, new taps
8.19 / 7.75 against 7.20 / 7.83 ms. The anisotropic part falls from about 1.3 to about 0.45 ms per frame.

Frame time could not be measured GPU-bound here. Uncapped runs (presentation-bound, owner's borderless ini, 3 repeats):
Surdana 8.55 to 8.26 ms (GPU total); canyons 14.72 to 12.84 ms in its first run, which had my replay tool and a
compiler beside it (the other two ran beside another agent's game and are not used). On the 16 November
setup, where the frame was the GPU's 5.3 ms, the busy-time saving should show as frame time; that is to be confirmed
when the owner's setup is back.

## 5. Changes

* `gs_shaders.h` / `gs_shaders_wgsl.h`: `sampleLvlHw`, used by `sampleLvl` for bilinear lookups when `cb_d.x` bit 14 is
  set and neither axis is REGION_REPEAT. Sampler `SmpWrap` (`s4`, WGSL binding 18).
* Direct3D 11 (`d3d11_device.cpp`, `d3d11_draw.cpp`): the wrap sampler, bound with the point and linear ones.
* WebGPU (`rhi_webgpu.cpp`, `rhi.h`): a third sampler (linear, repeat) at binding 18 of every program with samplers.
* Setting `[video] hardware_texture_filter` (default 1; `Config::hardwareTextureFilter`,
  `Renderer::setHardwareTextureFilter`, live), env `GSR_HW_FILTER`; `gs_draw.inl` sets the bit for draws with a
  sampled (non render target) texture. A toggle row "Hardware texture filtering" under Textures in the settings
  pages (every settings-store key has a row, `settings_schema_test`).

## 7. Tests

| what | result |
| --- | --- |
| ctest `runtime-tests` | 205 of 206 passed before the settings row was added (the failure was `settings_schema_test`: no row for the new key); after it the 4 settings tests pass |
| `python scripts/run_tests.py` | 562 tests OK (2 skipped) |
| clang-format on the changed C/C++ files | clean |
| offline FXC compile of `PSMain` (both blend variants and the material variant) | clean |

## 6. Open

* Frame time at the owner's real setup (GPU-bound 5.3 ms of 16 November) not re-measured: the machine's display path
  was different all day.
* What remains of the anisotropic cost is the tap count at the finer level; the hardware anisotropic sampler would be
  cheaper again (1.10 against 1.68 ms on the plane) but is a different picture, so it would be an owner's choice.
* WebGPU: the WGSL twin was checked on the captured Surdana frames only (`gsr_replay_webgpu --gpu-api d3d12`, frames
  1900 and 1920): with hardware taps it is bit-identical to the D3D11 replay with hardware taps, and old against new
  gives the same statistics as D3D11. Its GPU time was not measured.
