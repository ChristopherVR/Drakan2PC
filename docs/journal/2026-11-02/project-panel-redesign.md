# Project Settings panel redesign

The user found the Project Settings panel ("Levels, in campaign order" and the Checks column) looked improvised: loose
form fields for adding levels, a row of text buttons on every level, and a wall of WARNING paragraphs on the right.
The target was UE5's Project Settings / World Outliner / Message Log feel, in plain language.

## What changed

`editor/packages/app/src/editor/project/panel.ts` was rebuilt as three docked panes (header bar, body that scrolls on
its own) in the editor's existing tokens; `project.css` holds the new `pjx-*` rules. Pure helpers moved to the new
`project/checks.ts` (tested in `packages/app/test/project-checks.test.ts`).

- **Mod details**: a property grid (label column, value column): Name, Id (hint or error under the field), Version
  (flags anything that is not dotted numbers), Author, Description. Save / Open / New sit in a small toolbar. The
  project folder is one line with a folder icon, the path (full path in the tooltip), the last save time and a copy
  path button. The desktop app has no "reveal in Explorer" command yet (`src-tauri` has none), so the line offers
  "Copy the folder path" for now. A real "Show in folder" needs a small Tauri command.
- **Levels**: an Outliner-style tree table with the columns #, Level, Type (Disc / Copy), Changes, Exits (count plus
  "N to fix" or a tick), Start (a flag; click it to make Play start there) and row actions on hover or selection
  (Open, Play on desktop, Move up, Move down, Remove), each an icon button with a tooltip. Rows select on click, open on
  double-click, have a context menu with the same actions, and are keyboard driven (arrows, Home/End, Enter opens,
  Right/Left expand and collapse, Alt+Up/Down moves). Rows drag by the # column to reorder (`reorder()` in
  `checks.ts`). A level expands into nested exit rows: status icon, destination, portal / outside project tags, the
  exit name, a short status ("Arrives correctly" or e.g. "No way back from Andrellian Isles", full text in the
  tooltip) and "Change destination" (always visible on a problem row). With no levels the table shows an empty state
  with an "Add a level" button (or "Open disc image..." without a disc).
- **Add level**: a split button. The main part opens a searchable list of the disc levels; click one to add it (the
  list stays open and marks it "In project"), or its copy button to add a copy, which asks for the title (suggested by
  `suggestCopyTitle`). The arrow part offers "Add a level from the disc..." and "Add a copy of a level...". The
  `project.addCopy` command opens the list in copy mode.
- **Checks**: a message log. Groups: "Mod settings" first, then each level in campaign order, each with a count badge
  coloured by its worst severity, collapsible. Each message is one line with a severity icon; the chevron shows the
  long explanation with "Show in the list", "Change destination" or "Open the level". Clicking a message selects the
  level or exit in the table (switching from Map to List, expanding the level, flashing the row) or focuses the Id /
  Name / Version field for project messages. The header carries error / warning / note counts that double as filters
  (remembered in local storage). Short wording is in `describeIssue()`: for example the old
  `Surdana Copy: exit "Level Transition from 1 to 3" to Level 3A - Andrellian Isles: "Level 3A - Andrellian Isles" has no exit leading back to "Surdana Copy", so the player arrives with no marker (and falls).`
  reads "No way back from Andrellian Isles" under the Surdana Copy group.
- **Export and play**: the Export button with the reason it is disabled ("Fix 1 error first"), the play controls on
  desktop, the zip steps in a browser.

Kept: every id the harnesses and the Export dialog use (`pj-name`, `pj-id`, `pj-save`, `pj-open`, `pj-new`,
`pj-where`, `pj-view-list`, `pj-view-map`, `pj-route`, `pj-add`, `pj-copy`, `pj-copy-title`, `pj-export`,
`pj-new-name`, `pj-create`, `pj-open-start`), the List | Map toggle with the campaign map unchanged, the shared
`.pj-f` / `.pj-row2` / `.pj-note` / `.pj-report` rules of the Export dialog. Gone: `pj-add-sel` and `pj-copy-sel` (the
two selects; nothing in the repo used them). New ids: `pj-add-more`, `pj-add-empty`, `pj-add-search`, `pj-levels`,
`pj-checks`, `pj-filter-error|warning|info`, `pj-version`, `pj-author`, `pj-desc`, `pj-folder`, `pj-copy-path`.

## Verification

Static `vite build` served on a private port, Playwright with a private context, the real ISO fed to `#iso-input`:
new project, four disc levels plus a copy of Level 1A added through the new menu, exits read, exit rows expanded, a
message expanded and clicked, the warning filter toggled, Map view shown. Before/after shots are under
`build/scratch/project-ui/shots/` (`before-1500-*`, `after-1500-*`); the script is `build/scratch/project-ui/shots.py`.

## Follow-ups

- **Show in folder.** The desktop shell has a `folder_reveal(path)` command (`src-tauri/src/commands.rs`, registered in
  `lib.rs`, `build.rs` and `capabilities/default.json`; `permissions/autogenerated/folder_reveal.toml` is generated by
  the build). It opens Explorer (`open` on macOS, `xdg-open` elsewhere) only on a folder the shell already knows: a
  project folder opened or created this session, the game folder or the mods folder, or a folder inside one. The check
  is `paths::reveal_target`: both sides are canonicalised (so `..`, links and case cannot fake it), the target must be
  an existing folder, and the `\\?\` prefix is dropped before Explorer sees it. Cargo tests `reveal_only_known_folders`
  (sibling, parent, `..` escape, file, missing folder, a look-alike `My project2`, no roots) and `plain_paths`.
  `platform.ts` exposes it as `revealFolder()`; the project folder line has a "Show in folder" button (`pj-reveal`)
  under the path, with copy path (`pj-copy-path`) kept as the secondary icon. The browser build has no folder line, so
  nothing changes there.
- **Id stopped following the Name.** The Name handler compared the Id against the slug of `p.name`, where `p` was the
  project captured when the pane was drawn. The pane only re-renders 300 ms after an edit, so from the second
  keystroke the captured name was stale, the Id no longer matched its slug and was treated as hand-edited (typing a
  new name left the Id at "t"). Now `renameProject()` in `checks.ts` works on the latest state inside
  `projects.update`: the Id follows while it is empty or equals the slug of the current name, and stays once the user
  typed their own. The Id field also updates immediately while typing. Tests in `project-checks.test.ts`.
- Verified with a static `vite build` and private Playwright contexts (`build/scratch/followup-project/shots.py`;
  `desktop` mode stubs `__TAURI_INTERNALS__` to show the desktop folder line and record the `folder_reveal` call), and
  in the debug desktop app over CDP (`desktop_check.py`: refused before the project was opened, refused for
  `C:\Windows` and a `..` escape, accepted for the opened project folder, and the Explorer window that opened was
  closed again; `settings.json` restored).
