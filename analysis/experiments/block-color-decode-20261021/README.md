# Block color decode experiment

Private, unpromoted. Apply after the native upload and coalesced upload experiments.
The patch removes per-pixel layout dispatch and page/block calculations for CT32 and
CT24 upload rows. It retains the reference reader for all other formats. Aligned
256-byte blocks cannot straddle the 4 MiB boundary; each block base is wrapped first.

Compile test-blockdecode.cpp as C++20 with the patched runtime/gs/src include path.
It compares 8,573,040 pixels with LocalMemory::read, verifies output sentinels and
unsupported-format fallback, then alternates reference/block decoder timings.
Those timings are component diagnostics, not measured gameplay gains.

The patch was applied to an isolated baseline and matched the tested source.
Results: ../../../docs/journal/2026-10-21/data/block-color-decode-20261021.json.
