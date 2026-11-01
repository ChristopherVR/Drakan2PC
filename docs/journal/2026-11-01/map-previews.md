# Multiplayer map pictures from the disc (2026-11-01)

Goal: real pictures instead of the generated sketch in the Host / Join / lobby map card, made at run time from the player's disc.

## What exists now

* `mp_maps.ini` takes `preview = x y z [yaw [pitch]]` per arena (`net::ArenaDef::hasPreview/preview[5]`, `drakan_arena::MapEntry`).
  The three built-in arenas have one. Pitch > 0 looks up (checked in game: -25 looks at the ground, +30 at the sky).
* `drakan_arena::requestPreviewShot(file)`: places the player at the point, writes the pitch into the camera object
  (`*(*(camHandle+0xfc)+0x10)+0x84`), waits for the follow camera to hold still (60 ticks without moving 12 units, at most 900 extra),
  checks she really stands on the point (the engine refuses a teleport with no floor: 4 tries, then no picture), then
  `gsr::requestWindowShot`, then back to a spawn point.
* Host glue `mpPreviewCapture`: ~12 s into the first match on a map without a picture. The cache is `%LOCALAPPDATA%/Drakan2PC/previews`
  (`DRAKAN_PREVIEW_DIR`). Campaign levels (co-op / level matches) are captured from the start view.
* The title menu never called `updateMapPreviews()` (only the save-slot screen did), so no map picture ever loaded: fixed, and the title
  menu's icon source now serves `LevelPreview`. Captured files are drawn cropped to the band between the stats overlay and the
  native HUD orbs (uv y 0.08 .. 0.74).
* Harness: `arena_tour.py --maps-file`, `DRAKAN_ARENA_PREVIEW_SHOT`.

## Limits

* The shot always contains Rynn (the follow camera cannot be detached without a camera hook), so the picture is a "hero shot", not an
  empty overview. A free camera would need the camera object's matrix to be written after the engine's update every tick.
* There is no map vote in the menus yet, so there was nothing to put a picture on there.
* Character preview: not done. The title menu has no world and no model renderer (the Character page's Rynn is the live world
  camera, `ImageKind::WorldView`); the stand-in is the drawn figure. A rotating NPCS.MOD model would need a menu-time model draw path
  (load the model, a spare GS context, per-frame draw in the title) which does not exist.

## Update (same day, later)
The map vote now exists and uses these pictures, and the co-op character picker has a rotating model made the same way
(from the disc at run time, cached): see `map-vote-character-previews.md`.
