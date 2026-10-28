# RGB24 upload ownership and VU ready-flag guard

Private candidates; consistent 144+ raw FPS has not been established.

Apply `rgb-upload.patch` after the source-reuse candidate from
`../subblock-source-reuse-20261021/`. It accepts only complete nonwrapping
64x64 CT24 uploads at x=y=0 with buffer width 1. Every fourth storage byte
remains GPU-owned. Covered CT24 texture reads skip the conservative full-block
flush; broader reads and GPU reuse retain the byte-merging fallback.
The upload buffer now correctly counts three bytes per CT24 pixel.

`test_rgb_upload.cpp` includes the existing renderer `test_synthetic.cpp`.
Link with gsr, include src, and define GSR_OUT_DIR. It checks scale 1 and 4,
fragmented transfers, intervening CPU observation, full/partial replacement,
raw-pointer exposure, GPU draws, repeated and mixed uploads, decoded textures,
and exact VRAM alpha bytes. Existing sub-block and source-reuse suites also ran.

Apply `vu-ready-guard.patch` to the frozen packed-cache-fusion VU baseline.
It skips out-of-line flag retirement when the FIFO is empty or its oldest result
is not ready. The original retirement entry point and ABI are retained. Only
core/native-kernel objects were rebuilt; unrelated generated AOT objects are
byte-for-byte baseline objects. The source patch affects the core and native
finishFlagBatch path. No multicore scheduling behavior changed.

Private binaries are under build/texture-gameplay-20261021/{rgb-upload,rgb-vu}.
The first changes only renderer code; the second also links the VU guard.
Both patch applications were checked in isolated fixtures against built sources.

Evidence: ../../../docs/journal/2026-10-21/data/rgb-upload-20261021.json.
