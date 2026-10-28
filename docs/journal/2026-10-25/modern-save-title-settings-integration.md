# Modern title menu, Save / Load screens and one Options screen: integration — 2026-10-25

Wiring of the gameui `TitleScreen`, `SaveLoadScreen` and `SettingsPages` (library commits `bc718ec`, `10886ce`,
`86afcbb`) into the game, plus two requests during the work: one Options screen instead of Options + PC Settings in
the modern menus, and the user's "artifacts behind the main menu when hovering over some of the rows". Reference:
`docs/reference/mouse-menus.md` section 15, `settings-menu.md` ("One Options screen"), `save-slots.md` section 7;
contract: `runtime/gameui/README.md`.

## Setup

`scripts/harness/play_run.py --attempts 1` (sets `DRAKAN_NO_ACTIVATE`; a private copy of the player's
`drakan_native_options.ini` per run), private card copies (`build/harness/polish-save/mc0`: Surdana in slot 1; run D
started from run B's card), private `drakan.ini` / `drakan_input.ini` per run in `build/harness/msi-cfg`
(`drakan_modern.ini` / `drakan_native.ini`: all three options modern / native), `DRAKAN_AUTOPRESS=0`,
`DRAKAN_INPUT_AUTOSTART=0`, recipes with `clock milliseconds` in `build/harness/msi-cfg/*.txt`. Scripted input only;
the player's `dist/DrakanPC` inis, saves and log were not used (the user's log was copied to
`build/harness/msi-cfg/user_drakan_1634.log` for reading). Builds only with `Build-LocalGame.ps1 -Incremental`; the
deploy waited whenever another session's harness run held `dist`.

## Risks resolved

* **Overwrite / load in game: posted calls?** Not used. `IGuest::post` queues an HLE call on the dispatcher thread at
  priority 6 (`EeScheduler::queueInvocation`, `requestPreemptionIfHigher`): it preempts the GUI thread at the next
  checkpoint, wherever that is, and `443ac0` / `43b4d0` close screens (`2c41a8`) and free the slot list. Instead the
  glue arms what the native Yes arms (overwrite: the screen timer `+0x44 / +0x48`, message 9 from the draw ->
  `443ac0`; load: `+0x9c |= 0x10`, next message 10 -> `43b4d0`) without the box. Live: `msi-b` "overwrite slot 2: the
  save timer is armed" -> saved (new companion PNG, file rewritten); "load slot 1 in game: the load is armed" ->
  screen 14 the next frame -> gameplay.
* **Hiding the slot screens.** `0x2331` stays drawable; `43bfb0` only requests thumbnails (tail calls of the card
  manager's `vt+0x1a4` / `vt+0xac`, one per call; `16aa28` reads the save from the card and may block, so not nested).
  The draws run preemptible (card I/O inside: scans, card checks, the armed load / save).
* **The restore clobbered the screen's own changes.** The first run showed "No memory card" on a good card
  (`msi-a2/t_load.png`): the wrapper restored `0x2332`'s visibility from before the draw after the screen's message 10
  had hidden it. Now `2bea48` / `2be9a8` (the native show / enable setters) are followed while a hidden draw holds
  the widget (`msi-a3/t_load.png`: the list).
* **Hover artifacts (user report).** Not the menu mouse (it is off while `hostOwned()`). A dense series of 108 shots
  while moving the cursor over the rows caught one frame of the native title page (panel frame, "Load Game",
  "Options", "Credits", "Quit") behind the modern menu: `msi-dense/d_105_crop.png` (before). A time-slice preemption
  returns out of the host stack normally, so the wrapper restored the page and the resumed remainder drew it. Now an
  unfinished hidden draw keeps the page hidden until the next frame boundary or its next draw (`[pause-screen] hidden
  draw of ... interrupted (preempted): kept hidden`). After: `msi-dense2` (8 interrupted draws logged, no native frame
  in 108 shots; `d_105_crop.png` there is the same moment), `msi-dslots` (Load screen, 82 shots, none). The 3818 x
  2032 sweep (`msi-hover4k`, the user's window) showed nothing else. The pause menu draws were already no-preempt.
* **Thumbnail orientation.** First drawn upside down; the stored rows are top down (`build/harness/msi-cfg/
  thumb_compare.png`: native grid left, host card right, before the fix; after: `msi-dslots/s_idle_crop.png`).
* **Time-outs.** Frame-counted time-outs gave up after 0.25 s on the title (~1000 EE updates per second): the first
  Continue logged "the slot list was not read". All menu glue time-outs and retries are in milliseconds now.

## One Options screen

Request: "menu makes no sense having 'PC Settings' and Options, just show Options". `PauseConfig::unifiedOptions`
(`gameui/UnifiedOptions.h`): Options opens the PC settings pages titled "Options" with the game's options in Audio
("Game sound") and Controls ("Game controller"); no PC Settings row in the pause root, the pause Options page (gone),
or the title. The game rows become the stage 2 Set* actions with their native screen entered while the page shows.
Restore defaults per page: PC defaults + that page's game defaults. Live: title (`msi-a3`): Music 25 -> 15 (native
ini and the card's `Drakan 2.cfg` hold 15), Vibration off / on, render scale 4 -> 3 kept and 3 -> 4 reverted on the
time-out (`scale = 3` in the private ini), K bound to Move forward and the layer reset (`t_opt_bound.png`, the input
ini equal to the original afterwards); pause (`msi-b`): Music 25 -> 35 and Output volume 100 -> 90 from the same page
(native ini `music_volume = 35`, private ini `volume = 90`).

## Results

| Check | Run / evidence |
| --- | --- |
| Title look, no PC Settings row | `msi-a3/t_title.png`; Continue shows the newest save's location |
| Options landing / Display / keep card / revert / toggle | `t_opt_landing`, `t_opt_keepcard`, `t_opt_kept`, `t_opt_reverted`, `t_opt_toggle*` |
| Game + PC rows in one page | `t_opt_music.png`, `t_opt_controls.png`, `msi-b/b_opt_music.png`, `b_opt_output.png` |
| Arrow prompts ("Change", "Adjust") | drawn as arrowheads in a key cap in the game (`t_opt_reverted.png`, `t_opt_music.png`), not "^" |
| Key capture, bind, layer defaults | `t_opt_capture`, `t_opt_bound`, `t_opt_defaults_*` |
| Credits and back, Quit card Cancel | `t_credits*.png`, `t_quit_card.png`, `t_quit_cancel.png` |
| Load from the title: pages, Back, load | `t_load*.png` (native thumbnail), log ChangePage 1 / 0, Back, ConfirmLoad -> 14 |
| Continue | `msi-b` (slot 1), `msi-d` (the newly saved slot 2) |
| Save into an empty slot, overwrite, HD previews | `msi-b/b_save*.png`, `b_overwrite_card.png`, `b_load.png` (slot 2 HD companion, slot 1 native); card `Slot2/Level 1A - Surdana.rsg` + one `.preview.png` |
| Load in game | `msi-b/b_load_card.png`, `b_loading.png`, log -> 14 -> gameplay |
| Quit to Title, New Game | `msi-b/b_title_again.png`, `b_newgame*.png` (intro movie 0x1d) |
| Empty card | `msi-e/e_title.png`: no Continue, Load Game greyed, focus New Game |
| Native paths (options off) | `msi-c`: no menu-screens / pause-screen lines, the game's own title, load, pause and save (`c_save.png`, pager) |
| Watchdogs | `msi-w1` (title -> native title page), `msi-w2/w_load.png` (native load page with the pager) |
| Stress | `msi-d`: Load Game / Options opened and closed three / two times 300 ms apart, four Esc 300 ms apart in game, Save -> Back |
| Quit to Desktop (last) | `msi-d`: title Quit card -> "quit to desktop: closing the window" |

## Not verified

* Real keyboard / mouse / pad (scripted input only) and the user's exact hover sequence; the cause found explains a
  native-page flash on any frame, independent of hovering.
* The no-card and unformatted card states (no card cannot be simulated with the card folder present); the native
  boxes 0x104 / 0x109 (save failed, card changed); a level-change save (mode 1) through the modern screen.
* New Game "then quit back to the title": started (intro movie), not played into the level and quit.
* 4:3 / 21:9 viewports in the game (the library tests cover them).
* Cosmetic: the "Slots 1 - 4" label of the slot list renders without its hyphen in the game (the tracked small
  label); the slot list's time is the card's stamp, the title's "newest save" orders by file time.

## Latency of Continue, Load and Save (user report)

Report: "Clicking on Continue takes a moment or two to show the Loading... same with selecting a save game." The
user's log (copied to `build/harness/lat-cfg`, not modified) had Continue at t=45.68 and screen 14 at t=48.77 with
nothing in between. Runs: `scripts/harness/play_run.py --attempts 1`, a private copy of the user's card
(`build/harness/lat-cfg/mc0`, mtimes kept), private `drakan.ini` / `drakan_input.ini` / native options, recipes
`build/harness/lat-cfg/{continue,continue_noshot,flows,flows_noshot}.txt`. The user's game held `dist/DrakanPC`
the whole session, so the builds went to `build/final-game` with `-NoDist` and the runs used its `bin/drakan.exe`
with `GSR_TEXDIR` on the dist textures (baseline runs `lat-before-*`: the deployed dist exe).

* **Where the time went** (timestamps: `[menu-screens] frame=.. t=..` now carries the input clock; `overlay t=..:
  title / slot action` marks the click; `slot screen: shown / flags / entries` the list state):
  1. The Load / Save screen's fade-in. `2bd5f0` (called from the screen draw while `+0x54 == 2`) counts `+0x6c` down
     by the frame time and sets "shown" `+0x14` at 0; `43ac40` message 10 returns at once until then, so the scan
     (`43b108` / `43b6f0`), the card check and the deferred load wait. ~1.2 s after the title opens screen 4, and
     again after Continue's page change (`+0x14` drops to 0). Baseline `lat-before-cont`: Continue 17.98 -> Cross
     19.17. Fix: while the host owns the screen (page hidden) the glue writes `+0x6c = 0` (the value the timer
     reaches anyway) -> "shown" the next draw, scan one frame later (`lat-final-cont`: click 17.978, list 18.281,
     Cross 18.332).
  2. The native load / save is synchronous inside the screen's draw: Cross -> `43d028` -> `43b4d0` -> `23f170`
     (opens screen 0xe, then the card manager's `vt+0x19c` loads the save and the level); the EE comes back to the
     PadMgr update 2.2-2.8 s later, which is when screen 14 is first seen (and drawn). In game the armed load takes
     0.9-1.1 s, an overwrite 0.8 s. Not shortened (native); covered: `SaveLoadScreen` shows "Loading..." /
     "Saving..." from the confirm frame (`progress()`), the title shows "Loading..." in place of its column from the
     Continue click (`loading()`), and the Load screen bound for Continue shows it at once (`loadingSlot`). The
     overlay keeps presenting while the EE is inside the load (frame series `lat-after-cont/ts_*`,
     `lat-final-cont/ms_*`: the card from the click to gameplay).
  3. First title Load screen: ~0.26 s in which the EE waits in `GsrBridge::commitDisplay` and the GS thread runs a
     render-target readback, image uploads and texture inflates (`PS2X_HOSTPROF`, `build/harness/lat-prof`). Also
     without HD textures (`lat-tload-nohd`: 0.21 s). Not in game. Left as is (GS side).
  4. Not causes: the memory card HLE answers at once (no waits), the PNG decode (13-19 ms each, now on a worker),
     the screenshot requests of the recipes (they add ~0.25 s each while taken, so latency runs use `*_noshot`).
* **Fixes**: fade-in end (glue), progress / loading cards (gameui `SaveLoadScreen`, `TitleScreen`,
  `SaveSlotsState::loadingSlot`), HD companions decoded with `std::async` (the native thumbnail stands in), the false
  "No memory card" during Reading (the label `0x2332` is visible from the layout until the first message 10: it now
  counts only once the scan flags are clear and the list is empty; `lat-before-flows/f_load_400.png` before,
  `lat-final-flows/f_load_400.png` after), Continue / its failures reactivate the screens (`titleReactivated`,
  `slotsReactivated`). No posted calls; the fade write is the only new guest write and only while the page is hidden.
* **Numbers** (before = dist exe, after = this build, same card and recipes; click time of the baseline from the
  recipe clock):

| Path | click -> visible feedback | click -> native Loading screen (14) / save done |
| --- | --- | --- |
| Continue | 3.43 s (none until 14) -> same present frame ("Loading...") | 3.43 s -> 2.73 s (0.35 s to Cross + 2.38 s load) |
| Title Load Game (open) | list ~1.5 s, "No memory card" meanwhile -> list 0.30 s | - |
| Title Load, slot confirm | 2.2 s (none) -> same frame | 2.2 s -> 2.28 s (native load) |
| In-game Load, card confirm | ~1.5 s (none) -> same frame | ~1.5 s -> 0.92 s |
| Save (overwrite), card confirm | ~0.7 s (none) -> same frame ("Saving...") | ~0.7 s -> 0.77 s (native write) |

  The loads landed in the chosen saves: Continue -> slot 13 Shadowmire (`lat-after2-cont/c_loaded.png`), title
  slot 1 Surdana, in game slot 2 = the save just written there (`lat-final-flows`).

## Menu sounds (user report)

Report: "clicking on some of the menu items like 'Gameplay' or 'Key Bindings' is not playing a sound, but Audio and
Controls is." Cause: the host menus emitted `UiSound`s but only the Tab screen played them; every sound heard in the
title / pause / Options / Save / Load screens came from a native widget the glue pressed (Audio / Controls are
`EnterPage` presses on the hidden Options screen 9; Display, Gameplay, Key Bindings, tabs, toggles, cards and Back on
host pages have no native widget behind them).

* Sound ids: `2bb518` preloads the UI entry's `0x2a`, `0x2b` (focus ticks), `0x24` (click) and `0x2d`; `2bdf38`
  plays `0x2d` on key 1 / Esc (`2bebe8`, `43b1e8`, the pause resume `432708`): the back sound. The check box /
  slider widgets play `0x28` / `0x27` from their own property entries; the UI entry has `0x28` (id `0x10010`) too.
* `UiSound` gains Confirm, Back, Slider, Toggle (gameui `Screen.h`); TitleScreen, PauseScreen, SettingsPages and
  SaveLoadScreen set them (page / card opened = Confirm, left = Back, a toggle / choice = Toggle, a slider step =
  Slider, focus = Move, tab flip = Page). The overlay maps them to the properties above (`hostUiSound`) unless the
  frame's actions press / tap a native widget (`nativeSound`: EnterPage, LeavePage, Resume, Open*, Quit, the Set*
  of the toggles and the scheme, Restore Defaults, Quit to Title; all title rows but Quit to Desktop; Save / Load
  Back = START). The EE plays the request at the top of the next PadMgr update with `tabPlaySound` (guest
  invocation; the thread re-enters the hook with the request taken).
* The pause executor focused a row with UP / DOWN taps, each playing a native tick after the host's own tick for
  the move: it uses the setter `2be528` now (as `pausePress`).
* Verified (`DRAKAN_SND_DBG=1`, `[play]` events; `build/harness/lat-sound`, `lat-sound-pause2`): title rows tick
  once per move; Options (native press) one click; Display / Gameplay / Key Bindings one host click each, Back one
  back sound; Audio / Controls one native click, their Back one native back sound; a Gameplay toggle one `0x28`
  each time; pause moves one tick, Resume one native back, Options one native click, OpenSave one click (before:
  two extra native ticks); Save / Load slot moves one tick, cards and confirms one click. The Tab screen, strip and
  map are unchanged (their Move / Page ticks from `tabFrame`; their activations run native actions).
