# VU1 on several cores (23 October 2026)

> **Not merged.** The implementation was kept out of main because it is slower than the serial worker. It is preserved as `build/perf-review/vu1-par/0001-Add-an-exact-opt-in-parallel-VU1-mode-DRAKAN_VU_PAR.patch` (local, untracked; commit 62f980e on top of main 8fad672). The `DRAKAN_VU_PAR*` and `DRAKAN_VU_BENCH_TRACK` switches described below do not exist in the main build.

Follow-up to [the functional VU1 engine](vu1-functional-engine.md) and [the CPU ceiling](cpu-ceiling.md). With effects
off, Surdana runs at ~238 world FPS. The single VU1 worker is 96–100 % busy at that rate (~756 M pairs/s). Goal: spread
VU1 work over several cores without changing any observable result. Outcome: an exact parallel mode exists
(`DRAKAN_VU_PAR=1`), verified at 0 mismatches, but on this machine it is **slower** than the serial worker. It stays off
by default.

## The old batch path with the functional engine

`DRAKAN_VU_BATCH=1` (ps2_vu1_batch.cpp) cannot use the functional engine. Its workers trace dependencies inside the
exact engine (3.3 ns/pair plus tracing), and only the owner's "head" groups run functionally. Interleaved in the same
window: serial 148 FPS, batch 125 FPS. The gating now uses `vu1batch::legacyCapture`, so the batch path behaves
exactly as before.

## Design (ps2recomp/ps2xRuntime/src/lib/vu/ps2_vu1_par.cpp)

- **Units.** The owner (the VU1 worker thread) cuts the VIF1 stream into units of `DRAKAN_VU_PAR_GROUPS` (8) MSCAL
  groups. A cut goes right after the last MSCAL/MSCNT command before the next group, so the next object's uploads
  belong to the next unit.
- **Front scan.** The owner first runs each transfer's VIF1 code once on a private `PS2Memory` without the VU. VIF1
  state never depends on VU memory, so this gives the exact VIF1 state at each cut. If a mode-2 UNPACK would make ROW
  follow memory, the scan raises a desync and the work runs on the owner instead.
- **Helpers.** `DRAKAN_VU_PAR_THREADS` (3) helper threads run a unit's VIF1 slices and VU1 programs on private copies of
  VU memory and registers. Each copy is a snapshot taken when the unit is handed out (earlier units may still be
  running). GIF packets, drains, PATH3 masks and camera-hook uploads are recorded (`vu1batch::Sink`), not performed.
  PATH3 packets and renderer tasks stay ordered records in the stream.
- **Tracking.** A second instance of the functional engine (`blockFn<L, true>`) records `Vu1Func::Track`; the plain
  instance carries no tracking code. Track holds:
  - VU words and VF/VI/ACC/I/Q/P/MAC/CLIP state read before the unit wrote it (from per-block static read/write
    summaries).
  - Everything the unit wrote. VIF writes count as writes, including masked and V2/V3 lanes, which are classified
    exactly.
  - Inherited CLIP bits, followed through IAND/IOR. This is needed because the 0x29b0 FCGET reads six inherited bits
    that IAND then drops.
- **Commit, in stream order.** A unit is accepted if every inherited value it read equals the owner's current value
  (the exact state after all earlier units). Accepting copies the written words and lanes and the written registers;
  CLIP keeps inherited bits when there were fewer than four CLIPs. The recorded records are then replayed. Otherwise
  the unit runs again from the exact state, on a helper.
- **Prediction (it changes only the acceptance rate, never results).**
  - Snapshots are the owner memory plus the VIF1 uploads of units already handed out. The scan logs them, but only for
    qwords some unit has read as inherited.
  - Loads that are dead on every path (VF lane liveness fixpoint over the image; branch delay slots use the actual
    branch decision) are logged, not tracked. They only matter if the lane still holds the stale value at the end.
  - XGKICK payload words the unit never wrote are patched into the recorded packet from the owner's memory at commit.
    GIF tags stay inherited reads.

## Correctness evidence

- `DRAKAN_VU_BENCH` (20,000 Surdana runs): `check: 20000 runs, 0 mismatches` after every change. The plain engine is
  unchanged: 15.19 ms against 15.54 ms baseline (1.0 ns/pair).
- `DRAKAN_VU_PAR_CHECK=<groups>[,lags]` replays the recording as units: speculative start, validation, merge, and a
  comparison of VU memory, registers and packet hashes with the serial result. 0 mismatches for groups 1/8/32 and lags
  0–8.
- Live verifier `DRAKAN_VU_PAR_VERIFY=1` re-runs each accepted unit from the exact state and compares VU memory,
  registers, VIF1 state and records. 0 mismatches over 448k, 612k, 814k, 639k, 512k and 724k accepted units (six runs,
  across the changes above).
- Screenshots at frames 900/1500 (baseline, serial build, parallel mode) show the same scene.

## Rejections (share of units, live, 8 groups per unit)

| Prediction | Rejected | Main inherited reads |
| --- | --- | --- |
| owner memory only | 55 % | q7, q8, q0: header words a model uploads once while the cut splits the model |
| + VIF uploads of units in flight | 36 % | q8 (dead `LQ vf09, 8` at 0x27d8), q777 |
| + dynamic lazy loads | 10 % | q777: XGKICK at 0x2f48 sends `.xyz` lanes that only another path writes |
| + packet patching | 4 % | q186/q198 (look-ahead loads in the 0x0000 program), q65 |
| static dead loads instead of lazy ones | 5.6 % | q65 at 0x0f70 (delay-slot load live through E-bit paths) |

Several of these are real data flow, not artefacts:
- The 0x2ed0 loop writes only `.w` of a packet colour, so the RGB comes from whichever object last wrote that buffer.
- The 0x04d8 program transforms persistent data in place.

## Performance

All runs interleaved, still recipe, effects off. The machine was shared throughout (other sessions' node/tsc/headless
Chrome used several cores), so FPS varies a lot between windows:

| Window | Serial | `DRAKAN_VU_PAR`, 3 helpers | 5 helpers | 32 groups/unit | `DRAKAN_VU_BATCH` |
| --- | ---: | ---: | ---: | ---: | ---: |
| A | 167 | 108 | 96–125 | | |
| B | 148 | 88–108 | | 88 | 125 |

Why it loses (counters from `[vupar]` and the host sampler):

- **Helpers.** 55–80 µs per unit of ~19k pairs, i.e. ~3.4 ns/pair against 1.0 serial. The tracked engine alone costs
  1.45–1.5× in the replay (`DRAKAN_VU_BENCH_TRACK=1`). The rest is snapshot copy-in/out, VIF unpack, packet copies
  into records, and running next to a busy EE/GS on shared physical cores.
- **Owner.** The front scan costs ~16 µs per transfer; together with hand-out, commit and GIF replay the owner spends
  ~30 µs per transfer. Units commit in order, so a rejected or late head stalls the owner (5–24 s waiting per 70 s
  run). The EE then waits in `sync()` (12–21 s per run, ~0.8 syncs per frame).
- **Total.** CPU per frame is about twice the serial VU worker's, for lower throughput.

## Remaining ceiling and what would be needed

- The serial VU1 worker stays the limit at ~238 FPS. Even a perfect VU split would stop near 300 FPS: the GS worker is
  at ~80 % and the EE at ~75 % at 238 FPS.
- To make the parallel mode pay, all of these would be needed together:
  - A tracked engine within ~10 % of the plain one: per-block summaries only, with no per-memory-op bookkeeping in the
    vertex loops.
  - No duplicated VIF unpack on the owner, e.g. a register-only scan.
  - No snapshot copies: workers run in the unit's buffer.
  - Dependency fixes for the remaining real chains (q65 clip context, in-place transforms).
  - A quiet machine with free physical cores.

## Controls

- `DRAKAN_VU_PAR=1`: enable (default off). `DRAKAN_VU_PAR_THREADS` (3), `DRAKAN_VU_PAR_GROUPS` (8),
  `DRAKAN_VU_PAR_VERIFY=1`.
- Stats print at exit: units, rejections by reason, by qword and by pc of the first inherited read, helper µs/unit,
  owner costs.
- Replay: `DRAKAN_VU_PAR_CHECK=<groups>[,lag,...]` and `DRAKAN_VU_BENCH_TRACK=1` with `DRAKAN_VU_BENCH`.
