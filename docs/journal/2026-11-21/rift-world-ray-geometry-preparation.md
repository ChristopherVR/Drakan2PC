# Rift World ray geometry preparation

The Rift World tunnel profile recorded 2,600 timed DXR passes. Preparing
model geometry took a median 2.50 ms per pass, and most passes rebuilt
attributes for six animated meshes. Static terrain preparation usually took
0.02 ms. On terrain refreshes it could exceed 20 ms and reached 32 ms in this
trace. This profile used the same 4K DLSS Quality route with Dota 2 open.

For a welded vertex whose faces all fit inside a cone narrower than 30 degrees,
smoothing computes one weighted normal for the group. Every pair of faces
passes the existing 60 degree test, so the group preserves that test and the
addition order. The other groups continue to check each pair.

An experiment also reused face normals during animated model packing and
appended each triangle as a block. It produced no measured model-packing
savings and was removed. Thirteen gameplay pairs with these changes and the
normal shortcut pooled 98.15 versus 97.51 FPS, with no gaps over 33 ms in
either variant. A separate profiled comparison measured warm terrain geometry
preparation at 23.575 versus 22.930 ms at the median. Model preparation was
2.50 versus 2.54 ms on regular passes.

The renderer also uses a point-sampled draw for a progressive latched image
that is already the size of the viewport. At full coverage the draw skips the
clear. Scaling, letterboxing, the comparison capture and deinterlacing keep
the paths they need.

The scene geometry, DXR rendering and device-loss watchdog tests passed. The
DXR fixture ran with the CPU cache comparison enabled and reproduced all
7,831,552 bytes of the saved fixture snapshots.

## Combined benchmark

One Release comparison build alternated the old and new paths in five-second
phases on the same 4K DLSS Quality tunnel route. Seven valid pairs contributed
about 26.5 seconds per variant. Dota 2 stayed open and its sampled 3D-engine use
ranged from 16% to 21%. VSync, the frame cap and Modern Water were off. The
three DXR lighting effects were on. No phases were excluded for competing
builds or game launches.

| Metric | Before | After |
| --- | ---: | ---: |
| Pooled world FPS | 100.16 | 100.88 |
| 99th-percentile world-frame interval | 16.93 ms | 16.78 ms |
| 99th-percentile presentation interval | 13.10 ms | 13.03 ms |
| Presentation intervals over 33 ms | 0 | 0 |

The median paired FPS change was +0.92%, with a range of −0.75% to +1.74%.
In a separate same-frame check, the original and point-sampled presentation
paths produced identical RGBA8 values for all 215,654,400 pixels across 26
full 4K captures before the host overlay.

The final Release build averaged 101.08 world FPS over 53.09 measured
seconds. It recorded zero world or presentation intervals over 33 ms and
zero slow terrain worker records. Its longest presentation interval was
13.92 ms. Dota 2's median sampled 3D-engine use was 17%, and the renderer
exited cleanly.

Raw traces, captures, and profile scripts are in the ignored
`build/rift-perf-next-20261121/` directory.
