# Complete upload readback elision

Private candidate, unpromoted. Apply after readback-palette-20261021, including
its distinct-resource audit patch. The helper accepts only full, non-wrapping,
page-aligned CT32/T8 uploads covering every storage byte. A dirty CT32 render
target can be discarded only when its full conservative dirty page span is
covered and no pending world resolve exists. IMAGE payloads may span submissions. Eligible chunks are buffered until complete.
An intervening command resolves the original readback and replays the buffered
prefix. Exposing raw VRAM disables deferral for that renderer lifetime.

`test-complete-upload.cpp` checks packet availability and exhaustively enumerates
bytes for accepted coverage cases. `test_complete_overwrite.cpp` includes the
existing renderer test_synthetic.cpp helpers; build with gsr and its src/include
paths, defining GSR_OUT_DIR. It exercises full, aliased, partial, truncated,
fragmented, intervening-command/read, and unsupported-format transfers at scales
1 and 4. It checks raw VRAM (including alpha), captures, and readback policy.
The capture API intentionally forces alpha to 255; expected captures do likewise.

The tested patch was applied in an isolated fixture and matched built sources.
Results: ../../../docs/journal/2026-10-21/data/complete-upload-20261021.json.
