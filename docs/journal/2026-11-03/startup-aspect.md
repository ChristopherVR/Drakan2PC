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
