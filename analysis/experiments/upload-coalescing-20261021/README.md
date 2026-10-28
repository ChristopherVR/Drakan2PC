# Coalesced render-target upload experiment

Private prototype, not deployed. Apply the preceding
`../texture-submission-20261021/native-upload.patch` first, then this patch.
The test uses `rt_native_upload.h` from that preceding patch and `rt_dirty_runs.h`
from this one. It checks exact RGBA output and preservation of clean GPU rows
across all 256 dirty/clean patterns, in addition to the prior scale/state tests.
The patch was applied in an isolated fixture and matched the tested source.
Results: ../../../docs/journal/2026-10-21/data/upload-coalescing-20261021.json.
