# Freeze on the first gameplay frame with modern water (player report)

## Report

A player (RTX 3070, PAL ISO, 165 Hz, DLSS candidate) froze on the loading screen when loading a Surdana (Level 1A)
save, twice: once with Continue, once with Options then Load. Both `drakan.log` and `drakan.1.log` end at the same
two lines:

```
[in] t=71.37 frame 9189: gui screen 14 -> -1 (gameplay=1)
NR interop: same-LUID D3D11Device5/Context4, four shared textures and two shared fences created
```

The save had loaded (`resident-scene ... layers=483 waterLayers=171 triangles=168889`). That interop line is printed
from the first `RayWater::submit`, so modern water or ray shadows were on (both default off; the ini keeps them). After
it the `[frame-uncapped]` heartbeat stops: the render thread is blocked, not just slow. There is no `[modern-water]
bypass:` line, so the pass did not fail and recover in the normal way. The log is unbuffered (writer thread), so no
lines were lost.

## Cause (not reproduced)

Every frame waits on a fence that only another API can release, with no timeout. D3D11 queues a `Wait` for the
consumer value that the D3D12 queue signals after the pass, and D3D12 waits for the D3D11 producer value. Fence waits
on the GPU are not covered by TDR. Two ways this deadlocks, which the log cannot tell apart:

1. The driver performs the D3D11 wait for the shared fence on the CPU. `consume11` ran before the job was queued, so
   the render thread blocked before the pass thread had anything to signal.
2. A signal is lost or the pass thread blocks somewhere without a timeout. Then `Present` or `enqueue`/`finish`
   (two jobs at most) blocks forever.

Not reproduced here (RTX 5080, driver 32.0.16.1714, where the pass works).

## Fix

- `NrInterop::consume11` is split: `reserve11` returns the value, the job is queued, and only then does `consume11`
  issue the D3D11 wait. This rules out case 1.
- Watchdog thread in `RayWater` (see `docs/reference/perf.md`, "Effects path off the GS worker"): it logs a pass that
  is still incomplete after 1 s, then every 2 s. If a pass is still stuck 3 s after its signal was queued on D3D12, or
  21 s before that, it raises both shared D3D12 fences from the CPU (`release12`, repeated while the pass stays off,
  because signals queued earlier can lower them again). It also wakes the queue waits and turns the pass off for the
  session. The game keeps running with native water. `GSR_RAY_WATCHDOG_MS` sets the limit; 0 = report only.
- New logging (`[ray-water]` lines): the pipeline status with adapter name and UMD driver version; interop size and
  adapter; the first three passes step by step (queued with sizes, D3D11 wait issued, pass-thread time and scene
  status); slow passes (>250 ms, at most every 5 s); every fault with the device-removed reason (faults used to be
  silent); watchdog progress with the pass-thread stage and both fence values.
- Test: `GSR_RAY_TEST_DROP_SIGNAL=<n>` drops one consumer signal. `gsr_test_ray_water --lost-signal` (ctest
  `ray_water_lost_signal`) checks that a D3D11 readback which would block forever returns in about 1 s with a 300 ms
  limit, the pass reports the watchdog status, and later frames bypass it. `ray_water` still passes with the D3D12
  debug layer and logs no watchdog lines.

## For the next report

If it was case 1, the new build simply loads. Otherwise the log now shows `[ray-water] watchdog:` lines naming the
stage and fence values, and the game continues without modern water. Ask for the whole `drakan.log`.
