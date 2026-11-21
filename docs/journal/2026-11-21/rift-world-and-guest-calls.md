# Rift World profile and guest calls

## Rift World tunnel

The latest installed Windows PAL build at the start of this investigation
ran the existing Rift World tunnel route on a Ryzen 7 9800X3D and RTX 5080.
The harness loaded the level, turned the camera into the tunnel, and
captured the view at both ends of each window. Settings were D3D11,
3840 × 2160 output, 2560 × 1440 DLSS Quality input, ray-traced shadows,
reflections and global illumination. Modern Water, VSync and the frame
cap were off. The harness verified the level, DLSS mode, dimensions and
clean exit.

The baseline used frame and per-thread counters with stack sampling
off. The game completed 2,771 world frames at **121.1 world FPS**, with a
median interval of **7.42 ms** and a 99th-percentile interval of **22.4 ms**.
GPU utilization was **98% median** over 46 samples. The Emotion Engine
(EE) thread used **7.62 ms CPU per world frame**, or 92% of one logical
processor at that rate. The view was GPU-saturated, and the game thread
also had little headroom. The
[earlier Rift World captures](../2026-11-20/ootf-4k-gpu.md#rift-world)
showed that this view can instead be CPU-limited on a different build or run.

Two separate 35-second CPU sampling runs collected 16,342 and 16,458 EE
stacks. The linked symbols came from the same executable as the captures.
Each share below is a percentage of stacks in one run. Inclusive rows
can overlap, and wall-clock sampling includes time spent waiting.

| Work | Share of sampled game-thread stacks |
| --- | --- |
| World draw (`0x139010`) | 39.8–41.0% inclusive |
| Terrain draw (`0x152330`) | 22.7–23.7% inclusive |
| GS store-image and synchronization (`0x1dbfa8`) | 18.7–21.9% inclusive |
| Guest vector/geometry routine (`0x116990`) | 10.8–11.0% at the top resolved frame |
| Memory slow path (`PS2Memory::read64`) | 6.8–7.8% at the top resolved frame |
| Terrain surface lookup (`LookupTerrain::surface`) | 5.2–5.4% at the top resolved frame |

The game ran one GS readback per world frame and performed about 135,000
FINISH polls per frame during the baseline. Start with the measured GPU
work at 4K and with terrain traversal and GS synchronization when the
game thread limits throughput. Retest using world frame intervals.
The previous [readback investigation](../2026-11-12/perf-round2.md)
describes the exact `eager` path, which can block for the same bytes
without repeatedly polling. This profile measured the default path.

In a separate GPU-instrumented run, 1,235 DXR passes gave a median
**3.41 ms for lighting composition** and **5.34 ms for the full DXR pass**.
Another game was running during that capture. The GPU timestamps
identify composition as the largest measured DXR stage in that run.
Use the separate baseline for gameplay throughput.

## C++ guest call changes

- Co-op message and loot-spew hooks check the exact guest dispatch entry
  before replacing it and only retain the original when replacement
  succeeds. A second install leaves the active hooks in place.
- Input, menu, and portable camera adapters share checked synchronous
  dispatch. The caller can now distinguish a legitimate zero return
  from a call that stops early. The camera pitch check fails closed if
  the audited handler cannot return.
- The guest scheduler now receives all provided integer and float
  registers. Co-op message replay already supplies the message length
  and flag in arguments five and six. Co-op and PvP now use one named
  `postPlayerDamage` helper for the shared six-argument native method.
  The helper keeps the scratch damage word available until the current
  posted work finishes.
- Co-op state updates that depend on a post use its acceptance result.
  Immediate calls still require an audited nonblocking routine and live
  guest objects. See the [input integration guide](../../../runtime/input/README.md#wiring-into-drakan_overridescpp).

## Verification

The Windows PAL game built successfully. All 11 focused CTest and 18
Python checks passed. The portable camera source also compiled with the
portable UI configuration on Windows.

Two runs of the two-instance Surdana gold room test each passed all
eight harness checks. In both runs the client opened two chests, the
host replayed both, and the peers produced matching loot keys before
exiting cleanly. They matched nine keys in the first pass and eight in
the final executable retest. An earlier attempt started at the default
arrival and never opened its target chest. The passing runs placed both
players in the gold room using the existing playtest controls.

Raw captures and the symbol map are in the ignored
`build/rift-guest-profile-20261121/` directory.
