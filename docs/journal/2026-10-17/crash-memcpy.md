# Main-thread death ~45 s into the level ("memcpy size clamp 0xfffffffc", jump to 0xa21624a1): root cause and fix

Status 2026-10-17. Question: START mashed during the intro / level start, at ~46 s the runtime logged `[memcpy] size clamp from 0xfffffffc`
and the guest jumped to pc = ra = 0xa21624a1 (a1 = 0xfffffffc). Game bug (also on a PS2) or port bug?

**Answer: port bug, in the recompiler's FPU emitter. `cvt.w.s` was emitted as `(int32_t)nearbyintf(x)` (round to nearest even); the EE FPU
truncates toward zero and saturates.** Drakan's animated water texture converts a scroll position in 0..64 with `cvt.w.s` and uses it as a row index into a 64x64
16-bit texture. From 63.5 the port computed row 64, so the blit wrote a full row of texels (up to 128 bytes) past the end of the 8 KB pixel buffer, over the next
heap block. The overwritten objects then contain texels instead of pointers/sizes: `0xa21624a1` and `0xfffffffc` are two 16-bit texels each. The START presses are
irrelevant (reproduced with and without), the death is simply when the water scroll first crosses 63.5, ~960 game frames after the level starts, which is why it looked like a
START race at "46 s".

Fix: `patches/memcpy_fix.patch` (2 commits against `tools/PS2Recomp` branch `drakan` @ `7c1a942`, applies with `git apply`/`git am`, `ps2xRuntime/include/ps2_runtime_macros.h`
+ `Kernel/Stubs/LibC.cpp`). It is a header change: the generated code must be recompiled but **not regenerated**.

| runtime | boots that lost the main thread (START mashing, `scripts/archive/2026-10-17/memcpy_repro.sh`) |
|---|---|
| before, `recomp_fixes` exe as found (3 parallel streams) | **9 of 12** (null JALR 0x45bf68 x6, 0x162080 x3; 6 more boots were stopped before the crash point) |
| before, same source tree rebuilt with diagnostics only | **10 of 12** (6 null JALR 0x45bf68, 3 null JALR 0x162080, 1 jump to garbage `0x43883904` from 0x162080) |
| after, `cvt.w.s` fix | **0 of 12**, and **0 of 12** again with the final patch (fix + LibC hardening); all 24 boots ran to frame 2040 (80 s), the water scroll crossed 63.5 three to four times in each |

## 1. Static analysis of the call trace (task 1)

The trace in the report (`0x10523c/0x10525c <- 0x103828 <- 0x140e74`) is the runtime's *dispatch history*: the last return-landing addresses, oldest first (the missing-target report
prints it the same way as `[trace-ring]`). It is not the memcpy path:

* `0x140e74` is the landing after `jal 0x103268` in `FUN_00140ba8` (per-frame object update, calls virtual `update`/`animate` slots of every object in the lists at `[s2+0x44c/0x458/0x464/0x470/0x488]`).
* `0x103828` is the landing after `jal 0x104678` in `FUN_00103268`; `FUN_00104678` is the recursive skeleton animation evaluator (frame `-0x130`, calls itself at `0x104740/0x1047d0/0x105234/0x105254`, so `0x10523c/0x10525c` are its self-return landings).
  It only *finished* normally: those landings are the last thing it did before the crash. It does not call memcpy.
* The size is not a length that is computed anywhere: memcpy was called with a2 = 0xfffffffc because an object field that held a size/count had been overwritten by two texels (`0xffff,0xfffc` is a plausible white/almost white 5551 texel pair, i.e. the lit water). The failure is therefore always a *use of already corrupted data* (`FUN_00140ba8`-driven virtual calls with texel vtables, `jalr` null at 0x45bf68 `[[obj+0x30]+0x54]`, `jalr` at 0x162080 through `[obj+0x68]`, or a stub memcpy with a texel size) and varies with the heap layout.
* In the captures the corrupted words were: `a0[0..c] = 0xa532a618 0xa57af7d9 0xa108a10d 0xa108a16c` (0x162080 case, an object at 0xbaa4d0) and `[0xbaa3c0] = 0xa1aee4a8 / 0xa5e0d6d4`. All have the form `1xxx xxxx 1xxx xxxx` = bit 15 set in both halfwords = 5551 texels with the alpha bit.

## 2. Reproduction and how the writer was found (task 2)

Script: `scripts/archive/2026-10-17/memcpy_repro.sh <exe> <boots> [tag] [seconds]` (autopress 4000 ms + `build/input_tests/g2.txt`, START tapped every 60 game frames from frame 760; gameplay starts at ~28 s).
Only the game thread dies, the GS thread keeps flipping (`frames=4500` in `[gsr] exit`).

Facts that narrowed it down (all with a private rebuild of the `fixes` tree in `build/mcsrc` + `build/memcpy_dbg`, diagnostics env-gated and off by default):

1. Dead boots die at the same game frame (941..1000, ~44-46 s wall), the objects involved sit at the same heap addresses (`0xbaa390`, `0xbaa4d0`). Not a timing race.
2. `DRAKAN_AUTOPRESS_UNTIL=30000` (no autopress after 30 s, only the script's START taps): still dies. Autopress/`Save Game` (the `[MC]` spam is just the slot-select screen polling) is irrelevant.
3. `PS2X_WATCH_POLL=baa3c0` (poll thread on one guest word): `0x00a6c3a0 -> 0xa1aee4a8` at t = 46.3 s in every dead boot; the same word in a *surviving* boot is the middle of a pixel buffer that changes by +-1 per frame.
4. Not an HLE writer (`RdramMemory::write` hook on 0xbaa000..0xbaa800: only one write at t = 24 s), not the GS local->host readback (`PS2X_L2H_LOG`: 16x65536-byte readbacks at 0x16b09e0..0x16d1610 only), not MC (sceMcRead sizes are the game's).
5. Page-protect trap (VEH on the page of that word, `build/mcsrc/ps2xRuntime/src/lib/mc_trap.cpp`): the writer is a guest function, `f_0x493138` (resolved from the host RIP through the link map), called from `0x493738 <- 0x1bb488 <- 0x1ef8b0 <- 0x161a78`, storing halfwords (`sh`) in ascending order up to and over the target word.
6. `FUN_00493138` = animated 16-bit texture update. Hooking it and the texture lock (`0x1bb238`): destination surface `0x6e1d68`, 64x64, stride 128, pixel buffer `[*(+0x50), +0x2000)` = e.g. `0xba8380..0xbaa380`, **the object that died starts at 0xbaa390**. Scroll state `[a0+4]/[a0+8]` (layer 1 x/y) and `[a0+0xc]/[a0+0x10]` (layer 2 x/y) are floats advanced by `dt * size * k` and wrapped with a single subtraction when they reach 64; logs show `f4` stepping 63.13, 63.36, 63.61, 63.84 and the boot dying right after.

### The code

```
493460  cvt.w.s $f1,$f0          ; $f0 = [s3+0x10]  (layer 2 y scroll, 0..64)
493464  mfc1    $t2,$f1          ; t2 = y
...
4934b4  lw  v1,0x34(s2)          ; dst stride = 128
4934c0  mult v1,t2,v1            ; v1 = y*128
4934dc  addu a3,v0,v1            ; dst row = [sp] + 2*x + y*128
...        loop over 64 texels, wraps t0 -= 128 at the end of the row, row count 64, y wraps by -64 after the last row
```

`y` is supposed to be in 0..63 (the wrap `if (64 <= f) f -= 64` runs at the end of every call, after the blit). With truncation `63.6 -> 63`; with `nearbyintf` `63.6 -> 64`: the row base is `[sp] + 64*128` = first byte after the buffer and the 64-x texels of that row (up to 128 bytes) are written over whatever lives there. (Layer 1 `x = 64` is only a 2 byte overflow.) Whether that kills the game depends on what the allocator put behind the buffer: in ~1 boot of 12 it is unused, which is the "seen once" impression.

## 3. Game bug or port bug? (task 3)

Port bug. On the PS2 the same game code never produces row 64: `cvt.w.s` rounds toward zero (EE FPU has only RZ; PCSX2: `(s32)fpr` with saturation to 0x7fffffff/0x80000000 for exponent > 0x9d,
no NaN/Inf). The recompiler emitted `FPU_CVT_W_S(a) = (int32_t)nearbyintf(a)` (and `FPU_TRUNC_W_S` as a plain cast, which on x86 gives 0x80000000 for large positives instead of 0x7fffffff).
Every `cvt.w.s` in the 12k recompiled functions was affected (anything in `[n+0.5, n+1)` converted one too high); this one was visible because it indexes a fixed buffer.

Things that were checked and are **not** the cause:
* the runtime memcpy stub is a faithful replacement for newlib's memcpy at 0x4cd27c (same dst/src/len order; `memmove` at 0x4cd32c is stubbed separately). The *clamp* is not the cause but it was harmful: with size 0xfffffffc a real memcpy would run off RAM and crash too, the stub instead copied up to 32 MB from the destination, which is why the guest crashed only afterwards and the report looked like a smashed stack. The original game code calls the stub, there is no guest libc memcpy in use (config `memcpy@0x004CD27C`).
* scheduler / HLE completion timing / the earlier "callee unwound at entry" bug (docs/journal/2026-10-17/crash-listener.md): the dead boots are fully deterministic in frame number and address, no completion or preemption is involved.
* START / pause menu / Save Game: independent (see 2.2).
* LWL/LWR, SLT, DSUBU patterns: not involved (the faulting stores are plain `sh`).

## 4. The patch

1. `FPU: cvt.w.s truncates toward zero and saturates like the R5900`: new `ps2_cvt_w_s(float)`; `FPU_CVT_W_S` and `FPU_TRUNC_W_S` use it (`ROUND/CEIL/FLOOR_W_S` do not exist on the R5900 and are not emitted).
   The R5900 FPU knows no rounding modes, so no other emitted conversion needs changing. **Recompile the generated code, no regeneration** (the macro is in a header).
2. `LibC stubs: drop memcpy/memset/memmove with a size above RAM`: sizes > 32 MB (negative ints) were clamped to 32 MB and executed; now the write is dropped and the log line is
   `[memcpy] bad size 0xfffffffc ra=0x... a0=... a1=... a2=... sp=...: transfer dropped`. `memcmp` (read-only) keeps the clamp. This only makes the next such bug diagnosable (and stops the 32 MB smash); it does not change behaviour for any valid call.

Gameplay side effect: all float->int conversions the game does with `cvt.w.s` (positions to grid cells, texture scrolls, UI) now round like the console; expect tiny differences (a pixel/cell) in places that used to be off by one. No place relied on rounding.

## 5. Tools and files

* `scripts/archive/2026-10-17/memcpy_repro.sh` (above). Logs of this work: `build/memcpy_dbg/*.log` (`sa..sc` = unmodified exe, 6 boots each, `vp` = pre-fix control, `vf`/`vg` = after), summaries via `build/memcpy_dbg/tally.sh <prefix>`.
* Private diagnostic tree (not in the patch, tools/ untouched): `build/mcsrc` = copy of the `fixes` source with
  `PS2X_WATCH_POLL` + `PS2X_TRAP_AT=<s>` (page-protect write trap with host backtrace, resolve RVAs with `build/memcpy_dbg/mapsym.py`), `PS2X_L2H_LOG`, `PS2X_HLEW=<lo>,<hi>`,
  `PS2X_SCROLL_LOG` (hooks of `0x493138` / `0x1bb238` printing the scroll state), memcpy bad-size report with ra. Build: `build/memcpy_dbg/cfg.ps1`, then `cmake --build rt --config Release --target ps2EntryRunner`.
* Note for the build: the `drakan_settings` library is compiled from `decomp/runtime/settings` which another agent was editing during this work (a literal line feed inside a char constant in `Overlay.cpp` broke the build for a few minutes; same class of mistake as the `ps2_runtime.cpp` one in docs/journal/2026-10-17/crash-listener.md section 5).
* Technique worth keeping: *poll one guest word + page-protect trap + link-map symbolisation* finds any wild writer, guest or host, in two runs; `PS2X_WATCH_W` only sees writes through the `ps2TraceGuestWrite` path, which the perf build compiles out (`PS2X_WRITE_WATCH`), so it reported "0 writes" and pointed away from the guest code.
