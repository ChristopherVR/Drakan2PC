# VU1 loops several iterations per SIMD step (23 October 2026)

Follow-up to [the functional VU1 engine](vu1-functional-engine.md), [the CPU ceiling](cpu-ceiling.md) and
[VU1 on several cores](vu1-multicore.md) (exact but slower). With effects off, Surdana ran at ~238 world FPS with the
single VU1 worker 96–100 % busy (~756 M pairs/s). Goal: run the per-vertex loops several vertices at a time with
AVX-512 / AVX2, bit-identical to the functional engine.

## Profile first

- Replay (`DRAKAN_VU_BENCH`, 20,000 Surdana runs): the one-block loops of the image are ~40 % of the engine's samples.
  The recording mixes the 0x26E0 program with the 0x0000 program (loops at 0x0D10, 0x04D8, 0x0950, 0x0A70, 0x0BD0).
- In game (host sampler, VU worker): the 0x2ED0 vertex loop of the lit path was the top block (9.4 %), then 0x0FA8
  (6.3 %, part of a multi-block packet loop), the 0x2DA0 lighting loop (4.2 %), the 0x2990 bounding-box clip loop
  (3.3 %), the MSCAL entry and many small blocks. Lit batches are short: ~8 iterations per loop entry.

## Design (`vu/aot/ps2_vu1_func.cpp`, namespace `lanes`; packs in `vu/aot/ps2_vu1_lanes.h`)

A generic lane executor of the decoded pair table, no transcribed microcode:

- **Which loops.** Any block whose branch (in its last-but-one pair) goes back to its own leader. `buildPlan` derives
  everything from `kInfo` (the compile-time decoded image) and rejects what it cannot prove.
- **Lanes are iterations.** Each 128-bit lane holds one iteration's x/y/z/w (AVX-512: 4 iterations per `__m512`,
  AVX2: 2 per `__m256`). Upper and lower ops become nodes.
- **Reaching definitions per component.** An operand component comes from an earlier pair of the same iteration
  (distance 0, same lane), from the last writer of the previous iteration (distance 1: lane j reads lane j−1, lane 0 the
  value carried from the previous step or the register file), or is invariant. This turns the software-pipelined
  loops (vertex loaded in iteration i, transformed in i+1, divided and stored in i+2/i+3) into plain lane-parallel
  code. Nodes run in a topological order of these edges; a cycle is a real recurrence and rejects the loop.
- **VI.** Only constant self-increments (IADDIU/ISUBIU/IADDI, LQI/SQI), so every address and the branch register are
  affine in the iteration number. The branch condition is evaluated per iteration up front. A VI write in the delay
  slot leaves the same branch backup (`State::bkReg`) as the scalar blocks.
- **Q.** At most one DIV and a body of at least 7 pairs (no DIV stalls). A Q reader sees its own iteration's DIV
  (≥7 pairs later) or the previous one; the run starts only if a pending Q is ready before the first Q access. The
  final Q / pending-Q state is rebuilt as the scalar blocks leave it.
- **Flags.** FMACs that an FMAND may observe (`kMacObserved`), CLIP and flag reads reject the loop. The final MAC is
  the last FMAC's of the exit iteration: it is computed with the scalar `fmac` on that lane.
- **Exact FMAC values.** `Vu1Lanes::fmacBad` marks components where `Vu1Step::calculateFmac` would not return the
  plain SSE result (neither its fast path nor `flagsKnownClasses`: non-ordinary results, zero results without a zero
  operand, non-ordinary products). Those lanes are recomputed by the scalar `fmac` (exact slow path) and reinserted.
  `ps2xRuntime/tests/vu_lanes_tests.cpp` checks `fmacBad` against the scalar classification (and every pack op against
  its SSE counterpart) for both widths, round-to-zero and nearest; a deliberately dropped term fails it.
- **Operands** are normalized exactly where the scalar engine normalizes (`kNorm`), pushed into the sources:
  invariants and carried values once per run, FMAC/MAX/MINI/ITOF/ABS/DIV results never.
- **Memory, checked per run.** All addresses of the iterations about to run must lie in VU memory without wrapping.
  A quadword stored by them may only be loaded before its store in program order (same-stride streams compared by
  residue: in-place loops such as 0x04D8 pass), and no quadword is stored twice (the interleaved vertex outputs at
  0..3(vi06) with vi06 += 4 pass). Stores are performed after each step, in program order.
- **Run shape.** From the loop head: whole steps of W, then one masked partial step for the last 2–3 iterations, the
  exit iteration (branch not taken) included unless the cycle budget ends the run first. The block then continues at
  its successor. Otherwise the scalar blocks run the remaining iterations without another attempt (`State::laneHold`).
- **Dispatch.** `Vu1Lanes::detect()` (CPUID + XGETBV) picks AVX-512F, else AVX2, else none. MSVC compiles the
  intrinsics without `/arch`; other compilers build the packs only when the translation unit targets the ISA.
  `DRAKAN_VU_LANES=0` disables, `=2` forces AVX2, `DRAKAN_VU_LANES_STATS=1` prints per-loop counters.

Eligible loops in this image: 0x01B8, 0x04D8, 0x0950, 0x0A70, 0x0BD0, 0x0D10, 0x2B80 (unlit vertices), 0x2DA0
(lighting), 0x2ED0 (lit vertices). Rejected: 0x0318/0x03F0 (`FTOI vf08.xyzw` feeds its own unused x/y/z back: the exact
final register needs that recurrence), 0x05E0 (VI advanced by another VI register), 0x07E8 (recurrence), 0x0DB0 (MAC
read), 0x25B8 (25 pairs), 0x2990 (CLIP/FCGET/FMAND).

Codegen: the 0x2B80 step is ~320 instructions for 4 iterations (~80 per iteration; the scalar blocks spend several
hundred). `_mm512_kor`/`_kortestz` made MSVC 19.4x crash (C1001); the packs use masked compares and plain mask
arithmetic instead.

## Correctness

- `DRAKAN_VU_BENCH` (20,000 Surdana runs, registers, Q/P/I, MAC/clip, pc/branch state, VU memory, every GIF packet):
  `check: 20000 runs, 0 mismatches` with AVX-512 and with AVX2, after every change.
- `vu_lanes.exact` (new), the other VIF/VU standalone tests (9/9) and the `runtime-tests` preset (43 pass, 4
  data-dependent skips) pass.
- Screenshots (`play_run --shots 900,1500`, lanes on vs `DRAKAN_VU_LANES=0`): the same scene; frame 1500 differs in
  0.2 % of pixels (foliage), frame 900 by a small camera drift (frame-numbered shots land at different game times).

## Performance

Replay best of 30–40, interleaved, quiet machine:

| | ms / 20,000 runs | ns / pair |
| --- | ---: | ---: |
| lanes off | 15.29–15.63 | 0.98–1.00 |
| AVX2 (2 iterations) | 13.11 | 0.84 |
| AVX-512 (4 iterations) | 12.11–12.81 | 0.78–0.82 |

Host sampler on the replay: the eligible loops took 39.6 % of 15.25 ms (≈6.0 ms) without lanes and 23.0 % of 12.90 ms
(≈3.0 ms) with AVX-512 lanes, i.e. **~2.0× on the loops** including entry, partial steps and the exit iteration. The
whole replay gains less because ~60 % of its pairs are outside these loops.

In game (Surdana still view, effects off, `GSR_TEXREPLACE=0`, interleaved; the machine was shared, rounds with other
games running are marked):

| Round | main (baseline exe) | lanes off | lanes (AVX-512) |
| --- | ---: | ---: | ---: |
| quiet | – | 238 FPS, 755 M pairs/s | 263 FPS, 835 M pairs/s |
| 2 | 208 | 203 | 234 |
| 1, 3 (contended) | 133, 123 | 204, 185 | 219, 210 |

Lanes are +10–15 % world FPS and +10 % VU1 throughput over lanes off / main in every round. In game the lit loops
average ~8 iterations per entry (80 M iterations in 10 M entries per run), so per-entry setup is a large part of what
remains of them.

## Remaining ceiling

The VU1 worker is still 95–99 % busy. After this change the loops are ~7 % of its samples; the rest is spread over
non-loop blocks: per-object setup and the 0x2990 clip-flag loop of the 0x26E0 program, the 0x0F48–0x0FD0 multi-block
packet loop and the 0x0318/0x03F0/0x05E0 loops of the 0x0000 program, VIF unpack and the worker's job loop. A larger
step needs either faster scalar blocks in general, lanes for loops with flag reads (CLIP + FCGET + FMAND with a 4-pair
visibility window), or VU work on more cores (see vu1-multicore.md for why that lost so far).

## Risks

- The packs rely on MSVC accepting AVX/AVX-512 intrinsics without `/arch`; clang/GCC builds get the lane path only with
  `-mavx2` / `-mavx512f` for that translation unit (otherwise `detect()` returns 0 and nothing changes).
- Memory safety is decided per run from the VI values; a new access pattern that fails the checks falls back to the
  scalar blocks (counted as `memory` in the stats), it cannot run in lanes incorrectly.
- Default: on (widest supported width). `DRAKAN_VU_LANES=0` restores the previous behaviour.
