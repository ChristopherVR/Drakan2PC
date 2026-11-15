# Esc spam: the pause menu drawn stretched for a frame

Owner's report: "When spamming the Escape key the menu can sometimes look like it's stretching, so it's not drawing the
correct one initially." Scratch, private play folder (copy of `build/final-game/bin`, the owner's three ini files, a
private ELF), recipes, tools and runs: `build/review/esc-menu/` (private).

## 1. Which menu

The owner's `drakan.ini` has `pause_menu = native`, and the owner's `drakan.log` has no pause binding (its only
`[pause-screen]` lines come from the modern title and Save / Load screens). So the menu in the report is the game's own
pause page (screen 0x17), not the modern pause menu: `drakan_pause_screen.inl`, the overlay's `freezeFrame` and its fades
play no part. Neither of the two suspects in the brief applies as it was written.

## 2. Reproduction

Runs use the owner's settings: 16:9 borderless 2560 x 1440, `aspect = auto`, `hud_aspect_correct = 0`, `hud_scale` auto,
DLSS quality. They start with New Game on an empty private card in Surdana, or in the Order of the Flame Ruined Village
(`DRAKAN_GAME=d1`), and use scripted Esc taps at gaps of 2 to 30 input samples (`tools/mkrecipe.py`, `recipes/`).

* `runs/d2-spam1`: 15 openings, a shot every 2 samples. `c00_o002.png` is the bad frame. The dim panel is 0.40 of the
  frame width, where a centred 4:3 page is 0.30, and the HUD is still at gameplay size. The next shot is correct.
* `runs/d2-before-shots`: 48 cycles, shots 1..4 samples after each open and 1..3 after each close. 3 shots are
  stretched, all of them the first frame after an open (`c08_o1`, `c22_o1`, `c23_o1`, panel 0.409). The panel width is
  measured by `tools/width.py` against `base.png`. Sheet: `runs/sheet_before_d2.png`.
* Shots at 3840 x 2160 cost about 80 ms each, which slows the EE to about 25 samples a second, far from real play. The
  rate was therefore measured without shots, by a renderer log (`GSR_PAUSE_HUD_LOG=1`, section 4) that records the
  scale every pause page frame was drawn at. `runs/d2-before-noshot` (120 cycles, `recipes/spam_noshot.txt`): 92
  openings, and **28 of them had a pause page frame drawn at the gameplay scale 1.0**. `runs/ootf-before-noshot`:
  2 of 96 openings.

## 3. Cause

The host decides the native layout once per presented frame (`drakan_ws::hostTick` on the presenter thread). It polls
the active GUI screen and queues `setHudScaleX` (0.75 for a native menu at 16:9, and 1.0 in gameplay without
`hud_aspect_correct`). The renderer applies that queued setting only at a frame boundary.

The page draws themselves are classified by the frame's own ordered camera packet. Its `guiScreen` is 0x17 from the
first pause frame on (`hud::nativeMenuDraw`), so they take the menu page map `hud::menuMap(width, hudScale)`. When Esc
lands just after a host poll, the first pause frame (sometimes two) is drawn before the host's push arrives, still at
`hudScale = 1.0`. The 4:3 page then covers 0.40 of the wide picture instead of 0.30. The log lines of those frames read
`pause page drawn at hud scale 1.000 (host menu 0 screen -1)`.

`renderer.applyPending()` at the screen's packet, which the title and loading screens use, cannot help here: in the race
the host has not polled yet, so nothing is pending.

## 4. Fix (runtime/gs and the widescreen host)

* The host now also pushes the native pause page's scale at all times (`Renderer::setPauseHudScale`, live setting
  `F_PAUSE_HUD`, computed as `layoutPolicy(0x17, ...)`), whether the pause is up or not.
* `gsr::hud::pauseTransitionHudScale` / `PauseHudScale` (`hud_page.h`): when a camera packet names the pause while the
  host still reports gameplay (`nativeMenu` off), `hudScale` takes the pushed pause page scale. It is re-evaluated on
  every camera packet that changes the screen and whenever the host's menu or HUD settings land. A host push that
  arrives during the override is kept aside and restored when the override ends.
* The reverse case (a gameplay camera while the host still says pause) keeps the host's scale. In Order of the Flame
  levels, 21 to 51 draws per run are classified as pause page draws (inside the page box, after the world) under a
  gameplay camera while the host still reports the pause. These follow a close, and what exactly they are was not
  identified. A first version also forced the gameplay scale there and drew exactly those frames at 1.0
  (`runs/ootf-after-noshot`: 51). That rule was removed. With the host's scale (0.75, as before the change) the close
  shots show either the centred page or no page at all: `runs/ootf-after3-close`, 48 shots one sample after a close
  all at page width 0.295, and 89 shots two and three samples after a close with no page.
* `GSR_PAUSE_HUD=0` turns the override off (A/B aid). `GSR_PAUSE_HUD_LOG=1` logs every change of the scale and the
  scale of each frame's first pause page draw.
* Test: `runtime/gs/tests/test_hud_page.cpp` (open before the poll, close unchanged, host push kept aside).

The opening frame's other state was already right: `hud_size` is the same under the pause and in gameplay
(`gameplayHudSize`), the presenter is wide in both, and the frozen world and the pause HUD split follow the camera packet
(`hud::frozenWorld`, `pauseHudDraw`). The page log cannot see a page drawn while both the camera and the host still say
gameplay, because such a draw is not classified as a menu page. The shot series (section 5) show no stretched page
after the fix, so no extra ordered page marker was added.

## 5. Result (same recipes)

The builds: build 1 had the opening rule and the removed close rule. Build 2 is build 1 plus a wider page log (it
also logs page draws under a gameplay camera). Build 3 is final: build 2 minus the close rule. "Before" rows ran on the
final exe with `GSR_PAUSE_HUD=0`.

| Run | Build | Openings | Page frames at 1.0 (stretched) | Frames drawn under the override |
| --- | --- | --- | --- | --- |
| `d2-before-noshot` | 1, off | 92 | 29 (28 openings) | 0 |
| `d2-after-noshot` / `d2-after2-noshot` | 1 / 2 | 92 each | 0 | 45 / 11 |
| `d2-after3-noshot`, `d2-final-noshot1`, `d2-final-noshot2` | 3 | 92 each | 0 | 0 / 24 / 6 |
| `ootf-before-noshot` | 1, off | 96 | 2 | 0 |
| `ootf-after-noshot` | 1 (close rule) | 98 | 51, all under a gameplay camera | 55 |
| `ootf-after3-noshot` | 3 | 97 | 0 | 0 |
| `d2-before-shots` (shots o1..o4, x1..x3) | 1, off | 41 | 3 shots stretched (`c08_o1`, `c22_o1`, `c23_o1`) | 0 |
| `ootf-before-shots` (shots o1, x1) | 3, off | 78 | 1 shot stretched (`c76_o1`, panel 0.395) | 0 |
| `d2-after-o1` (shots o1, x1) | 2 | 74 | 0, 180 of 180 shots at page width | 1 |
| `ootf-after3-shots` (shots o1, x1) | 3 | 78 | 0, 180 of 180 shots at page width | 7 |
| `ootf-after3-close` (shots o1..o4, x1..x3) | 3 | 50 | 0 | 0 |

"Openings" is counted from the log, where page frames less than 4 renderer frames apart count as one opening. A rapid
re-open is therefore merged into its neighbour. The race depends on timing: the same recipe gave 0 to 45 override frames
per run. The proof is therefore the scale of every page frame in the log, together with the shot series, not one clean
series. Sheets: `runs/sheet_before_d2.png` (before), `runs/sheet_after_d2_o1.png` and `runs/sheet_after_ootf_o1.png`
(after: every first frame after an open is the centred page; the first opening shows the native open transition
without the brass frame, also centred). Single frames: `runs/d2-spam1/v_c00_o002.png`,
`runs/d2-before-shots/v_c08_o1.png` and `runs/ootf-before-shots/v_c76_o1.png` (stretched, before).

Other checks: section 6.

## 6. Other screens

All on the final build, Surdana, the owner's settings.

* `runs/d2-children2/sheet.png`: the native Options child opens from the pause, and Esc goes back to the centred pause
  page with no stretched frame (`cback_1`, `cback_3`). The modern Tab screen shows the new-game Equipment help, then
  OK, open, close and a Tab spam. A mixed Esc / Tab / Esc burst opens the pause centred. The script reached `quit`.
* `runs/d2-children/sheet.png`: the modern Save screen (`save_menu = modern`) opens from the pause. After Esc, the native
  pause comes back through its own open transition (two frames without the page, then the centred page). Load Game
  is greyed on the empty card, so the Load child was not run. The rest of that script was swallowed by the Equipment
  help box, which `children2` dismisses.
* `runs/d2-modern-spam` (`DRAKAN_PAUSE_MENU=modern`, 90 quick cycles): the native page stays hidden (no page draw was
  logged) and no stretched page shows (`runs/sheet_modern.png`). On quick taps the modern menu's open and close often
  land one tap later than the native toggle. That is the documented gate, under which the opening Esc counts only after
  its release (`mouse-menus.md` section 14). It was not changed here.

## 7. Notes

* Not measured: other native screens opened straight from gameplay may have the same race. The renderer recognises
  only the loading screen (14) and the pause (0x17) by their camera packet (`hud::nativeMenuDraw`); the inventory has
  its own packet.
* `build/final-game.build.lock` dated 12 November was present the whole time with no build running. The builds here ran
  without waiting for it, and it was left in place.
* The build refreshes `dist/DrakanPC`, so the owner's folder has this change after the build.
