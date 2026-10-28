# Readback causes, GPU reuse validation, and exact palette flushing

Private, unpromoted. Apply after the exact-mip-resource-reuse experiment.
The first patch adds diagnostic readback reasons and GSR_VALIDATE_REUSE. When
set, the first 64 reused textures during world gameplay are copied back and all
mips compared exactly; failures abort. This intentionally stalls the GPU and
must never be treated as a raw FPS benchmark. Leave the variable unset normally.

The second patch flushes the actual GS storage blocks read by the palette loader,
including CSM2 pitch/offset addressing and VRAM wrapping, rather than a fixed
64-block interval. test-clut-blocks.cpp compares the result with exhaustive
per-palette-pixel address enumeration across 20,000 cases. Compile as C++20 with
runtime/gs/src on the include path. Both patches were applied in an isolated
fixture and matched the built candidate sources.

Results: ../../../docs/journal/2026-10-21/data/readback-palette-20261021.json.
