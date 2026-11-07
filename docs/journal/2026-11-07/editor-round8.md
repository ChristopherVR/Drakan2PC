# Editor round 8: puzzle forms, keyboard, spoken names, the exit zone

Browser build (Vite dev server, Playwright Chrome, the player's PAL disc, Stratos and Surdana).

## First-user pass on the puzzle templates (Logic > Ready-made setup)

* **Bug fixed: the form never closed.** After Create (and Cancel) the setup form stayed on screen because `.lg-form { display: flex }`
  beat the `hidden` attribute. A global `[hidden] { display: none !important }` in `style.css` fixes this class of bug everywhere.
* Door pick: grouped into Doors (class role or a name with door, gate, portcullis) before Other mechanisms; hints are now visible text,
  not only a tooltip; a Locked door shows a warning in the form (written, not seen firing: no Stratos door carries a Locked value);
  "Show in level" selects and frames the pick; template errors also appear inline (`role=alert`).
* Verified by clicking: lever and pressure plate setups create, the Logic graph shows the wiring (card per object, labelled links),
  Ctrl+Z removes the whole setup (3354 to 3350 objects) and Ctrl+Y brings it back.
* New guide "Build a puzzle" (`guides.ts`, also in the Ready-made menu); `help.guide.puzzles`.
* **Level exit template** placed an exit object that nothing could fire. It now also places a Trigger Detector (player, repeatable,
  On, like the shipped ones) linked to the exit. The first class in the list is chosen by instance count; a class with no instance
  in the level writes a default body (unverified), the export says so.

## Keyboard and focus

* Logic graph: arrows move between cards, L starts a link (arrows choose the target, Enter opens the message list, Escape cancels),
  E steps through a card's links, Enter changes the chosen link, Delete removes it, F fits; every step is announced through a live
  region. Checked with real key presses: create a link, step to a link, Escape from its menu, Delete, undo.
* Context menus return focus to where it was (the graph, an SVG element, was dropped to the body) and take Home and End.
* Story timeline: a key moves with Left and Right (0.1 s, Shift 1 s) and keeps focus; its name includes the actor.
* "Open ISO" in the top bar was a label nothing could focus: now a button for Tab, Enter and Space.
* Hover and focus states measured by script (computed style before and after hover and focus) for buttons, tools, dock tabs, segmented
  buttons, rows, drop-downs, inputs and tabs: all change on hover and show a focus ring (a global `:focus-visible` ring), except
  Outliner rows, which use the tree's own keyboard cursor.

## Drag-over states

A class or story set-up dragged over the Place, Outliner, Details or Content Browser panel now shows a red ghost and a not-allowed
cursor (the drop works only in the level view). The OS-file drop frame appears only for files and names what it takes (disc image,
saved level, save). Not driven with a real OS file drag (Playwright cannot start one).

## Accessibility tree read (Playwright aria snapshots)

Fixed: link pills had no names (now "A opens B. Change what this link does"), severity counts were bare numbers, sliders spoke raw
values (Size 0.512, Camera speed 0), quest rows read "J", "Map", "Opt", two tab strips shared the name "Story editors", the left
dock's help said "Place" in Landscape mode, the Project type column said "Disc". Not run: a real screen reader.

## Wording

"Disc Files" is now "Game Files"; "a disc level" reads "an original Drakan level"; "disc profile is unknown" reads "Open your disc
image first". The ootf class-name fallback ("Effects class 12180") was already in place; unresolved classes read that way.

## New exit fires in the game (proved)

Pack from `scripts/harness/level_exit_author.mjs` (Surdana, class "Transition from 1(A) to 1 (E)", exit object at 735232 152900 771800,
zone 4 x 3 x 4 cells at 735232 153900 768800), played from a copy of the play folder (`build/editorplay2`) with
`level_change_walk.py --exit 735232,768800 --dist 2500 --y 153400 --face --src build/editorplay2 --env DRAKAN_PLAYTEST_GOD=1
--env DRAKAN_TRACE_FN=365138,361440`. Result: the exit handler `365138` receives message 0x20 on the NEW exit's handle (a0 = the handle
the `[levelchange]   exit ... pos 735232 152900 771800` line shows), screens 2 and 5, `exit taken in 'Level 1A - Surdana' at 735232 152900
771800` (the new exit's position, not the shipped Kragmor exit's), then `afterLevelLoad Level 1E - Inquisitor's Lair`, arrival 582012 151911 513024.

Two failed runs first, both useful: starting 3300 units back dropped Rynn back to the level start (the placement did not hold, so
start 2500 back with a height of 153400); and a 6 x 3 x 1 zone (the shipped size, but the shipped box is turned 90 degrees) never sent a
message to the new exit although Rynn walked through it (the shipped Kragmor exit fired instead, trace shows only its handle). A
4 x 3 x 4 zone fired at once, so the template default is now 4 x 3 x 4 (width, height, depth in cells).

## Not done

* The shop form to the end and the shop opening in game; Save As clicked through in the desktop app; the key lock; Landscape tool
  keyboard use beyond reading the tree; a real screen reader.
