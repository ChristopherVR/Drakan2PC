# GPU readback stalls and gameplay validation — 2026-10-21

The consistent 144+ raw FPS goal remains unmet. No installed executable, frame cap or default geometry mode was changed. All candidates below remain private.

## Verified texture reuse

Four matched Surdana movement/combat diagnostic runs each copied 64 reused gameplay textures back from the GPU and compared every mip byte with its decoded CPU source. All 256 reuse events and 4,194,304 pixels matched. Four follow-up runs checked 32 distinct GPU resources each, including multi-mip textures, with zero mismatches. The completion records verify normal scripted quit and unchanged save hashes. These are resource-level checks, not a full baseline-versus-candidate gameplay image comparison.

## Readback causes

Added separate counters for palette loads, CPU uploads, downloads, local copies and texture coherence. The movement control performed about one palette-triggered readback per latched world frame. Replacing the fixed 64-block palette flush with exact palette storage blocks removed that trigger, but the following CPU upload still forced the readback. The footprint helper passed 20,000 cases against exhaustive pixel-address enumeration. The palette change alone does not solve the stall.

Steady combat traces include full replacements of dirty shadow-target storage: a 256x256 T8 upload and a 128x128 CT32 upload each cover 64 KiB. Other uploads only partially overlap a dirty target. For example, a movement request spans blocks 14706–14961 while the dirty CT32 target starts at 14720 and its dirty page span extends through 14975. Skipping that readback wholesale would lose the uncovered tail.

## Guarded complete-upload candidate

Implemented a bounded full-storage overwrite proof for page-aligned, non-wrapping CT32/T8 transfers. It only discards a dirty CT32 target when the incoming upload covers its entire dirty page span and no world resolve is pending. The initial candidate requires the whole payload in the same GIF submission. Follow-ups defer coherence and then buffer fragmented inputs until complete; any consuming command/read flushes old GPU data and replays the received prefix. Raw VRAM pointer exposure permanently disables buffering to preserve retained-pointer semantics.

The buffered candidate passed 4,800 storage-coverage cases and hardware renderer tests at scales 1 and 4: 3,620,864 captured-pixel comparisons plus 26 complete 65,536-byte VRAM comparisons, including alpha. Cases cover full and aliased replacement, partial/truncated input, split submissions, fragmented IMAGE data, intervening commands/reads, direct raw-pointer writes, aborted transfers and CT24 fallback. An initial direct-pointer failure was fixed before gameplay testing. Captures intentionally force alpha to 255; the raw VRAM checks validate the stored alpha.

## Gameplay outcome

Final five diagnostic windows, normalized per completed display latch:

| Candidate/run | Readback maps per latch | Complete targets discarded per latch |
|---|---:|---:|
| rb-motion-overwrite-a | 0.999 | 0.000 |
| rb-combat-overwrite-a | 0.410 | 1.373 |
| rb-motion-deferred-a | 1.000 | 0.000 |
| rb-combat-deferred-a | 1.019 | 0.188 |
| rb-motion-buffered-a | 0.999 | 0.000 |
| rb-combat-buffered-a | 1.010 | 0.284 |

The complete-overwrite path exercised in combat, but movement retained approximately one map per latch. These runs have different contention and simulation trajectories; the table demonstrates path coverage, not a comparative speedup. All enable diagnostics, and all were externally contended. No clean raw FPS improvement or consistent 144+ FPS result is established.

The buffered variant is not promoted: its extra complexity has no demonstrated gameplay benefit over the narrower same-submission guard. The remaining movement case requires finer-grained GPU/CPU memory ownership so a partial upload can preserve untouched GPU-only pixels without forcing a whole-target CPU readback. Removing correctness guards is not a valid fix.

Evidence: [readback and GPU validation](data/readback-palette-20261021.json), [complete-upload results](data/complete-upload-20261021.json). Patches and tests: [palette/readback audit](../../../analysis/experiments/readback-palette-20261021/README.md), [complete-upload experiment](../../../analysis/experiments/complete-upload-20261021/README.md).
