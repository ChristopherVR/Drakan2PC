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
