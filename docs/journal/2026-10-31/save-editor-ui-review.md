# Save editor UI review and redesign (AbioticEditor as the reference)

Feedback: the Save mode "looks very poorly vibe-coded"; make it work and look similar-ish to AbioticEditor. This note
records the review (step 1), what was built (step 2) and how it was checked (step 3). Ideas only: no AbioticEditor code
or assets were copied, and nothing in `D:\Development\uesave` was modified.

## 1. AbioticEditor review

Source studied: `src/AbioticEditor.Web.Shared` (Razor components, `wwwroot/parity.css`, `Components/Shared/WorkspaceShell`),
`docs/guide/*.md` and its screenshot set (`docs/public/screenshots/*.png`). It was not run: there is no existing
front-end build to start without installing, and `npm install` inside its folder was off limits, so the look comes from
the CSS and its own screenshots.

Patterns that define it:

- **Frame.** App bar (logo, "SAVE EDITOR" with version, the active save folder in a monospace line, mode and OPEN
  buttons) with a hazard-yellow rule under it; a left sidebar of save files; a main column with a title row; a bottom
  status strip with a green dot and the last message. Two docks can be collapsed and a right "Details" pane explains the
  selection.
- **Save list.** Searchable, sectioned (World story, Players, World regions), each row a name, a coloured type tag and a
  size. The selected row has an orange left bar.
- **Document header.** File title in large type, a type tag and size under it, and RELOAD, REVERT and SAVE on the right.
  Edits stay pending until SAVE; every write keeps a `.bak`; browsers without write access fall back to EXPORT.
- **Tabs.** A strip of raised, equal-height buttons (General, Vitals, Character, Inventory, Skills ...); the active one
  is a solid orange chip. Sub-strips for the world area.
- **Cards.** Dark panels with an orange title, a thin rule, then label / control rows. Numbers are shown in a green
  LCD-style monospace; inputs are recessed dark wells; sliders are orange with the value at the right.
- **Inventory.** The game's own slot layout (equipment, hotbar, backpack) with item pictures, counts as green numerals,
  durability bars, and a searchable item catalog on the right (category chips, pictures). Drag to swap.
- **World lists.** List on the left, details on the right with a picture, a "Show in 3D" button and the explanation of
  what each field does. Search and chips filter every list. Story events grouped by area with a short description and
  an action button.
- **Feedback.** Unsaved chip, SAVE disabled until something changed, a how-it-works line under dangerous controls,
  confidence/warning wording on risky fields.
- **Compare / Transfer** are pages of their own with a from / into header and a table of values with checkboxes.

## 2. What was wrong with the old Drakan Save mode

Screenshots of every tab before the change are in `build/scratch/save-ui/before-*.png`.

| Problem | Where | AbioticEditor does |
| --- | --- | --- |
| Reused the inspector's pale default chrome: system font, grey-white panels, stock blue accent, light theme by OS setting | everywhere | One deliberate dark, warm theme with a single accent |
| Three equal-width columns with a 3D viewport taking the whole right third and a one-line "Pending edits" box under it | layout | Sidebar, content, and a dock whose main job is the edits tray |
| Pending edits were a footnote; SAVE and EXPORT sat in a toolbar next to "no pending edits" text; no unsaved indicator, no revert-all, no reload | header | Title row with RELOAD / REVERT / SAVE and an unsaved chip |
| Character tab was a stack of identical white tiles, then an `EDIT` heading and more boxes with a bare textbox each; confidence badge on every field; no portrait use beyond a small image | Character | Hero card, stat rows with sliders, grouped cards |
| Tabs were small outlined buttons, "Repair" and "Transfer" created from script after the HTML | nav | Large raised tab chips with icons |
| Save list rows were plain text with a tiny thumbnail and no search, no slot labelling, no empty state | sidebar | Searchable rows with tags and an active bar |
| Empty and blocked states were grey sentences (`Open a save to use this tab.`) | all tabs | Explanatory empty states with the next action |
| Inventory tiles were 22px-tall bars once the shell changed, the grid sat in a bare box and notes were walls of text | Inventory | Slot grid with pictures and a details panel |
| Feedback only in the global header line, easy to miss; errors looked like info | status | Status strip plus toast; errors in red |
| No keyboard focus ring, no labels on icon-only controls, no responsive layout below 1000px | a11y | n/a |
| Class names like `.card`, `.field`, `.badge`, `.grid` collided with the shell-wide stylesheets | CSS | n/a |

## 3. What was built (all in `editor/packages/app/src/save/*`, `saveui.ts`)

- `save/shell.ts` (new): builds the whole Save shell inside the empty `#save-main` mount point so `index.html` and
  `style.css` need no edits. App bar, searchable save list, header with Reload / Revert / Export / Save and undo/redo,
  tab strip with icons, panes, right dock (pending-changes tray with Revert all and the Save changes button + `.bak`
  note, then the 3D view), status strip and a toast.
- `save/dom.ts`: inline SVG `icon()`, `button()`, `card()` and `emptyState()` helpers; badge class renamed `sv-badge`.
- `save/save.css`: rewritten, nested under `#save-main.sv` (so legacy rules cannot leak), `--sv-*` tokens for the warm
  dark theme with a `:root[data-theme='light']` variant; fonts, radii, status hues and the 3D axis colours read the
  shared tokens with fallbacks. No dependency on the Level editor's stylesheets beyond that.
- `saveui.ts`: character tab rebuilt (hero card with the save's screenshot as portrait, health slider with number and
  "Heal to full", mana bar, stats list, skills with sliders, position X/Y/Z with "Show in 3D", Advanced collapsed);
  pending-changes tray with per-edit revert and before-to-after values; reload / revert-all; save-list search and
  tags; world tab as list on the left, map and details on the right; compare tab with A/B header and cards; empty
  states everywhere; scroll position kept across redraws.
- `panels.ts`, `inventory.ts`, `journal.ts`: empty states, cards, `sv-` class names (the old generic names were the
  collision risk), transfer as a from / into header with a values card.

Kept: stats, skills, inventory flags and durability, quest done edits, compare, transfer, export, raw and advanced
views, undo/redo, 3D player marker drag, SAVE with `.bak`.

## 4. Verification

Static build served from `build/scratch/save-ui/dist`, driven with Playwright against copies of three saves from
`build/ghost-attack` and the ISO from `build/portable`. Before/after screenshots are in `build/scratch/save-ui/`
(`before-*.png`, `after-*.png`). Edited `stat.gold` 112 to 1234, exported, and
`python scripts/savetool/savetool.py get exported.rsg stat.gold` printed `1234` (the untouched copy prints `112`).
Playwright's Chromium has no File System Access picker, so the in-place Save path (with `.bak`) was not exercised
here; it is unchanged code (`saveWithBackup`) behind the same button.

## Open points

- The vite dev server HMR reloads the page whenever another agent saves a file, so scratch checks used a static build.
- Nested CSS `@keyframes` breaks the production minifier (lightningcss); the keyframes are at top level.
- The Level editor stylesheet adds a global `button { height: var(--h-ctl) }`; Save mode sets its own heights.
- Not done: item catalog panel and drag to swap (the game ignores added items), pictures for story quests, a
  collapsible dock, real Chrome check of the File System Access save.
