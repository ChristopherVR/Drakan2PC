# Start page redesign: "What do you want to make?" as a template picker

User report: the start page cards were confusing because clicking one "doesn't really do anything".

## What the cards did before

Measured with a private Playwright context on a static `vite build` and the real disc image.

| Card | What a click did | What the user saw |
| --- | --- | --- |
| Arena | `task.multiplayer`: switched the right sidebar to World Settings, set the project task | The start page stayed; a small change in the right column and the checklist card |
| Puzzle | `openTemplate('pressure-plates')` | Toast "Open a level from the disc first." and nothing else |
| Cut scene, Dialogue, Quest | Opened Story in the bottom dock | The start page stayed on top; the dock changed at the bottom edge |
| Edit a campaign level, Level copy | Set `projects.pickFor`, which redrew the page as a level list | This one worked |
| Change the story | Opened the Story tab | Worked, but the project was still called "My multiplayer map" from an earlier click |

So most clicks changed something at the edges of the window while the page itself stayed the same. The checklist
was there but had no visual link to the click, and on windows under 1100 x 760 it started as a collapsed pill.

## What it is now

Laid out like Unreal's Project Browser (`editor/packages/app/src/editor/startpage.ts`, `startpage.css`):

- Template tiles grouped as Levels and maps, Story and characters, Saves and imports. Each tile has a small top-down
  SVG sketch drawn from theme tokens (`startart.ts`, no game assets), a title and one line.
- A click selects the tile and fills the details pane on the right: a larger sketch, two sentences, "What you'll do"
  (the first steps of its checklist), what it needs, and one primary button. Double-click or Enter on the selected
  tile starts it.
- The button does the first real step and says which (`startPlan` / `primaryLabel` in `starttemplates.ts`):
  - no disc: "Open your disc to start" opens the disc picker; the pane shows "Waiting for your disc image..." with
    Choose again / Cancel, and once the disc is read the editor carries on with the same template;
  - needs a level: "Choose a level" swaps the tiles for a level picker (good place to start, opened recently, all
    levels; Surdana is suggested for shops), then opens the level and runs the template;
  - otherwise it runs straight away (Story editor, Menus editor, Save Editor window, Order of the Flame importer).
- Running a template files the work in the mod project under the right task (a new project is named after it, e.g.
  "My puzzle mod"), opens the editor that makes it, and starts its checklist (`startflow.ts` keeps the running
  template).
- The checklist (`project/checklist.ts`) shows the running template's steps, "Step n of m", the next step highlighted
  with its button, "Done, next step" for steps the editor cannot see, and a "Making" list to switch template. It
  opens itself with a short ring when a template starts; collapsed it still names the next step. Over an open level
  it sits in the viewport's bottom-left corner, so it no longer covers World Settings or Details.
- The disc bar, the mod project row (Project Settings, Open, New, recent projects), recent levels, "Just look around:
  open any level" and "Open a saved level" are kept below.
- Narrow overlay (under 760 px): one column of tiles and the details shrink to a bar pinned to the bottom (title and
  button).

Templates: Change a campaign level, Multiplayer arena, New level from a copy, Pressure plate puzzle, Shop and prices,
Cut scene, Dialogue line, Quest, Story texts, Menu page, Edit a save, Order of the Flame level. Shop, Menu page and the
importer only appear once their feature has registered its command.

## Verified

Every template was started from a fresh browser context with the disc open; each one lands in the right place with the
checklist on its step 2 (level templates) or step 1:

| Template | Lands in |
| --- | --- |
| Campaign level | Level Editor, level open |
| Arena | Level Editor, World Settings shows the start form for the picked level |
| Level copy | The copy open, Project Settings tab added |
| Puzzle | Level Editor with Logic and the pressure plate setup below the viewport |
| Shop | Vendors tab with the first shop of the level |
| Cut scene / Dialogue / Quest | Story in the bottom dock on Cut scenes / Write a new line / Place a quest marker |
| Story texts | Story tab |
| Menu page | Menus tab |
| Order of the Flame | Importer dialog |
| Edit a save | `save.html` in its own window (the checklist is left alone) |

Without a disc: Arena (disc picker, then level picker, then the arena) and Story texts (disc picker, then the Story
tab) both continue after the disc is read. Double-click starts a template; the light theme and a 1000 x 720 window
were checked. `npx vitest run` (`packages/app/test/starttemplates.test.ts` covers the plan, labels, availability,
suggestions and step states), `tsc` and prettier pass.

## Notes

- The pressure plate setup needs a plate class in the level; Surdana has none ("This level has none to copy"). A
  suggested level for the puzzle template would help once we know which levels have plates.
- The arena still needs one press of "Start new map" in World Settings; the checklist's first step says so.
- `.start-task*` rules in `project/project.css` are no longer used by the start page.
- Small hook in `levelui.ts`: the start page's `openLevel` returns the load promise and `levelOpen()` reports whether a
  level is open.

## Round 2: instant feedback when a level opens

User report: "click on something like editing a level goes back there and only after a few seconds it loads the 3D
view world."

### What happened

Measured with a private Playwright context on a static `vite build` and the real disc image (Change a campaign level,
pick Surdana, timings from the click):

- The click on a level redrew the start page with the template tiles again (the picker closed) and a small "Opening
  Surdana..." line in the details pane. That is the "goes back there".
- The page stayed until the worker had parsed the level: 6.6 to 18.7 s over five runs, then the viewport showed the
  objects; terrain 0.4 to 0.7 s later; models 15.7 to 33.6 s after the click.
- Why so slow: at disc open the Story session reads every string table of the disc (`loadStoryEdit`, 23 s on its own
  that way). Worker requests interleave at every disc read, so the level parse shared each read with that scan. And
  each read of a browser `File` is its own asynchronous slice: the level parse makes about 4,500 small reads (class
  tables), 0.3 s in Node but seconds in the browser.

### What it is now

- **Block cache for the disc** (`blockCacheSource` in `packages/formats/src/bytes.ts`, used by the worker): 64 KiB
  aligned blocks, 16 MiB kept, long reads bypass it, every result is a copy (callers transfer buffers). The level parse
  drops from about 8 s (even alone) to 0.2 to 0.6 s; `loadStoryEdit` from 23 s to 0.1 to 0.5 s.
- **Foreground first in the worker** (`worker.ts`): `loadLevel`, `loadTerrain` and `loadModels` are foreground; the
  whole-disc scans (story texts, quest and string usage, exits) read through a gated view of the disc that waits while
  a foreground request runs and for 250 ms after (the viewport asks for the next model batch right after drawing the
  last). The level parse moved to `readLevel(fs, dir)` so the scans use the gated view.
- **Loading card in the viewport** (`editor/loadprogress.ts`, `loadprogress.css`, pure state in `loadsteps.ts`): the
  moment a level is chosen (start page, toolbar, Ctrl+P, project panel) the start page goes and the viewport shows
  "Opening Surdana" with the steps Reading the level, Placing objects, Building the terrain, Loading models, a bar
  and Cancel. Model batches are real counts; the two steps without a count of their own fill from an elapsed-time
  estimate that never reaches its end. Once the objects are placed the card becomes a strip under the viewport bars
  ("Loading models: 120 of 538", Stop loading) so the level can be used while terrain and models fill in; at the end
  it says "Surdana is ready" (and how many object types have no model) and goes. Cancel while reading goes back to
  where you were (the picker, with the level still chosen); Stop keeps what has loaded. Opening a level over another
  dims the old one under the card. The checklist steps aside while the card covers the viewport.
- `levelui.ts` `openLevel`: hides the start page at once, drops a late result of an older open (token), keeps the
  start page from coming back while the document is empty (`opening`), and feeds the card from `sceneload.ts`
  (`loadLevel(..., progress)`, new `stop()` that only stops the models). A cancel rejects with `LoadCancelled`, which
  `fail` ignores.

### Start page clarity

- A line above the tiles shows the way in: 1 Choose what to make, 2 Choose a level (or Open your disc), 3 Continue in
  the Level Editor (or the Story editor, Vendors, ...), with the current step marked (`flowSteps`).
- The selected tile has a check in its corner and an accent title; its tooltip says to press the button or double-click.
- The level picker selects first: a click marks the level, the button then says exactly what it does ("Open
  Surdana", "Copy Surdana") and the line under it starts with "Next:" ("Next: Surdana opens in the Level Editor, with
  World Settings on the right."). Double-click or Enter opens straight away. Recent and suggested levels in the picker
  no longer have coloured borders, so only the chosen one is coloured.
- The button comes right after the two-sentence description (it was below the steps and out of view on a 900 px
  window); while picking, the sketch makes way for it.
- Back to the start page: a Home button at the left of the toolbar and File > Start page (`view.startPage`, also in
  the desktop menu). Over an open level the page shows "Back to Surdana" (Esc does the same) and sits above the
  viewport's story badges and hints; the checklist hides meanwhile.

### Timings (click to ...)

| | Before | After |
| --- | --- | --- |
| Start page leaves, feedback shown | 6.6 to 18.7 s (small spinner on the start page) | 0.03 to 0.05 s (loading card) |
| Objects in the viewport | 6.6 to 18.7 s | 0.51 to 0.73 s |
| Terrain | 7.0 to 19.3 s | 1.2 to 1.7 s |
| All models | 15.7 to 33.6 s | 3.1 to 4.5 s |

Surdana (4,675 objects, 538 classes), 1500 x 900, three to five runs each; before and after runs alternated. Probes
and frame sequences are in `build/scratch/start-flow/` (`measure.py`, `tour.py`, `shots/`), not committed.

Checked: dark and light themes, 1500 x 900 and 1000 x 720, Cancel while reading (back on the picker with Kragmor
still chosen), Stop while loading, switching levels from the toolbar, Home and Esc. Tests: `loadsteps.test.ts`,
`starttemplates.test.ts` (button labels, Next lines, steps line), `packages/formats/test/blockcache.test.ts`.

### Notes

- The level parse itself has no progress of its own; it is short now, so the estimate is enough. A worker progress
  message would be the next step if a level ever takes long again.
- At 1000 x 720 the window scrolls sideways a little (the toolbar is wider than the window); not part of this change.
