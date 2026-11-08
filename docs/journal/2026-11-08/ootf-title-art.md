# Order of the Flame title art instead of the text plaque

User report: with Order of the Flame chosen, the Modern title drew a large "DRAKAN / Order of the Flame" text plaque over
the native Ancients' Gates logo, and the loading screen after New Game showed the Ancients' Gates cover art. Wanted: the
menu background replaced by the Order of the Flame picture already shown on "Choose your game".

## Cause

* The plaque was `TitleState::brandTitle` / `brandSubtitle`, set in `drakan_menu_screens.inl` for any non-disc game and
  drawn by `TitleScreen::draw`. The art under it stayed the disc's (GUI screen 8, wide image `screen8_*`).
* The loading screen after the title (screen 14) redraws a grab of the title page; the renderers replace it with the wide
  `screen8` image (widescreen.md, "Loading screen after the title"), which is the disc's art.

## Change

* The plaque is gone (field, drawing and its test).
* The host resolves the chosen game's picture (`previews/background-<id>.png`, else its card cover) and passes it to
  `gsr::setTitleBackdropImage`; both renderers use it in place of the `screen8` set, filling the output (16:10 art is
  cropped top and bottom on 16:9) and shrinking very large pictures (the supplied art is 7680 x 4800) to 3840 wide.
  This covers the Modern and the Original title styles and the loading screen, since all three show the screen 8 art.
* Test: `gsr_test_title_backdrop` (set naming, cover placement, shrink). `gameui_saveload_test` checks no plaque.

## Verified

Private run `build/ootf-run/title-art` (D3D11, 1280 x 720, `DRAKAN_GAME=ootf`, Modern title): the main menu shows the
Order of the Flame picture with no plaque; New Game > Solo shows "Loading..." and its bar over the same picture
(`ms_0022025.png`); the log names `previews/background-ootf.png` as the title art. Not checked live: WebGPU, the Original
title style, a 4:3 output (not covered, see widescreen.md).

## Follow-up: the disc art flashed after choosing the game

User report: choosing Order of the Flame on "Choose your game" showed the Ancients' Gates art for a moment before the
Order of the Flame picture. The renderer decoded the 7680 x 4800 PNG on its own thread the first time the title drew it,
after the choice, and the last frame (the disc art) stayed up meanwhile. Now the host asks for every campaign's picture at
start-up (`gsr::preloadTitleBackdropImage`); a worker decodes and shrinks it (`title_backdrop::preload`), and the
renderer only waits if the worker has not finished. Run `build/ootf-run/title-swap` (game card, Up, Enter, 30 ms window
shots, `switch_sheet.png`): the frames go from the card straight to the Order of the Flame art; the log shows the game
switch, the title art and the backdrop set in the same frame as the choice.
