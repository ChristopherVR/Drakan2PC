# Host-drawn I strip, M map panel and Esc menu: integration and the M view fixes — 2026-10-25

Integration of the gameui `StripScreen`, `MapScreen` and `PauseScreen` (commits `2d148b8`, `87ee613`) into the game,
plus the user's M view reports ("takes a moment or two to close", "doesn't show the location, where we need to go",
"the cursor / player placement jitters a bit"). Reference: `docs/reference/mouse-menus.md` sections 12 (I / M), 13
(dispatcher, tracker time-outs) and 14 (pause menu).

## Setup

All runs: `scripts/harness/play_run.py` with the Surdana card copy `build/harness/polish-save/mc0`, private
`build/harness/mvi/drakan*.ini` / `drakan_input.ini` (`DRAKAN_INI`, `DRAKAN_INPUT_INI`), logs in the run folders,
1600 x 900 window, 16:9. The user's `dist/DrakanPC` ini files, saves and log were not touched;
`drakan_native_options.ini` was compared with a copy after the runs that opened Options (unchanged). Another game held
the foreground and the GPU (99 % busy) the whole time: no real keyboard / pad input was sent; the game still presented
(present rates 40-280 fps in the counters). F1 was tested by posting `WM_KEYDOWN VK_F1` to the game window
(`PostMessage`, no focus change).

## Baseline (build before this work, `build/harness/mvi-base`)

* Close: the map stayed fully drawn for 17 shots (EE frames) after the M key (`c_001..c_017`), gone at `c_018`. The
  native close of 0x15 is a game-time animation (`screen+0x15c` = -2): 0.50 s (93 EE frames) on the native Tab close
  in `mvi-timing`.
* Jitter, measured as colour-class centroids over shots taken every EE frame (`build/harness/mvi/jitter.py`):
  settled map (60 frames): markers 0.00 px, the arrow 0.37 px (its own pulse), the selection glow is an animation;
  open animation (every 4th frame): the arrow 4.2 px range, one 4.1 px step. The I strip's frames only change where
  Rynn idles.

## Changes and results

| Item | Before | After | Run |
| --- | --- | --- | --- |
| M close (close action -> screen gone) | 17 EE frames of native close (0.5 s of game time) | 2 EE frames, t 21.03 -> 21.04 s; host panel faded within 3 shots | `mvi-timing`, `mvi-v3/mc_*` |
| I close | same animation | 2 EE frames | `mvi-timing` |
| Arrow during the map open (host map, shots every frame) | 6.8 px range, 3.6 px steps (`mvi-v2/mo_*`) | 0.64 px range (= its pulse), 0.31 px max step (`mvi-v3/mo_*`) | |
| Settled map (60 frames) | markers 0.00, arrow 0.64 px | markers 0.00, arrow 0.64 px | `mvi-v3/mj_*` |
| Objectives | region name "Surdana" for every row (texts not resident) | real texts and place names after the string requests | `mvi-v2/map.png` vs `mvi-v3/map.png` |
| Equip from the strip / Tab screen | reported refused ("That can't be used right now") although it happened | confirmed (Tab 46 frames, strip 95 frames) | `mvi-eq`, `mvi-eq2`, `mvi-strip3`, `mvi-strip4` |

* **Jitter cause.** Not the Tab screen's fractional crop offset (commit `5230805`): it is only applied for
  `View::Modern`, and the views' markers did not move at all over 60 frames. Not the integer rounding of the markers
  either (they are rounded the same every frame). It was the 3D "you are here" arrow during the native open
  animation of 0x15: for ~8 frames it sat 3-4 px left of its settled place on the scaled map, then snapped back. The
  views have no sliding page art, so the open animation is now finished from the third frame after the activation.
  It cannot be finished on the first frame: the opening key injects Select for two frames, and 0x15 ignores Select
  only while the animation runs; finishing it at once closed the view on its second frame (first attempt, `mvi-v1`).
* **Close.** `+0x158` is set to 0.001 while the animation runs backwards; the native step then ends it through
  `42f758` (camera restore), so the world camera comes back as before (`mvi-v3/mc_004.png`: gameplay view, HUD back).
* **Location.** `ctx+0x98` holds the level path (RAM dump `build/harness/tabreal-series9/ram_h1`:
  `cdrom0:/English/Levels/Level 1A - Surdana/Level 1A - Surdana.lvl`); the stem after " - " is shown ("Surdana").
* **"Where we need to go".** The objectives panel lists the main and optional objectives with their text; the main
  objective "Find Lady Myschala at the palace in Surdana." has no marker of its own in this save (the quest records
  of the objectives carry no map caption), so it is not ringed; the places (The Palace, Jade's Magic Emporium, Brekk the
  Blacksmith) are linked both ways (focus a row -> ring + name tag on the map; Enter -> the marker; Left -> the
  nearest marker), `mvi-v3/map_jade.png`, `map_jade_marker.png`, `map_left.png`.
* **Strings.** `1b9fb0(mgr, id, buf, 1, 0xff)` as guest invocations (5 ids for Surdana: three objective texts, two
  captions) made them resident within a few frames; no stall or hang was seen in any run.
* **Tracker time-outs.** The snapshot-counted time-outs assumed ~60 EE frames per second; the uncapped EE ran ~250.
  The executor now passes the eased EE frames per 60 Hz tick to `ActionTracker::begin`.

## Screens checked in screenshots

* I strip (`mvi-v2`): `strip.png` (bar, card, prompts, bow focused, 3D icons in the host cells), `strip_right2.png`
  (focus moved, card follows), `strip_hot.png` (F: "Quick Slot" mark and "Unslot" prompt), `ic_*` (closed, HUD back);
  `mvi-strip2`: pad D-pad focus with pad glyphs (`pad_right.png`), Circle closes (`pc_006.png`); `mvi-strip3/4`:
  mouse hover focuses the club, double click equips it (Rynn draws it, `mvi-strip4/dbl_equip.png`).
* M map (`mvi-v3`): `map.png`, `map_jade.png`, `map_jade_marker.png`, `map_left.png`, close series `mc_*`, Esc close
  `me_*`.
* Pause (`mvi-pause1`): `pause.png`, `focus_save.png`, `save_native.png`, `back_from_save.png`,
  `options_native.png`, `quit_box.png`, `held_340.png`, `resumed.png`; F1 (`mvi-f1`): `f1_open.bmp`,
  `f1_closed.bmp`; watchdog (`mvi-pausewd`): `wd_hidden.png` (nothing drawn), `wd_after.png` (native page back);
  native options (`mvi-native`): `native_pause.png`, `native_strip.png`, `native_map.png` with
  `DRAKAN_STRIP_SCREEN=native DRAKAN_MAP_SCREEN=native` and `pause_menu` unset.

## Open points

* Real keyboard / mouse / pad runs were not possible (another application in the foreground); the scripted path
  applies the same `KeyGate`. Only 16:9 at 1600 x 900 was run.
* Rapid Esc: four taps 10 / 4 / 4 frames apart opened, resumed, opened and resumed; a fourth press 4 frames after a
  resume did not reopen the pause (the game took no START then; not compared with the native pause).
* The elixir's icon in the strip was sometimes missing for a while after opening (cell shows only "100"); the Tab
  screen shows it. The elixir "Use" refusal in `mvi-v2` happened before the time-out fix and was not retested.
* Under a native Save / Load page the HUD orbs show (the host screen is faded out then); not compared with the native
  pause.
* Quit Yes, Options > PC Settings from the modern pause, `resume_on_close = 1`, 21:9 / 4:3 were not run.
