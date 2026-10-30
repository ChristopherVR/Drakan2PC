# CI and install banner review

Reviewed the failures in [Actions run 40975729601](https://github.com/ChristopherVR/Drakan2PC/actions/runs/40975729601)
against checkout `ab02323`. The linked run checked out `23c8f77`, not the later renderer status and installer changes.
This is a review; no implementation fixes or remote changes were made.

## Confirmed findings

- **P1: Linux static archive ordering.** `runtime/gs/CMakeLists.txt:105-106` puts the backend selector into
  `libgsr_webgpu.a`, before `libgsr_core.a` on the generated link line. The test initially references `Renderer`,
  so GNU ld skips the selector object; `gs_core.cpp.o` subsequently introduces unresolved `Backend` references.
  The selector/core/backend dependency needs explicit object inclusion or a correctly represented rescan group.
  Building `gsr_test_synthetic_webgpu` with the existing `linux-runtime-tests` preset reproduced the CI link
  failure on the current checkout under WSL. An ignored probe replayed the same link command with the static
  archives enclosed in `--start-group` / `--end-group`; it linked successfully without changing source files.
- **P1: Missing Emscripten fetch linkage.** The `texture_core_tests` branch in
  `scripts/buildtool/check_web_runtime.py:91-99` compiles `gs_texup.cpp` without `-sFETCH=1`. Its browser texture
  loader references `emscripten_fetch_attr_init`, `emscripten_fetch` and `emscripten_fetch_close`. All three are
  undefined at link time. `python3 scripts/buildtool/check_web_runtime.py --test texture_core_tests` reproduced
  this with the pinned SDK. Other branches compiling this source already include `-sFETCH=1`. An ignored probe
  added that flag to this command only: compilation succeeded and all 30 texture checks passed in Node.
- **P2: Install banner text exceeds the SVG viewport.** The deployed `assets/art-build.svg` matches
  `docs/site/assets/art-build.svg`. Its heading starts at x=338 with font-size 38; in Windows Chromium with
  Castellar, `getBBox()` measures its right edge at x=980.40, outside the 960-unit viewBox and 944-unit inner
  border. This clips the ending of "The build runs on your PC" wherever the banner is used (Windows, Linux and
  Browser install steps). In a browser-only preview, font-size 34 puts the right edge at x=912.79 with Castellar
  and x=732.14 with Georgia. The preview did not modify the SVG source. Evidence screenshot:
  `build/review-art-build-before.png` (ignored).

## Windows validation blocker

The CI Windows job built successfully, then `ray_trace` timed out after 120 seconds on the Microsoft Basic
Render Driver. `synthetic_webgpu` subsequently aborted with `Parent device is lost` in buffer writes, command
encoder finish and queue submit. These are separate from the two link failures above.

The current Windows test targets rebuilt successfully. Forcing `DRAKAN_GPU_BACKEND=d3d12` locally also made
`ray_trace` exceed its timeout (125.23 seconds reported by CTest), this time on an NVIDIA RTX 5080. The timeout
therefore is not limited to the CI software adapter. The log has no pass status after device creation; the exact
stall location has not been established. Do not treat increasing the timeout or skipping the test as a proven fix.

The forced-D3D12 `synthetic_webgpu` test passed locally in 111.46 seconds. The CI device-loss abort was not
reproduced on the local hardware; the cause remains open.

The updated settings schema test passed: 873 checks, zero failures.
