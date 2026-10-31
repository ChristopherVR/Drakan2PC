# One Export, project folders on desktop, checklist fix

## What changed
- **One Export.** The Mod tab's own export (single level, zip / mods folder) is gone. Ctrl+E, the top-bar Export button,
  File > Export, the palette ("Export mod...") and the Project tab all open one dialog (`editor/project/exportdialog.ts`)
  that builds the pack from the project (`buildProjectPack`: every changed project level, copies, story edits). It shows
  the pack contents and warnings, edits name/id/version/author, and writes to: the project folder's `mod/<id>.zip`
  (desktop, project on disk), a Save-as dialog (desktop, no folder) or a download (browser). One checkbox also installs
  into the game's mods folder (ticked by default on desktop). The open edited level joins the project automatically.
  The Mod tab keeps the change list, status and play steps and has an "Export..." button that opens the same dialog.
  Removed: `project.export` command, `createModPanel` form/`exportMod`/`suggest`/`values`/`restore`, the Project tab's two
  output buttons.
- **Project folder (desktop).** `project.drakan.json` (project fields + `edits`: level title -> autosave record) inside
  a folder chosen with native dialogs. Rust commands `project_new/open/open_path/write/file_write` (granted per
  session, recents in settings `recentProjects`, atomic write with `.bak`). File menu: New Project, Open Project, Open
  Recent Project, Save (Ctrl+S saves in place when a folder is open). Saves in place automatically 1.5 s after a project
  or level-autosave change; the last folder reopens at start. Level edits are restored by writing them back into the
  editor's autosave and auto-restoring when the level opens (no banner). Browser: same file as download/upload, edits included.
- **Checklist card** no longer covers panels below 1100x760: it is a small pill until clicked.
- Menu "&Inspect" renamed "&Files" (the tab is called Files).

## Proof (desktop debug exe, private WebView2 + CDP, screenshots in build/scratch/pj/shots)
Created "Proof Mod" through the native folder dialog; opened Level 1A, deleted 3 objects, added the level to the project:
`project.drakan.json` held the edit (4672 objects). Deleted the IndexedDB autosave and the cached project, restarted: the
project reopened from the folder, Level 1A opened with 4672 objects and no banner. Export wrote `mod/proof-mod.zip`
(mod.toml + overrides/LEVEL1NA.LVL/0020_0_0.bin). Checklist verified at 1000x640 (pill, expanded) and 1600x900.

## Limits
Story edits are not stored in the project file yet (they still export from the story session). Autosave records are
per level title, so switching projects replaces the autosave of the new project's levels. The export dialog waits for the
worker (can take about a minute right after opening a level while models decode). Menu-bar clicks and the recent-project
menu items were not driven natively; the recents path is exercised by the start-up reopen.
