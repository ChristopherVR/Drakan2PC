# Menu goes soft/blocky when New Game or a save is chosen; subtitles missing from dist

## Subtitles
`dist/DrakanPC/subtitles` was only ever hand-staged (the `.srt` files are gitignored game text) and survived builds
through the keep-list. Fresh dist, another `-DistDirectory`, or a deleted folder lost them. Fix: persistent gitignored
source `local/subtitles/`, copied into the play folder by `Build-LocalGame.ps1` (warns if neither exists).

## Blur on load / new game (fixed)
Repro: `scripts/harness/play_run.py` with a recipe of `shot` every 200 ms and `--env DRAKAN_AUTOPRESS=7000`. At the
switch to loading screen 14 (`0x43d340`) the engine redraws the menu at native resolution (a 512x512 grab plus the
live widgets) into the render target once and never clears it, so the whole load showed a blocky, nearest-filtered
menu. Draw-level suppression did not help: the stale pixels stay in the target.

Fix (`d3d11_present.cpp`): `renderDisplay` keeps a GPU copy of the last presented menu frame (`menuHoldTex`, taken on
every latch while the GUI screen is neither gameplay nor 14; dropped in gameplay so a level change never shows a stale
menu). While screen 14 is up, the latch blits that copy over the top 85.5 % of the display, leaving the loading bar
band below it untouched. Result: the HD menu stays up under the loading bar until the level starts.
