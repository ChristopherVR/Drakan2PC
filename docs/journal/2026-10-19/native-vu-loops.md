# Native VU loop candidates

Status: source promoted and correctness validated. Native loop modes remain opt-in because clean alternating measurements showed no repeatable gain over default block dispatch. Default dispatch measured 89.51/89.49 FPS; mode 3 measured 89.40/90.07 FPS after the ordered-queue change. The larger gains came from CPU submission and ray staging fixes, described in [CPU render pipeline performance](cpu-render-queue.md). The 144+ target remains open.

## Changes prepared

- Simplify entry-register retirement after the existing three-cycle horizon.
- Execute bounded native self loops at microprogram pair PCs 55, 155, 188 and 334.
- Reuse the proven-ready register state at backedges only for eligible Q-safe
  blocks with no pending exit writes. Check budget, stop and actual branch target
  at every iteration. Reject overlapping guest data and interpreter/context memory.
- Extend normalization proofs to the destination lanes actually consumed by FMAC
  arithmetic. Memory and integer writes still invalidate the affected lanes.

The arithmetic, instruction schedule, flags and fallback interpreter are retained.
No frame generation, resolution reduction or guest-cycle shortcuts are involved.

## Validation

| Check | Result |
| --- | --- |
| Register retirement, overwritten deadlines and ring wrap | 200,000 cases passed |
| Native loop state, data and packets | 32,768 comparisons passed |
| Loop bounds | 1, 2, 32 and 256 iterations |
| Budget and resume coverage | Budgets 1â€“256; pending DIV/SQRT/RSQRT/VF/ACC/VI; resumed tails |
| Selected-lane normalization | 200,000 randomized pairs, four host rounding modes, passed |
| Original normalization proof | 200,000 randomized pairs, passed |
| Scalar and flag timing | 8,192 comparisons passed |
| Recorded reference | 24,000 runs across three modes and recovery enabled/disabled; zero mismatches |

Static normalization skips increase from 235/757 to 260/757 with the extended
proof. The experimental steady path uses that extension only in the four loops.
This count does not establish a throughput improvement.

Code-size inspection sums all chained Windows x64 unwind fragments. Measuring
only a function's first fragment understates several existing compiled bodies.

## Review and measurements

The authoritative source is now in the canonical runtime patch. The earlier
V2 proposal at `patches/proposed/native-vu-loops-20261019.patch` is retained as a
historical experiment and does not describe the current full build.

| Control | Behavior |
| --- | --- |
| Unset / `DRAKAN_VU_NATIVE_LOOPS=0` | Default block dispatch and simplified retirement |
| `DRAKAN_VU_NATIVE_LOOPS=1` | Bounded native loops with full entry checks |
| `DRAKAN_VU_NATIVE_LOOPS=2` | Proven-ready backedges and selected-lane proof |
| `DRAKAN_VU_NATIVE_LOOPS=3` | Mode 2 plus the checked PC55 memory-fill loop |
| `DRAKAN_VU_NATIVE_LOOP_LIMIT` | Default 32, clamped to 1–256; exact budget and stop checks |

The fill loop passed 100,000 independent scalar comparisons and 17 fallback
checks, plus the full-state native-loop tests and recorded reference checks.

Current deployment evidence: `docs/journal/2026-10-19/data/cpu-render-queue-20261019.json`.
Historical V2 validation remains in `docs/journal/2026-10-19/data/native-vu-loops-20261019.json`.
