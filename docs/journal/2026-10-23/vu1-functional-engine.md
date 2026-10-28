# Functional VU1 engine (23 October 2026)

Follow-up to [the performance review](../2026-10-22/perf-review.md): VU1 was the ceiling of the no-effects
configuration (one worker at 100 %, 3.3 ns per instruction pair, ~3.2 M pairs per Surdana frame).

## Why a functional engine is exact for this image

The exact engine models every cycle, but only a few timing effects are observable by the game's microcode:

- Q: 15 DIV, 5 WAITQ, 31 Q readers. Every reader is at least 7 pairs after its DIV on all paths, or after a WAITQ.
- CLIP: each FCGET reads exactly 4 pairs after its CLIP (visible).
- MAC: FMAND at 0x29c0 reads the SUBz.w 4 pairs earlier; FMAND at 0x0e10 reads SUBz.w at 0x0df0 (4 pairs) while the
  ADDw.z at 0x0df8 (3 pairs) is still hidden. Only NOPs lie between, so no stall can change either outcome.
- P: ERLENG at 0x06c8, MFP at 0x0788, 24 pairs = EFU latency.
- VI branch delay: a branch right after a VI write reads the old value (one site in the image, 0x1110).
- XGKICK: the image never writes a buffer while it is being transferred, so sending the packet at issue is exact.

`analysis/scripts/vu_timing_reads.py` reproduces this list from the local microcode dump. Since stalls only lengthen
distances, a pair counter plus the DIV/WAITQ/XGKICK stalls decides every read the same way as the exact model.

## Implementation

`ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_func.cpp`, entered from `VU1Interpreter::run` when the image matches, the
exact pipelines are idle and the pc is an MSCAL entry or an MSCNT resume point. Leaves no pending state, so consecutive
runs stay on it; code it does not cover falls back to the exact loops at the committed pc.

- Each pair is decoded once at compile time into a compact table (the full constexpr decoder per template instance
  crashed the compiler).
- Blocks end at branch delay slots, E-bit delay slots or leaders; each block returns its successor function, so the
  dispatcher is `while (fn) fn = fn(s)`. The budget is checked on backward branches.
- Pair K of a block issues at `s.now + K`; stalls move the base.
- Known-normal VF/ACC lanes from a fixpoint over the pair graph (entries unknown) remove most operand normalization.
  Q, I and P always hold normalized values.
- MAC history only for FMAC ops within 16 pairs before an FMAND; the final MAC is the last FMAC result.
- `run()` sets round-toward-zero with MXCSR instead of `fesetround` (repeated A/B: no change in frame rate).

## Results

- Replay of 20,000 recorded Surdana runs against the reference interpreter: 0 mismatches (VF/VI/ACC, Q/P/I, MAC, clip,
  pc and branch state, VU memory, every GIF packet). 3.3 -> 1.06 ns/pair (305 -> 942 M pairs/s).
- Surdana, effects off, slow turn: 82 -> 198 world FPS; still view 173. VU worker (~550 M pairs/s in game: VIF unpack
  ~15 % of its samples) and GS worker near saturation.
- Exact and functional screenshots at frame 1500 match.
- Installed settings (DLSS, neural rendering, ray water/reflections): ~75 FPS, limited by the GS worker.

## Measurement hazard

Several runs in one stretch measured ~40 FPS for both engines and both rounding variants while the GPU sat at 46 % and
every CPU stage on the GS worker was slower; repeated runs later measured 74-76 for all variants. Copy-Item also kept an
old timestamp on a restored source file, so one "A/B" binary was stale; force a recompile (touch) before comparing builds.

## Remaining cost with effects on (GS worker, per frame)

Per-draw state and submission ~3.7 ms; surface-triangle reconstruction for the ray scene (`ResidentScene::match`,
`RasterToWorld::point`) ~2.4 ms producing ~61k per-frame triangles; ray scene build/upload ~4 ms; model-motion guides
~1.5 ms; kernel calls from D3D11/D3D12 interop flushes and fences.
