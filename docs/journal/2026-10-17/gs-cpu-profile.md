# Opt-in backend CPU call timing

Set `GSR_PROFILE=2` before launch. Unset, zero, or nonnumeric values disable
profiling. Positive values select a reporting period of 2 to 60 seconds;
positive values outside that range use two seconds. No renderer feature or
scheduling policy changes. The bridge is untouched.

One bounded `[gsr-profile]` line reports each completed interval. Each bucket
contains `calls/inclusive_ms/exclusive_ms/max_ms`. Inclusive time includes its
instrumented children; exclusive time subtracts them. Do not sum inclusive
bucket times. `root` is the sum of nonoverlapping outermost instrumented calls,
not total process CPU usage. Timings are CPU-side elapsed wall time and can
include driver/GPU waits, descheduling and profiler overhead. They are not GPU
execution times. The profiler uses fixed storage with no per-call allocation,
GPU queries, additional locks or per-draw log lines. Disabled scopes do not read
the clock. Reports occur on return from outermost calls, so a long blocked call
delays the next report; idle periods and the final partial interval produce no
forced report.

Buckets cover draw, resolveTexture, syncRT, readbackRT, readbackMap,
readbackPackUnmap, temporalResolve, presentLatched, presentCall, pumpUp,
snapshot, windowScreenshot, latchDisplay and endFrame. Calls include early
returns/no-ops. `readbackMap` isolates the staging Map call; pack/unmap is a
separate child. `readbackCopy` and `syncUpload` count submitted uncompressed
texture bytes, not native GS bytes or actual bus traffic. Snapshot timing is
submission time; its eventual GPU cost may appear in a later driver wait.

`presentLatched` starts INSIDE the backend, after the bridge acquired its
renderer lock. `presentCall` covers the existing DXGI/NR presentation block.
Neither measures the bridge mutex acquisition or frame-latency wait. Compare
these against main's independent bridge lock-wait instrumentation. A long host
tick with short backend presentation points outside this presentation code;
it does not by itself prove starvation. The main-owned fair handoff remains a
separate change.

For clean performance runs, remove `DRAKAN_SND_DBG`, `GSR_WINSHOT`,
`GSR_WINSHOT_AT` and capture automation. Keep scenario, warm-up, duration,
feature settings and focus consistent when comparing resolutions. Screenshot
work is separately labeled if accidentally enabled. Profile overhead is not
zero: compare profiled and unprofiled runs before interpreting small changes.

Integration: cherry-pick the profiler commit, including `src/cpu_profile.h` and
the backend scope insertions. No build-system change, runtime bridge patch,
generated recompilation or deployment is included. This diagnostic does not
implement the separately reviewed RT ownership or cache lifetime fixes.
