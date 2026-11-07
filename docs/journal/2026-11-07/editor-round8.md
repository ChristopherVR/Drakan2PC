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

## Not done

* New exit firing in the game: the pack was authored (`scripts/harness/level_exit_author.mjs`, a copy of the play folder
  `build/editorplay2`, `level_change_walk.py --exit 735232,768800 --dist 5000 --y 153300`). The new exit loaded (the engine's exit list
  shows `exit ... pos 735232 152900 771800 dest 'Level 1E - Inquisitor's Lair'`), but the placement did not hold: Rynn later read
  380000 157000 378000, so the zone was never walked. Not proven. Project map chain with a second level copy not run either.
* The shop form to the end and the shop opening in game; Save As clicked through in the desktop app; the key lock; Landscape tool
  keyboard use beyond reading the tree; a real screen reader.
