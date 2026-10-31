# Editor workflows: projects, tasks, help, Files tab (2026-10-31)

Problem (user): "it's still not clear how to actually use this editor to build things, campaigns, maps, etc. The Inspect tab
is also not very clear at all." Reference for users: [editor-guide.md](../../reference/editor-guide.md). Tags: **[browser]**
walked in a headless browser against the real PAL ISO (screenshots in `build/scratch/workflows/`, git-ignored), **[test]**
unit tests, **[open]**.

## What was built

* **Mod project** (`editor-core/src/project.ts`, `app/src/editor/project/`). A small JSON document: name, id, author, version,
  description, levels in campaign order (disc levels and copies), the level Play starts in, the task and its checklist ticks.
  No game data. Kept in local storage; "Save project" writes a `.drakan-project.json` (native Save dialog in the desktop
  app), "Open project" reads one. Level edits stay in the per-level autosave (IndexedDB) and the story edits in the story
  session; the project says which to pack.
* **Project dock tab** [browser]. Details form; levels list with up / down / remove / "Start here" / "Open", a per-level
  exit list (destination, "arrives correctly" or the plain-language problem, "Show exit" opens the level and selects the exit
  object so its destination is changed in the Logic panel), "Add level", "Add copy" (source, title), "Order by route";
  a Checks list (also fed into the Validation tab through `addValidationProvider`); export zip, write to mods folder,
  desktop Play controls. One pack holds every project level with changes (as `overrides/`), every copy (as
  `files/ENGLISH/LEVELS/<mangled>/<mangled>.LVL` plus the source level's splat files, `levels = [...]` in the manifest),
  and the story edits when there are any. Copy pack checked structurally [browser]: `mod.toml`, `LEVEL1PY/LEVEL1PY.LVL`,
  `LEVEL1TS.DB`, `LEVEL1TS.TXD` for a copy of Level 1E with one added object.
* **Task start page** [browser]. Five task cards (campaign level, multiplayer map, new level from an existing one, story,
  save) plus the OotF card when `file.importOotf` is registered (it only runs that command); without a disc the card asks
  for the disc first and then continues. Campaign and new-level tasks show a level picker; the others open their panels.
  The old four "capability" cards are gone; a project card and "Just look around" (all levels) remain.
* **Checklist card** [browser] (bottom right on every tab, collapsible, dismissible, task switcher). Steps tick themselves from
  what the editor sees (level open, edits, validation errors, project levels / copies, multiplayer map name / spawns /
  pickups, story edit count, save open / pending changes, exported, played) or by hand where it cannot see (checkbox). The
  next step shows its one sentence, an action button (runs a command) and a "How" link to the guide.
* **Help drawer** [browser]: top bar "Help" button and the palette ("Open the help guides", "Help: <guide>"). Nine guides
  (what the editor does, objects, triggers and exits, campaign, multiplayer map, story, packaging, save, Files), each with a
  numbered path diagram, steps and notes. Small "?" on Place, Details, Validation, Mod, Project, Logic, Multiplayer Map,
  Story, Save and Files panels. Text lives in `editor-core/src/guides.ts`; `docs/reference/editor-guide.md` is generated from it
  and a test fails when they differ. No screenshots are committed (the repository guard allows no binary but the icon, and
  screenshots of levels are game content); the in-app illustration is the path diagram.
* **Files tab** (was Inspect) [browser]. Decision: **keep it, rename it, move it last and say what it is.** It is a useful
  tool for the people who want to know what is in the data (texture sets, string tables, level layout), but it is not a
  step of building anything, so it is the third tab, dimmer, tagged "Advanced", with a header: "Browse the files on your
  disc and in your mod; view raw entries and textures. You do not need this tab to build a mod, and nothing here changes your
  disc." Layout: levels + disc file tree (root listed on open), entry list (kinds named: texture, object list, dependency
  list; offset and compression in the row tooltip), preview with Picture / Text / Hex (default by type: textures draw, `.XDB`
  readable text, else hex), and buttons above it: LVL of a disc level -> "Open this level in the Level tab", `.XDB` -> "Edit
  these strings" (Story > Strings), `.SSD` / `.SDB` -> "Open the Story editor". Hex paging, "decompress" and
  "Show all textures" stay but sit in the preview. Internal ids (`tab-inspect`, `inspect-main`) are kept because the desktop
  menu (`menu.rs`: "&Inspect") and `desktop.ts` use them.
* The app now opens on the Level tab (start page) instead of a hex view.

## Walkthrough as a new user [browser]

Real ISO, 1600x900, Playwright against a production build of the app (the dev server kept reloading while other sessions
edited files). Screenshots `01`..`26` in `build/scratch/workflows/`.

| Workflow | Result |
| --- | --- |
| Start page without a disc, with a disc | Tasks visible in both; disc-less cards say they ask for the disc first (`01`, `02`) |
| Edit a campaign level: pick Level 1A, drag a class into the world | Steps 1, 2 and 4 ticked on their own, step 3 waits for a tick or the Logic action; level joined the project on its first edit (`03`, `05`, `06`) |
| Project panel: details, exits, copy | Exits of every level read after about 4 minutes in this environment (all 14 levels are loaded); then 11 exits for Level 1A, 2 flagged ("Level 3A has no exit leading back to Level 1A" is real data: the transition arrives with no marker); adding a copy of 1E flagged its exit to 1A as having no marker, which is the engine's rule, plus a "no exit leads here" note (`07`..`09`) |
| New level from an existing one | Picker -> copy "Level 1E - Inquisitor's Lair (copy)" created and opened under its own title, Project panel shown (`21`, `22`); export of the copy with one edit gave the 4-file pack above (`23`) |
| Multiplayer map | Task opens the Multiplayer Map tab; "Start new map" ticked open and details; spawns step waits for two spawns (`10`, `11`) |
| Story | Task opens the Story tab (`12`, `26`) |
| Save | Task switches to the Save tab; "Open a save" stays unticked until a save is open (`15`, `24`) |
| Help | Drawer lists guides; a guide shows path diagram, steps, notes (`13`, `14`) |
| Files | Root listed on open; Level 1A folder -> LVL -> entries named -> "Open this level" button; TXD -> texture entry draws in Picture (`16`..`20`) |

Not walked: placing spawns and pickups and the game launch (needs the game and the desktop app), Save mode with a real save
(no save file in the tree), the OotF importer.

## Fixed during the walk

* Start cards collapsed to 24 px (global `button` height): `height: auto` on `.start-task`.
* The exit "Portal" with destination text `portal` raised "destination level portal is not on this disc": it is an in-level
  portal exit, now skipped. Exit problems are warnings only (untouched disc levels have some), never blocking an export.
* "Fix the exits" ticked before the exits were read: now needs the exits loaded (`exitsChecked`).
* Save mode was redesigned by another session while this was built; the checklist reads `#sv-title` and `#sv-pending`.
* `setSelectionRange` on a `<select>` in the Project panel's focus restore threw: guarded.

## Still confusing / open

1. **A copy is not reachable in the campaign.** It appears in the level list; no exit leads to it unless an exit's destination
   string is changed, and that is done per exit class through the Logic panel (the destination string is shared by every
   exit of the class). The Project panel only lists the problem and opens the exit. A "retarget this exit to the copy"
   button needs a per-object destination, which the format does not have.
2. **"Start here" is only the editor's Play button** (`DRAKAN_START_LEVEL`); the game's own New Game is not changed, and
   starting inside a copy was not run. Nothing in the pack format sets the first level.
3. **Copy packs were not loaded in game this session.** The pack layout follows modding.md and the clone test there (splat
   files copied under the copy's `TS` name); the 14-level exit reading and the export were exercised, the game was not.
4. Reading all exits takes minutes with a 1.5 GB ISO behind a browser `File` (the worker loads every level). The Logic panel
   pays the same price. A cached exit table per disc profile would remove it.
5. Level edits live in this browser's autosave, not in the project file: opening a project on another machine brings the
   level list and settings but not the edits (save each level as `.level.json`, or export the pack). A real project folder
   (desktop) needs Tauri commands that another session owns.
6. The multiplayer map is not a project item: its draft lives in the Multiplayer Map tab and exports from there; the
   checklist ticks "validate" and "export" by hand because the panel exposes neither.
7. The Mod tab and the Project tab both export. The Mod tab is the older one-level export; the Project panel is the
   one the guides and the checklist point to. They should be merged.
8. The checklist card and the Help drawer sit over the right-hand panels at small window sizes.
9. `style.css` still holds the old `#inspect-main` grid rules and `menu.rs` still says "&Inspect"; `files.css` overrides the
   first, the second needs a change in the desktop package ("&Files").
10. Pack-level checks that were not written: story pack and level pack touching the same container are merged key by key
    (a clash is reported as a warning, the story file wins).

## Files

New: `editor-core/src/{project,guides}.ts`, `editor-core/test/{project,guides}.test.ts`, `app/src/editor/project/{store,export,panel,checklist,help,install}.ts`,
`project.css`, `app/src/files.css`, `app/test/project-store.test.ts`, `docs/reference/editor-guide.md` (generated).
Changed: `editor-core/src/index.ts`, `app/index.html`, `app/src/main.ts`, `app/src/levelui.ts` (`openLevel(dir, asTitle?)`),
`app/src/editor/startpage.ts`, `editor/README.md`, `docs/README.md`.
