# Intermittent hang at boot under load (EE alloc RPC timeout)

## Symptom

Three sightings (`build/harness/g1_hang1`, `g2_2_b`, g3 batch runs): flips/s and vblank/s drop to 0 around the
memory card warning / first level load, the EE thread sits at 100% CPU, `[stall] guest input boundary silent for 8 s`
and a `[watchdog]` with pc inside `f_0x286008` (a vtable dispatch loop over a linked list, return labels 0x286084 /
0x2860e8; the sampled pc varies from run to run, so it is a live guest loop, not a stuck instruction). It happened on
about 3 of 5 launches while several instances were loading at once, rarely alone.

## What the logs showed

Two lines precede every hang and were read as noise:

```
[hle] post(sid=0x1303 fno=7225712): guest handler 0x1eff90 did not return within 20 s
[hle] resource: EE alloc (0x1303) failed for cookie 006e4170
```

with the perf lines before them showing `ee_thread_cpu=0% vblank/s=0` for about 20 s right after the first INTERFCE
font atlas was registered. So the EE thread was not running guest code for 20 s at boot, the IOP side gave up on the
EE allocation RPC, and the game later spun in the list walk.

## Cause

1. Stress reproduction: 12 instances plus 16 CPU hogs on a 16-core host reproduced the 20 s EE stall in 4 of 12 runs
   (8 launches alone or in separate directories did not, which is why it looked rare).
2. New stall forensics (`PS2X_PERF=1`: `[perf-stall]` lines, call chain of the EE thread and of every thread after 3 s
   without flips). The EE thread is in `GsrBridge::commitDisplay` (called from `drakan_frame_rate::flipHook`), waiting
   for a slot in the GS worker queue. The worker thread is inside the NVIDIA driver (`nvgpucomp64.dll`, the shader
   compiler, called from `nvwgf2umx.dll`) creating the first pipelines. Under CPU contention that takes 20 to 30 s.
   This is a plain host stall, not a deadlock: the EE thread resumes when the compile ends.
3. `EeRpcPoster::post` (drakan_overrides.cpp) delivers IOP-to-EE calls (sid 0x1301 / 0x1302 / 0x1303 / 0x1310) by
   queueing the registered guest handler on the EE scheduler and then waited for it for **20 s of wall time**. During
   the host stall the handler cannot start, so the wait expired, `post` returned -2, the resource service reported an
   allocation failure for the cookie and the request/response protocol with the game desynchronised (the handler still
   runs later against a buffer that has been reused). The game then walked a list built from the half-completed request
   and never left the loop.

The guest scheduler is deliberately independent of host speed (`postAfterGuestCycles`), so a wall-clock bound on a
guest call was the wrong semantics.

## Fix

- `EeScheduler::publishedCycles()` (relaxed atomic mirror of the EE cycle counter, updated in `accountCycles`).
- `post` now charges its 20 s budget in guest time: while the EE accounts cycles it charges `cycles / 294.912 MHz`;
  while the EE thread is inside the guest but accounts no cycles (blocked in a host call) it charges nothing; an idle
  EE is charged wall time as before. Stop requests and a 600 s wall backstop still end the wait.
- `ps2_perf.cpp`: the `[perf-stall]` forensics above (only with `PS2X_PERF=1`).

## Verification

Same stress (hogs on, 12 and then 20 simultaneous launches): before the fix 4 of 12 runs hit the timeout and hung;
after the fix 0 of 32 runs logged the timeout or `[stall]`, including runs whose EE thread sat blocked for 12 to
30 s at boot. Details in the final report of the session; raw logs in `build/harness/s7*` (before) and
`s8*` / `s9*` (after).

## `[gsr-presentation] host handoff timed out at latch N`

Unrelated. `GsrBridge::Impl::waitForHandoff` is a bounded (100 ms) wait on the GS worker for the window thread to show
the latched frame; it only paces the worker, never the EE thread, and prints at most four times. At boot it fires
because the window thread has not presented the first frame yet.

## Follow-ups not done

- The first pipeline compile still blocks the EE for up to ~30 s under heavy contention; moving PSO creation off the
  GS worker (or warming it before the first guest frame) would remove the stall instead of only surviving it.
- A `post` that does give up (real guest deadlock) leaves its invocation queued; it should be cancelled if it has not
  started.
