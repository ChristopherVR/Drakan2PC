# UI test pass: I strip, M map, modern Tab screen, modern Esc menu, quick slots 1..5 — 2026-10-25

Scope: `docs/reference/mouse-menus.md` sections 12-14, `input.md` section 17. All runs scripted through
`scripts/harness/play_run.py` (`DRAKAN_NO_ACTIVATE`, never the foreground) with private copies: `drakan.ini`
(`inventory_screen = modern`, `pause_menu = modern`) and `drakan_input.ini` under `build/harness/uitp/cfg/<run>`, the
memory card copied from `build/harness/polish-save/mc0` (Surdana) or `pause-water-diag/card/mc0` (in water), the
native options ini copied into the run (or `build/harness/uitp/nopt_s<k>.ini` for the schemes). The user's
`dist/DrakanPC` inis, saves and log were not touched. Runs: `build/harness/uitp-*`, recipes `build/harness/uitp/r/`.

## Bugs found and fixed (commit `80f0560`)

| Bug | Cause | Fix |
| --- | --- | --- |
| Tab Map page: the map's left part cut off with a letterboxed picture (`DRAKAN_WS_ASPECT` 21:9 and 32:9 on a 16:9 output; also in the earlier `tab3-219` run) | the host map area mapped to GUI x = -61 at 21:9 (the page is compressed by `hudScaleX` 0.57), the GS front end clips the native window at 0 | the map area is clipped to `[8, W - 8] x [8, H - 8]` (`drakan_views::clipToWindow`); the map is fitted into it and the capture reports the real rectangle |
| Tab grid at a 32:9 picture: icons squashed / missing (club shaft only, elixir flattened) | grid target y -53..231: above the window | the grid target is kept whole inside the window (`keepInWindow`: shrunk about its centre if larger, then moved in; measured: scale 1.565, target 8..504 x 8..253) |
| Rynn cut in half by the 4:3 pillarbox (known issue) | the host column's centre lies left of the 4:3 picture (EE clamp 0.1 of the picture width) | `previewCentreInPicture`: her centre stays 0.2 x picture height inside the picture; at 16:9 / auto unchanged |
| Tab Map page list: "Location 1", "Location 3" | marker captions are resident only after the M view / Journal asked for them | the Tab screen's Map section requests them like the M view (`tabRequestStrings` while `screen+0x154` = 3): "The Palace", "Jade's Magic Emporium", "Brekk the Blacksmith" |
| Tab Map page: two markers highlighted (host ring on the focused row's marker, the native halo on the native selection) | the host list does not move the native selection | the native selection frame (`4398f8` from `43a22c`) is skipped on the Map page as in the M view |
| (test aid) the watchdog could not be run over an owned Options screen: `DRAKAN_DEBUG_PAUSE_HIDE=1` fired on the root page | the host never drew, so it never entered Options | `DRAKAN_DEBUG_PAUSE_HIDE=2`: the host pause stops drawing once a host page is open |

Tried and withdrawn: keeping the strip's grid inside the window too. At a 21:9 picture it moved the native grid up out of
the plate, where it showed over the open world (`uitp-ws219b/strip.png`); below the window it had drawn fine. The strip
keeps its old placement: a 32:9 picture on a 16:9 output still cuts its icons (remaining issue).

Tests: `inventory_views_policy_test` (clip / keep inside), `tab_preview_policy_test` (pillarbox centre).

## Result matrix

### A1 Layout, crops, hit targets, text

Back buffer = window; `aspect = auto` unless `DRAKAN_WS_ASPECT` is named. The tour recipe (`r/tour.txt`) shows every Tab
page, the strip, the M map, the pause root, Options, Controller, Audio and the Quit card.

| Case | Result | Evidence |
| --- | --- | --- |
| 16:9 1600 x 900: all pages, strip, map, pause pages | PASS | `uitp-tour169`, re-run on the final build `uitp-tour169b` |
| 4:3 picture on 16:9 (`DRAKAN_WS_ASPECT=4:3`) | FAIL -> fixed (Rynn cut) | before `uitp-ws43/t_inv.png`, after `uitp-ws43b/t_inv.png` |
| 21:9 picture on 16:9 | FAIL -> fixed (Tab map cut) | before `uitp-ws219/t_map.png`, after `uitp-ws219b/t_map.png` |
| 32:9 picture on 16:9 | FAIL -> fixed for the Tab grid and map; strip icons still cut (remaining) | `uitp-ws329`, `uitp-ws329b` |
| 4:3 window 1200 x 900 | PASS (text small but fits) | `uitp-win43` |
| 21:9 window 2520 x 1080 | PASS | `uitp-win219` |
| 32:9 window 2560 x 720 | PASS (crops checked at full size) | `uitp-win329`, `uitp/win329_*_zoom.png` |
| 3840 x 2160 | PASS at 3818 x 2032: Windows limits the window to the 2560 x 1440 display; a true 4K back buffer NOT RUN | `uitp-win4k` |
| Mouse hit targets at 16:9: grid cells (club, elixir, dagger) move the focus and the card, filter tab click, page tab click, right click closes; strip cells; M markers (ring + name tag, row highlighted) and rows; pause rows hover, click Options, right click back / resume | PASS | `uitp-mouse169/sheet_a.png` |
| Mouse hit targets at the other aspects | NOT RUN (the layout that draws also hit-tests; only drawing was checked there) | |
| Spells page with one spell (Fireball bought at Jade's, `DRAKAN_DEBUG_OPEN_SHOP=2`) | PASS | `uitp-spells/sp_page.png` |
| Spells page with several spells | NOT RUN: 646 gold buys only Fireball (Ice Blast 540, Combust 585, both Magic 2) | `uitp-spells/shop_*.png` |
| Skills / Journal pages | PASS; the native page shows faintly through the blurred backdrop (cosmetic, by design of the backdrop) | `uitp/skills_ghost.png` |

### A2 Quick slots

Recipe `r/qs.txt`: Tab, F on bow / club / elixir, 2 while the Tab screen is open, close; 2 (club); club combo with 1
in the middle; LMB held (bow aim) and 2; Esc + 1; M + 3; I + 3; 1; 3.

| Case | Result | Evidence |
| --- | --- | --- |
| Key during a live club combo (1 between attack taps) | PASS: bow drawn, `confirmed, cell 0 equipped=1` | `uitp-qs*/q_combo.png` |
| Key while aiming the bow (`bow_aim = modern`, LMB held) | PASS: club drawn; the release then sends one attack with the club; FOV back | `q_aim.png`, `q_aim_key.png`, `q_after_aim.png` |
| Schemes A / B / C (native options `controller_scheme` 0 / 1 / 2, confirmed `scheme(+0x2e8)` in the log) | PASS, the same confirms in all three | `uitp-qs0`, `uitp-qs1`, `uitp-qs2` |
| Keys on the Tab screen, the strip, the M map, the pause menu | PASS: no `[quick-slot]` line, nothing used | logs of `uitp-qs*` |
| Save -> load (from the modern pause after the Options pages) | PASS: the hot slots persisted, the first press after the load equipped the bow, 2 the club, 3 drank the elixir | `uitp-save3` (`sv_*`, `ld_*`) |
| In water | PASS (limited): the inventory does not open in water, so no slot can be marked; keys report `empty (0 quick items)`, nothing stuck | `uitp-swim` |
| Climbing, running summon, cut scene | NOT RUN (no fixture; only Fireball, which is not a summon) | |

### A3 Esc menu

| Case | Result | Evidence |
| --- | --- | --- |
| Save after visiting Options / Controller / Audio (overwrite Yes on slot 1 of the private card) | PASS: "... was successful", the save resumes the game | `uitp-save3/sv_*.png` |
| Load from the modern pause ("Leave current game?" Yes) | PASS | `uitp-save3/ld_*.png` |
| F1 over Controller and Audio (posted F1, overlay screenshots) | PASS, the pages take input again afterwards | `uitp-f1/f1_3.png`, `f1_4.png` |
| F1 over Options | PASS by the log (`F1 allowed (screen 9 ...)`); the screenshot came before the overlay drew | `uitp-f1/drakan.log` |
| Quit card | PASS (layout, Cancel) | `uitp-f1/f1_after_quitcard.png` |
| Watchdog while an owned Options screen is open (`DRAKAN_DEBUG_PAUSE_HIDE=2`) | PASS: `native pause page restored: watchdog` with screen 9 open, the native Options page is shown, Esc -> native pause, Esc resumes; the next Esc opens the modern pause | `uitp-wd3` (`uitp-wd2` with the first version of the aid) |
| Watchdog on the root page (`=1`) | PASS | `uitp-wd` |

### A4 Robustness (`r/robust.txt`, `uitp-robust`)

| Case | Result |
| --- | --- |
| 50 rapid open / close cycles of Tab, I, M, Esc (gaps 3..120 samples) | PASS: no hang, no `guest-branch:missing-target`, HUD back at the end, every session closed |
| Opening one view 2 samples after closing another | PASS (no stuck state); that second press is dropped while the first screen is still going (the I / M views end 2 EE frames after the close). 15 samples after a Tab close the I strip opens (`uitp-drop`). Not a practical problem |
| Right-click spam (12 in 36 samples) over items, Tab and strip | PASS: closed, nothing stuck |
| Double-click equip then Tab at once; strip Use then I at once | PASS: the club equipped (Rynn holds it), screens closed |
| Esc while the Tab screen is open | PASS: closes the Tab screen |
| Tab while paused | Tab resumes the game (the shared Close key); documented in section 14 |
| M during the Tab close | M 6 samples after the Tab close is dropped, the next M opens the map |

### A5 Map gaps

| Case | Result |
| --- | --- |
| Tab Map page "Location n" | FAIL -> fixed (captions requested) |
| List not moving the native selection | FAIL -> fixed by not drawing the native halo (the host ring is the only highlight); the native selection itself still does not follow the list |

### B Real input

NOT RUN. The user was active the whole time (GetLastInputInfo idle 0-1 s at every check) with Call of Duty
(`sp22-cod`) in the foreground, so no input was sent and focus was never taken. Pad: NOT RUN.

## Remaining issues

* 32:9 (or wider) game picture on a 16:9 output: the strip's native icons are cut (the strip's grid lies below the
  letterbox band); real 32:9 windows are fine.
* Real keyboard / mouse / pad runs of all four screens (section B).
* True 3840 x 2160 back buffer (needs a 4K display or a borderless 4K mode).
* Quick slots: climbing, summon, cut scene.
* Spells page with several spells (no gold in the fixture).
* A press within ~2 frames of another screen's close is dropped.

Incident: a stopped background run left `play_run.py` retrying (3 attempts); its new game instance locked `dist` during
a deploy, which then failed half-way; the next incremental build refilled `dist`. Runs now use `--attempts 1`.

## Follow-up: strip icons at wide pictures, Map selection, instant Tab close (2026-10-25, later)

Runs `build/harness/lat-*` (recipes `build/harness/lat/r/`: `lat.txt` close latency series every 2 samples, `ui.txt`
strip / Tab Map page / M view, `strip.txt`), private inis under `build/harness/lat/cfg*`, Surdana card copy. The
user's `dist` inis, saves and log were not touched; their game was left running and every deploy waited for it.

| Issue | Cause | Fix | Evidence |
| --- | --- | --- | --- |
| I strip icons cut with a 21:9 / 32:9 picture on a 16:9 output (21:9 lost the dagger row, not only 32:9) | the plate lies below the picture, where the GS front end clips the native window; inside the window the grid showed over the world | d3d11 off-screen grid capture (mouse-menus section 12): the grid is kept inside the window, the renderer copies its box to a side texture and restores the frame under it; the overlay crops from the copy | before `uitp-ws219/strip.png`, `uitp-ws329/strip.png`, `uitp-ws329b/strip.png` (moved, over the world); after `lat-ui169c`, `lat-ui219c`, `lat-ui329c`, `lat-s329/strip.png`, `lat-win219c`, `lat-win329c`; `lat-peek329c` (`DRAKAN_DEBUG_TAB_PEEK`: no native grid in the presented frame) |
| first capture versions left the black box, then a club piece, on the world | the scope's end was tied to EE returns (437170 / 433610 / 42fd18): the item draws reach the GS later, and the display latch of the other buffer closed it early | the scope ends when its own target is displayed (latch whose circuit shows its `fbp`) | `lat-s329` runs during the fix |
| Tab Map page list did not move the game's selection (the native caption under the map named another place) | the host list never touched `section+0x6c` | `SelectMarker`: RIGHT taps until `+0x6c` is the focused record, bounded, taps only; also in the M view | before `uitp-tour169b/t_map.png` (Palace focused, caption "Jade's Magic Emporium"), after `lat-ui169c/t_map0..3`, `m3`, `m4`; log `select-marker(n) -> confirmed` 1-4 frames |
| "a bit of a delay before the inventory closes" (Tab / Esc on the modern Tab screen) | the host screen stayed up through the ~0.5 s native close animation, then faded at 9 / s; the close also waited 3 idle snapshots | close animation finished at once (as the I / M views), idle snapshots counted from before the close, fade 24 / s | `lat-before` vs `lat-after3` |

Latency (key -> screen 0x15 gone / -> first shot of gameplay with the HUD): Tab before ~500 ms / ~600 ms, after 8 ms /
45 ms; Esc before ~650 ms (first closed shot), after 12 ms / 49 ms; Esc does not open the pause menu (one pause binding
in the run, the deliberate one). The game's camera then eases back from the preview zoom and Rynn turns back for
~0.3 s, live (the native close shows the same under its page). Modern pause Resume: world at once, HUD fade done
~85 ms after the key, unchanged.

Not verified: real keyboard / mouse (scripted input only; the user was active), mouse hit testing of the strip at the
wide pictures, the software renderer (no capture there: the strip keeps its old placement, `gridCapture` false), a
first-visit help box over the strip with the capture (the grid is hidden while a box is up; not run).
