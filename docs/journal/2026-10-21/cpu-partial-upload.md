# Partial upload coherence and Surdana validation - 2026-10-21

The consistent 144+ raw FPS goal remains unmet. These changes are private, tested candidates; the installed game, frame cap, saves, and default geometry settings were not changed.

## Change

Track complete uploaded 256-byte blocks separately from GPU-owned dirty pixels. A CPU upload can replace part of a dirty render target without reading its untouched tail back. Later GPU synchronization copies only CPU-owned block rectangles; a later CPU read merges the new blocks with the preserved GPU data before readback. Subsequent GPU draws conservatively restore GPU ownership. Fragmented transfers retain the earlier bounded buffering and fallback behavior. First raw-pointer exposure drains pending work and disables future deferral.

A second extension recognizes small contiguous block rectangles inside one page. This covers the remaining recurring combat write: CT32, base 12544, stride 1, 16x16 pixels, blocks 12544-12547. Gapped, partially written, wrapping, and unsupported layouts retain the original readback path.

## Correctness checks

The final candidate passed 13,369,344 GPU pixel comparisons in 48 cases plus 6,684,672 comparisons in 24 cases using distinct payloads per storage block: **20,054,016 pixel comparisons and 72 exact 64 KiB VRAM checks**, including alpha. GPU images were checked before readback could repair coherence and again afterward. Cases cover shifted CT32/T8 uploads in both directions, untouched tails, subsequent draws, repeated uploads, incomplete/interrupted transfers, raw pointers, unsupported CT24 transfers, small contiguous/noncontiguous writes, and scales 1 and 4.

The small-rectangle helper passed 20,000 parameter cases; all 714 accepted random rectangles were checked against exhaustive byte-address enumeration. The exact observed combat write and a noncontiguous rejection were also checked.

## Gameplay diagnostics

The movement and combat scripts use existing Level 1A Surdana fixtures. They are proxies for the reported city approach/combat issue, not a visually confirmed reproduction of the exact viewpoint. Counters below cover the last five profiling windows. They describe observed path coverage, not matched-simulation performance comparisons.

| Candidate and route | Latches sampled | Readback maps per latch | Reused GPU resources checked | Pixel mismatches |
|---|---:|---:|---:|---:|
| rb-motion-partial-a | 751 | 0.0000 | 32 | 0 |
| rb-combat-partial-a | 554 | 0.1426 | 32 | 0 |
| rb-motion-block-a | 683 | 0.0000 | 32 | 0 |
| rb-combat-block-a | 386 | 0.0155 | 32 | 0 |

The earlier buffered candidate still performed about one readback map per latch in its movement and combat samples. The partial-overlap candidate removed movement maps, but combat retained occasional 16x16 upload maps. The final small-block extension addresses that trigger. A zero count in these windows does not imply that all game readbacks are unnecessary or removed.

The final combat sample still has six upload-triggered maps across 386 latches (0.0155 per latch). Trace metadata identifies 8x8 T8 writes at blocks 15831 and 16242: each replaces only 64 of the 256 storage bytes. The remaining 192 bytes cannot be discarded. These genuine sub-block writes require finer ownership or a GPU byte-preserving merge; they are outside this candidate. The largest remaining map in these windows was 9.21 ms, so a residual stutter source remains.

## Raw FPS attempts

Profiling and GPU texture validation were disabled for these runs. All runs were contended; their averages do not establish a speedup or regression. Raw interval histograms were not collected, so these runs cannot establish 144+ FPS frame-time consistency.

| Run | Raw world FPS | Other processes, average CPU cores |
|---|---:|---:|
| raw-motion-buffered-b | 71.36 | 6.60 |
| raw-motion-partial-b | 70.20 | 6.26 |
| raw-combat-partial-b | 50.44 | 6.91 |
| raw-combat-buffered-b | 51.58 | 6.40 |
| raw-motion-block-c | 63.88 | 8.19 |
| raw-combat-block-c | 49.72 | 7.99 |

The diagnostic frame-time percentiles are preserved in the JSON records. The observed stalls were reduced at their source, but overall performance remains below the target. Once the upload maps are absent, the next investigation should remeasure VU geometry and draw/texture submission rather than continue optimizing readback counts alone. A clean benchmark and a broader gameplay image comparison remain necessary before promoting the candidate.

## Artifacts

- `analysis/experiments/partial-upload-20261021/partial-upload.patch`: block ownership, applied after the prior complete-upload candidate.
- `analysis/experiments/block-upload-20261021/block-upload.patch`: small whole-block rectangle extension, applied after the partial-upload patch.
- Both directories include renderer tests; the block directory also includes the coverage test. Both patches were applied to isolated fixtures and compared with the tested sources.
- `docs/journal/2026-10-21/data/partial-upload-20261021.json` and `docs/journal/2026-10-21/data/block-upload-20261021.json`: binary hashes, test logs, path counters, GPU reuse checks, contention measurements, and frame intervals.
- Private final executable: `build/texture-gameplay-20261021/block-upload/drakan.exe`.
- Reproduction scripts: `run-block-upload.ps1` (diagnostic) and `run-block-raw.ps1` (raw FPS), under `build/texture-gameplay-20261021`. Use fresh run names when repeating; the harness preserves private save directories.
