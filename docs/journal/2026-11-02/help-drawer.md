# Help drawer rework and guides on the Pages site

The in-editor Help drawer (`editor/packages/app/src/editor/project/help.ts`, guides in
`editor/packages/editor-core/src/guides.ts`) was checked in a static `vite build` served on a free local port, in a
private Playwright context at 1600x900, without a disc and with the PAL ISO opened through `#iso-input`. Screenshots
were kept under `build/scratch/help-fix/` (not committed).

## Issues found

- List rows overlapped: `style.css` gives every `<button>` `height: var(--h-ctl)` (24 px), so the two-line guide rows
  (`.hp-item`, 51-68 px of content) were clipped to 24 px and drawn on top of each other.
- The "All guides" back button showed on the list itself: `.ui-btn { display: inline-flex }` beat the `hidden`
  attribute. Its chevron pointed right.
- No search; no keyboard path through the list; opening did not move the focus into the drawer and closing did not
  give it back; Esc was handled only by a window listener.
- No link to the published guides, although `docs/reference/editor-guide.md` (generated from the same file) is on the
  site as `guides/editor-tasks.html` with an `<a id>` per guide.
- Two "?" buttons stacked on the right column when World Settings was shown (one for `#ed-details`, one for the
  `.panel.mp` inside it), and a panel moved from its own tab into the bottom dock (Story) kept its own "?" next to the
  dock strip's.
- The flow diagram drew its connector before each step, so a wrapped line began with a dangling dash.
- Stale text: the start guide described the old toolbar and help; objects named no Place panel groups; campaign
  described the pre-redesign Project Settings (no Levels table, Checks, "+ Add level" split button or Map toggle);
  package pointed at a "Mod panel" (now Package and Play); there was no guide for the Menus editor although the start
  page has a "Menu page" template. The keyboard overlay still said "Asset Browser" and "autosaved in this browser".

## Changes

- Drawer: header (back, title, "open the guides on the web", close), a search box over title, summary, flow, steps
  and notes (`searchGuides` in editor-core: every word must match, title hits rank first) with a "n of 11" count,
  rows with an icon, summary, a snippet of the matching step and the step count. Arrow keys / Home / End move through
  the rows, Enter opens, typing on a row continues the search, Esc clears the search and then closes, Backspace in a
  guide goes back to the list. Focus goes to the search box (list) or the summary (guide) and back to the opener on
  close. The left edge resizes the drawer (320-760 px, remembered; double-click resets).
- Guide view: summary, "Show me" (when a tour exists), "Open on the web" (the guide's anchor on the site; in the
  desktop app, which has no external opener, it copies the link and says so), the path, numbered step cards, notes as
  callouts and previous / next guide. Search words stay highlighted in the guide opened from a search.
- "?" buttons: tooltips name the guide; nested hosts show only the innermost visible "?" (`.hp-shadowed`); a panel
  docked into the bottom area drops its own "?"; Vendors and Menus panels got one. Help menu gained "Open the guides on
  the web" (`help.web`).
- Guides: updated for the Place panel, template start page and Home button, Project Settings Levels table / Checks /
  Map, story badges in the viewport, World Settings "Start new map", Package and Play; new `menus` guide. Optional
  `tour?: string` on `Guide`; `GUIDES_WEB_PAGE` / `guideWebUrl(id)` give the published address.
- Tour hook: `editor/packages/app/src/editor/tour/registry.ts` (`registerTourStarter(start, has)`, `hasTour`,
  `startTour`, `onTourRegistry`); no-ops until the tour system registers. Help asks `hasTour(guide.tour ?? guide.id)`
  and redraws when a starter registers. With the tour agent's work in the tree, "Show me" on "Place and edit objects"
  started the "Change a campaign level" walkthrough and closed the drawer.
- Site: `build_pages.py` now checks that every guide id in `guides.ts` is an anchor on `guides/editor-tasks.html` and
  that `GUIDES_WEB_PAGE` matches the base path (`validate_editor_guides`, unit test in `test_build_pages.py`).
  `guides.test.ts` checks ids are anchor-safe, present in the generated page, and that `start` stays first (the site's
  `start_at`).

## Left open

- The desktop shell has no command to open a URL in the system browser, so "Open on the web" copies the link there. A
  small Tauri opener command would make it a real link.
- F1 still opens the keyboard shortcuts (UE5 uses F1 for documentation); left unchanged because other tools bind it.
