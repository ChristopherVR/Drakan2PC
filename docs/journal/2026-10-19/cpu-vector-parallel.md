# Multicore vector geometry investigation — 2026-10-19

The new `vu_parallel_replay` diagnostic runs recorded geometry on independent vector interpreters with private scratch memory and packet buffers. It validates every result against a preceding serial replay. This establishes a working multicore compute prototype; it is not integrated into the live game's ordered VIF worker. The 144+ raw FPS target remains open.

## Implemented and checked

The existing gameplay recording contains 4,000 vector calls: 234 MSCAL starts and 3,766 MSCNT continuations. It forms 235 scheduling groups, including an initial partial continuation. Each whole group can replay on one worker while other groups run on other cores, provided its recorded input state and scratch uploads are supplied.

The diagnostic exercises both individual-call checkpoints and whole continuation groups on 1, 2, 4 and 8 workers. All 32,000 output comparisons match the serial oracle exactly: architectural registers and flags, guest cycles, all 16 KiB of scratch memory, packet count, packet bytes and per-call packet order. Another 4,000 comparisons check instrumented pair dispatch against the block/native path.

The checkpoint omits the decoded-code cache, retains scheduler and branch state, rebinds host destinations, and copies only the used packet prefix for an active transfer. A checkpoint occupies 9,392 bytes before active packet payload, versus a 484,544-byte interpreter. The code cache remains local to each worker. A further 3,072 interrupted/resumed comparisons pass, including 131 active PATH1 transfers, varied cycle budgets and all four caller rounding modes. Total: **39,072 exact comparisons, zero mismatches**.

The oracle uses the saved noalias-control runtime library, whose arithmetic was previously validated; that annotation experiment was not deployed. The diagnostic's provenance is recorded in `docs/journal/2026-10-19/data/vector-parallel-20261019.json`. The source is `tools/PS2Recomp/ps2xRuntime/tests/vu_parallel_replay.cpp`. An `EXCLUDE_FROM_ALL` CMake target is provided alongside the vector tests; invoke the executable with a VUR1 recording path. It is deliberately not an automatic CTest because it requires a local recording. Debug dump, trace, check and statistics modes are rejected because their helpers assume one stream.

## Dependencies found

Starting a whole group with cold registers produces the same scratch output and packets in this recording, but **3,996 final states differ**, even after ignoring the absolute clock. Dropping that inherited state in the game would therefore be incorrect.

A pair-dispatch trace follows reads before writes across each complete MSCAL-led group. Of the 234 complete groups, 89 read inherited VF8.xyz lanes. No other VF, VI or ACC live-in reads occur in this capture, apart from architectural constants. This trace is deliberately limited to register accesses; it does not establish arbitrary-path, memory, scalar or flag independence. Some inherited values can be read without affecting visible packets, which explains why a cold-state geometry comparison alone is insufficient.

Live integration must provide immutable VIF uploads and persistent constants, preserve continuations on one worker, carry or correctly merge final architectural state, and return packets plus motion metadata to the ordered collector. MSCNT cannot simply be sent to the next available worker. Shared VIF registers, VU scratch results, PATH1/PATH2/PATH3 order and readback barriers must remain correct.

## Clean compute measurements and limits

After the competing build and game exited, an audited replay completed with no observed competing game/compiler/linker process. For the same 4,000 calls, the serial chain measured **20.9737 ms**. Whole continuation groups measured **10.8915 ms on two workers (1.95×)**, **5.5450 ms on four (3.82×)** and **2.9709 ms on eight (7.14×)**. Each timing is the best of seven passes after correctness validation and worker warmup. Earlier contended runs remain recorded separately, and are not used for these numbers.

Worker timings include private input copies, checkpoint restoration, computation, packet capture and task dispatch. They exclude the preceding serial oracle that prepares the checkpoints, live VIF input preparation, final GS submission and rendering. A speedup in this diagnostic does not imply a live FPS speedup. The installed executable, quality settings and original-30 controls were not changed by this investigation.

## Arithmetic candidate

A separate private candidate extends the existing selected-lane normalization proof to every block where it removes an actual operand normalization. Eleven block entries change: 118, 126, 127, 218, 257, 277, 298, 315, 334, 947 and 1132. Arithmetic, classification, flags, scheduling and fallback guards retain their existing implementation. The replacements are compiled separately; unrelated AOT slices do not need recompilation for this experiment.

The candidate passes the replay comparisons. Three clean alternating ABBA compute comparisons produce speedups of **1.0221×, 1.0023× and 0.9938×**. This does not establish a repeatable gain. The candidate remains private and is not enabled or exported as a production optimization. The optional diagnostic can exercise it when compiled with its private probe module; normal builds do not use that module.
