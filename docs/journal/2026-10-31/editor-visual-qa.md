# Editor visual QA and one control family (2026-10-31)

Feedback: "quite a few minor styling issues and obvious AI-generated styling tells. Dropdowns are notorious for styling
issues." Target: the Unreal Engine 5 editor family for Level mode, AbioticEditor for Save mode. Restrained, dense,
consistent.

Method: static builds of the editor (`vite build` into `build/scratch/visual-qa/dist-before` and `dist-after`), driven by
Playwright (Chromium, 1600x900, plus one 1280x720 shot) with the PAL ISO copy from `build/portable/...` and the save
`build/qs/pw/a.rsg`. Scripts: `build/scratch/visual-qa/shoot.py` (every mode, tab, dock panel, menu, picker, both
themes) and `kbd.py` (dropdown keyboard and positioning checks). Shots are in `build/scratch/visual-qa/before/` and
`after/` with the same file names (gitignored).

Builds on `editor-ui-review.md` (tokens, components) and `save-editor-ui-review.md` (Save mode).

## 1. Audit (before)

| # | Problem | Where (before shot) | AI tell? |
| --- | --- | --- | --- |
| 1 | Native `<select>` everywhere (about 30 call sites). The open list is the light Windows popup over a dark UI, the closed box has the OS chevron, 24 px tall next to 26-28 px tool buttons. | `05-level-picker-open`, `04-level-open` (vp bar 256 / 15 degrees), `20-bottom-05-logic`, `52-save-04-transfer` | native next to custom |
| 2 | Arena "Terrain from" select clips its text vertically (24 px box with 3 px padding). | `20-bottom-06-multiplayer-map` | misaligned baseline |
| 3 | Story Strings toolbar starts with an empty, unlabelled select. | `20-bottom-07-story` | empty state |
| 4 | Native checkboxes and radios (light blue OS rendering) in the Show menu, Arena modes, Story filters, Save World filter, Details booleans. | `10-menu-02-show`, `22-arena-started`, `52-save-03-world` | native next to custom |
| 5 | Native range sliders (camera speed, Content Browser size) in OS blue. | `04-level-open` | native next to custom |
| 6 | Pills: search fields 12 px round while every other field is 3 px; chips 11 px; count badges 8 px; arena tags 999 px; Save confidence badges 9 px and chips 12 px. | `04-level-open` (Outliner, Place search), `23-arena-03-modes`, `52-save-00-character` | rounded pill everything |
| 7 | Tracked-out ALL-CAPS labels: arena section heads (SPAWN POINTS, MAP, RULES, BOUNDS, GAME MODES, WHAT THE GAME RUNS TODAY, MOD PACK), Mod status strip (1 EDITS, 2 MOD CONTENTS ...), story group headings, shortcut sheet columns, Save World sub-headings. | `22-arena-started`, `23-arena-01-place`, `20-bottom-04-mod`, `32-shortcuts` | caps eyebrow labels |
| 8 | Radii: 2, 3, 4, 5, 6, 8, 9, 10, 12 px and 999 px across the Level stylesheets. Font sizes: 42 raw pixel sizes (10, 10.5, 11, 11.5, 12, 12.5, 13 ...) beside the token scale. | all | inconsistent radii and type |
| 9 | Double outlines: three floating panels had a 1 px border plus the popup shadow, which already draws a 1 px ring. | ref picker, help card | double borders |
| 10 | Save mode: "high" confidence pill on every stat (noise; only doubtful fields need one), 2 px hazard-orange rule under the Save app bar, orange filled brand tile, empty-state icons on tinted discs, gradient card backgrounds (hero card, inventory slots), glow ring on selected slots, drop shadows under item art. | `52-save-00-character`, `52-save-01-inventory`, `52-save-03-world` | gradients, glows, icon discs |
| 11 | Shortcut sheet: key column is a min-width flex item, so descriptions start at different x positions and wrap. | `32-shortcuts` | misaligned grid |
| 12 | Place kind chips and Content Browser chips cut through a word at the panel edge ("Usat", "I"). | `04-level-open` | clipping |
| 13 | Truncated placeholder copy: "Search by class, id or kinc", "Search details: position, scale, sound, link..."; the empty Details state repeats a viewport tip. | `04-level-open`, `06-details` | over-explaining copy |
| 14 | Logic graph canvas reads undefined tokens (`--bg-0`, `--bg-2`, `--fg-dim`) and hard-coded hex, so it stays near-black in the light theme. | `42-light-bottom-05` | light theme |
| 15 | Angle step select shows "1..." once styled: width of a custom trigger follows its current label, not its longest option. | found while fixing #1 | jumping widths |
| 16 | Toolbar heights: tools 28 px, selects 24 px, viewport tools 26 px in the same strips. | `04-level-open` | inconsistent heights |
| 17 | Disabled context-menu items repeat "Available in the desktop app" three times. | `08-context-menu` | filler |
| 18 | Mod and Logic panels carry long instructional paragraphs beside the controls. | `20-bottom-04-mod`, `20-bottom-05-logic` | over-explaining |
| 19 | Light theme: Save mode's warm light palette sits under the cool grey global app bar. | `56-save-light-00` | two palettes |
| 20 | At 1280 px the two viewport overlay toolbars overlap: the "stack below 980 px" container rule came before the base `.vp-bar { top }` rule, so it never applied. | `45-narrow-1280` | collision |

## 2. One control family

All in `editor/packages/app/src/components.css` (look) and `editor/dropdown.ts` / `editor/ui.ts` (behaviour). Tokens in
`tokens.css`.

* **Dropdown** (`editor/dropdown.ts`). `installControls()` (called once in `main.ts`) upgrades every `<select>` in the
  document, now and whenever one is added later, to a `button.ui-dd[role=combobox]` plus a `div.ui-dd-pop >
  [role=listbox]`. The native select stays the data model, so no panel had to change its logic: `value`,
  `selectedIndex`, `option.selected`, `options` mutations, `disabled`, `hidden`, `title`, `aria-label`, `aria-invalid`
  and class changes are mirrored; picking an option sets the native value and fires `input` + `change`. The native
  element is visually hidden but kept in the layout tree (`.ui-dd-native`), so harnesses that set `#ed-level` or use
  Playwright `selectOption` still work.
  * Keyboard: closed, Enter / Space / F4 / arrows open and typing opens on the first match; open, arrows, Home / End,
    PageUp / PageDown, type-ahead, Enter picks, Esc closes (focus returns), Tab closes.
  * Popup: `position: fixed` on `document.body` (or the nearest `[data-dd-host]`, used by Save mode to keep its palette),
    at least as wide as the trigger, below it or above when there is more room there, max 360 px with scrolling,
    follows the trigger when a dock or panel scrolls, closes when the trigger scrolls out of view, on resize, blur and
    outside click. Option groups become headings; disabled options are skipped.
  * Search: lists over 12 options get a filter box (`data-search="on|off"` overrides).
  * Width: the trigger's minimum width is its longest option (canvas text measure), capped at 240 px, so it does not
    jump when the value changes; form cells (`.mp-text`, `.rp-scope`, `.ifield`, `.ui-prop`) may shrink below it.
  * Opt out with `data-native`; `multiple` / `size > 1` selects stay native. `data-placeholder` is shown when nothing is
    selected; a select with no options shows its `aria-label` in the faint placeholder colour.
  * `selectField(options, value, onChange, attrs)` builds one for new code; `refreshSelect`, `dropdownTrigger`,
    `closeDropdown` are exported.
* **Inputs**: text, search and number share one well (height `--h-ctl`, `--r-sm`, `--border-strong`, accent border on
  focus, red on `aria-invalid`); number spin arrows removed; search clear button drawn with a mask; search fields have
  the same corner radius as every other field.
* **Checkbox / radio**: drawn with `appearance: none` (14 px box, accent fill, masked tick; indeterminate bar), so both
  themes and Save mode match. `.ui-check` / `.check` is the label + box row.
* **Range**: 4 px track, 10 x 14 px square thumb, filled part in the accent (`--fill`, kept current by
  `installControls()` on input, insert and hover).
* **Segmented control**: `.ui-seg > .ui-seg-btn` (the Level shell's `.seg / .seg-btn` are the same rule now; the copy in
  `editor.css` was removed).
* **Switch, menus, tooltips, chips, badges** keep their components; chips and badges lost their pill radius, menus and
  dropdown rows share the row height and accent highlight.
* Tokens: radius scale is now 2 / 3 / 4 / 6 (`--r-xs .. --r-lg`) and every raw radius in the Level, story and arena
  stylesheets maps onto it; every raw font size between 9 and 15 px maps onto `--fs-2xs .. --fs-lg`; new `--h-tool`
  (26 px) is the one height for tool buttons and dropdowns inside toolbars and viewport bars; `--mask-check`,
  `--mask-x` hold the control glyphs.
* Save mode re-maps the shared tokens (`--surface-*`, `--field`, `--fg*`, `--accent*`, `--border*`) to its `--sv-*`
  palette at `#save-main.sv`, so every shared control inside it takes the warm palette; its dropdown trigger is 32 px
  like its other controls.

`kbd.py` against the after build: all 24 checks pass (open, move, commit, single change event, focus return, label
sync, Home / End, Esc without change, type-ahead, programmatic `value` and `option.selected` repaint, disabled mirror,
search on long lists, flip above at the bottom edge, filter and pick, follow a scrolled container, close when scrolled
away, trigger removed with its select).

## 3. Fixes to the audit list

| # | Fix |
| --- | --- |
| 1-3, 15, 16 | Dropdown component above; arena select no longer clips; empty selects show their name; toolbar tools and dropdowns share `--h-tool`. |
| 4, 5 | Drawn checkbox, radio and range. |
| 6 | Search fields, chips, badges, arena tags, Save badges and chips use `--r-sm`. |
| 7 | All `text-transform: uppercase` + tracking removed from `style.css`, `editor.css`, `story.css`, `arena.css` (sentence-case, semi-bold instead). |
| 8 | Radius and type scales above. |
| 9 | Border removed where the popup shadow already draws the ring. |
| 10 | Save: confidence badge only for doubtful fields (`saveui.ts confidenceBadge`), 1 px app-bar seam, quiet brand tile, plain empty-state glyphs, flat card and slot fills, no glow or art drop shadow, smaller toast shadow. |
| 11 | Shortcut rows are a two-column grid (136 px keys). |
| 12 | Place chips wrap; Content Browser chips fade at the edge instead of being cut. |
| 13 | Placeholders "Search objects", "Search classes", "Search details" (full text kept as `aria-label`); Details empty state is one sentence; Outliner empty state is "No level open / Pick a level to list its objects." |
| 14 | Logic graph styles read `--surface-*`, `--fg*`, `--border`, `--warn`. |
| 20 | Container rule moved after the base rule; the transform bar now sits below the display bar under 980 px. |

Before / after pairs (same name in `before/` and `after/`):

* Dropdowns: `05-level-picker-open`, `20-bottom-05-logic`, `35-logic-dropdown`, `36-grid-dropdown`,
  `20-bottom-06-multiplayer-map`, `52-save-04-transfer`, `56-save-light-04`; after only: `37-dropdown-search-flip`.
* Checkboxes, radios, sliders: `10-menu-02-show`, `10-menu-03-display`, `22-arena-started`, `23-arena-00-map`,
  `52-save-03-world`.
* Pills, caps, type: `04-level-open`, `06-details`, `07-details-scrolled`, `23-arena-01-place`, `23-arena-03-modes`,
  `20-bottom-04-mod`, `32-shortcuts`, `21-story-*`.
* Save: `50-save-empty`, `51-save-open`, `52-save-00 .. 06`, `55-save-light`, `56-save-light-00 .. 06`.
* Light theme: `40-light-level`, `41-light-context`, `42-light-bottom-00 .. 07` (05 is the logic canvas).
* Rest (unchanged layouts, token effects only): `01`, `02`, `03`, `08`, `10-menu-*`, `11-tooltip`, `20-bottom-*`,
  `30`, `31`, `33-ref-picker`, `45-narrow-1280`.

## 4. Still open

* Copy in panels owned by other agents was left alone: the Logic panel's side help paragraph, the Mod panel's play
  steps, the three "Available in the desktop app" hints in the object menu (#17, #18), the arena "What the game runs
  today" notes.
* "Add setup..." in the Logic toolbar is a select used as an action menu; it works with the dropdown, but a menu
  button would be the right control (logic agent).
* Save mode light theme still stacks the cool global app bar over its warm palette (#19); Save keeps its own 32 px
  control scale by design (AbioticEditor density), Level mode uses 24 / 26 px.
* The Place kind chips now wrap to two or three rows in a narrow panel; a filter dropdown would be denser.
* Raw pixel paddings and margins remain in `editor.css` (only radii and font sizes were mapped to tokens).
* No unit test for the dropdown: vitest runs in the `node` environment without a DOM; `kbd.py` is the check.
