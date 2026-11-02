# Guided tours: interactive tutorials in the editor (2026-11-02)

User request: "let this editor have templates we can use and guide us, highlighting what to click, playing
animations, focusing the UI blurring everything else onto a specific part". Built as Unreal-style interactive
tutorials (coach marks) in `editor/packages/app/src/editor/tour/`.

## What the user sees

- The rest of the window is dimmed and blurred (`backdrop-filter`; a darker plain dim where it is not supported). The
  part the step talks about stays sharp and usable: the shade has a rounded hole there, so clicks reach it.
- Corner brackets close in on the target when a step starts (the one animation per step), and breathe while the step
  waits for the user.
- A callout card sits next to the target (the side with room, an arrow notch toward it): tour name, a progress bar of
  the steps, title, one or two plain sentences, "n of m", Back / Next, "Leave tour". Keyboard: Enter or Right = Next,
  Left = Back, Esc = leave (Esc closes an open menu first). Keys typed into fields are left alone.
- Steps that wait for the user say "Waiting for you to ..." and show a pointer tapping the target (a pulse only on a
  big target such as the viewport). After a while (12 s by default) a hint appears. Next becomes "Skip this step".
- Drag demo: a pointer carries a labelled chip along a dashed arc from a Place row into the viewport, on a loop.
- Open menus, dropdowns, popovers and the Add menu automatically get holes too, so "click Tools, then Save Editor"
  works without the shade getting in the way.
- A missing target (panel not open) leaves the card in the middle; after 6 s it says the part is not on screen and Next
  moves on.
- `prefers-reduced-motion`: no bracket/pointer/pulse animation; the drag path is drawn but not animated.
- Everything is re-measured each frame, so window resizes, splitter drags and scrolling keep the spotlight on its
  target; a target cut off in its scroll container is scrolled into view once.
- Finished: "Tour finished" card with More tutorials / Close.
- Help > Start a tutorial... (`help.tours`, also in Ctrl+P) lists the tours in two groups with Start, or Resume /
  Restart for a tour left part-way, and "Done" for finished ones. Typing "tutorial" or a tour name in Ctrl+P lists
  them too.
- Progress is kept in localStorage `drakan-editor:tours` (`{ done: {id: time}, left: {id: step} }`), read and written
  in try/catch.

## Tour ids

| Id               | Title                   | Start template / Help guide                            | Notes                                                                                                        |
| ---------------- | ----------------------- | ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------ |
| `editor`         | Editor tour             | guide `start`                                          | disc, level, Place, pick a group, drag demo, 3D view, select, Outliner, Details, Content Browser, Play, Help |
| `campaign`       | Change a campaign level | template `campaign`, guide `objects`                   | select, move (waits for a change), Details value, drag from Place, undo, Play                                |
| `newlevel`       | Level copy              | template `newlevel` (alias `level`)                    | Project Settings via the project button, create project, Add a copy, name it, the level list                 |
| `puzzle`         | Pressure plate puzzle   | template `puzzle`, guide `triggers`                    | Add > Puzzle, the Logic form, Create (waits for the change), the graph                                       |
| `cutscene`       | Make a cut scene        | template `cutscene`, guide `story`                     | Add > Cut scene, name, camera points, steps, Create cut scene                                                |
| `vendor`         | Shop and prices         | template `vendor`, guide `vendors`                     | suggests Surdana; Add > Vendor, the shop, stock (waits for a change), add an item                            |
| `build-campaign` | Build a campaign        | (guide `campaign` should set `tour: 'build-campaign'`) | Project Settings, mod details, Add two levels, Map, the Campaign map, Checks, Export                         |
| `arena`          | Multiplayer arena       | template `arena`, guide `multiplayer`                  | Add > Arena, Start new map in World Settings, spawns, Validation                                             |
| `save`           | Edit a save             | template `save`, guide `save`                          | Tools > Save Editor; hands over to `save-window` in the new window                                           |
| `save-window`    | Save Editor             | (Save Editor window only)                              | open a save, the section tabs, change a value, Save / Export                                                 |

`hasTour(id)` also accepts the aliases in `TOUR_ALIASES` (`tours.ts`): `start`, `objects`, `triggers`, `vendors`,
`multiplayer`, `story`, `level`. Note the Help guide `campaign` is "Build a campaign", which is the tour
`build-campaign`; the plain id `campaign` is the start template "Change a campaign level".

Handing over to the Save Editor: the `save` tour's step that opens it writes `drakan-editor:tour-next-window`
(`{id, at}`); `saveapp.ts` reads it once on start and runs that tour if it is less than two minutes old.

## Code

- `tour/model.ts` (pure, tested): `Tour` / `TourStep` types, `TourRunner` (next / back / goTo / close, `skipIf` in
  the direction of travel, waits: `next`, `click` on the target, `command` ids, an editor `event` with a filter,
  `until` polled every 250 ms, hint timer), `TourProgress` (storage-safe), and the geometry: `placeCard`, `padBox`,
  `mergeBoxes` (overlapping holes would cancel out under even-odd), `shadePath`.
- `tour/engine.ts`: `TourOverlay`, the DOM: shade with `clip-path: path(evenodd, ...)`, brackets, card, pointer and
  drag demo (Web Animations), keyboard, outside-click nudge ("Use the lit-up part of the window. Esc leaves the
  tour."), the per-frame layout loop.
- `tour/events.ts`: the event bus. `tourBridge(ctx)` is a hidden panel in the Level Editor's panel list that emits
  `level-load`, `selection` (detail: selected count) and `change`; every registered command emits `command` (detail:
  id) via the new `onCommandRan` in `registry.ts`. Helpers `levelIsOpen`, `discIsOpen`, `selectedCount`.
- `tour/install.ts`: one tour at a time, resume / restart, the tour list dialog, `help.tours`, the palette source, and
  `registerTourStarter` for `tour/registry.ts` (the API the start page and Help call: `startTour(id)`, `hasTour(id)`).
- `tour/tours.ts`: the tours. `tour/index.ts`: `installEditorTours()`, `installSaveWindowTours()`, `tourBridge`.
- `tour/tour.css`: theme tokens only, light theme checked.

Small edits elsewhere: `registry.ts` (commands are stored wrapped so `onCommandRan` hears every way of running
them), `levelui.ts` (`tourBridge(ctx)` added to the panel list), `main.ts` (`installEditorTours()`), `saveapp.ts`
(`installSaveWindowTours()`), `menubar.ts` (Help > Start a tutorial...). No `data-tour` attributes were needed: the
targets are existing ids (`#ed-add`, `#ed-play`, `#project-chip`, `#pj-add`, `#pj-view-map`, `#sv-open`, ...) and,
for buttons without one, `byText(selector, text)`.

## Adding a tour

1. Write a `Tour` in `tours.ts` (or in the feature's own module) and add it to `EDITOR_TOURS`, or call
   `addTours([tour])` from the feature's install. Use the start template id when it belongs to one.
2. Steps: `target` is a selector (first visible match) or a resolver; `also` lists extra elements that stay usable;
   `side` is a preference. Reuse `discStep`, `levelStep()`, `quickAddSteps(row, title, opened, text)`,
   `placeDragStep`, `playStep()`, `exportStep`.
3. Make a step wait only when the editor can see the action: an `until` test on the DOM or project state is the most
   robust (the Add menu runs its rows directly, not as commands); give it `doing` ("open a level") and a `hint`.
   `skipIf` passes over steps already done (disc open, level open, project exists).
4. Text: plain, one or two sentences, names written as they appear on screen. `test/tours.test.ts` checks ids are
   unique and texts short.
5. Run it: `build/scratch/tours/runtour.py <id> [level]` (scratch, not committed) drives a tour start to end in a
   private Playwright context on a static build with the disc image and screenshots each step.

## Checked

Static `vite build` of the app, a private Playwright context per run (Python, its own port), the PAL disc image. Each
tour was started from Help > Start a tutorial and run start to end; each waiting step was finished by doing the action
it asks for, and moved on by itself:

| Tour                   | Level                                                                             | Result                                                                                                                                  |
| ---------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `editor`               | Intro, 1600 x 900; again at 1000 x 700 with reduced motion                        | all 13 steps; disc, level, Creatures, drag from Place into the viewport placed Ballista, selection skipped (the new object is selected) |
| `campaign`             | Intro                                                                             | Outliner selection, Details value; the move step was skipped by the script (no gizmo drag scripted)                                     |
| `newlevel`             | (Project Settings)                                                                | project created, copy "Tour copy" added through the copy popover                                                                        |
| `puzzle`               | Ravenshold (Intro has no plate class: Create stays disabled and the hint says so) | Add > Puzzle, Create made the plates; also run in the light theme                                                                       |
| `cutscene`             | Intro                                                                             | Add > Cut scene, camera point added, Create cut scene                                                                                   |
| `vendor`               | Surdana                                                                           | Add > Vendor, price change in the stock table, Export points at the project menu in the Vendors tab                                     |
| `build-campaign`       | (Project Settings)                                                                | project, two levels added, Map shows the Campaign map, Checks, Export                                                                   |
| `arena`                | Intro                                                                             | Add > Arena, Start new map                                                                                                              |
| `save` + `save-window` | (Save Editor window)                                                              | Tools > Save Editor opened `save.html`, which started its own tour; a save opened, a value changed, Save                                |

Progress after each run: `{"done":{"<id>":...},"left":{}}`. Screenshots of every step (spotlight, blur, demo
pointer, menus kept in holes, finished card) are in `build/scratch/tours/shots/` (not committed). `npx vitest run`
(`packages/app/test/tours.test.ts`: sequencing, skips, waits, hint timer, progress storage, geometry, content), `tsc`
and prettier pass.

Fixed on the way: the Add menu runs its rows directly (no command), so those waits look at what the row opens; the
card now keeps clear of an open menu and of a step's extra elements; thin overlaps between padded holes are trimmed
instead of joined (joining unblurred the Content Browser in a small window); a target pushed outside the window
counts as not on screen.

## Left

- The start page "Show me" buttons and the Help drawer are wired by their owners with `startTour(id)` / `hasTour(id)`.
- Not checked: the desktop app (WebView2), touch, a screen reader pass.
- The campaign tour's "Move it" step waits for a level change; dragging the gizmo is the only way it is met.
