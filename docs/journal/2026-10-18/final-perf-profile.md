# Final performance scenarios — 2026-10-18

Candidate SHA-256:
`F40A8EACDB9DD8CCF69334EDC70C162646B70B7A5C680245A1CDB721CD2A64E5`.
Source snapshots: outer `e23d53f`, nested runtime `7114bb9`.
Three sequential runs completed under an exclusive GPU/quiet-CPU grant. All
passed scripted quit, exit-zero, executable/config identity and source/private
fixture preservation checks. The original user configuration was unchanged.
The slot was returned after run three; no optimization or synchronization change
was made during this investigation.

| Scenario | Actual scale during selected gameplay | Completed worlds/sec | Guest vblank/sec | Selected seconds |
|---|---:|---:|---:|---:|
| Combined Quality+NR, profiled, 1920x1080 windowed 4:3 | 4x | 36.479 | 59.985 | 22.121 |
| Combined Quality+NR, quiet deployment receipt, same fixed4 settings | 4x | 36.933 | 59.989 | 22.122 |
| Native Auto, quiet, 3840x2160 borderless 16:9 | 8x | 39.306 | 59.973 | 22.159 |

All scenarios use uncapped world rate, VSync off and presentation limiter off.
The actual-user Auto scenario uses the copied user configuration, copied texture
assets and audio enabled. The fixed4 cases retain audio output off. Startup/menu
Auto scale transitions are not the selected gameplay scale: gameplay stayed at
8x. The Auto workload has four times the fixed4 target pixel area, a different
aspect ratio, audio and user assets. These scenario rates must not be used as a
matched native-versus-NR or decoder-only comparison.

## NR phase timing

The bounded NR phase profiler measured approximately:

| CPU elapsed phase | ms per completed world |
|---|---:|
| Whole NR evaluate | 8.209 |
| SDK evaluate CPU call | 0.256 |
| Input transport | 0.127 |
| Evaluation submit, inclusive | 7.736 |
| Evaluation submit's CPU wait for GPU completion | 7.699 |
| Output transport | 0.068 |
| Presentation submit's CPU completion wait | 0.282 |

The waited fence includes preceding queued D3D11 rendering, SR, transport and NR
GPU work. This is not an isolated measurement of the NR kernel. Nested scopes
overlap and must not be summed. Independent report windows make the per-world
normalizations estimates. Profiling reduced measured throughput by approximately
1.23% relative to the quiet case. The profiled timestep sum was approximately
0.99973 seconds per wall second; whole-game timing parity remains unverified.

Preserve synchronization for deployment. The next bounded diagnostic should
bracket the D3D12 evaluation stream with timestamp queries, reading results after
the existing completion fence without adding a forced wait. Correlate this with
producer/SR work before considering an opt-in deferred CPU wait. Any experiment
must protect allocator reuse, GPU fence/copy ordering, failed-evaluation and
transport paths, and resize/reconfigure/shutdown lifetimes. It may move a wait
without increasing throughput; no improvement is established.

Quiet logs do not confirm every feature evaluation or establish visual quality.
Completed-world/presentation telemetry is not confirmed scanout FPS. No baseline
or optimization gain is claimed from these three different scenarios.

The exact final quiet combined deployment path, SHA fields, settings, completion
checks, phase samples and user-config preservation are recorded in the source-only
[`final-perf-profile-20261018.json`](data/final-perf-profile-20261018.json).
