# VU1 AOT slices: compile cost and chunked blocks

Follow-up to [builder-resume-and-progress.md](builder-resume-and-progress.md): the four `ps2_vu1_aot_N.cpp` slices
were the one part of the player build that did not fit a 16 GB PC (up to ~17 GB each in cl), and the reason the
builder compiled them one after another.

## Where the cost is

Slice 3 (pairs 1143 to 1522) compiled with the builder's flags (9800X3D, 62 GB; runs overlapped, so the minutes are
inflated but comparable within a row):

| What is instantiated | Time | Peak working set |
| --- | --- | --- |
| everything (current code) | 20.3 min | 17.9 GB |
| per-pair fallback functions only (`registerPairs`) | 0.5 min | 1.5 GB |
| blocks only (`registerBlocks`) | 29.4 min | 12.8 GB |
| everything, blocks in chunks of 16 pairs | 13.0 min | 9.6 GB |
| everything, blocks in chunks of 8 pairs | 8.8 min | 2.1 GB |

The 380 per-pair functions are cheap; the blocks are not, although the largest has only 32 pairs (block table dumped
from `kTable`: 221 blocks, sizes 1 to 32). The cost grows faster than linearly with the pairs inlined into one
function: the sum of squared block sizes per slice (6039, 3192, 4163, 6701 for slices 0 to 3) orders the slices the same
way as their measured memory (slice 1 7.2 GB, slice 0 12.4 GB, slice 3 16.6 to 17.9 GB).

## Change

`ps2_vu1_aot_slice.inc`: a block longer than 8 pairs runs as consecutive `PS2_VU_NOINLINE` chunks of 8 (`blockChunk`),
each still the same `blockPair` calls with the decoded pair, normalised inputs and static issue cycle as compile-time
constants. Entry check, exit, flag batch and the timed-Q variant are unchanged. `PS2X_VU1_AOT_CHUNK` sets the size
(0 = whole blocks, the old code).

All four slices, chunked, compiled side by side outside MSBuild: 6.9, 7.0, 4.5 and 4.3 min, 1.77 to 1.91 GB each.

## Runtime A/B

4000 VU1 runs recorded in Surdana (`play_run.py --recipe fps_steady_wall.txt`, `DRAKAN_VU_RECORD=...,40000,4000`;
7.38 M pairs), replayed with `DRAKAN_VU_BENCH=<file>,50` (best of 50) by the current exe and by the same exe relinked
with only the four chunked slice objects replaced (`build/vu1-compile-ab`; `dist/` and `build/final-game` untouched),
alternating:

| Round | Whole blocks | Chunks of 8 |
| --- | --- | --- |
| 1 | 6.83 ms | 6.92 ms |
| 2 | 6.90 ms | 7.06 ms |
| 3 | 6.98 ms | 6.94 ms |
| 4 | 7.53 ms | 7.45 ms |
| 5 | 7.28 ms | 6.84 ms |
| mean | 7.10 ms | 7.04 ms |

No difference beyond the noise (about 1 GPairs/s either way; 30 fps needs about 83 MPairs/s). The reference-interpreter
check gives the same result for both: 4000 runs, 1 mismatch, `state.clip` on run 0, which is the recording starting
mid-stream and is there before the change too.

## Builder

The slices now fit the 2 GB per compiler process budget, so `Build-Drakan.ps1` no longer passes `-SerialVuSlices`
(the switch stays for `PS2X_VU1_AOT_CHUNK=0`), `Get-BuildParallelism` drops its 17 GB reservation (24 GB now runs
two projects of four), the low-memory warning moves from 24 GB to 8 GB, and the built-in slow-file estimate from 10
to 6 minutes. Not measured: a full player build with the new sizing, and a PC with less than 16 GB.

## Installer option

The old code stays one choice away: the setup's "Compile the VU1 microcode as whole blocks" (`/VUBLOCKS=whole`) passes
`Build-Drakan.ps1 -VuBlocks Whole`, remembered in `build/build-options.json` like the CPU choices. That gives
`Build-LocalGame.ps1 -VuChunk 0` (CMake cache `PS2X_VU1_AOT_CHUNK=0`, slices compiled one after another), the 17 GB
reservation in `Get-BuildParallelism -WholeVuBlocks`, the 24 GB warning and a 10-minute slow-file estimate. CMake only
puts the define on the slices for a non-default value, so the default build's MSBuild batches are unchanged. Switching
back and forth reconfigures and recompiles the four slices only.
