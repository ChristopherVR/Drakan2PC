# Where the time goes: Level 0 intro cutscene and 3D gameplay (measurements for the perf agent)

Measured 2026-10-17 on the `drakan` branch (/O2 build, `PS2X_GS=d3d11`, scale 1, RTX-class GPU, 16 threads) with the diagnostics added
in this round: `PS2X_VU1_STATS=1` (wall time inside the VU1 interpreter, printed every 20000 runs) and `PS2X_GS_STATS=<n>`.
No optimisation was done; this is only the profile.

## Summary

**The VU1 interpreter (`ps2xRuntime/src/lib/vu/ps2_vu1_core.cpp`) is the bottleneck: 50 % to 80 % of the wall time, ~9 Mcycle/s.**
GS draw count, the D3D11 backend, the HLE and the EE code are not what limits the frame rate. The 2-3 fps of the intro cutscene, the 2 fps
of gameplay and the apparent "stall"/"Reloading..." state while a level streams in are all the same thing: one game frame costs 0.3-0.5 s,
and the level loader only advances one step per game frame.

| phase | wall | VU1 runs | time inside interpreter | VU1 cycles | per run | speed |
|---|---|---|---|---|---|---|
| New Game, level load + intro (before the camera fix below, world degenerate = cheap geometry) | 110 s | 280 000 | 55 s (50 %) | 491 M | 1750 cycles, 197 us | 8.9 Mcycle/s |
| gameplay after the camera fix (real geometry) | ~600 s | 3 080 000 | 485 s (80 %) | 4 262 M | 1385 cycles, 157 us | 8.8 Mcycle/s |

* 112-113 ns per VU1 cycle. A plain interpreter should reach 50-150 Mcycle/s; the cost per cycle is dominated by the pipeline model
  (`commitReadyPipelines`, `calculatePairReadyCycle`, `getDecodedInstructionPairForPc`, the per-instruction FMAC flag emulation) and the
  per-run set-up (`fesetround(FE_TOWARDZERO)`/restore, scheduler reset). A VU1 AOT / threaded-code path (as the perf branch plans) removes
  this; correctness of the existing interpreter is the reference.
* The game issues ~470 VU1 runs per game frame in gameplay (each `MSCAL` + `MSCNT` chain), so 3-5 game frames/s are the ceiling at the
  current speed even with an infinitely fast GS.
* GS side (d3d11): ~150 draws and ~13 k vertices per game frame in gameplay (495 k draws / 3360 presented frames), 20 texture
  decodes per frame while streaming; the GPU is idle. `PS2X_GS=sw` was not slower in the menu, i.e. not the limit either.
* The intro cutscene (FMV-like VDB stream `LEVEL0RO.VDB` type 0x343, 213 MB) is *loaded* by the game's streaming loader, one 100-130 KB
  chunk per loader tick, ticks being tied to the game frame. At 3 fps the 213 MB stream needs many minutes. This is what the user sees as
  "Reloading..." / a menu that does not go away after New Game. Making frames 10x cheaper (VU1) fixes it; the HLE latency
  (`DRAKAN_RES_LATENCY_US`, now guest-time based) is 2 ms per batch and not significant.
* Boot-to-gameplay is ~140 s with the real geometry (was ~65 s while the world was degenerate).

## What generates the VU1 work

One microprogram (see `docs/reference/graphics.md`): P0 vertex/primitive pipeline (entry 0x0000, MSCNT ladder) and P1 object setup (entry 0x26e0).
In the profiled gameplay the 1385 cycles/run average is dominated by the vertex pass (lighting `0xaa8-0xc48`, transform 0xd80, clipper
`0x15f8-0x26b8` for objects crossing the frustum) and by the flag 0x7064 batches (grid/warp generator 0x520-0x640 + point lights). The
clipper is the most expensive path per run (thousands of cycles).

## Reproduce

```
set PS2X_VU1_STATS=1
set PS2X_GS_STATS=120
build\input_tests\run_bw.sh   (script `sync play 10`, quit) or just run drakan.exe and read stderr
```
Lines: `[vu1-stats] <runs> runs, <s> s inside the interpreter, <cycles> cycles`, `[gsr] frame N: packets=.. vertices=.. draws=..`.
