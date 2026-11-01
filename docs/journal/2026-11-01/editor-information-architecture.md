# Editor information architecture

The user: "the tabs at the bottom don't make sense in the flow ... Isn't Project supposed to be a global aspect? Why is
Mod a separate tab, Multiplayer Map etc." Later: "the Save tab is meant to be a separate application (save game
editor)" and "some of the UI elements in the Save tab can be reused in the Level".

## Before

One page with three top tabs (Level / Save / Files). Everything else of the Level tab piled into one bottom dock of
11 tabs, which at 1280 px wide collapsed to unlabeled icons:

| Surface | Where it lived |
| --- | --- |
| Content Browser, Output Log, Validation | bottom dock |
| History (undo list) | bottom dock |
| Mod (changes of the open level, export, play steps) | bottom dock |
| Logic graph | bottom dock |
| Multiplayer Map (arena) | bottom dock |
| Menus, Story (quests, dialogue, cut scenes, text), Vendors | bottom dock |
| Project (name, levels, exits, export) | bottom dock, the last tab |
| Save editor | top tab, a different app with its own Drakan skin |
| Disc file browser | top tab "Files" |

The project, which every export, play and checklist works on, was a tab beside the Output Log. The Story editor got
five rows of strings at 720 p.

## After

Modelled on Unreal Engine 5, simpler.

- **Two applications.** The level/mod editor (`index.html`) and the save game editor (`save.html`; on the desktop its
  own window, label `save`, declared in `tauri.conf.json` with `create: false` and opened by `windows.rs`). The save
  editor keeps its Drakan skin, has a Level Editor button back and a Disc button for item pictures and quest text, and
  shares the code packages and the shared components. Tools > Save Editor, the start page "Edit a save" card, a dropped
  `.rsg` and the desktop File menu's Open Save File / Memory Card Folder open it.
- **The project is the global context.** A project chip in the top bar (name; menu with Project Settings, Save, Export,
  Play, Package and Play, New/Open and recent projects on the desktop). Project Settings is its own editor tab
  (Ctrl+,, Edit > Project Settings). The start page shows the project row (Project Settings / Open / New) and recent
  projects. Export and Play stay in the main toolbar.
- **Editors open as their own tabs** in the top bar next to the Level Editor, closable (x or middle click): Story,
  Menus, Vendors, Project Settings, Package and Play (the old Mod tab). Story, Menus, Vendors and Logic can be moved
  between "own tab" and "below the viewport" (Dock in Level Editor / the pop-out button on the docked tab); the choice
  is remembered.
- **Editors that work on what is in view open below the viewport:** Logic (Ctrl+Shift+K), and the Story tools that
  place things in the world when started from + Add (cut scene, dialogue line, quest marker).
- **Per-level settings sit beside Details:** World Settings = the multiplayer arena of the open level.
- **The bottom area keeps only** Content Browser (Ctrl+Space), Output Log and Validation, plus docked editors on demand.
- **History** is Edit > Undo History (Ctrl+Shift+H), a small floating window.
- **Menus:** the browser gets a menu bar (File, Edit, Window, Tools, Help) built from registered commands
  (`editor/menubar.ts`); the desktop native menu (`menu.rs`) has the same structure and forwards `cmd:<id>` items to the
  same commands. Disc Files is Tools > Disc Files, a closable tab.

Where an editor opens is one table, `editor/ia.ts` (`PLACEMENT`); `editor/workspace.ts` places panels registered with
`registerPanel`, so features never touch the layout. Panel ids and `panel.<id>` command ids did not change, so every
caller (checklists, Quick Add, buttons in other panels) still works. Shortcuts marked global (palette, save, export,
Window keys, help, theme) also work in editor tabs; viewport keys (Delete, W/E/R, ...) only in the Level Editor.

## Shared components (reused from the save editor)

`src/shared/components.ts`: `changeRow` / `changeList` (the save editor's pending-changes tray row: what, before ->
after, tag, Revert / Show), `card`, `statTile`. Each takes a class prefix: the save editor passes `sv-edit` /
`sv-card` / `tile`, so its Drakan CSS styles them unchanged; the level editor uses `c-*` styled with the UE5 tokens
(`shared/components.css`). Used in the level editor by Package and Play's change list (tagged Added / Deleted / Changed,
Show selects and frames). Not done: per-change Revert in the level editor (level edits have no per-change inverse
outside the undo stack yet); the save editor's item grid and list rows are still its own.

## Clicks, from the Level Editor with Surdana open

Counted with the scratch harness on a static build (1280x720); keyboard shortcuts are listed where they exist.

| Job | Before | After |
| --- | --- | --- |
| Open project settings | 1 (unlabeled dock icon) | 2 (Edit > Project Settings or project chip), Ctrl+, |
| Story editor | 1 (dock, 5 rows tall at 720 p) | 2 (Window > Story, full window), Ctrl+Shift+Y |
| Logic graph | 1 (dock icon) | 2 (Window > Logic), Ctrl+Shift+K |
| PvP arena | 2 (+ Add > Arena) | 2 (+ Add > Arena, opens World Settings) |
| Vendor / Menus | 2 (+ Add) | 2 (+ Add, own tab) |
| Puzzle / cut scene / dialogue | 2 (+ Add) | 2 (+ Add) |
| Package and play | 1 (Play) | 1 (Play) |
| Undo history | 1 (dock icon) | 2 (Edit > Undo History), Ctrl+Shift+H |
| Disc files | 1 (top tab) | 2 (Tools > Disc Files) |
| Edit a save | 1 (top tab) | 2 (Tools > Save Editor; its own app), 1 from the start page |

Click counts stay the same or gain one for rarely used surfaces; what changed is that every entry is labeled and found
where its kind of thing lives, the bottom strip went from 11 tabs to 3, and editors get the whole window.

Screenshots: `build/editor-shots/ia/before/` and `build/editor-shots/ia/after/<size>-<theme>/`.

## Open items

- The save task's checklist steps ("save open", "save edited") read the save editor's DOM and cannot tick from the
  level editor window now; the save editor has its own flow.
- The desktop save window was built (cargo) but not launched in this round.
