# Editor shell: resizable panels and the Place sidebar (2026-11-02)

Two complaints about the Level Editor: "we're unable to resize panels" and "where is the left-hand sidebar?" (Unreal
has Place Actors on the left).

## Why resizing did not work

`editor/layout.ts` already wired the right column and the bottom drawer to splitters, and in a static build with a mouse
drag both did move. What made the layout feel fixed:

- The Outliner / Details splitter had **no height**: `#ed-split-outliner` sits in a flex column and `.splitter.h` only
  set a cursor, so it collapsed to 0 px and could not be grabbed at all.
- The splitters were 4 px of `--surface-0` between panels of almost the same colour, with no wider grip: hard to see and
  hard to catch. `.ui-splitter` in `components.css` was paint only (a hover line), no cursor, no hit area, no size; the
  cursors lived in `editor.css` on `.splitter` only.
- Double-click collapsed the panel instead of restoring a size, the window shrinking never refitted the panels, and the
  stored sizes were trusted as they came out of localStorage.

## What changed

- `editor/splitmath.ts` (new, pure): size limits, room left for the viewport, drag, keyboard steps, defensive read of
  the stored layout. Tests in `packages/app/test/shell-layout.test.ts`.
- `editor/layout.ts` (rewritten): four panes with one code path: Place (left), Outliner and Details (right), bottom
  drawer, and the Outliner / Details split. Each splitter is a focusable `role=separator` with `aria-valuenow/min/max`,
  `aria-controls` and a plain label. Drag; arrow keys (16 px, Shift 64 px), Home / End (smallest / largest), Enter
  (hide / show a side panel or the drawer); double-click goes back to the starting size. Dragging a side panel or the
  drawer almost shut hides it; a tab on the viewport edge ("Place", "Outliner") brings a hidden side panel back. The
  viewport keeps at least 40 % of the width (360 px floor) and 30 % of the height; a window resize refits the panels
  without forgetting the sizes the user chose. Sizes and hidden panels are stored under `drakan-editor:layout` (the old
  `outliner-h` key is read once); `store` in `dom.ts` already wraps storage in try/catch.
- `components.css` `.ui-splitter` / `.splitter`: cursor per direction, an 8 px invisible grip (`::before`), a 2 px accent
  line on hover, drag and keyboard focus, reduced-motion respected.
- The dock layout CSS moved from `editor.css` to `workspace.css` (`#ed-body` is now
  `left | 4px | viewport | 4px | right`); a hidden panel's splitter stays as a draggable gap.

## Place sidebar

`editor/placepanel.ts` + `placepanel.css` (new), grouping rules in `editor/placecats.ts` (new, pure). Like Unreal's Place
Actors: groups down the side (Recently placed, Characters, Creatures, Items, Chests & breakables, Doors & mechanisms,
Lights, Triggers & logic, Cut scenes, Conversations, Quests, Vendors, Level exits, Sound, Effects, Sky & weather,
Scenery, Other), the group's rows beside them, a search box over every group.

- Groups come from the display category in `editor-core/src/categories.ts` (the same one the Outliner and Content
  Browser use, RFL id first), split further for the sidebar: characters vs creatures by name, quests vs conversations
  vs shops, and spell parts / generators left in "other" go to Effects.
- Rows drag into the viewport with the Content Browser's pointer drag (`assetdrag.ts`, `LevelView.dragKey` /
  `dragHover` / `dragDrop`); `attachAssetDrag` got an optional `onDrop` so the sidebar hears about a drop. Double-click
  or Enter places in the middle of the view (`ctx.placeClass`).
- Groups also list set-ups that make something: Quick Add rows (cut scene, dialogue line, quest, vendor) and the logic
  templates (lever opens door, pressure plates, key lock, level exit ...). They open the editor that builds them.
- "Recently placed" keeps the last 12 things placed or set up (`drakan-editor:place-recent`).
- Shown or hidden from Window > Toggle the Place panel (`view.left`), the toolbar's Show menu (Panels), the button in
  the panel header, Enter on its splitter, or the edge tab.
- `levelui.ts` change is four lines: make the panel, put it in `#ed-left`, add it to the panel list. `index.html` has the
  `#ed-left` aside and its splitter.

## Checked

Static `vite build` and a private Playwright context with the PAL disc, level 0 (Intro), 1600 x 900:

- Every splitter is hit at its centre (`elementFromPoint`) and resizes: right 340 -> 460, drawer 280 -> 380, Outliner
  300 -> 380, Place 280 -> 380 by mouse; Place +80 by keys; Enter hides it (edge tab shows), the tab restores it;
  double-click restores the right column to 340; sizes survive a reload; at 1100 x 700 the panels refit (viewport 440).
- Dragging "Arokh" / "Ballista" from Creatures onto the ground placed it ("Added Ballista #436 with default settings.")
  and it appeared under Recently placed. Search "torch" lists matches by group.
- `npx vitest run` (706 tests), `tsc`, prettier on the touched files.

Not checked: the desktop app (WebView2), touch, a screen reader pass, light theme.

## Left

- The Content Browser's own hint ("Drag a class from the Content Browser into the world") does not mention the Place
  panel (viewport hint, owned elsewhere).
- Row thumbnails: rows show the category icon; the model thumbnail is used only as the drag ghost when it exists.
- The character / creature split is a name list; an RFL-level split (villager vs enemy AI classes) would be better.
- Splitters inside other editors (Story, Logic graph, Project) are not converted to the shared splitter yet.
