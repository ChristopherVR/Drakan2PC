# Runtime performance and game function audit

## Performance decision

Keep the C++ runtime and recompiler. Optimizing shader work, game-thread
work and render-worker data structures addresses the measured limits. The
project translates the original PS2 instructions into native host code
through generated C++, with specialized vector-unit paths. Emitting Rust
would still require the same guest register, memory, scheduling and
hardware behavior. A different compiler could help a particular CPU
routine, but its benefit would need a controlled comparison.

This audit recalculated the existing 20 November gameplay captures on
the Ryzen 7 9800X3D and RTX 5080. The runs used Direct3D 11, 4K output with
DLSS Quality, ray-traced shadows, reflections and global illumination,
with VSync, the frame cap and Modern Water off. The FPS comparisons used
paired measurement windows. The GPU use and game-thread timings come
from separate diagnostic windows.

| View | Measured result | Likely limit in that capture |
| --- | --- | --- |
| Surdana town square | Median 69.1 to 74.0 world FPS after reducing terrain and material preparation, faster in all 10 pairs across two launches. The 99th-percentile frame time fell from 19.9 to 18.9 ms. | Render-worker CPU work mattered at these settings. |
| Ruined Village and Volcanic arrivals | 95.5% and 97% median GPU use in the diagnostic windows. Volcanic rose from 69.12 to 69.98 world FPS after the DXR optimization, faster in all five pairs. | GPU work mattered. Ruined Village also had substantial game-thread load. |
| Rift World tunnel | One diagnostic captured 107.5 world FPS, 76% median GPU use and 9.1 ms of game-thread CPU per world frame. An installed-build capture measured 87.0 world FPS, 58% GPU use and 11.0 ms of game-thread CPU per frame. | The game thread was near a full core in both recordings. Sampled call stacks were too sparse to identify a single routine to rewrite. |

For a CPU optimization, use the Rift World tunnel to profile guest
rendering and terrain lookup and Surdana to measure render-worker work.
Measure world-frame throughput and the slowest frame intervals, with
profiling disabled during the comparison. For GPU-side work, keep
Volcanic as a repeatable rendering check. The existing
[Surdana profile](../2026-11-20/dlss-render-worker.md) and
[five-level retest](../2026-11-20/ootf-4k-gpu.md) include paired ranges,
correctness checks and settings. There was no new timed gameplay launch
or Rust/C++ comparison in this audit. Other game runs were active during
the investigation.

If Rust is adopted for memory safety, a self-contained parser or tool is
a better first candidate than the PS2 execution path. A later performance
experiment could compare one profiled CPU kernel through a small C ABI
using identical inputs, build settings and output checks, then verify
that any gain reaches gameplay. [Rust FFI reference](https://doc.rust-lang.org/nomicon/ffi.html)

## Game functions and integration points

The executable in the PAL play folder matched the build executable by
SHA-256. Comparing the unique start addresses from
[`analysis/functions.csv`](../../../analysis/functions.csv) against
`kGeneratedFunctionTableEntries` in the build's
`generated/register_functions.cpp` found all 12,173 starts in 82,120
unique populated dispatch addresses. The remaining 69,947 addresses
include internal entry and resume points. The function list has 12,117
names beginning with `FUN_`. Ghidra's discovery uses heuristics and some
indirect jump targets remain unresolved. Registration establishes
coverage of the current function map. Semantic coverage and safe host
calls require additional evidence for each routine.
[How the map is built](../../reference/ghidra.md)

| Work | Existing entry point |
| --- | --- |
| Read actors, player pose or equipment | `runtime/world` snapshots and readers. Their readers include object and pointer validation. |
| Trigger inventory, spell, shop or map UI behavior | `runtime/gameui` action queue and readers. Game-thread actions use existing native routes and check their outcomes against later snapshots. |
| Translate host input or make an already-audited guest call | `runtime/input` and its guest adapter. Immediate calls run from an existing guest context. Calls that can block use the guest scheduler. |
| Add or replace level resources | The `runtime/hle` mod pack and resource services. [Mod pack format](../../reference/modding.md) |
| Add a feature that needs a guest function or layout | Resolve the verified address and layout through the disc profile, check exact dispatch presence, and put the calling contract and preconditions in a named subsystem adapter. [Disc profiles](../../reference/regions.md) |

The raw [runtime lookup](../../../ps2recomp/ps2xRuntime/src/lib/ps2_runtime.cpp#L1313)
returns a non-null diagnostic handler for a missing entry. The
[co-op installer](../../../ps2recomp/ps2xRuntime/src/lib/drakan_coop.cpp#L2721)
uses a non-null check on that lookup for the message and chest hooks.
Both addresses are present in the audited PAL table. For an absent
address on another build, this guard would accept the diagnostic handler
as the original. New hooks should follow the existing `hasFunction`
pattern. The input guest adapter also uses a bounded dispatch loop and
returns the register value even if that loop stops before the guest
returns. An API intended for general use needs a result that distinguishes
completion from an absent entry or incomplete call.
[Follow-up fixes and Rift World profile](rift-world-and-guest-calls.md).

USA has a separate 12,128-start analysis map and remains experimental.
It has audited input, movie and settings capabilities, while several
advanced hooks still require PAL layouts. No USA executable was
available in this workspace to check its generated table or run gameplay.
The next step for broader reuse is a small catalog of named, verified
operations, starting with the ones used by the next feature. Keep the
disc mappings, argument and return types, game-state preconditions and
thread rules with each adapter. Validate mutations in a gameplay harness.

## Verification

- The built PAL and USA profile checks passed. All 15 Python region checks passed.
- The focused Release CTest run passed 59 tests across input, UI, world,
  saves, resources, networking, scheduling and disc profiles. Three
  default RAM-fixture tests skipped because their original captures were
  absent. The broad run was stopped while it was still running graphics
  tests, and the focused run used the existing test build.
- Running the world and pose readers directly on three existing Wartok
  Canyons captures passed 6,665 assertions. The UI reader passed 12
  assertions on the two on-foot captures. The generic UI fixture wrapper
  failed two assertions on the mounted capture because it requires
  on-foot inventory state. A direct call to the built UI reader on the
  same capture verified the expected `Dragon` status, disabled inventory
  and available quest data. The passing UI unit suite covers that case.
