# CPU upload block decoder and audited gameplay, 2026-10-21

The 144+ raw FPS target remains unmet. These candidates are private and unpromoted.

## Code and validation

Added a CT32/CT24 render-target decoder that computes page and block addresses once per eight pixels instead of invoking the general GS memory reader for every pixel. Other formats retain the reference path. This builds on the GPU native upload and adjacent dirty-row coalescing candidates.

The Release test passed 8,573,040 exact pixel comparisons against LocalMemory::read, including widths 0/1/7/8/9/63/64/65/512, base addresses 0/1/31/32/16360/16383, buffer widths 0/1/3/8, row offsets crossing pages and VRAM wrapping. Output sentinels and unsupported-format fallback passed. The archived patch was applied to a separate fixture and matched the tested source.

The alternating decoder-only test was faster for the block decoder, but external load affected timings and this is not a gameplay FPS claim. The complete raw log is in the JSON evidence.

The previously validated seven-block VU zero-reset candidate was retested with its switch forwarded and activation marker checked. It uses its own matched older renderer; compare it only against its geometry control.

## Gameplay results

| Run | Raw world FPS | Peak external CPU cores | Contended |
|---|---:|---:|---|
| audited-decode-change-c | 17.66 | 13.33 | Yes |
| audited-decode-control-c | 51.39 | 9.47 | Yes |
| audited-full-change-c | 74.36 | 5.85 | Yes |
| audited-full-control-c | 84.47 | 6.36 | Yes |
| audited-geometry-change-c | 51.00 | 13.55 | Yes |
| audited-geometry-control-c | 77.68 | 6.80 | Yes |

This investigation ran one executable at a time and did not run its builds or component tests during these measurements. ModernWarfare was active during the initial comparisons; the process audit recorded substantial external CPU load. Contended runs do not support either a speedup or regression claim. Original and private save hashes remained unchanged; each recorded run exited successfully through scripted quit.

The preceding clean control remains 99.37 FPS. There is still no clean candidate/control comparison supporting promotion. Installed binaries, settings, frame caps and default geometry mode were not changed.

Evidence: [JSON](data/block-color-decode-20261021.json). Reproducible source patch and reference test: [experiment](../../../analysis/experiments/block-color-decode-20261021/README.md).
