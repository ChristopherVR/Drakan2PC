# Editor leftovers (round 10)

Items from `docs/journal/2026-11-07/outstanding-work.md` section 3 and `editor-round9.md`. Own exe copy for in-game checks:
`build/ed-b/play` (a private copy of `dist/DrakanPC` without textures, movies and previews; `dist` was not touched).

## Wizard focus trap, in a real browser (done, with a new check)

`npm run keyboard-check -w @drakan/editor-app` (`editor/packages/app/scripts/keyboard-check.mjs`) drives the built editor in headless
Chrome with real key events (Input.dispatchKeyEvent, no mouse): Enter on the start page opens the wizard, "Make a campaign" turns
the campaign on, and on every one of the six steps 60 Tab and 60 Shift+Tab presses must never leave the wizard (checked at 1280x720
and 1920x1080). Result: the trap holds on all steps. Escape must close the wizard and leave the focus on a control of the editor.

Real defects the check found and fixed:

* Escape closed the wizard but the focus fell to the page body: the opener (the start page button) is redrawn while the wizard opens,
  so the remembered element was `body`. The wizard now remembers the last control outside it that had the focus and, when that is
  gone or hidden, falls back to the first visible control of the editor.
* Pressing "Make a campaign" in the wizard redrew its body and dropped the focus on `body`; it now lands on the step heading
  (`tabindex=-1`, no outline).
* A closed dropdown kept `aria-controls` pointing at a list that only exists while open (10 findings per screen). The attribute is
  set on open and removed on close.
* The logic graph added a second `<defs id="lg-arrow">` every redraw (duplicate id): checked against the canvas, not the cleared layer.
* The step heading inherited the global `h2` panel-header bar (grey band at 1920x1080); it is a plain heading again.

The same script runs axe-style structure rules on the start page, the wizard intro and each step: every button, link, input and
select has an accessible name, dialogs are named and `aria-modal`, ids are unique, `aria-labelledby`/`describedby`/`controls` targets
exist, tabs sit in a tablist, headings do not skip a level, images have `alt`, a live region exists, the page has `lang`, a title and a
main landmark. 0 findings after the fixes. Not covered: a real screen reader (NVDA), colour contrast.

## Quota overflow of the converted-levels store (done as a test, found a real bug)

Not drivable in a real browser without a disc (a 2 GB allocation crashed the tab on the earlier try), so it is a unit test on fake
IndexedDB (`test/virtualstore.test.ts`, "a browser store that fills up while writing"). It found a real bug: `saveConverted` put the
files one by one and a `put` that threw (a full store) left the transaction open, so the earlier puts of the same import still
committed: half of a level was kept. The write now aborts the transaction on any failure, so the store stays exactly as it was; the
page still shows the "no room left" sentence. Switching language with the disc open still does not re-derive a restored pack
(unchanged, reopening the disc does).

## 1920x1080 polish

Read the sweep screenshots again (start page, wizard, Project Settings, Vendors). Fixed the wizard heading band. Left as is:

* Project Settings at 1080 high: the Campaign pane needs about 960 px and gets 606 px next to Checks and Export, so the cover
  picture and its buttons are below a scroll. A first attempt (a smaller flex basis for Checks) did not change the layout and was
  reverted; it needs the pane restructured (two columns inside the Campaign pane at this width).
* After the wizard closes by a mouse-driven reset the focus ring shows on the "File" menu (the fallback target). Only seen in the
  sweep.

## Play with two editors open (fixed in the desktop shell)

Two editor windows share one config folder, one play folder, one mods folder and one `mods.ini`. `game_running` only looked at the
window's own child process, so a second window could Play while the first window's game ran: it rewrote the pack the game was reading
and, when the first game closed, the first window switched its pack off under the second game (and `editor-play-ootf.zip` was
rewritten while locked). Now the window that starts the game keeps `game-session.lock` open (no sharing) until the game and its extra
instances close (`playpack::begin_game_session`, a 1 s watcher ends it); the other window's `game_running` is true while that lock is
held, so Play says "The game is already running. Close it first." and writes nothing. The older `play-pack.lock` (restore at start)
is unchanged. Unit test `a_running_game_blocks_a_second_window_until_it_ends` (Windows; other systems do not share-lock, so there it
is not enforced). Not run with two real editor windows (needs the desktop UI on a quiet desktop).

## Puzzle scenarios (scripts/harness/puzzle_door_check.py)

Run from `build/ed-b/play` with `build/final-game/bin/drakan.exe` (the exe in `dist` made `plate_hold` fail once: the door stayed
open, yaw 1.72 to the end; the final-game exe passes it, so the newer dist exe may behave differently there; not chased). New
scenarios, both measured from guest RAM (`DRAKAN_PLAYTEST_WATCH`), no screenshots:

* `plate_walk_off` PASS: Rynn stands against the held plate until the door is open (yaw 1.77), then holds S and ends 5229 units back
  from it; the door is shut again (yaw 0.02). The stock plate is a solid block she cannot step onto, so "walking off" is leaving the
  side of the plate she pressed it from; the plate rises and sends Off.
* `killall_bow` PASS, control `killall_bow_nobow` PASS: a Bow item is placed 60 units in front of Rynn (new author option `bow`), the
  two dormant skeletons stand 2300 units away (new option `killDz`), she picks the bow up (E), equips it in the inventory (TAB,
  ENTER, RIGHT, E) and taps attack with AUTOAIM on. She never leaves a 1200 unit stretch around her start, so the kills can only be
  arrows: the counter gets both "I'm Dead" messages and the door opens (yaw 1.69). The same recipe without the bow item leaves the door
  shut (yaw 0.00), so the sword and the autoaim lunge cannot reach them. (A first version with the skeletons 900 units away opened
  the door without a bow: melee reached them.)

The scenario list is now 19 (`--only plate_walk_off,killall_bow,killall_bow_nobow`).

## Save As in the desktop app: not run (needs a quiet desktop)

Exact steps, on a desktop nobody else is using:

1. `set WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS=--remote-debugging-port=9340` and start the debug desktop exe
   (`npm run desktop:build -w @drakan/desktop`, then the exe under `packages/desktop/src-tauri/target/debug`).
2. Open a project (or make one with Make a campaign) with one edited level so there is something to copy.
3. Bring the editor window to the front yourself, press Ctrl+Shift+S (File > Save Project As), type a new name in the ask dialog.
4. In the native "Choose where to create the project folder" picker pick an empty folder and confirm.
5. Check: the new folder holds `project.drakan.json` and the level files; the toast names the folder; the title bar and the Project
   menu show the new name; edit a level and close the window: the autosave lands in the copy, the original folder is unchanged.
   Attach to `http://127.0.0.1:9340/json` with a CDP helper to read the page state; send no keystrokes to anything else.

## Not done

* The project map chain with a second level copy in game. Already proven separately: an exit into a copy, the way back, a walk out of
  a copy (`docs/journal/2026-11-01/level-copy-campaign.md`, `2026-11-02/level-copy-direct-play.md`). The chain (copy A's exit
  retargeted to copy B in one project, built by `buildProjectPack`) needs a headless project-pack builder (the scratch one is gone):
  import `project/export.ts` under vite-node with `fake-indexeddb`, a fake `self` for `worker.ts`, a project of two copies and a
  `LEVEL1ON.XDB` override for the Surdana to Kragmor exit, then `level_change_walk.py --exit ...` from the Surdana copy.
* A real screen reader; colour contrast; the OS file drag over panels.

## ui-sweep

Rebuilt and run after the fixes: 192 states in 3 sizes, 0 palette entries missing, 0 findings.
