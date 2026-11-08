# PC Settings help panel: preview pictures flickered

Report: the setting pictures in the PC Settings help panel flicker, as if loading for the first time.

## Cause

`SettingsPages::drawHelp` (`runtime/gameui/src/SettingsPages.cpp`) asks the host for the picture of the value the
focused row shows right now (`settingShotId`). The host (`settingShotPicture` in
`ps2recomp/ps2xRuntime/src/lib/drakan_settings_host.cpp`) only registers the PNG path; `updateMapPreviews` then decodes
it on a worker (`std::async`, a 1280 x 720 PNG plus the black-bar scan) and uploads it some frames later. Until then
`previewCrop` has no texture, so `GameUiRenderer::image` draws nothing and the 16:9 box is an empty inset. Every value
change (a toggle, a choice, each slider step) and every newly focused picture row therefore showed: picture, empty box,
new picture. Decoded pictures were already kept for the session (`g_mapPreviews`, never evicted), so the flash was
literally the first load of each value, repeated across values.

## Fix

* `gameui::setSettingShotReady` (host: id -> uploaded, or its decode finished without a texture) and
  `gameui::settingShotToDraw(wanted, shown)`: the panel draws the wanted picture once it is ready, else keeps the last
  ready one (`SettingsPages::shownShot_`) up. A value whose file fails to decode shows the empty box, not the previous
  value's picture.
* No change to the decode cache: a picture decoded once is drawn again at once.

## Verification

`gameui_preview_test` `testPreviewKeepsLastReadyPicture`: toggling HD textures while the other value's capture is not
ready keeps the first picture in every frame, then switches once it is ready. With the old call the same test fails 6
checks. `ctest --preset runtime-tests -R "settings|ui|gameui"`: 21/21 pass (gameui_ram_test skipped, no data). The
host change in `drakan_settings_host.cpp` is only built by the game build.
