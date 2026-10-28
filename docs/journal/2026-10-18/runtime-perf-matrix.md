# Uncapped fixed4 performance matrix — 2026-10-18

Eight forward runs used one frozen candidate:
`0B6CB62F80ACFE341A377130685B9F5CEF7BB0AA8AB03A1BCB24744AE88E87E7`.
All runs passed scripted completion, candidate/config identity, and source/private
fixture preservation checks. This is a source-only summary; no game assets or
save contents are included. Aggregate evidence is in
[`runtime-perf-matrix-20261018.json`](data/runtime-perf-matrix-20261018.json).

Settings: uncapped world rate, fixed 4x internal scale, 1920x1080 windowed 4:3,
VSync off, presentation limiter off, asynchronous GS, audio output off. Each case
paired profiling off/on. Analysis anchored at the end of the first positive-world
pacing window, discarded 10 seconds, then selected complete windows within the
following 24 seconds, requiring at least 20 seconds. Slow windows were retained.

| Requested settings | Baseline worlds/sec | Profiled worlds/sec | Profiling rate change |
|---|---:|---:|---:|
| Native | 47.525 | 47.567 | +0.09% |
| SR Quality | 47.259 | 46.445 | -1.72% |
| NR | 36.446 | 35.975 | -1.29% |
| SR Quality + NR | 35.817 | 35.627 | -0.53% |

These are completed-world telemetry rates, not confirmed scanouts. Quiet logs
show NR enabled and runtime creation during evaluation. They do not confirm
every evaluation succeeded. SR startup reports a device candidate, which does
not establish successful SR evaluation. No visual correctness claim is made;
the table names requested settings.

## Timing localization

Baseline NR and combined add approximately 6.40 and 6.88 ms per completed world
versus native. Profiled temporal resolve costs approximately 7.46 ms/world for NR
and 8.17 for combined; native is effectively zero. Worker idle disappears and
commit-retirement backpressure reaches approximately 6.68 and 6.99 ms/world.
Host renderer-lock waits remain negligible. This localizes the added elapsed
cost to neural renderer work and producer backpressure, not a fixed 30 Hz
scheduler deadline. No scheduler/bridge policy change is justified.

`resolveTexture` costs approximately 6.3 ms/world in all modes. It is a shared
hotspot. Native readback Map costs approximately 1.67 ms/world; combined is 0.89.
CPU elapsed scopes can include driver/fence waits and descheduling. Scene timing
includes temporal work; guest dispatch includes bridge waits. Do not add nested
or cross-thread buckets. Profile report windows are independent, so per-world
normalizations are estimates.

Clock audits show approximately 60 Hz guest vblank and timestep accumulation of
0.9997 seconds per wall second. NR/combined maximum observed vblank delay is
approximately 9–9.5 ms. Whole-game timing parity remains unverified.

## Texture instrumentation and next comparison

The follow-up source change adds six child buckets under existing `GSR_PROFILE=2`:

- `textureCoherenceFlush`: existing cache-hit and decode-path flushes.
- `textureStampCheck`: span validity scans when the RAM clock changed.
- `textureDecode`: individual mip-level decodes.
- `textureCallback`: existing hit/miss callbacks.
- `textureUpscale`: existing miss-path upscale/replacement setup.
- `textureCreate`: texture creation with initial-data upload and SRV creation.

The pre-cache coherence flush remains before the clock shortcut and stamp tests;
decode-path flushes remain before decode. No cache validity, upload, scheduling,
queue-depth, guest-time, or presentation policy changes are included. The new
scopes were not present in the measured candidate above.

Measure inclusive/exclusive child times and profiling overhead before choosing
an optimization. If identical per-frame uploads cause redundant decodes, first
establish authoritative post-flush data and write provenance. A comparison
against stale RAM must not bypass a dirty GPU overwrite. Preserve CLUT/TEXA/mip
keys, wrapped/aliased spans, stamp updates and callback behavior.

The GPU slot was returned after the forward eight. Further GPU runs require a
new grant. Reverse runs are deferred until needed for a bounded-fix comparison.
Historical ~32.6 versus ~42 runs used different candidates/settings and are not
a matched performance comparison with this matrix.

## Follow-up texture profile and bounded CPU decoder change

Build02 candidate `1A964FC65436C8041F1824A3D93DBF360EBB92AB3ABCEE470ACEC8CAC9573FD8`
ran one fixed4/native profile after a separate exclusive GPU grant. It completed
cleanly, with unchanged candidate/config and source/private fixtures. Over 22.081
selected seconds it measured 48.321 completed worlds/sec, 59.982 guest vblank
starts/sec and timestep accumulation of 0.999697 s/s. This was not a matched
profiling-off run or an optimization A/B. The slot was returned immediately.

Normalized resolveTexture cost was 5.744 ms/world: decode 4.125, texture creation
and upload 0.849, upscale setup 0.499, coherence flush 0.011 and stamp checks
0.008. The callback bucket had no calls. Decode, not coherence scanning,
dominates this texture workload. See
[`texture-profile-20261018.json`](data/texture-profile-20261018.json).

The follow-up bounded change specializes the pure CPU decoder by PSM outside
pixel loops and expands indexed CLUT colors once per decode. Tiny mip levels
use direct palette lookup; 1x1 has a single-pixel path. Palette/TEXA changes are
observed on every call, including CSA arithmetic and 511-index wrapping. No
memory/cache/coherence, callback, scheduler or NR behavior is changed.

The original decoder remains only as the test oracle. Each of two CPU passes
verified 403,335 cases and 546,252,570 exact pixels across formats, TEXA, CSA,
palette modes, wrapped VRAM bases, TBW values and zero/tiny/odd/large dimensions.
The MSVC optimized benchmark used 54 format/size cases and five alternating
old/new timing pairs per case. Aggregate old/new timing ratios were approximately
1.40 for 64�256 textures and 1.19 including tiny mips in both passes. Several 1x1
cases are slower by a few nanoseconds; the aggregate remains positive. Synthetic
format/size weighting is not the gameplay distribution and does not predict FPS.

CPU backend syntax validation passed. Run the reproducible CPU check with
`runtime/gs/tests/run_texture_decode_cpu.ps1`; source-only results are in
[`texture-decode-cpu-20261018.json`](data/texture-decode-cpu-20261018.json).
A rebuilt candidate and a new GPU grant are still required for bounded gameplay
A/B and visual validation. This change does not address the NR temporal cost.

## Matched build03/build04 gameplay pair

A later exclusive slot completed exactly four quiet fixed4 runs with identical
frozen DLLs, INIs, NR bundle, input and fixtures. Only the executable differed:

- Build03: `26CF253402B4DC21ECDF8C7FEC003517A096D3937FA748C27F9D1C73DAC76EB8`.
- Build04: `A31156A75FC585F6BE7E40B25BB6230FC9B26C50A289C77F72B6F571E2D31455`.

| Requested settings | Build03 worlds/sec | Build04 worlds/sec | Change |
|---|---:|---:|---:|
| Native | 48.549 | 48.376 | -0.36% |
| SR Quality + NR | 38.031 | 37.955 | -0.20% |

Each run supplied approximately 22 seconds of selected steady windows. Guest
vblank remained approximately 60 Hz; all completion, executable/config identity
and source/private fixture preservation checks passed. This forward pair shows
**no measurable gameplay FPS gain**. Small negative differences do not establish
a regression. The pure CPU microbenchmark gain must not be advertised as a
verified FPS improvement. Build04 also includes parent camera/viewport changes,
so this is not a decoder-only executable A/B.

Quiet timing logs retain their feature/visual limitations. The parent separately
reported a baseline combined probe passing 23 checks with 1797 successful SR and
1685 successful NR evaluations; that is not a per-evaluation receipt for these
quiet runs. The final new combined case passed the deployment helper's SHA,
Quality+NR, scripted-quit and exit-zero requirements. Its exact private evidence
path is recorded in the source-only
[`texture-decode-game-pair-20261018.json`](data/texture-decode-game-pair-20261018.json).
The GPU/quiet-CPU slot was returned immediately after run four. No reverse or
extra deployment run was performed.
