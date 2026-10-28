# Performance review: where the frame goes (22 October 2026)

Question: why does a PS2 game run at ~25 FPS on a Ryzen 7 9800X3D + RTX 5080? Measured on the installed build
(`dist/DrakanPC`, f6b5ec2), Surdana start position via `scripts/harness/play_run.py`, audio on, 3840x2160 borderless,
internal scale 4, uncapped world rate. Raw logs: `build/perf-review/` (recipes `steady.txt` = slow turn, `still.txt` =
stand still; `summarize.py` averages `[frame-uncapped]` after autostart).

## Config matrix (world FPS, slow turn)

| run | settings | world FPS |
|---|---|---|
| A | as installed: DLSS Quality + neural rendering + modern water + ray reflections | ~24-28 |
| B | A, neural rendering off | ~48 |
| C | B, modern water and ray reflections off | ~83 |
| D | C, DLSS SR off | ~87 |
| E | D, scale 2, texture upscale off | ~86 |

D = E: resolution is irrelevant; without effects the game is CPU bound. Effects add ~24 ms per frame
(neural ~15 ms, ray water ~9 ms).

## Installed settings: CPU and GPU take turns

GPU during A: 30-38 % utilisation, ~135 W (nvidia-smi). The GPU is idle two thirds of the time.

`DRAKAN_RUNTIME_PROFILE=1` (still view, A): the game thread spends ~68 % of wall time in `commitRetirement`, waiting
for the GS worker to retire the previous frame. The GS worker is never idle. `GSR_PROFILE=5`, per frame on the GS
worker (~44 ms):

| stage | ms/frame |
|---|---|
| `draw` exclusive (stalls inside D3D11 calls; 0.8 ms without effects) | 14 |
| `rayScene` (pack 4.2, triangles 2.6, BLAS/TLAS build 1.9, wait 1.7, upload 1.3, consume 1.0) | 10 |
| `neuralEvaluation` | 8 |
| texture resolve/decode/create/upscale | ~5 |
| model guides, present | ~2 |

Every frame contains full CPU<->GPU drains on that one thread:
- `ray_water.cpp` `Impl::wait()` right after `ExecuteCommandLists` (CPU blocks until the DXR work finishes);
- `gsr_nr.cpp` `submit()` -> `finishGpu()` (CPU blocks until the NR queue drains), preceded by `order11Before12()`
  (D3D11 Flush + queue wait on all earlier D3D11 work); `present()` orders 11->12 again;
- the GS bridge then holds the worker in `waitForHandoff` until the window thread has presented the frame.

So frame time = CPU work + GPU work + handoffs, not max(CPU, GPU). The ray scene (169k resident triangles) is also
re-packed and its acceleration structures rebuilt every frame on that thread.

## Without effects: VU1 is the ceiling

D, still view: ~76-87 FPS. One thread at 100 % (VU1 worker, ~240 M VU pairs/s, ~3.2 M pairs per frame), GS worker
~66 % busy (`gif` ~8.8 ms/frame), EE game code ~2-3 ms/frame. VU1 work per frame is genuine (the PS2 used ~55 % of
VU1 at 30 FPS here); at 300 FPS the exact-timing VU1 path would need ~1 G pairs/s, 4x the current single-core rate.

GS worker waste found on the way: standing still, ~8,300 texture decodes, ~3,100 upscales and ~3,100
`CreateTexture2D` per second. The game re-uploads textures into GS memory every frame; the cache is keyed by GS
address and invalidated by memory stamps, so identical re-uploads redo decode, D3D texture creation and upscaling
(~3 ms/frame). A content-hash reuse would remove most of it.

## What 300 FPS needs (in order of payoff for the installed settings)

1. Pipeline the effects: no CPU waits on the GPU in ray/NR/present (fence waits on the GPU timeline, CPU waits only
   before reusing allocators/upload buffers, 2-3 frames in flight); move ray-scene packing off the GS worker and keep
   static level BLAS across frames. Expected: installed settings toward the no-effects ceiling (~80 FPS).
2. Texture reuse by content hash on re-upload (GS worker -3 ms/frame).
3. VU1: beyond ~85 FPS needs either untimed native translation of the fixed microcode (flags/Q/P resolved at
   compile time) or high-level replacement of the geometry microprograms. Cycle-exact parallel batching was
   measured slower than serial (2026-10-19..21 journals).

## Fixes and results (same day)

Commits 4de3972 (GPU sync in the effects path) and 1f43ac7 (readbacks and texture rebuilds). Measured with the slow-turn
recipe in Surdana, installed settings unless noted (`build/perf-review/`, `summarize.py`):

| step | world FPS (still view) |
|---|---|
| start | 23.7 |
| ray water: frame slots, persistent resident BLAS, VRAM materials; NR: allocator ring, GPU-side fences | 28.0 |
| draw vertex buffer as a ring (NO_OVERWRITE; a DISCARD per draw blocked once frames overlapped) | 51.3 |
| identical texture re-uploads revalidated by raw block hash (was ~3,100 decode+create+upscale/s) | 62.5 |
| exact CLUT flush range, GPU-side aliased texture assembly, uploads into drawn targets on the GPU | ~67 |

Slow-turn recipe: installed settings 28.1 -> 76.4 FPS. GPU readbacks in gameplay went from one per frame (forcing the CPU
to wait for the whole frame's GPU work) to none. The game thread now rarely waits on the GS worker.

Findings on the way:
- The per-frame readback was not the frame-buffer patches but `loadClut` flushing a fixed 64-block range: the HUD palette
  sits five blocks before the HUD orb target (fbp 498), whose liquid is rendered every frame. The HUD atlas (tbp 15931)
  also aliases that target; it is now built from the target's blocks with GPU copies (both store raw GS words).
- Targets now count GPU writes (`RT::writes`) and remember their last draw frame; an aliased texture is rebuilt after any
  write to its target, and a stale dirty target still takes one readback instead of being patched forever.

Without effects the rate is unchanged within run-to-run noise (old exe 80.6/82.8, new 76.7/81.7, interleaved): that
configuration is bound by the VU1 worker (one thread at 100 %, ~3.2 M pairs per frame).

## VU1 ceiling (not yet changed)

Offline replay of 20,000 Surdana runs (`DRAKAN_VU_RECORD`/`DRAKAN_VU_BENCH`, 0 mismatches): 3.3 ns/pair, 305 M pairs/s.
In game the worker reaches ~225-240 M pairs/s (VIF unpack and packet handoff ~6-8 % of its samples). 98 % of runs are one
program: MSCAL 0x26e0 (6,882) and its MSCNT continuations (12,771). The image has 15 DIV and only 5 WAITQ with 31 Q
readers, so Q latency is load-bearing and an untimed translation would be wrong; MAC flags are read by only 2 FMAND, clip
flags by 8 FCGET, no EFU ops. Reaching 300 FPS needs about 1 ns/pair (or several cores): a functional VU1 translator
with a Q/stall cycle model and flags only where read, or a native (HLE) implementation of the 0x26e0 geometry program,
validated against the reference with the replay bench.
