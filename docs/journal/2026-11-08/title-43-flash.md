# Main menu art showing 4:3 for a moment when a game loads (2026-11-08)

Report: "there are cases where the main menu, once we load a game, can show for a split second the 4:3 aspect ratio of
the main menu image". **Not reproduced.** A detector was added so the next occurrence names its path.

## Paths recorded

Card: `build/harness/rt13-dir-always/card/mc0` (one Surdana save), `DRAKAN_GAME=d2`. Recipes in the session scratchpad,
driven by `record_window.py` at 60 fps (1280x720, vsync on) and by `play_run.py` with the player's `drakan.ini`
(borderless 3840x2160, vsync off).

| Path | GUI screens | Result |
| --- | --- | --- |
| Title, Continue | 8, 4, 14, gameplay | crossfade into the wide title picture under "Loading...", no 4:3 frame |
| Title, Load Game, slot 1 | 8, 4 (modern slot list), 14 | same |
| In game, F5 then F8 (quick load) | -1, 23, 4, 14 | the loading screen shows the wide `default` backdrop (title art, blurred under the modern card); not 4:3 |
| In game, pause, Quit, Quit to Title | 23, 1, 8 | the host title menu shows over black for about 1.5 s, then the wide `screen8` picture; no 4:3 frame |

Frame-by-frame checks: per-frame differences over the transitions, plus contact sheets. A first reading of the quick
load frames as "stretched 4:3" was wrong: logo and Rynn positions match the title-path loading frame; the softness is the
modern card's backdrop blur.

## What was ruled out along the way

* The present-time held menu frame (`menuHoldTex`, `d3d11_present.cpp`) is refilled from the pause and Load pages in
  game and drawn over the top 85.5 % of an in-game loading screen; it shows the paused world, not the title.
* `GSR_RB_SKIP=0`, `GSR_GPU_COPY=0`: no change. `GSR_PALETTE_HD=0` (or HD textures off) changes the in-game loading
  screen from the `default` backdrop to the dimmed game frame: the 32x32 fade quad on screen 14 is then not taken as menu
  artwork. Both are wide, neither is 4:3.

## Detector

With `GSR_BACKDROP_LOG=1`, D3D11 now also prints
`[backdrop] frame N: native title tile (gui= menu= nativeMenu= world= hudScale= replaced=)` for every one of the six
exact title tiles that reaches the native draw path instead of being replaced by a wide image (first 400). On a wide
output that line is exactly the 4:3 flash. None appeared in any run above. Ask the player to run once with
`GSR_BACKDROP_LOG=1` (and `DRAKAN_WS_LOG=2`) and send `drakan.log` when they see it.

Unrelated observation: after Quit to Title the harness window switched from its forced 1280x720 window to borderless
4K, so the player's `fullscreen` setting is re-applied on the way back to the title.
