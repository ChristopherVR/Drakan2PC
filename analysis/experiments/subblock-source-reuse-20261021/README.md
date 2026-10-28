# Deferred sub-block uploads and exact source-byte texture reuse

Private candidates. Apply byte-upload.patch after block-upload-20261021;
apply source-reuse.patch after byte-upload.patch, and upload-memo.patch after
source-reuse.patch. All three patches were applied
in isolated Git fixtures and compared with the built sources.

The 8x8 T8 path records CPU-owned bytes while preserving GPU-owned bytes.
Exact small-texture reads may use the uploaded bytes without readback.
A broader CPU read or render-target reuse merges the bytes through the original
readback path. Complete replacement cancels the pending merge. Raw-pointer and
interrupted-transfer fallbacks remain conservative.

Source reuse compares exact VRAM byte snapshots after coherence and stamp checks.
It applies only to stable nonvolatile texture slots, preserving the volatility
window and falling back for UI, wrap, preview, and highlight changes. Snapshot
storage is charged to the texture cache budget. Changed source/mip bytes decode
normally; unchanged sources retain callbacks and GPU validation.

The upload memo compares complete payloads byte-for-byte and checks VRAM block
stamps before skipping CPU stores. Coherence, stamps, and callbacks still run.
The memo is limited to 128 entries of at most 64KiB each (8MiB total payload).
Raw-pointer exposure disables it; reset clears it.

Tests include test_synthetic.cpp from the renderer's existing tests. Build with
gsr, src/include paths, and GSR_OUT_DIR defined. GSR_TEST_SUBBLOCK_MIN=10 runs the
mixed-ownership cases added after the first sub-block suite.

Results: ../../../docs/journal/2026-10-21/data/subblock-source-reuse-20261021.json.
