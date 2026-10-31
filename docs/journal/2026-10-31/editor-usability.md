# Level editor usability pass (2026-10-31)

Goal: make the Level tab easy for a first-time modder without losing the Unreal-style look (commit 418d230).
Method: opened the editor in a browser (Vite dev server, Playwright, a copy of the PAL ISO), then tried the tasks a new
modder would try. Screenshots are in `build/scratch/editor-ux/` (gitignored, `before-*` and `after-*`).

## 1. Friction found

Tasks tried: open a level, find an object, move it, place a creature, change its health, duplicate, undo, export a mod.

| # | Friction | Status |
| --- | --- | --- |
| 1 | The Level tab opened onto an empty viewport. "Open your disc image, then pick a level from the toolbar" does not say where, and the level picker is a bare `Open level...` dropdown that looks like a label. | Fixed: start page |
| 2 | No way to search across things. Finding "the Dragon" meant expanding Outliner groups by hand, or knowing which of three search boxes (Place, Outliner, Content Browser) to use. | Fixed: command palette |
| 3 | Clicking a group name in the Outliner selected its objects but did not open it; only the tiny chevron expanded. | Fixed |
| 4 | Commands were only reachable by memorising shortcuts or hunting through menus (Arrange, Display, Views). No list of "what can this editor do". | Fixed: palette lists every command with its key |
| 5 | Nothing told a new user what to do next once a level was open (drag to place, box select, fly, undo). | Fixed: dismissible tip strip |
| 6 | "Unsaved changes" wrapped to two lines in the toolbar and pushed Export/Play around. | Fixed |
| 7 | Other panels (multiplayer maps, logic graph, story content, desktop shell) had no way to add themselves except editing `levelui.ts`, which several people were doing at once. | Fixed: registry API (below) |
| 8 | The app opens on the Inspect tab, so the first screen is a hex view. | Not fixed (`main.ts`/`index.html` are shared with the desktop shell work). Suggested: open the Level tab once a disc is loaded. |
| 9 | The Place panel and the Content Browser show the same classes in two different layouts, and "Creature" is both a Place filter chip and an Outliner group, so the first click often does something unintended. | Not fixed |
| 10 | The export step is spread over the toolbar button, the Mod tab and the Play button. There is no single "what will be in my mod / where did it go / how do I turn it on" summary. | Partly: palette commands `Export mod` and `How to play your mod`; the Mod tab is unchanged |
| 11 | Duplicate places the copy 500 units away on x and z with no hint. Fine, but the History label only says "Duplicate". | Not fixed (`editor-core`) |
| 12 | A run of the gizmo drag could not be verified in this pass: the shared dev server kept returning 500s while other agents had half-written files in `panels.ts` and `main.ts`. The code path was not touched. | Open |

Things that already worked well and were left alone: drag-and-drop placement, the field Inspector with search and
"Changed" filter, box select, Alt-drag duplicate, History panel with jump-to, autosave restore banner, status bar.

## 2. What changed

* **Start page** (`editor/startpage.ts`). Replaces the empty state. Without a disc: one primary button (Open disc image),
  "Open a saved level", four cards saying what the editor can do, links to search and shortcuts. With a disc: a grid of
  the 14 levels (number plus name), the last four opened on top, autosave reminder. Recent levels are kept in
  `localStorage` (`drakan-editor:recent-levels`).
* **Command palette** (`editor/palette.ts`, `editor/shellcommands.ts`). `Ctrl+P` searches everything, `Ctrl+Shift+P` starts
  in commands. Prefixes narrow it: `>` commands, `#` objects (by class name or id, selects and frames), `@` classes
  (places one at the view centre), `:` levels (opens). Disabled commands are greyed and sorted last. Recent commands
  show first on an empty query. About 90 commands cover file, edit, tools, grid and angle sizes, selection, align and
  distribute, camera, view, panels and help; each shows its shortcut as key caps.
* **Tip strip** (`editor/hint.ts`). One rotating sentence at the bottom of the viewport while nothing is selected; the
  close button hides it for good.
* **Shortcut sheet** gains `Ctrl+P` and `Ctrl+Shift+P` (they also work while typing in a field).
* **Outliner**: click on a group name now also expands it.
* **Dock**: `createDock(...).addTab()` so panels can be added after start-up.

Existing shortcuts, drag-and-drop placement, field edits, mod export and the Inspect and Save modes are unchanged.

## 3. Registration API for other panels (`editor/registry.ts`)

```ts
import { registerCommand, registerSource, registerPanel } from './editor/registry.ts';

registerCommand({
    id: 'mp.newMap', title: 'New multiplayer map', category: 'Multiplayer',
    keys: 'Ctrl+Alt+M',          // display only; the key is not bound for you
    hint: 'Start a deathmatch map from the current level', keywords: 'pvp arena',
    icon: 'plus', enabled: () => hasLevel(), run: () => { /* ... */ },
}); // returns an unregister function

registerSource({                 // dynamic palette results
    id: 'triggers', label: 'Triggers', prefix: '%',
    search: (q, limit) => [{ id: '12', title: 'Door trigger #12', run: () => select(12) }],
});

registerPanel({                  // bottom dock tab + "Show <title>" palette command
    id: 'logic', title: 'Logic graph', icon: 'sliders',
    panel: { el, onSelection() {}, onChange() {}, onLoad() {} }, // same Panel type as the built-ins
});
```

* All three can be called before the editor starts (panels are queued until `levelui.ts` binds the dock).
* A panel registered this way receives `onSelection`, `onChange`, `onVisibility` and `onLoad` like the built-in ones.
* `fuzzyScore(text, query)` is exported for sources.
* Commands call the toolbar actions (`save`, `exportMod`, `openLevel`), so the desktop shell only needs to change those
  actions. Opening the disc goes through `requestOpenIso()` in `levelui.ts` (clicks `#iso-input`); replace that one
  function with the native dialog.
* Style new UI with the tokens in `tokens.css`; shell-level CSS lives in `editor/shellux.css`.

## 4. Not done

* Gizmo and selection changes you listed (surface snapping while dragging, marquee, align/distribute, bookmarks, F to
  frame, snap UI) already exist from the earlier redesign; no further work was needed there beyond palette access.
* Plain-language field labels and tooltips beyond what the schema already provides.
* A unified Save / Export / Play status panel (item 10), and opening the Level tab after a disc loads (item 8).
* Re-verification of the mouse gizmo drag against the current tree (item 12).

## Polish pass (later the same day)

Done and checked with a real mouse in Playwright (build/scratch/editor-polish/after-*.png):

* **Editor icons** replace the textured placeholder boxes of helper classes. `scene/icons.ts` (one instanced billboard mesh,
  constant screen size with a distance shrink and fade, hover and selection recolour the rim), `scene/instances.ts`
  (groups Triggers, Sound, Cameras, Lights, Waypoints, Timers, Spawn points, Barriers; classes with unknown names such as bird
  emitters or "Dragon Barrier" are recognised by their 12-triangle cube model sharing the debug texture of the named ones).
  Selecting or hovering a trigger or barrier icon draws its translucent wire volume. Show menu: "Draw helpers as icons" plus one
  check per group with counts. Icons are picked first (15 px), box-selectable, hide with their group.
* **Tabs** no longer squash: no wrapping, ellipsis, icon-only for inactive tabs when the dock is narrow (tooltip names them),
  sideways scroll with the wheel as the last resort (`editor/panels.ts`, `components.css`).
* **Linking**: Details > Links explains "When this triggers, it sends [message] to these objects", has the message dropdown and
  a primary "+ Add link" (pick mode with a banner: click the target in the viewport or the Outliner, Esc or Cancel ends it);
  right-click on an object (viewport or Outliner) has "Link to..."; a link handle next to the selection is dragged onto the
  target (green line when over a valid object). All of it ends in `editor/linking.ts addLinks` -> `ctx.edit`, so the logic
  graph, link lines and undo stay in sync. The viewport right-click (a click without dragging) now opens the same object menu;
  `registerContextItem` (registry.ts) is the hook other modules use (the play-test agent already added items).
* **Esc**: the shortcut table said `Esc` but key events say `Escape`, so Esc never cancelled a pick or cleared the selection.
  Fixed; Esc inside a popover, dialog or menu is left to it.
* The Level tab opens after a disc loads (`main.ts`). History labels name objects ("Duplicate Orc #123", "Move Timer #7",
  "Link Trigger Detector #1 to Timer #2"; `describeObjects` in editor-core).

Still open: see the list in `editor-ui-review.md` section 6 (Details search at top, draggable dock tabs, nested Content Browser
folders, texture grid, Suggested for array fields, light theme polish, animation preview), a single Save / Export / Play status
panel, Place panel vs Content Browser dedupe, gizmo move/rotate/scale and marquee and asset-drag runs with a real mouse (only
the link handle, link pick, context menu, icon click and hover were exercised), logic graph node and link drags, and trigger
face-handle drags. The viewport toolbar is clipped when the viewport is narrower than about 600 px.

### Second polish pass (real-mouse run, same day)

Verified with Playwright mouse.down/move/up on Surdana (screenshots `build/scratch/editor-polish/v-*.png`, `w-*.png`):
gizmo move (X arrow, grid 256 snap; history "Move Birch Tree 01 #9351"), rotate (15 degree snap on) and scale (0.05 snap), marquee
select (16 objects), drag from the Place panel and from a Content Browser tile (history "Add Stomp Player"), logic graph node drag
and port-to-node link drag, trigger face-handle drag ("Resize trigger volume", opposite face stays), door radius ring drag ("Resize
radius"), reference picker (Esc closes only the picker, the selection stays). No bugs found in those; only the Esc shortcut bug
from the first pass.

New: Mod panel status strip (Edits / Mod contents / Written to / Play) reusing the existing export and play code; Details search
box at the top of the panel (also hides Transform, Links and Info sections that do not match); dock tabs reorder by dragging, order
saved per dock; Content Browser containers expand into one sub-folder per kind; texture picker is a grid (arrow keys move by cell);
reference fields put the name above the button so the play button no longer squeezes the label; viewport toolbars wrap and the tool bar
moves to the bottom edge under 640 px; Show menu helper counts include model-learned groups; Place and Content Browser chips explain
what each kind means.

Not done: dragging a tab into another dock (only one tabbed dock exists; the Outliner and Details are fixed panels); "Suggested" for
array fields (needs the class-default array words in `SchemaProvider.refValues`); light theme polish (handed to the styling agent);
animation preview (the 0x0208 joint block is still undecoded, level-format.md section 11); history labels of logic-graph link
edits and trigger resizes still lack object names (`logicpanel.ts`, `logicview.ts` belong to the logic agent).
