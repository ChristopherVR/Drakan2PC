# Vector operand CPU work, 19 October 2026

The installed native port's busiest worker spent about 81% of sampled time executing the game's vector program. This follow-up targets the repeated SIMD work in its compiled arithmetic blocks. The public interpreter header and object layout remain unchanged.

## Retained operand changes

`Vu1Simd::normalize` replaces exponent extraction and separate zero/nonfinite selection with a magnitude clamp and threshold comparison. The mapping is exact:

| Input | Output |
| --- | --- |
| Finite normal | Original bits |
| Positive or negative zero | Original sign and zero |
| Denormal | Zero with original sign |
| Exponent 255, including infinities and all NaNs | FLT_MAX with original sign |

The mapping uses six integer SIMD operations. Seven shared Step sites now use it, covering full operands, broadcast operands, Q/I and accumulator normalization throughout the compiled program. It returns a normalized value without changing the source register.

Three related reductions are included: one biased signed comparison for ordinary-number classes; one XOR for positive integer-conversion saturation; and immediate-mask lane merges. The original floating-point operation order, result classification, flags, guest cycles, interlocks and packet timing are retained. Existing lane-normalization proofs still skip normalization where it is already known to be redundant.

## Correctness evidence

- Every one of the **4,294,967,296** possible input bit patterns matched frozen pre-change normalization, ordinary-class and saturated-conversion implementations.
- **1,048,576** lane merges matched exactly across all masks and four caller rounding modes.
- The existing SIMD suite passed **150,000,000** conversion, classification and lane-write comparisons.
- Full compiled-program, frozen FMAC, live-game and final artifact results are recorded in the deployment receipt after benchmarking.

## Excluded experiments

Moving nonordinary flag handling into the shared slow helper passed correctness tests but regressed raw FPS from 97.430 to 89.712. It is excluded.

An optional AVX dispatch experiment passed 286,720 exact SSE/AVX comparisons. Its mean was 99.255 versus 98.143 FPS for SSE; the small difference was within the observed run spread. It is excluded from this candidate, which retains the existing CPU requirement.

Packet flag batching also passed exact comparisons but measured 98.536 versus 98.583 FPS for the original queue. It is excluded.

Evidence for these experiments is retained in `docs/journal/2026-10-19/data/vector-cold-flags-20261019.json`, `docs/journal/2026-10-19/data/vector-avx-and-packet-flags-20261019.json` and the corresponding build directories.

## Benchmark conditions

The raw CPU comparison uses the same bow encounter fixture, audio on, 4x internal resolution, 3840x2160 window target, uncapped world cadence, and disabled DLSS/neural/water/rays/frame generation/VSync/frame cap. Profile, screenshot and live-reference runs are excluded from FPS averages. Process overlap and source/private/original save hashes are audited for every selected run.

Enhanced rendering is checked separately with DLSS quality, neural rendering, modern water, ray reflections and shadows enabled. Saved graphics settings, launchers, saves and original-30 controls are preserved. **The 144+ raw FPS goal remains open until measured otherwise.**

## Installed artifact

Executable SHA-256: `cd25328dbe3e3ba8d962636216667d5990636731f7ffc9c76404aa1efca3e1cb`. Backup: `C:\Games\Drakan - The Ancients Gates\decomp\build\release_backups\vector-operands-20261019-074129`. Runtime patch SHA-256: `9fd810ef4ccc4f91e55dea1e74a96f6d8e715d88f9c570d462eb18d63a6e347c`.

Clean final raw means: **98.560 -> 100.458 FPS (+1.93%)**. Controls use the prior installed executable; both optimized runs use this final artifact. The 144 FPS goal remains open.

Enhanced graphics, same final binary and unchanged quality: **25.682 -> 25.631 FPS**. This single pair remains about 27 FPS; no material enhanced gain is established. Final enhanced image was inspected.

Fresh CPU profile (same artifact and matching map; sampling run excluded from FPS averages):

```text
busiest thread 25036, 7729 samples
   5896  76.3%  VU1Interpreter
    639   8.3%  dll:ntdll.dll
    507   6.6%  other
    390   5.0%  PS2Memory
    174   2.3%  dll:VCRUNTIME140.dll
     60   0.8%  dll:ucrtbase.dll
     23   0.3%  PS2Runtime
     18   0.2%  dll:MSVCP140.dll
     14   0.2%  dll:KERNEL32.DLL
      8   0.1%  gsr (d3d11 GS)
-- top functions
    472   6.1%  ?packetKernel@Vu1Step@@SA_NAEAVVU1Interpreter@@AEAUCtx@1@@Z
    429   5.6%  ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
    374   4.8%  ?processVIF1Data@PS2Memory@@QEAAXPEBEI@Z
    354   4.6%  ??$blockFn@$0CP@$0A@@?A0x5f570f00@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    252   3.3%  ?settleFlags@Vu1Fast@@SAXAEAVVU1Interpreter@@@Z
    214   2.8%  ?execLower@VU1Interpreter@@AEAAXIPEAEIAEAVGS@@PEAVPS2Memory@@I@Z
    210   2.7%  ?progressXgkick@VU1Interpreter@@AEAAXXZ
    186   2.4%  ??$blockFn@$0NB@$0A@@?A0x129fae0b@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    174   2.3%  ?uvKernel@Vu1Step@@SA_NAEAVVU1Interpreter@@AEAUCtx@1@@Z
    170   2.2%  ??$blockFn@$0NE@$0A@@?A0x129fae0b@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    139   1.8%  ??$blockFn@$0BJ@$0A@@?A0x940bdca5@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    134   1.7%  ?blockReadsReady@Vu1Step@@SA_NAEBVVU1Interpreter@@PEB_K1PEBI@Z
    130   1.7%  ?run@Worker@?A0x0b481fcb@vu1async@@QEAAXXZ
    123   1.6%  ?fast@vif_unpack@@YA_NPEAEPEBEIIIIEE_N2IAEAI3@Z
    117   1.5%  ??$blockFn@$0CI@$0A@@?A0x940bdca5@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    117   1.5%  ??$blockFn@$0GL@$0A@@?A0x5f570f00@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    105   1.4%  ??$blockFn@$0M@$0A@@?A0x940bdca5@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
     93   1.2%  ??$blockFn@$0EO@$0A@@?A0x5f570f00@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
-- system-DLL samples by (module, rva, first caller in the exe)
    470   6.1%  ntdll.dll+1659c4  <- ?run@Worker@?A0x0b481fcb@vu1async@@QEAAXXZ
     44   0.6%  ntdll.dll+162114  <- ?run@Worker@?A0x0b481fcb@vu1async@@QEAAXXZ
     38   0.5%  ntdll.dll+6ed2d  <- ?now@steady_clock@chrono@std@@SA?AV?$time_point@Usteady_clock@chrono@std@@V?$duration@_JU?$ratio@$00
     21   0.3%  VCRUNTIME140.dll+1dafb  <- ??$_Insert_counted_range@PEBE@?$vector@EV?$allocator@E@std@@@std@@AEAAXV?$_Vector_const_iterator@V?$
     18   0.2%  VCRUNTIME140.dll+1dafb  <- ??$assign@PEBE$0A@@?$vector@EV?$allocator@E@std@@@std@@QEAAXPEBE0@Z
     12   0.2%  ntdll.dll+44d0b  <- ?processGIFPacketPath@GS@@QEAAXEPEBEI@Z
     11   0.1%  ntdll.dll+44d0b  <- ?submitGif@GsrBridge@@QEAAXEPEBEI@Z
     10   0.1%  ucrtbase.dll+56835  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      7   0.1%  MSVCP140.dll+18514  <- ?now@steady_clock@chrono@std@@SA?AV?$time_point@Usteady_clock@chrono@std@@V?$duration@_JU?$ratio@$00
      7   0.1%  VCRUNTIME140.dll+1dd6c  <- ??$assign@PEBE$0A@@?$vector@EV?$allocator@E@std@@@std@@QEAAXPEBE0@Z
      7   0.1%  ucrtbase.dll+566e5  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      7   0.1%  ucrtbase.dll+56830  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      7   0.1%  ucrtbase.dll+56839  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      6   0.1%  VCRUNTIME140.dll+1db00  <- ??$assign@PEBE$0A@@?$vector@EV?$allocator@E@std@@@std@@QEAAXPEBE0@Z
      5   0.1%  KERNEL32.DLL+18629  <- ?processGIFPacketPath@GS@@QEAAXEPEBEI@Z
```

Protected INI/CMD files remain byte-identical. Existing build receipts are retained. No save, DLL or asset deployment changes. The 144 FPS goal remains open.

The canonical standalone CMake/CTest target `vu_operand_bits.exhaustive` passed in 2.92 seconds. The normal build configuration has been restored. VU register storage alignment was inspected: production VU instances are embedded in `PS2Runtime`. No alignment or public-layout change is included; runtime address measurements would be needed to assess that hypothesis.
