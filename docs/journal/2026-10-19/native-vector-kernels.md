# Native vector kernels, 19 October 2026

The remaining raw CPU bottleneck is native execution of the game's vector program. The previous installed build's busiest worker spent 81.9% of sampled time in vector execution, including 9.9% in the packet trace. This change removes repeated instruction dispatch and pipeline bookkeeping from two hot routines while retaining guest arithmetic and timing.

## Retained changes

- Fuse the packet assembly path at microprogram entry 489: native integer arithmetic, register rotation, loads, branch decisions and constant-mask stores. Preserve every branch-prefix exit, original MUL helper, wrapping addresses, store issue cycles and overlapping PATH1 reads.
- Fuse the UV transform loop at 155..161. Keep the original normalization, arithmetic, clamping and exceptional-float handling. Batch only flags already due by the completed loop backedge.
- Retire entry register deadlines once across bounded complete iterations. Compile-time schedule and ROM-word assertions establish the fixed program and ready-at-backedge invariants.
- Fall back to the original compiled dispatcher for pending scalar work, insufficient budgets/readiness, noncanonical zero registers or aliased host buffers. No public interpreter object-layout change.
- Enable both kernels by default. `DRAKAN_VU_NATIVE_KERNELS=0` restores the previous dispatch; `1` selects packet only, `2` UV only, `3` both. Existing original-30 cadence and frame-cap settings remain available.

## Correctness and build evidence

Each new kernel passed **114,688** exact old/new state, data, packet and dependent-resume comparisons. Cases cover budgets 1..1024, branch-prefix exits, exceptional IEEE bits, wrapping stores/transfers, PATH1 hazards, pending DIV/SQRT/RSQRT and register writes. Six dependent probes per initial case read recent register results and immediate MAC/status flags.

The final default-enabled executable passed **327,680 live frozen-reference vector runs, zero mismatches**. These checks and screenshot/profile runs are excluded from FPS averages.

The private Release build compiled the new kernel and dispatcher against the installed runtime. Its four unchanged generated VU image slices reused the previous Release objects: the only shared Step header changes are two static method declarations, with no data layout or existing inline-body changes. Canonical CMake now lists the new source and both regression targets normally; the temporary private build override is removed after review.

## Measurements

Final artifact, uncapped, audio on, 4x internal resolution and 3840x2160 window target. DLSS, neural rendering, modern water, rays, frame generation, VSync and cap are disabled for the raw CPU test. The same executable is used for controls and optimized runs; only kernel registration changes.

| Raw run order | Kernels | World FPS |
| --- | --- | ---: |
| final-off-a | disabled | 90.910 |
| final-default-a | default | 97.130 |
| final-default-b | default | 96.750 |
| final-off-b | disabled | 91.508 |

Mean **91.209 -> 96.940 raw FPS (+6.28%)**. Runs have no overlapping game/compiler processes and unchanged source, private and original saves. This is a measured improvement, but **144+ raw FPS is not achieved**; another approximately 48.5% throughput gain is required from this scene.

Earlier component measurements reached 95.504 FPS with packet alone and 97.988 with both, against 91.264 disabled. These are development measurements, not the final artifact average above.

## Rejected experiments

Read-only input-normalization hoisting passed exact tests but fell from 92.284 to 90.948 FPS with both forms enabled. Transient input normalization fell from 92.128 to 89.936 FPS. Neither implementation is promoted or compiled into the final candidate. The retained kernels use the existing arithmetic helpers and normalization proofs.

Final deployment, enhanced graphics, visual review and fresh profile evidence are recorded in `docs/journal/2026-10-19/data/native-vector-kernels-20261019.json` and `build/vu-hoist-20261019/deployment.json`. Existing deployment receipts, user graphics settings, launchers, original-30 controls and saves are preserved.

## Installed artifact

Executable SHA-256: `0ea05b06ac5c7a9a3f87846df9aa3bd4836f0347bd82d1e9b0e2cb17638b075e`. Backup: `C:\Games\Drakan - The Ancients Gates\decomp\build\release_backups\native-vector-kernels-20261019-061858`. Runtime patch SHA-256: `2c453931e33e2c463f00d3720e7bcba2e46fe563ba11413becf9d69a61eb8e68`.

Enhanced graphics, same final binary and unchanged quality: **26.999 -> 27.052 FPS**. This single pair remains about 27 FPS; no material enhanced gain is established. Final enhanced image was inspected.

Fresh CPU profile (same artifact and matching map; sampling run excluded from FPS averages):

```text
busiest thread 62720, 7793 samples
   6335  81.3%  VU1Interpreter
    495   6.4%  other
    353   4.5%  PS2Memory
    322   4.1%  dll:ntdll.dll
    162   2.1%  dll:VCRUNTIME140.dll
     69   0.9%  dll:ucrtbase.dll
     24   0.3%  dll:MSVCP140.dll
     19   0.2%  PS2Runtime
      8   0.1%  dll:KERNEL32.DLL
      6   0.1%  gsr (d3d11 GS)
-- top functions
    528   6.8%  ?packetKernel@Vu1Step@@SA_NAEAVVU1Interpreter@@AEAUCtx@1@@Z
    416   5.3%  ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
    381   4.9%  ??$blockFn@$0CP@$0A@@?A0x36ea3e3c@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    339   4.4%  ?processVIF1Data@PS2Memory@@QEAAXPEBEI@Z
    264   3.4%  ?uvKernel@Vu1Step@@SA_NAEAVVU1Interpreter@@AEAUCtx@1@@Z
    217   2.8%  ?settleFlags@Vu1Fast@@SAXAEAVVU1Interpreter@@@Z
    216   2.8%  ?execLower@VU1Interpreter@@AEAAXIPEAEIAEAVGS@@PEAVPS2Memory@@I@Z
    209   2.7%  ??$blockFn@$0NB@$0A@@?A0x7b229f37@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    204   2.6%  ?progressXgkick@VU1Interpreter@@AEAAXXZ
    168   2.2%  ??$blockFn@$0NE@$0A@@?A0x7b229f37@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    156   2.0%  ??$blockFn@$0EO@$0A@@?A0x36ea3e3c@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    140   1.8%  ?blockReadsReady@Vu1Step@@SA_NAEBVVU1Interpreter@@PEB_K1PEBI@Z
    139   1.8%  ??$blockFn@$0CI@$0A@@?A0xfdb6ed99@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    129   1.7%  ?run@Worker@?A0x8db9c5c5@vu1async@@QEAAXXZ
    128   1.6%  ??$blockFn@$0BJ@$0A@@?A0xfdb6ed99@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    123   1.6%  ??$blockFn@$0M@$0A@@?A0xfdb6ed99@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    122   1.6%  ??$blockFn@$0GL@$0A@@?A0x36ea3e3c@@YA_NAEAVVU1Interpreter@@AEAUCtx@Vu1Step@@@Z
    111   1.4%  ?fast@vif_unpack@@YA_NPEAEPEBEIIIIEE_N2IAEAI3@Z
-- system-DLL samples by (module, rva, first caller in the exe)
    149   1.9%  ntdll.dll+1659c4  <- ?run@Worker@?A0x8db9c5c5@vu1async@@QEAAXXZ
     34   0.4%  ntdll.dll+6ed2d  <- ?now@steady_clock@chrono@std@@SA?AV?$time_point@Usteady_clock@chrono@std@@V?$duration@_JU?$ratio@$00
     29   0.4%  VCRUNTIME140.dll+1dafb  <- ??$assign@PEBE$0A@@?$vector@EV?$allocator@E@std@@@std@@QEAAXPEBE0@Z
     23   0.3%  VCRUNTIME140.dll+1dafb  <- ??$_Insert_counted_range@PEBE@?$vector@EV?$allocator@E@std@@@std@@AEAAXV?$_Vector_const_iterator@V?$
     18   0.2%  ntdll.dll+162114  <- ?run@Worker@?A0x8db9c5c5@vu1async@@QEAAXXZ
     16   0.2%  ntdll.dll+44d0b  <- ?submitGif@GsrBridge@@QEAAXEPEBEI@Z
     15   0.2%  ntdll.dll+44d0b  <- ?processGIFPacketPath@GS@@QEAAXEPEBEI@Z
     10   0.1%  ntdll.dll+45a4e  <- ?processGIFPacketPath@GS@@QEAAXEPEBEI@Z
     10   0.1%  ntdll.dll+45a4e  <- ?submitGif@GsrBridge@@QEAAXEPEBEI@Z
     10   0.1%  ucrtbase.dll+566e5  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
     10   0.1%  ucrtbase.dll+56835  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      8   0.1%  ntdll.dll+45a30  <- ?submitGif@GsrBridge@@QEAAXEPEBEI@Z
      7   0.1%  ucrtbase.dll+55e59  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
      6   0.1%  VCRUNTIME140.dll+1dd6c  <- ??$assign@PEBE$0A@@?$vector@EV?$allocator@E@std@@@std@@QEAAXPEBE0@Z
      6   0.1%  ucrtbase.dll+55e6e  <- ?run@VU1Interpreter@@AEAAXPEAEI0IAEAVGS@@PEAVPS2Memory@@I@Z
```

Protected INI/CMD files remain byte-identical. Existing build receipts are retained. No save, DLL or asset deployment changes. The 144 FPS goal remains open.
