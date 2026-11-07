# Multiplayer map pictures showed a frame of the last match (2026-11-07, issue #1)

Report (RabeOhneNamen): after a multiplayer match, the map's picture in the multiplayer menu looks like a random, ripped
frame of that match instead of a proper preview.

## Root cause

Not a texture lifetime problem. The menu's map textures (`g_mapPreviews`, `drakan_settings_host.cpp`) are immutable D3D11
textures made from PNG files, only released in `detachRenderer()`; nothing in a level load frees or aliases them.

The frame is a real file. Since 2026-11-05 (`issue-1-multiplayer-language.md`) `mpPreviewCapture` no longer moves the
player to the authored preview point (that teleport was replicated to the other players); it captures *the current view*
about 12 s into the first match on a map without a picture and writes it to `%LOCALAPPDATA%/Drakan2PC/previews/`. The
menu then loads that file as the map's picture for good. What it shows is whatever the player looked at in that moment
(a wall, a fall, mid turn, the HUD cropped away). The tester's log loads three such files at the title
(`map picture 9cc1bc85: loaded (2560x1440)`, the window size), one per built-in arena.

## Fix

* `drakan_map_preview_policy.h`: the in-match capture is opt in (`DRAKAN_MP_CAPTURE=1`), and the data folder those
  captures went to (and `previews/captured/`) is only read when it is on, so the old captures on players' disks stop
  showing. `previews/<name>.png`, `previews/mods/` and an explicit `DRAKAN_PREVIEW_DIR` (the harnesses' picture source)
  are read as before. Maps without a picture show the drawn sketch.
* Test: `map_preview_policy` (ctest, `scripts/tests/CMakeLists.txt`).

A good automatic picture needs a camera that can be placed without moving the networked player (a camera hook), or an
offline authoring pass (`DRAKAN_ARENA_PREVIEW_SHOT`) whose output ships as `previews/<name>.png`.

## Verification

One instance from `dist/DrakanPC` (scripted input: Host Game on Surdana Meadow, Begin Game, Start Match, ~34 s in the
match, Esc > Quit > Quit to Title, back to Host Game), with an old current-view capture
`%LOCALAPPDATA%/Drakan2PC/previews/Surdana_Meadow.png` on disk:

* default: no `map picture` lines in the log, no capture during the match, and the Host Game card shows the overhead
  sketch before and after the match (`build/harness/issue1-mappreview-fixed/`).
* `DRAKAN_MP_CAPTURE=1` (the old behaviour): five old captures load at the title and the card shows a random gameplay
  frame of a castle gate as Surdana Meadow's picture (`build/harness/issue1-mappreview-optin/`), the reported symptom.
