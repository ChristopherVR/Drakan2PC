# Editor UI review and Unreal-style redesign (2026-10-31)

Feedback: the Game / Mod / Level editor "looks very poorly vibe-coded" and should feel like the Unreal Engine editor.
Scope: `editor/packages/app` (Level mode shell, shared tokens and components). Save mode (`save/*`, `saveui.ts`,
`save/save.css`) belongs to another agent and was not touched; it consumes the new tokens.

Screenshots: `build/scratch/editor-ui/before/` and `build/scratch/editor-ui/after/` (gitignored). Taken at 1600x900 against
a real disc (Level 1A, 4675 objects) with Playwright on a Vite dev server.

## 1. Review of the old UI (before)

Files: `before/01-inspector.png` (Inspect mode), `02-level-empty.png`, `03-level-open.png`, `04-outliner-expanded.png`,
`05-outliner-children.png`.

What read as amateur:

* **Two stacked headers.** An app header ("Disc Inspector", Open ISO, three tab buttons, status text) sat directly above
  the editor toolbar. Two bars with unrelated styling, the app name describing only one of three modes.
* **Toolbar wrapped to two rows at 1600 px** and pushed "Export mod" and the theme/help buttons onto a second line,
  floating alone on the left. 17 controls in one strip: file, tools, snap, edit, menus, camera and a slider, with no
  hierarchy. The viewport tools (move/rotate/scale, snap, camera speed) lived in the app toolbar, far from the viewport.
* **No tab or panel identity.** Panel titles were small grey capitals ("OUTLINER", "INSPECTOR") with letter spacing, the
  bottom dock tabs were underlined text. Nothing said which panel was active; panels had no seams, just thin lines.
* **Light theme by OS preference.** A pale grey-white editor around a 3D scene is the opposite of every game editor; the
  grey grid, grey terrain and light chrome had no contrast. Colours were also defined three times (`style.css`,
  `editor.css` per theme, `save.css` fallbacks), with different greys in each.
* **Inconsistent controls.** Buttons were 3 px, 4 px, 6 px, 8 px and 999 px rounded in different places; control heights
  were 22, 24, 26, 28 px; borders on some inputs and none on others; selects were native and unstyled next to custom
  buttons; the "Export mod" button was the only filled control and was light blue on white.
* **Type.** `system-ui` at 13 px in the shell and 12.5 px in the editor, headings in all-caps with tracking, monospace
  for labels that were not data, bold group names competing with bold object names. No type scale.
* **Density and alignment.** 24 px rows with 6 px gaps in the Outliner, but 28 px property rows in the Inspector and
  36 px list rows elsewhere; labels truncated at 12 characters ("Can Arokh Land ...") while the control column had spare
  width; the Inspector put its search box below the transform block.
* **Empty states** were centred grey text with no icon or next step ("Open a level to see its objects.").
* **No feedback layer.** No context menus anywhere, native `title` tooltips with a 1 s delay and no shortcut keycaps,
  toasts centred over the viewport, no log of what the editor had said, status bar with no way into any panel.
* **No orientation widget** in the viewport and no overlay toolbars; the Asset Browser showed every class in one long
  scroll with no way to browse by container (ODB).
* **Iconography** was already one hand-drawn 24-pixel stroke set (good) but small and mixed with text-only buttons.

## 2. What the Unreal Engine 5 editor does (patterns adopted)

Sources: Epic's Unreal Editor Interface and Level Editor documentation, plus screenshots of UE 5.x. Layout and feel only;
no Epic assets, icons, fonts or code.

* **Near-black neutral workbench**: window gaps darker than panels, panels darker than tab strips, recessed input wells,
  one blue accent, orange only for viewport selection. Borders are seams, not outlines.
* **Main toolbar** (Save, Source Control, Content, Play) above everything; **Modes / Place Actors panel** on the left
  with categories and draggable items; **Outliner** over **Details** on the right; **Content Drawer** at the bottom with
  a folder tree, filters and thumbnail tiles; **Output Log**; **status bar** with the drawer buttons on the left and a
  command/message area.
* **Viewport overlay toolbars**: left (menu, Perspective, Lit, Show), right (transform tools, world/local, snap grid,
  rotate snap, scale snap, camera speed). The orientation gizmo sits in a corner.
* **Tabbed, dockable panels** whose tab headers have a coloured top edge when active; panels resize with thin splitters.
* **Details**: categories as collapsible bars, a search field, an "override" marker on changed properties.
* **Density**: about 12 px text, 22-24 px rows, 24 px controls, tooltips with key caps.
* Right-click context menus on the Outliner and content tiles; Ctrl+Z/Y, W/E/R, F, Del, Ctrl+D.

## 3. Design system

Tokens: `editor/packages/app/src/tokens.css` (stable names; legacy aliases kept so `save.css` and older rules work).

| Group | Tokens |
| --- | --- |
| Surfaces | `--surface-0` (window gaps, viewport) `--surface-1` (panel body) `--surface-2` (toolbars, headers) `--surface-3` (raised controls) `--surface-4` (hover) `--field` (input well) |
| Text and lines | `--fg` `--fg-muted` `--fg-faint`, `--border` (seams) `--border-strong` (control edges) |
| Accent | `--accent` `--accent-hover` `--accent-fg` `--accent-soft` `--select` (selected row) `--focus` |
| Status | `--ok` `--warn` `--bad` `--play` and `--ok-soft` `--warn-soft` `--bad-soft`; axis colours `--axis-x/y/z` |
| Type | `--font`, `--mono`, `--fs-2xs` 10, `--fs-xs` 11, `--fs-sm` 12, `--fs-md` 13, `--fs-lg` 15, `--lh` |
| Space and shape | `--sp-1..6` (2 4 6 8 12 16), `--r-xs/sm/md/lg` (2 3 5 8), heights `--h-appbar` 32, `--h-toolbar` 40, `--h-tab` 28, `--h-ctl` 24, `--h-ctl-sm` 20, `--h-row` 22, `--h-status` 24 |
| Effects | `--shadow-1`, `--shadow-pop`, `--ring` (focus), `--t-fast`, `--t-med` |
| Legacy aliases | `--bg --panel --panel2 --raise --hover --line --text --dim --sel --shadow --r --ed-font --ed-mono` |

Dark is the default (`data-theme` on `<html>`, set before first paint by a script in `index.html`, persisted by
`editor/theme.ts`; light stays available through the theme button).

Components: `editor/packages/app/src/components.css` (one definition of each look; the Level shell's older class names
are listed beside the `ui-*` names) and `editor/packages/app/src/editor/ui.ts` (behaviour).

* Buttons `.ui-btn` (`.primary .play .danger .ghost .sm`), icon buttons `.ui-tool` (`.active .wide .sm`).
* Fields `.ui-input .ui-select .ui-search` (`searchField()`), `.ui-number` with `.scrub.axX/axY/axZ`, `.ui-switch`,
  `.ui-check`.
* Structure `.ui-tabs > .ui-tab`, `.ui-panel-head`, `.ui-panel-title`, `.ui-panel-tools`, `.ui-section(-head)`,
  `.ui-row` (tree and list rows, `.sel`), `.ui-prop`, `.ui-splitter`.
* Feedback `.ui-menu` (`.ui-menu-item .ui-menu-label .ui-menu-sep`, `contextMenu()` / `onContextMenu()`), `.ui-toast`,
  `.ui-empty` (`emptyState()`), `.ui-chip`, `.ui-badge`, `.ui-kbd`, `.ui-dot`, `.ui-tooltip` (`installTooltips()` turns
  any `title="Move (W)"` on a tool button into a styled tooltip with a key cap).
* Icons: the existing hand-drawn 24 px stroke set in `editor/dom.ts` (`icon(name)`), extended with 20 glyphs (folder,
  folder-open, outliner, details, place, terminal, gauge, rocket, filter, ...). It is original artwork, so there is no
  third-party licence to credit; Lucide was not added to keep the lockfile unchanged.

## 4. Level mode rebuilt

* **App bar** (`#appbar`): brand mark, Open ISO, mode tabs (Inspect / Level / Save) with an accent underline, status text.
* **Main toolbar**: level picker, open, save, undo / redo / duplicate / delete, Select and Arrange menus, then dirty state,
  Export mod, **Play** (opens the Mod panel's launch steps) and theme / help. It no longer wraps.
* **Viewport overlay toolbars** (`#vp-bar-left`, `#vp-bar-right`): Show, Display, frame, top view, Views on the left;
  select / move / rotate / scale, world-local, grid snap and size, angle snap and step, camera speed on the right (they stack
  below 980 px of viewport width). **Orientation gizmo** (`editor/navgizmo.ts`) follows the camera; clicking Y gives the
  top view.
* **Left: Place panel** (`editor/place.ts`): search, scope (in this level / all classes), kind chips, classes grouped by
  kind with thumbnails; drag a row into the viewport (shared pointer drag in `editor/assetdrag.ts`), double-click or
  Enter places at the view centre.
* **Right: Outliner over Details** with a draggable divider (height persisted). Outliner has the rounded search field,
  kind colour dots, visibility and lock toggles and a **right-click menu** (frame, select same class, hide, lock,
  duplicate, delete). The old Inspector is now **Details** with sectioned bars and quieter category headings.
* **Bottom: Content Browser** (folders: All classes, In this level, Favourites, then every ODB container with counts,
  breadcrumb, kind chips, size slider, tile context menu), **Output Log** (timestamped history of every toast and status
  message, severity filter, badge in the status bar), Validation, History, Mod.
* **Status bar**: Content Browser and Output Log drawer buttons (second click folds the dock), message, selection,
  snap, camera, autosave and frame stats. Toasts moved to the lower right.
* Dock sizes and the Outliner divider persist (`editor/layout.ts`); double-click a splitter to collapse or reset.
* All earlier behaviour is kept: element ids used by harnesses (`#ed-tool-*`, `#ed-save`, `#ed-export`, `#ed-level`,
  `#sb-*`, ...) are unchanged; shortcuts (Q/W/E/R, F, Del, Ctrl+D/Z/Y/C/V/S/E, F1) are the same table.
* Inspect mode picked up the tokens (app bar, headings, lists, tables) and no longer follows the OS colour scheme.

## 5. Verification

* `npm test`: 206 passed (18 files; vitest 3.2.7). `npx tsc` (via `npm run typecheck`) clean.
* Browser (Chromium via Playwright, real disc, Level 1A): opened the level, expanded the Outliner, selected an object,
  Details fields, right-click menu, Show menu, Output / Validation / Mod / Content Browser tabs, folder filter (DEBUG),
  theme toggle, 1280x720 layout, Inspect mode. **Drag from the Place panel into the viewport added an object (4675 to
  4676)**; W / E / R switch tools; Ctrl+D added one, Ctrl+Z undid it.
* Before / after pairs: `before/02-level-empty.png` vs `after/02-level-empty.png`; `before/03-level-open.png` vs
  `after/03-level-open.png`; `before/04-outliner-expanded.png` vs `after/04-selected-details.png`; `before/01-inspector.png`
  vs `after/14-inspect.png`. Extra after shots: `05-context-menu`, `06-tooltip`, `07-show-menu`, `08-output-log`,
  `09-validation`, `10-mod`, `11-content-folder`, `12-narrow-1280`, `13-light`, `15-drag-from-place`, `16-after-edit`.
* `npm run build` passes (it failed once mid-session on a nested `@keyframes` in the Save agent's `save/save.css`, since
  fixed). `prettier --check` flags only `save/save.css` and `save/shell.ts`, which belong to the Save redesign.

## 6. Not done / follow-ups

* No dockable tab *dragging* between docks (tabs are fixed per dock); sizes and collapse state do persist.
* The Details panel keeps the Fields search inside its own section rather than at the top of the panel.
* Viewport right-click is the fly camera, so the viewport has no context menu (Outliner and tiles do).
* Content Browser folders are flat (one level of ODB containers); thumbnails are the existing model renders.
* Light theme is functional but less polished than dark; the Save mode stylesheet still holds its old rules in
  `style.css` (tail) until that agent migrates them to the tokens.
* A Lucide (or similar) icon package was not adopted; icons remain hand-drawn.
