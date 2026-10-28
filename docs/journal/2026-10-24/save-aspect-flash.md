# Save flashes the picture to 4:3 — 2026-10-24

Symptom: saving from the pause menu switched the whole picture (UI included) to 4:3 pillarbox for about a second,
then back to the window's widescreen aspect.

## Repro

`scripts/harness/play_run.py --name save-aspect --env DRAKAN_WS_LOG=1` with a recipe that does pause (ESC), Save Game
(ENTER), picks a slot and confirms (output in `build/harness/save-aspect`). The host decisions on the way out of the
save browser:

```
[ws] vsync=3621 world=1 screen=5  worldAge=0
[ws] vsync=6086 world=1 screen=26 worldAge=0
[ws] vsync=6101 world=0 screen=26 worldAge=15   -> push wide=0 (4:3)
[save-preview] saved 1024x1024 companion for Level 1A - Surdana.rsg
[ws] vsync=6142 world=1 screen=26 worldAge=0    -> push wide=1
[ws] vsync=6143 world=1 screen=-1 worldAge=0
```

The input log only shows `gui screen 5 -> -1`; the save-in-progress screen 26 (0x1a) is too short-lived for its
per-frame poll. 0x1a is the screen that the GUI render pass already treats like load / save / new game
(`subtitles.md`, `FUN_002cc8e0`).

## Cause and fix

On 0x1a the game renders the world for the slot picture and then writes the card, with no camera update for ~15
vblanks. 0x1a was not in `worldMenuScreen`, so `WorldPolicy::update` expired the world after six vblanks without a
camera tick, and the presenter and projection went to 4:3 until the camera ran again.

`saveWriteScreen(0x1a)` (`drakan_widescreen_policy.h`) now keeps an established world like the pause menus. It cannot
establish one by itself, and it is deliberately not a native menu screen. Apart from the stall it was already drawn
with the gameplay layout, which looked correct. Tests: `widescreen_policy_tests.cpp` ("save write keeps the world
through the card write stall").

Verified live (`build/harness/save-aspect-fixed`, same recipe): screen 26 now logs `world=1` with no `wide=0` push
and the shots stay widescreen. Unrelated, seen in both runs: "Save was successful." is drawn twice, large at the
top left and small at the top centre. Same cause as the objective popup (`inventory-sky-textures.md` section 5,
fixed in a674329): draw log `build/harness/save-msg-drawlog` shows the same shadow sprite batch (`flags=237`) and
FGE triangle fill (`flags=037`) at 23..157 x 15..37. With that fix both passes take the same corner map; the
message is drawn once at the top left (`build/harness/save-msg-nofps2/tops.png`).
