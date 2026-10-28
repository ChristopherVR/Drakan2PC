# VU1 correctness investigation (clipping wedges, grid/warp overflow)

Status: **root cause found and fixed, and it is not in the VU1 interpreter.** A DMA transfer on the VIF1 channel with `CHCR.DIR = 0`
(peripheral -> memory, the GS local->host read-back) was fed to the VIF1 packet interpreter as if it were a VIF packet. The read-back buffer
(game data, floats) was decoded as VIF codes; the garbage contained `STCYCL` codes, which permanently replaced the game's `CYCLE = 0x0404` with
random values (`CL=45, WL=4`, `0x9a58`, ...), so **every later UNPACK was a "skipping write" that scattered vertex/ST/colour data across VU1 data
memory** (wrapping at 1024 qw), overwrote the constants/GIF tag templates and fed the microprogram with junk. That is the wedges, the stretched
textures, the "grid/warp batches writing outside buffers" and `[VU1 reserved lower] ... 0xfffffff8`.
Fix: `patches/vu1_fix.patch` (~40 lines in `ps2_memory.{h,cpp}` + `ps2_runtime.cpp`). Before/after: `build/screenshots/vu1fix_{before,after}_{lookdown,lookup,turned}.png`.

## 1. What was built

### 1.1 `runtime/vu1ref/` - standalone VU1 reference simulator (C++17; CMake target `drakan::vu1ref` + tools `vu1ref_replay`, `vu1ref_dumptest`)
* `vu1ref.h/.cpp`: independent re-implementation of the VU1 for this microprogram following PCSX2's interpreter (VUops.cpp) semantics:
  upper/lower pair executed in parallel (both read the pre-pair state), FMAC results visible after 4 cycles with per-lane interlock stalls,
  **MAC/CLIP results are not interlocked and visible `issue+4`** (FMAND/FCGET read the architected value; CLIP is a 6-bit shift register,
  `clipRun = (clipRun<<6)|new`), DIV Q latency 7 (only `WAITQ`/second DIV stall), ERLENG P latency 24, ILW 4-cycle result for branch readers,
  PCSX2 VI branch-hazard window (a VI written by the previous pair is seen with its OLD value by IBxx), I-bit LOI (the upper unit of the same pair
  still sees the old I), E-bit + delay slot, MSCNT resume pc, XGKICK snapshot with GIF tag walk, PS2 float rules (operands: denormal -> +-0, exp 255 -> +-Fmax;
  results: underflow -> +-0, overflow -> +-Fmax, round toward zero; MAX/MINI sign-magnitude compare, no MAC update; FTOI saturating;
  DIV by 0 -> +-Fmax). Options (`Vu1RefOptions`): flag/div/fmac latency, branch hazard on/off, stall model on/off.
* `vu1ref_replay.cpp`: replays recorded runs (see 1.2) through the reference and compares final VU memory + all VF/VI/pc with what the runtime
  produced. Extras: `-geom N` (decode XGKICK packets, print screen extents), `-dump i` (decode vertices of record i), `-unf i n` (trace an aborted run),
  `-flag/-div/-nostall/-nohazard` (sensitivity), `-clip` (only clipper kicks).
* `vu1ref_dumptest.cpp`: smoke test on the old `PS2X_VU1_DUMP` files (they lack the entry pc/registers, so only partially usable).
* Intended to back `DRAKAN_VUCHECK` later (it is a third, independent implementation; the existing checker compares only runtime-vs-runtime).

### 1.2 Capturing runs
`runtime/vu1ref/recorder_hook.txt` is the recorder (pasted into `ps2_vu1_core.cpp` of a scratch copy; nothing in `tools/` was modified):
`DRAKAN_VUREC="<file>,<after_s|-1 = wait for <file>.go>,<max>,<mask>[,<stride>[,<failOnly>]]"` writes, per `run()` (MSCAL/MSCNT), the entry
`VU1State` + 16 KB data memory, the exit state + data memory and the XGKICK addresses (35 KB/record). Captures used `build/input_tests/cap_run.sh`
(retries the intermittent loading hang, touches `<file>.go` N game frames after gameplay starts) with the scripted input `nb5.txt` (mouse look down/up,
walk, turn) on an `-O2` build of the committed `drakan` branch (old interpreter) and of `drakan-perf` (AOT interpreter).
(`build/vu1ref/` holds the scratch trees, builds and the captures `cap1..cap10.rec`, `*.vif.log`.)

## 2. Results of the differential test (reference vs runtime)

| capture | records | notes |
|---|---|---|
| `cap1` (menu/loading, all programs) | 6000 | **0 mismatches** (memory, VF, VI, pc) |
| `cap2` (gameplay, 14.7k clipper XGKICKs) | 9000 | **0 mismatches** |
| `cap3` (look down/up, poisoned VIF) | 16000 | 0 mismatches on the 15464 completed runs; 536 aborted runs (see 3) |
| `cap10` (after the fix) | 8000 | **0 mismatches, 0 aborted runs** |

Sensitivity of the model (cap1, 6000 runs): flag latency 5 -> 370 mismatching runs, 3 -> 94, no stall model -> 290, branch hazard off -> 18.
So the microprogram really does depend on the exact PCSX2 timing rules (FMAND/FCGET exactly 4 cycles after the SUB/CLIP, the `ADDw.z` filler that must
NOT be visible yet, DIV at +7, the `IADDIU/IBEQ` hazard) and **the runtime's interpreter implements those rules identically to an independent implementation**.
Conclusion: the CLIP shift register, FMAND on the W sign, DIV/Q latency, XITOP/XTOP, MAC timing, MINI/MAX, ITOF/FTOI, the IADDIU/IBEQ hazard and the MSCNT
resume pc are all fine; the clipper outputs in the neutral view are geometrically right (all fan vertices lie exactly on +-256 px).
(One cosmetic caveat: in the poisoned captures `cap5` 80 of 3000 records differ in a few VF registers (`vf10.y/z` = 0 vs +-Fmax) and `vi5`; these are runs fed with garbage/overflowing operands
(exp-255 operands) - a last-ulp/overflow-edge difference between the SSE fast paths and my scalar model, irrelevant for sane data; not investigated further.)

## 3. How the actual cause was found
1. In the look-down captures 3.4 % of the runs did not finish (`start=0x2a60`, hit the 65536-cycle budget or stopped with `[VU1 reserved lower]`),
   with `[7].x` = a float instead of a flag word and constants `qw0..6` = (0, -Fmax, +Fmax, 0): the VU data memory was garbage *before* the microprogram ran
   (the reference reproduces the runtime bit-exactly from that state, so the VU side is innocent). `cap3.log` has 259 856 `[VU1 reserved lower]` lines.
2. A VIF log (`DRAKAN_VIFLOG`) showed every UNPACK executing with `cl=45 wl=4` (`CYCLE=0x042D`; other runs `cl=184 wl=47`, `wl=46`, ...), i.e. skipping writes
   (4 vectors, skip 41) - impossible for this game, whose only `STCYCL` is `0x0404` at start (`[stcycl] imm=0404 run=0`).
3. Logging `STCYCL` found dozens of random immediates (`9a58, 92f8, fce8, 5b38, 2db8 ...`) in one `processVIF1Data` call of exactly **65536 bytes**.
   The recognised "commands" in that buffer are floats (`7fb2dfa4`, `31809e00`, ...), not VIF codes.
4. The call comes from `[dma-big] VIF1 normal mode madr=016cbef0 qwc=4096 chcr=00000100`: DMA channel 1, **CHCR = 0x100 -> DIR = 0**, 4096 qw = 128x128x32 bit.
   The game reads back a 128x128 image of the framebuffer through the VIF1 FIFO (BUSDIR) into RAM (screen warp / brightness texture, 16 times per run).
   `PS2Memory::writeIORegister` ignored DIR and queued the destination buffer as a `PendingTransfer` for `processVIF1Data`.
5. With the fix: 0 `[vif-desync]` (the new diagnostic), 0 `[VU1 reserved ...]`, 0 aborted runs, correct frames in every pitch/yaw.

## 4. The fix
`patches/vu1_fix.patch` (apply in `tools/PS2Recomp` with `git apply`, checked against the current merged `drakan` tree):
* `PendingTransfer.toMemory`: set when a VIF1 normal-mode DMA has `CHCR.DIR == 0`.
* In `processPendingTransfers`: such a transfer is **not** a VIF packet; it is completed by copying `qwc*16` bytes of the GS local->host FIFO
  (`GS::consumeLocalToHostBytes`, via a new `PS2Memory::setLocalToHostFn` callback wired in `ps2_runtime.cpp`) to the destination RAM; the DMA completion
  interrupt is still raised as before.
No change to the VU1 interpreter, the AOT generator or the `vu/ref` checker is needed (they are correct).
Not verified: the *content* of the read-back image (the warp/brightness effect consuming it); the old behaviour left the buffer untouched (stale data), the
new one fills it from the GS model if a local->host transfer was staged by the preceding `BITBLTBUF/TRXPOS/TRXREG/TRXDIR=1` packet.

### Evidence (same batch type, before and after)
Before (`cap3.rec`, record 196, P0 primitive pass, `build/vu1ref/cap3.rec`):
```
XGKICK #0 at pc 1060 qw773: NLOOP=9 NREG=4
 v0 st=(-3.37e+38,-3.33e+38) rgba=(127,127,127,140) xyz2: x=0.00    y=0.00    z=0 adc=0
 v4 st=(-3.38e+38,-3.26e+38)                        xyz2: x=2047.94 y=2047.94 z=0 adc=0     <- degenerate wedge corners
```
After (`cap10.rec`, record 300, lit strip, clipper-free):
```
XGKICK #0 at pc 2f48 qw774: NLOOP=11 NREG=4
 v0 st=(7.1e-06,9.4e-06) rgba=(81,86,97,128)  xyz2: x=-118.25 y=-29.00 z=3050191 adc=0
 v1 st=(1.1e-05,1.1e-05) rgba=(96,99,108,128) xyz2: x=-113.62 y=-30.69 z=2975389 adc=0
```
Statistics over equal-length look-down captures: aborted runs 536/16000 -> 0/8000; clipper XGKICKs per recorded run 1.07 (garbage polygons) -> 0.12; `[VU1 reserved lower]`
259 856 -> 0.

## 5. Other observations (not changed)
* `processVIF1Data` has no carry-over state between calls: a VIF command whose data is split across two DMA transfers/FIFO writes is mis-parsed
  (the chain path concatenates a whole chain, normal mode and `VIF1_FIFO` writes do not). Nothing in Drakan triggers it after the fix; a desync
  detector like `[vif-desync]` (garbage STCYCL) is a cheap guard worth keeping in the scratch builds.
* UNPACK details differ from PCSX2: V2 should write `z=x, w=y`, V3 writes `w` = next source word (the runtime keeps the old memory content). The game's masked V2/V3
  unpacks (`fmt 0x74/0x78`) mask those lanes, the unmasked V3 header `[9]` leaves `.w` untouched; nothing visible was found, but this is the first place to look
  if a future artifact appears.
* `LOI` and the upper unit of the same pair: the upper unit sees the OLD `I` (PCSX2 order). The only site (`0x818 MINIi.w ... I` + `LOI 255.0`) needs the old value
  (128 alpha clamp) - correct as implemented.
* Intermittent loading hang (frame 1470 in `[in-log]`, ~30-50 % of boots of the scratch builds) is the known pre-existing one, unrelated.

## 6. Reproduce
```
cmake --preset runtime-tests && cmake --build --preset runtime-tests --target vu1ref_replay vu1ref_dumptest
# tools land in build\runtime-tests\bin\Release\ (vu1ref_replay.exe, vu1ref_dumptest.exe)
vu1ref_replay analysis\vu\vu1_micromem.bin build\vu1ref\cap3.rec -showdiff 3     # compare, list aborted runs
vu1ref_replay ... -geom 1000 -n 700 ; -dump 196 -n 197 -skip 196                 # geometry / decoded packet
```
Capture: add `recorder_hook.txt` to `ps2_vu1_core.cpp`, run `build/input_tests/cap_run.sh nb5.txt <name> <count> <stride> <frames> <tries> [",1"]`.
