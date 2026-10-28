# Sub-block upload ownership and redundant texture work — 2026-10-21

Consistent 144+ raw FPS has not been achieved. Three private renderer candidates were built and tested. The installed game and root renderer sources were not replaced. Save hashes remained unchanged in every recorded gameplay run.

## Implemented changes

`byte-upload` extends the preceding `block-upload` candidate to complete 8×8 T8 uploads. These replace only 64 bytes of a 256-byte storage block. Per-target byte masks preserve GPU ownership of the other bytes. Exact 8×8 texture reads can use the uploaded bytes without a GPU readback. Broader CPU reads and render-target reuse still merge through the conservative readback path; full replacement cancels obsolete ownership. Fragmented transfers, raw-pointer exposure, and unsupported aliases retain conservative fallbacks.

`source-reuse` adds exact source-byte comparisons for stable texture slots whose stamps changed. Unchanged contents retain the decoded texture and GPU resource instead of decoding and upscaling again. Source snapshots count against the cache budget. Changed base/mip bytes, UI interpretation, wrap state, volatility windows, preview state, and highlight interpretation fall back to the existing decode path.

`upload-memo` additionally caches up to 128 complete payloads of at most 64 KiB each. Matching bytes and unchanged block stamps can skip CPU swizzled stores, while coherence handling, stamps, and callbacks still run. Reset clears the cache; raw-pointer exposure disables eligibility. This remains experimental: it produced eight hits per sampled movement latch but no steady combat hits, and raw timings do not justify its extra cache work.

## Correctness validation

- Source candidate: 16,990,208 GPU pixel comparisons and 74 exact 64 KiB VRAM comparisons across sub-block, mixed ownership, full overwrite, fragmented transfer, CPU observation, raw-pointer, and later GPU draw cases at scales 1 and 4.
- Source texture reuse: 23,040 exact mip pixels and 13 format/CLUT assertions. Base/mip changes, wrap/UI changes, and volatility-window rollover invalidated reuse as intended.
- Upload memo: 2,228,224 GPU pixel comparisons, four exact 64 KiB VRAM comparisons, and 60 upload callbacks; additionally 3,620,864 sub-block GPU pixel comparisons and 26 exact VRAM ranges. Source reuse tests also passed against this candidate.
- Each of six gameplay diagnostic runs checked 32 distinct GPU texture resources with zero mismatches. The memo's first 32 CPU payload checks also had zero mismatches; these checks are not world-gated and may include menus.
- All three archived patches were applied in isolated Git fixtures and their resulting source contents compared with the built trees.

## Gameplay diagnostics

Routes use the existing Level1A Surdana movement and hostile combat fixtures. They exercise relevant gameplay paths, but the exact city viewpoint reported by the user has not been visually confirmed. Routes are wall-clock timed, so different CPU contention changes simulation progress; these are not matched-frame comparisons.

Final five profiling windows:

| Candidate / route | Latches | Readback maps | Texture decodes / latch | Source reuse hits / latch |
|---|---:|---:|---:|---:|
| byte-upload / movement | 930 | 0 | 85.986 | 0 |
| source-reuse / movement | 886 | 0 | 14.308 | 25.649 |
| byte-upload / combat | 650 | 0 | 135.569 | 0 |
| source-reuse / combat | 654 | 6 | 39.613 | 39.399 |
| upload-memo / movement | 859 | 0 | 14.681 | 25.314 |
| upload-memo / combat | 379 | 2 | 42.377 | 39.662 |

These demonstrate exercised paths and avoided redundant work, not an isolated speedup. Source combat recorded 17 sub-block deferrals, 38 covered texture reads, and four merges. Memo combat recorded 25 deferrals, 54 covered reads, and two merges. Deferral counts are not equivalent to maps avoided.

The remaining combat trace includes PSM_CT24 64×64 uploads at blocks 16293/16203 in the source run and 16315 in the memo run, overlapping CT32 render targets. The memo trace also includes a PSM_CT16 32×32 upload at block 15407. These are outside the 8×8 T8 shortcut. A CT24 extension must preserve the GPU-owned alpha byte for every pixel; treating it as a full CT32 overwrite would corrupt memory. No such extension has been enabled. Some merges remain necessary for broader reads or render-target reuse.

## Raw FPS and CPU evidence

Final runs disabled profiling, reuse validation, frame generation, and the FPS cap. All were externally contended:

| Candidate / route | Raw world FPS | Other processes, average CPU cores |
|---|---:|---:|
| source-reuse / movement | 66.57 | 7.25 |
| upload-memo / movement | 36.05 | 7.35 |
| upload-memo / combat | 42.07 | 8.08 |
| source-reuse / combat | 36.87 | 9.63 |

These cannot establish a speedup, regression, or 144 FPS consistency. The memo movement run included a 1.46 FPS two-second window. Earlier runs and diagnostic interval distributions are preserved in the results JSON rather than selectively excluded. Competing ModernWarfare activity was observed; unrelated processes were not stopped.

An independent combat thread-CPU capture over 17.57 seconds measured approximately 0.752 cores on the VU geometry worker, 0.520 on the graphics/GIF worker, and 0.245 on the EE worker. Wall RIP samples identify thread roles but include blocked time; they are not CPU-time function percentages. This keeps vector execution and graphics submission relevant even when upload maps disappear. Map elimination alone is insufficient to reach the target.

Next work should target CT24 byte-preserving ownership and measure VU arithmetic/pipeline work with thread CPU counters. A clean raw run with frame-time percentiles and matched city/combat image coverage remains necessary before promoting a candidate or claiming consistent 144+ FPS. Prefer `source-reuse` as the next experimental baseline; the upload memo has no demonstrated combat benefit.

## Reproduction and artifacts

- `analysis/experiments/subblock-source-reuse-20261021/`: sequential patches and renderer tests. Apply byte-upload after block-upload, then source-reuse; upload-memo is optional and experimental.
- `docs/journal/2026-10-21/data/subblock-source-reuse-20261021.json`: binary hashes, test logs, all 14 gameplay runs, path counters, reuse checks, contention, and diagnostic intervals.
- `docs/journal/2026-10-21/data/source-reuse-thread-profile-20261021.json`: independently measured thread CPU times and associated wall-sample states.
- Private preferred executable: `build/texture-gameplay-20261021/source-reuse/drakan.exe`, SHA256 `07B81AF3721EFE1C2B325477D61569EF63E2AFBD9EFC00408FC7EA080884865A`.
- Build: `build/build-texture-gameplay-20261021.ps1 -Variants source-reuse`.
- Scripts under `build/texture-gameplay-20261021/`: `run-byte-source-diagnostics.ps1`, `run-byte-source-raw.ps1`, `run-memo-diagnostics.ps1`, `run-memo-raw.ps1`, `run-source-cpu-profile.ps1`, `watch-source-cpu.ps1`, and `archive-byte-source.py`. Use fresh names for new gameplay runs.
