# Partial overlap upload candidate

Private, unpromoted. Apply after complete-upload-20261021. Complete uploaded
256-byte blocks become CPU-owned; untouched dirty blocks remain GPU-owned.
Readback requests entirely covered by CPU-owned blocks need no GPU wait.
Render-target synchronization patches contiguous runs of CPU-owned CT32 blocks,
preserving GPU-only pixels. A subsequent draw conservatively restores GPU
ownership. Readbacks merge CPU patches first. Raw pointer exposure drains pending
work and disables future deferral.

The test includes the existing test_synthetic.cpp helpers. Build it with gsr,
src/include paths, and GSR_OUT_DIR defined. It compares GPU pixels before readback,
exact VRAM including alpha, and GPU pixels after readback. Cases include shifted
CT32/T8 uploads, both overlap directions, subsequent draws, repeated uploads,
fragmented/interrupted transfers, raw pointer writes, CT24 fallback, and scales 1/4.

The patch was applied in an isolated Git fixture and compared with tested sources.
Results: ../../../docs/journal/2026-10-21/data/partial-upload-20261021.json.
