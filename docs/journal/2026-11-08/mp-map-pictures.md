# Multiplayer map cards show a sketch instead of the map (2026-11-08)

Report: in the Multiplayer menu, selecting a map shows a drawn vector sketch, not a picture of the map.

## Cause

Not a renderer bug. The card draws `ImageKind::LevelPreview` only when `mapPreviewId()` finds `<safe map name>.png` in the
search folders (`drakan_map_preview_policy.h`). Since 2026-11-07 (`mp-map-preview-stale-frame.md`) the in-match capture is
opt in and its data folder is not read by default, because those files were random frames. The captures still on disk
(`%LOCALAPPDATA%/Drakan2PC/previews/`) confirm it: stats overlay in the corner, HUD orbs, one fully black (City of the Damned,
"World 0.0 fps", a level not yet drawn). Nothing else made pictures, so every built-in map fell back to the sketch. The 11-07
note named the missing piece: an offline pass with `DRAKAN_ARENA_PREVIEW_SHOT` whose output ships as a picture file.

## Fix

* `scripts/harness/map_previews.py`: one game run per `[arena]` in arena world mode (`DRAKAN_ARENA_FORCE`, no network
  session, so the teleport to the preview point moves nobody else), `DRAKAN_ARENA_PREVIEW_SHOT` from the authored `preview`
  point. Window 1600 x 900 at 16:9, `DRAKAN_SHOW_FPS=0`, `DRAKAN_HUD_SCALE=0.5` so the orbs stay in the bottom strip, which
  is cut (top 84 % kept), then centre-cropped to 16:9 (1344 x 756). Black (mean luma < 12) or flat (spread < 8) frames
  are rejected and retried once. Standard library PNG read / write (the build's Python may lack Pillow).
* Output `previews/maps/<safe name>.png` in the play folder; `searchDirs` reads it after the player's `previews/` and the
  mod packs' `previews/mods/`, so both still override it. `map_preview_policy` test updated.
* `Build-LocalGame.ps1` runs it after the launch config is staged (Windows, PAL, with a play folder; `-NoMapPictures`
  skips it). It is a detail line of step 6, not a new step (the resume review matches on the six titles). A failure only
  warns. Maps that already have a picture are skipped, so only the first build pays (~30 s per map).

## Result

All three built-in arenas give a clean frame on the first attempt (`build/harness/map_previews/<id>/`): Surdana Meadow (the
meadow, trees and tent), Skeleton Square (dead trees and graves under the storm sky), The Depths (the stone arch in the
cave). Rynn stands in the frame: the follow camera cannot be detached without a camera hook (2026-11-01 limits).
