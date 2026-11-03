# First-frame title/loading settings

The GS front end queued live aspect, native-menu and HUD settings until presentation/display completion.
An ordered title (`0x43e4c8`, screen 8) or loading (`0x43d340`, screen 14) packet flushed the preceding batch but did
not consume those settings. The new page could therefore draw with the preceding movie's native canvas/artwork;
applying the settings at its display latch was too late to correct those pixels.

`Renderer::noteSceneCamera` now consumes pending settings at these two explicit GUI boundaries, after flushing the
previous page and before publishing the new GUI packet. Other camera/material/world markers retain their behavior.
This is shared by D3D11 and WebGPU and does not change the intended 4:3 presentation of movies.

Validation:

- Added CPU recording-backend checks to `scripts/tests/native_display_commit_test.cpp`: older draw, settings, GUI
  packet ordering; configured aspect/stretch/menu/HUD values before any new page draw; no delayed settings application
  at the first latch. Covers title and loading, 4:3, 16:9, 21:9 and window fill (48 assertions).
- The regression failed against the original GS core and passed after the change.
- Focused CTest run: `native_display_commit`, `widescreen_policy`, `hud_page`: 3/3 passed.
- Before-change cold title captures/logs: `build/harness/startup-aspect-before-20261103/`. The earliest scripted
  capture showed the modern menu over a black background before the wide artwork arrived; this is not a pixel-level
  reproduction of a 4:3 frame.
- Incremental build/deployment through `Build-LocalGame.ps1 -WorkDir build/final-game -Incremental -Jobs 8` succeeded;
  existing player INIs were preserved. Deployed executable SHA-256:
  `5662fb5f21e8c412d5f591541f274c4c95c944c32c16c2de2cf667ca6d81b157`.
- After-change cold startup completed cleanly: `build/harness/startup-aspect-after-20261103/`. The title selected the
  configured window-fill aspect and `screen8` wide backdrop. The earliest scripted capture still has the host menu
  over black before the first title artwork submission; this change does not remove that separate overlay lead-in.
  The specific reported 4:3 flash was not reproduced pixel-for-pixel, so live confirmation by the player is still needed.
- Changed C++ files pass `clang-format --dry-run --Werror`; `git diff --check` and the repository source audit pass.

## Follow-up: title artwork before the handler

The player confirmed the flash remained after `8c24eef` and identified the image as the Drakan title artwork, not
the Sony/Surreal logo or loading text. The previous change only moved settings application to the title-handler
packet; that packet is not guaranteed to precede the title's first artwork draw.

The backends now distinguish the six exact title-tile hashes from the other scenic menu images. A one-shot
`hud::StartupTitle` policy snapshots the configured startup presenter before movie policy temporarily selects 4:3.
At the first title texture draw on a full-size target it establishes screen 8, the configured aspect/fill and the
centered menu foreground, allowing synchronous wide-backdrop replacement before drawing the native tile. If the
host has already selected the title, its current settings win instead; subsequent title draws never restore the
snapshot over a live settings change. Both D3D11 and WebGPU use the same policy.

CPU tests in `test_hud_page.cpp` cover exact title/non-title classification, movie/scenic and off-screen rejection,
4:3/native/16:9/21:9/32:9 and window fill with stale movie settings, one-shot behavior and newer host settings.
Additional pre-change early startup captures are under `build/harness/startup-early-20261103/`.

Follow-up validation:

- Focused CTest: `hud_page`, `widescreen_policy`, `native_display_commit`: 3/3 passed.
- Both D3D11 and WebGPU backends compiled; incremental build/deployment succeeded with player INIs preserved.
  Final executable SHA-256: `5006bc3f9faa56e371c98cc3cc986b6ecb42e5f99a852414448c6f64b8d2566d`.
- Cold D3D11 startup completed cleanly in configured fullscreen/window-fill (1920x1080, vsync off) and forced
  21:9 (1680x720, vsync on). Captures/logs: `build/harness/startup-title-fix-4k/` (historical run name; actual output
  1080p) and `build/harness/startup-title-fix-21x9/`. The first artwork draw selected `screen8`; captured title artwork
  used the wide replacement rather than the native 4:3 tiles. The earlier black-background host-menu lead-in remains.
- These live runs already had host title metadata by the first artwork draw; the delayed-host case is covered by
  the shared CPU policy tests, not claimed as a reproduced live race.
- Changed C++ files pass clang-format; `git diff --check` and the repository source audit pass.
