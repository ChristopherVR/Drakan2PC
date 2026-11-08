# Title "freeze" after choosing Drakan on the game card (German, Order of the Flame pack installed)

Report: a build from before the card reorder (911fec17) froze in German after "Drakan" was picked on the title's game card
(`game = ask`, `ootf-levels` and `drakan-character-motion` on). The process was gone afterwards and Play.cmd keeps no
console log.

## Repeat

Own play folder `build/gm17/play` (the dist folder's ini, mods, movies, textures, `language = german`, `game = ask`), a copy
of the player's card (`iso/mc0`, newest save Catacombs) through `DRAKAN_MEMCARD_DIR`, input scripts with `clock
milliseconds` / `sync screen 8 3` and key taps, `PS2X_PERF=1`. Runner and recipes in the session scratch dir; outputs in
`build/gm17/runs/<name>/`.

| Exe | Run | Result |
| --- | --- | --- |
| pre-reorder `build/perf14/new3` (a4e62021...) | card, ENTER (Drakan), New Game | `[crash] 0xc0000005 read 0` on the main thread right after `overlay: title session 1 opened`, before any key; window stops, process exits about 5 s later |
| dist before this fix (9b343916..., card reorder) | same | same crash, same place |
| same, under cdb | idle on the title | same; stack below |

Main thread stack (map file `build/final-game/bin/drakan.map`): `RayWater::status` (this = null) <
`D3D11Backend::waterRenderingStatus` < `DrakanSettingsHost::capabilities` < `ModernSettings::tick` < `pcSettingsSchema` <
`titleDraw` < `hostScreenDraw` < `SettingsOverlay::render` < `overlayCallback` < `presentLatched` < `GsrBridge::tickWindow`.
The ray prewarm thread was meanwhile inside the NVIDIA driver compiling the DXR compute pipelines
(`CreateComputePipelineState`); with a cold driver cache that took 18 s here (1 s once cached).

## Cause

Two faults meet since the ray tracing prewarm (4deaf0b0):

* `waterRenderingAvailable` answers "Ray tracing is starting" while the prewarm thread still builds the pass, leaving
  `waterRenderer` null; the settings host then asks `waterRenderingStatus`, whose "pass completed" branch read
  `waterRenderer->status()` without a null check.
* That branch was taken on the title because `lastPresentedWater` was true there: the present latch compared
  `waterOutputEpoch == worldEpoch`, and both are 0 for the first world images (logos, the title backdrop) before any water
  pass ran. The same holds for `rayOutputEpoch`, and for the WebGPU backend.

The menu re-reads its capabilities every 0.5 s, so the title crashes on the first re-read while the DXR pipelines are
still compiling. A crash before the compile finishes leaves the driver cache cold, so every launch of that exe repeats
it. Nothing card, language or campaign specific: German, the card order and the game switch only decide when the player
happens to be looking at the title. The prewarm was in the reporter's build too (same crash offset in the older exe).

## Fix

* `runtime/gs/src/ray_status.h`: `outputLatched` (an output epoch 0 never matches) used by both present latches of both
  backends, and `waterStatus`, the modern water text, which only reads a pass that exists. D3D11 also reports "Ray tracing
  is starting" there while the prewarm runs.
* Test: `gsr_test_ray_status` (ctest `ray_status`): the epoch-0 latch, the water text without a pass, failed and ready
  passes.

## After the fix (exe 82832194...)

| Run | Result |
| --- | --- |
| card, Drakan, New Game, Solo (DXR compile 18 s, title open meanwhile) | no crash; `Level 0 - Intro` from `\GERMAN`, intro movie plays to 120 s |
| card, Order of the Flame, Esc back to the card, Drakan, New Game | `switched to ootf` (card `mc0_ootf`), `switched to d2` (card `mc0`), intro loads and plays |
| card, Drakan, Continue | Catacombs loads from the save; first gameplay frame holds about 5 s while DLSS and the NR interop are created (`[perf-stall]`: EE in `commitDisplay`, main thread in `tickWindow`), then 120 fps. A first-frame hitch, not a hang |

## Still open

* The game card's descriptions ("The game on your disc.", the Order of the Flame line) are English in the German menu.
* With a cold driver cache, a world frame that needs ray tracing before the prewarm finishes waits for it on the GS
  worker (`takeRayWater`), up to the compile time.
* If a report like this comes again: send `drakan.log` (and `drakan.1.log`, the run before) from next to the exe, which
  hold the `[crash]` lines even without a console, before the next build: Build-LocalGame refills `dist/DrakanPC` and does
  not keep them. Starting `drakan.exe` from a console (or `console = 1`) shows the same text live.
