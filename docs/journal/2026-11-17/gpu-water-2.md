# Modern water with the GPU vertex path, second round: the GPU cost of the GPU surface path (2026-11-17)

Follows [gpu-water.md](gpu-water.md) (its section 5, "What remains"). Goal: cut the GPU cost of
`GSR_VERTEX_SURFACE=gpu` so it can become the default with modern water on, then move more of the `SurfaceBuilder`
and `RayWater` work to the GPU. The session was cut short by the owner's wrap-up; this note records what was done,
what was proven, what was measured and what was not. Evidence: `build/review/gpu-water-2/` in the main checkout
(untracked).

**Outcome.** Nothing changes by default. `GSR_VERTEX_SURFACE=gpu` stays opt-in. Inside it (and inside the read-back
mode `GSR_VERTEX_SURFACE=1`) the surface stream is now written by the batch's first colour pass instead of a second
draw (`GSR_SURFACE_SO_FUSED`, on within those modes, proven exact). An integer form of the kernel's float division
(`GSR_SURFACE_GPU_FAST_DIV=1`) is exact but gave no measurable gain, so it is opt-in. Two other attempts were
dropped (sections 4 and 5).

## Machine and display

The 4K panel (3840x2160, primary, 150 % scaling) is driven by the AMD integrated GPU; the RTX 5080 drives the
1920x1080 secondary display. The session was the local console, not Remote Desktop. A 4K borderless game window on
the primary display is therefore presented across adapters, so uncapped frame times at the owner's settings are
not the RTX 5080's own; GPU pass times (`GSR_GPU_PASS_TIMING`, `GSR_RAY_GPU_TIMING`) and CPU per world frame are the
numbers to trust. Another agent's benchmark and a game from the main checkout ran during all timed runs (see
section 6).

## 1. The colour pass streams the surfaces out (GSR_SURFACE_SO_FUSED, on in the surface modes)

Before, a streamed strip batch was drawn twice: once through `VSProgramRaw` with a stream-output stage and no
rasterised stream (only for the stream), then through `VSProgram` for colour. Now `VSProgramSurface`
(`vertex_program_hlsl.h`) returns `VSProgram`'s outputs followed by the raw words of the same `vpEvaluate`
(`RAWPLACE`, `RAWCOLOUR`, `RAWSTQ`); the renderer makes a stream-output object from it whose stream 0 goes on to the
rasteriser (`buildSurfaceProgramShader`, `d3d11_vertex_program.cpp`), and the batch's first colour pass binds it
(`drawProgramVertices`). Stream output keeps primitive order, so the stream holds the same triangle list. The pixel
shaders read only the leading `VSOut` part. `GSR_SURFACE_SO_FUSED=0` keeps the separate pass; `GSR_SURFACE_GPU_TOGGLE=<s>`
switches between them every s seconds inside one run (`[surface-gpu] now old|new`).

Proof:

* `gsr_test_vertex_program` (new comparison `surfaceEntryDiffers`): `VSProgramSurface` through a stream-output
  object with all seven outputs, and through the renderer's object (raw outputs only, stream 0 rasterised), against
  `VSProgram` and `VSProgramRaw` on the same 600,000 test vertices: 0 differ, with both arithmetic libraries
  (doubles and limbs).
* In game, `GSR_SURFACE_GPU_VERIFY=1` (every world's GPU-built slots read back and compared with `reconstructDraw`
  on the CPU-evaluated vertices, so it checks the stream end to end), owner's settings with `vertex_processing =
  gpu`, Surdana start, this build with the fused pass and the integer division on: 420 worlds, 14,929,272 slots
  (14,833,642 triangles) compared, **0 differ**, 0 stream overflows, 0 draws on the CPU path
  (`verify-surdana/verify-0/drakan.log`). The screenshot of that run (`verify-surdana/verify-0/b_gpu_0.png`) shows
  the usual Surdana start with HD textures.

Not measured: its effect on GPU and frame time. The only in-run toggle taken (`toggle-surdana-s8`) lost the ray pass
to a device removal (section 5) before the strip shaders were ready, so its two phases ran without water.

## 2. The kernel's float divisions in integers (GSR_SURFACE_GPU_FAST_DIV=1, opt-in)

`fdiv` (`surface_gpu_hlsl.h`): for normal operands and a normal quotient, a long division of the significands in
integers (eight bits per step) and rounding to nearest even, the vertex programs' `vpDivideNearest` with signs;
everything else (zero, subnormal, infinite or NaN operands, a quotient that would be subnormal or overflow) through
the double division as before. A double quotient rounded to float is the correctly rounded float quotient
(53 >= 2 * 24 + 2), so both forms give the same bits. The streamed depth word is converted to float in integers too
(`utofNearest`).

Proof (`gsr_test_surface_gpu`, extended):

| check | result |
| --- | --- |
| synthetic draws against the CPU path (now with 64 sliver resident triangles) | 58,070 slots, 20,743 appended, 0 differ; 253 observe records equal to the CPU path's calls |
| integer-division kernel against double-division kernel, same draws | 0 of 2,148,590 output words differ |
| `fdiv` on 1,048,576 operand pairs (similar exponents, quotients next to overflow and subnormal, mantissa extremes, exact quotients, zero / subnormal / infinite / NaN operands) | the two forms differ in 0; 836,285 normal quotients against the CPU's float division: 0 differ |

Timing (unit test, RTX 5080, best of 9): 0.271 ms with the integer division, 0.266 ms with the double division for
58,070 slots: **no gain**. The divisions are not what the kernel spends its time on. Experiments on the same test
(not kept): without the resident match 0.165 ms; without the match and with the un-projection in float 0.117 ms.
In game the kernel's own pass (`surfaces`, new in `GSR_GPU_PASS_TIMING`) measured 0.19 ms per world in Surdana.

## 3. Where the GPU time of the GPU path goes (Surdana, scale 8, partial)

`bench-surdana-s8` (`scene_ab_bench.py --cycle video.vertex_processing=cpu,gpu`, variants `yday` = first
implementation and `new` = this build, `GSR_RAY_GPU_TIMING=1`), three runs before it was stopped; frame times are
not usable (16 to 30 ms against 8.4 ms yesterday: two other games and another benchmark ran). Medians per world:

| run | mode | D3D12 build (copies, BLAS, TLAS) ms | D3D12 total ms | kernel ms | EE | VU1 worker | GS worker | SurfaceBuilder | RayWater | all other threads |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| yday-0 | cpu | 0.762 | 1.576 | | 7.00 | 7.58 | 7.93 | 5.90 | 5.38 | 34.91 |
| yday-0 | gpu | 1.010 | 1.823 | 0.196 | 7.18 | 5.40 | 7.05 | 4.43 | 3.00 | 31.99 |
| new-0 | cpu | 0.765 | 1.577 | | 6.42 | 7.39 | 7.55 | 5.78 | 5.30 | 35.78 |
| new-0 | gpu | 1.003 | 1.817 | 0.189 | 6.96 | 5.22 | 6.01 | 3.47 | 3.40 | 24.80 |
| new-1 | cpu | 0.766 | 1.574 | | 7.12 | 7.76 | 9.05 | 5.24 | 5.97 | 33.00 |
| new-1 | gpu | 1.010 | 1.823 | 0.189 | 7.38 | 5.51 | 7.42 | 3.56 | 4.07 | 30.65 |

CPU columns are milliseconds per world frame on a loaded machine (yesterday's quiet numbers were about half), so
read them as ratios: the GPU path again takes about a third off the `SurfaceBuilder` and `RayWater` threads and about
2 ms off the VU1 worker per world. On the GPU the D3D12 setup of the ray pass grows by 0.24 ms (the texture-to-buffer
copy of the slots, the attribute copy into the material buffer and the extra BLAS build) and the kernel adds
0.19 ms: about 0.43 ms, the same as yesterday's `guides` growth (2.90 -> 3.33 ms). So the next GPU cut is in the
D3D12 setup, not in the kernel.

## 4. Dropped: resident-match sub-cells

Splitting each 2048-unit match cell into 256-unit sub-cells (an entry listed in every sub-cell its tolerance region
reaches) was exact in the unit test, but in Surdana the resident triangles are large: the table went from 65,088
cells and 141,133 entries (at most 10 per cell) to 4,165,632 cells and 9,032,512 entries, built in 540 ms, and the
cells are short already. Removed. The renderer now logs the table once per rebuild (`[gsr] gpu surfaces: resident
match table ...`, 8.5 ms in Surdana).

## 5. Dropped: overlapping the frame BLAS builds

Each frame BLAS build (three groups and the GPU slots) with its own scratch buffer and one UAV barrier after all of
them, instead of one shared scratch and two barriers per build. In both runs with it the D3D12 device was removed
(`0x887a0006`, `toggle-surdana-s8/new-0/drakan.log`, pass 4 and pass 11), the second time before any GPU slot existed,
so the overlap is the likely cause; the run that would have confirmed it was stopped at the wrap-up. Removed; the
code is not in the commit.

## 6. Not measured

* Frame time of any change on a quiet machine, the 1 % and 0.1 % lows, DLSS quality, scale 2 or 4, the Surdana town
  square and a second water scene: every timed run shared the machine with another agent's benchmark and a game from
  the main checkout, and the session ended before a clean set.
* Step 2 of the plan (the water-state draws and the remaining draws' reconstruction on the GPU, instance and TLAS
  preparation) was not started; nothing was profiled on the `SurfaceBuilder` and `RayWater` threads beyond the
  per-thread totals above.

## 7. Next

* The D3D12 setup of the GPU slots (+0.24 ms): let the kernel's attributes land in the material buffer without the
  second copy (start the attribute part on a row boundary and copy those rows straight into the material buffer at a
  512-byte aligned slot base), and build the slots' BLAS from active slots only.
* The kernel (0.19 ms): the resident match (about 40 % in the unit test) and the double-precision un-projection
  (about 20 %), not the divisions.
* Time the fused colour pass inside one run with `GSR_SURFACE_GPU_TOGGLE` on a quiet machine, then the default
  question of `GSR_VERTEX_SURFACE=gpu` with water on, at scale 8 and at scale 2 or 4.
