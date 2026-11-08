# CI: world_readback exit 0xc0000409 after the shader prewarm

CI run 42438833806 (commit 355d4518, "Windows runtime checks / no game data") failed ctest `world_readback` with
exit code 0xc0000409 and no output. At 143e97e6 the same test was skipped (exit 77): the clean runner has no DXR, so
`RayWater::ready()` is false.

Cause: 4deaf0b0 added the start-up shader prewarm thread (`D3D11Backend::Impl::prewarmThread`, started from
`init()`). Only `~D3D11Backend` joined it. `test_world_readback` builds a `D3D11Backend::Impl` directly, so on any
return from `main` (the skip path on CI, the pass path locally) the still joinable `std::thread` was destroyed and
called `std::terminate`, which the UCRT reports as a fail-fast 0xc0000409. Buffered stdout to the ctest pipe was lost,
so even the pass line never showed. Reproduced locally on an RTX 5080 (same exit code; `GSR_SHADER_PREWARM=0` passed).

Fix: `D3D11Backend::Impl` now has a destructor that calls `joinPrewarm()` (idempotent; the backend destructor still
joins earlier, before NGX and the pools go). New ctest `renderer_teardown` (`tests/test_renderer_teardown.cpp`)
creates the renderer with WARP allowed, so it runs on the clean runner, and destroys it right after `init()`, once
with only the shader prewarm and once with a ray build started. It exits 0xc0000409 without the fix.
