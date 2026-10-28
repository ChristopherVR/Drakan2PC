# CPU ceiling without effects (23 October 2026)

Follow-up to [the functional VU1 engine](vu1-functional-engine.md). With DLSS, neural rendering and ray effects off,
Surdana ran at ~175-190 world FPS with the VU1 worker and the GS worker both near saturation. Goal: remove per-run,
per-packet and per-qword work on the VU worker and per-primitive work on the GS worker without changing behaviour.

## Changes (in commit order)

| Commit | Change |
| --- | --- |
| 5ef9893 | `resetScheduler` skips clearing the exact-engine pipelines/ready tables/stall ring (several KB per MSCAL) while only the functional engine ran since the last full reset (`Vu1Fast::Spare::schedClean`). The `PS2X_VU1_STATS` scope reads the clock only when enabled. |
| dcababb | GIF records of one VU1 job are staged per thread (`gsr_batch::Scope` in `vu1async::Worker::run`) and appended to the bridge queue once; every other record of that thread and every wait for the renderer appends them first (`GsrBridge::Impl::lockProducer`), so the renderer order is unchanged. |
| fce926a | The functional engine sends non-wrapping XGKICK packets straight from VU memory (no copy into the 64 KB buffer). |
| 6978954 | `vif_unpack::maskedRuns`: SSE2 path for STMASK and STMOD 1/2 unpacks without fill cycles (they went through the per-lane scalar loop). Exact-width source loads. |
| 65f57d4 | PSMT8 uploads: four rows of a 16-aligned rectangle fill one 64-byte block column with 16 PSHUFB/OR. |
| aae9eda | `beginDraw` reuses the draw state of the last primitive type until a non-vertex register, a CLUT load or a reset invalidates it; CSM1 CLUT entries are read through the page offset tables. |
| e293eef | PATH1/PATH2 packets that would be drained at once from an empty GIF arbiter skip the arbiter copy (same `GsCapture::onPacket` + `GS::processGIFPacketPath` calls; `gs/gs_direct_path.h`). |
| b9b1f67 | `GS::processGIFPacketPath` skips the frontend's recursive mutex on the VU1 worker (the bridge orders its records; `m_gsr` outlives the worker). |
| ad4acd4 | Fixed-capacity draw batch (no growth checks per vertex); `kick` builds the vertex in its queue slot. |

## Measurements

Machine shared with another agent's builds and runs; all FPS numbers are interleaved A/B runs of the baseline exe
(main 64bb666, copied from the main dist) and this branch, `still.txt` recipe, effects off, no HD textures.

- VU replay (`DRAKAN_VU_BENCH`, 20,000 Surdana runs): `check: 20000 runs, 0 mismatches` after every VU change;
  16.4 -> 15.3 ms best of 20 (1.05 -> 0.98 ns/pair; the bench runs `execute` per run, so it sees the cheaper reset).
- World FPS (still view, 9 interleaved pairs): baseline 168-188 (mean ~179); this branch 204-211 after the
  first six commits, 228-238 after all nine. The last runs sit at 236-239 with a 1-2 FPS spread: the uncapped world
  rate waits for the host to show each frame (`waitForHandoff`) and the host presents ~240 times per second here
  (`flips/s`), so 240 is the current measurement ceiling, not the CPU.
- In-game VU1 throughput: 575 -> 750 M pairs/s (3.19 M pairs per frame in both). GIF packets: 1.2 -> 1.5 M/s.
- GS worker (`DRAKAN_RUNTIME_PROFILE=1`): GIF execution 3.64 -> 2.48 ms per frame, busy time ~4.4 -> ~3.2 ms per frame
  (idle 14 % -> 18 % of wall time at a 27 % higher frame rate); wake-ups 80k -> 8k per second.
- Host sampler, VU worker (share of its samples): VIF unpack (processVIF1Data + fast + masked) 14.5 % -> ~6 %;
  arbiter copy ~3 % -> <1 %; frontend lock 2 % -> 0; microcode blocks 50 % -> 68 %. ~5 % of the worker's samples are
  the idle spin and ~3 % its idle wait, i.e. it is ~92 % busy at 238 FPS (~258 FPS if it were the only limit).
- Host sampler, GS worker: `image_upload::write<PSMT8>` 12 % -> `writeT8Rows` 1.6 %; vertex path (kick, emitTri,
  toDV, gifSubmit, handlePacked, beginDraw) ~25 % of its samples.

## Verification

- VU replay: 0 mismatches (registers, VU memory, every GIF packet).
- `vif_unpack_tests`: 30k random masked/offset uploads against the scalar rules, plus the existing guards; all VU
  standalone tests (`ps2xTest/vif_unpack`, 8 tests) pass.
- GS tests (`runtime/gs`, 24 tests, `live` skipped): pass; the upload test adds aligned PSMT8 shapes and was checked to
  fail with a deliberately wrong shuffle table.
- `runtime-tests` preset: 38 pass, 4 skip (data-dependent); `native_display_commit` does not link because its
  recording backend lacks `Backend::beginTargetUpload`/`completeTargetUpload` (added in 1f43ac7) and the
  `drakan_ws::initialPresenter` stub: pre-existing, not touched here. (It also exposed a duplicate `gsr_batch`
  definition, fixed in b159836.)
- Screenshots at frames 900/1500: same scene as the baseline. The quest message at frame 900 (shown on this branch
  because frame 900 is earlier in game time) renders identically in a baseline shot at frame 650.

## Remaining limits

- Presentation: world frames are latched until the host shows them, so the world rate cannot exceed the host present
  rate (~240 here). Going past it is a presentation design decision (`d3d11_present.cpp`, `waitForHandoff`).
- VU1 worker: the microcode blocks are now ~68 % of it; the rest is VIF unpack (~6 %), per-run entry/exit (~5 %),
  GIF record copies (~3 %). Another large step needs faster blocks or VU work on a second core (`DRAKAN_VU_BATCH`).
- GS worker: per-vertex GIF parsing and draw emission (~25 %), `Backend::draw` and its vertex copy (~6 %, other
  owner), `pixelAddr` from block stamping/target sync (~2.5 %). `pullSigFromGsr` reads SIGLBLID through the renderer
  lock once per GIF record.
