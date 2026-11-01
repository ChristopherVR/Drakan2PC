# Story edits in the project file, desktop menus driven, export responsiveness, Play on a copy, per-exit destinations

Follow-up to `project-folder-and-export.md` and `level-copy-campaign.md`. Screenshots: `build/editor-shots/desktop2/`.

## 1. Story edits in `project.drakan.json`
`ProjectStory` (editor-core `project.ts`): string edits, quest edits, new quests, and per-level-folder dialogue / cut scene
edits (sequences as base64 of their SSD bytes). `StorySession.exportState/importState`, `ContentSession.exportState/importState`,
`StoryEditors.exportProject/importProject`; `serializeProjectFile(p, edits, story)` / `parseProjectFile` (malformed parts are
dropped, an empty story is omitted). Restored edits wait for the disc: `reset(keepEdits)` keeps them when a disc opens after
the project, and building the pack loads the string tables and level folders on demand. A story change saves the project 1.5 s
later like a level edit. Browser: story edits only survive in the downloaded project file (the level edits also live in the
autosave). Unit tests: `app/test/story-content.test.ts` (round trips, reset, malformed input, byte-identical pack entries).
Proven on desktop: the project file held `story.strings = 1`; a fresh WebView profile opened it through File > Open Recent
Project and the Export dialog listed "Story: 1 edit(s), 1 file(s)".

## 2. Native menus and the browser fallback
Desktop debug exe, private WebView2 (CDP port 9381) and a private play folder. The native menu items were activated with
`WM_COMMAND` posts to the menu ids (`menu2.ps1`, ids read with GetMenu/GetSubMenu; not mouse clicks), folder dialogs filled
through UI Automation (`Folder:` edit + Select Folder):
* File > New Project opened the Project tab with the name form; Create project folder... + native dialog made
  `projects/Desk Proof/project.drakan.json`.
* File > Save and a real Ctrl+S key event both rewrote the file (mtime moved, "Project saved" toast).
* File > Open Recent Project (submenu item) and File > Open Project... (native dialog) reopened it: "Opened project (1 level with
  edits restored)". Recents are in the submenu, not the top level (the separator ids sit next to it).
Browser fallback (own Playwright context): see the result line at the end of this note.
Hazard: another agent's app had port 9347; my first scripts talked to their app until I moved to 9381. Use a unique port.

## 3. Export dialog no longer waits for the worker
Cause: the export re-read the open level through the worker (`loadLevel`), queued behind model decoding, 215 s on this loaded
machine. The editor already has that read: `rememberLoadedLevel` seeds the export cache when a level opens. Now 2.4 s with
1010 changes. The dialog also shows what it is doing ("Reading ... from the disc (1 of 2)", elapsed seconds), reuses the
preview's pack on Export and skips levels without edits before touching the disc. `buildProjectPack` takes a progress callback.
Related: the Logic panel's exit picker waited for `exitIndex` of all 14 levels behind the same queue (423 s here while models
loaded). `exitIndex(only)` answers for the open level alone and the picker uses that first.

## 4. Play on a level copy (found a bug)
The toolbar Play button (Mod panel `doPlay`) never used the copy pack: with a copy open it launched the game with
`DRAKAN_START_LEVEL=City Copy` and no pack ("Game started (no edits, ...)"). Fixed: `copyPlayPack` (playtest.ts) is used by
`doPlay`. Desktop proof: Add copy "City Copy" in the Project panel, Open it (590 objects), click Play: status
"Game started (mod 'desk-proof' installed, starting in City Copy)", `mods/desk-proof` held the copy LVL + splat files, `mods.ini`
enabled it, the game log showed `mods: desk-proof: level 'City Copy' registered`, "1 levels", and
`level 'City Copy' loaded (playable)`. **Caveat**: that interactive run then logged a guest `missing-target` JALR and showed the
Load Game menu (a second game from another agent shared that play folder and card, the machine was swamped); I did not get a
clean in-world shot from the editor-launched game. The same pack (copied from that play folder) run through the harness
(`DRAKAN_START_LEVEL=City Copy`, `DRAKAN_MODS`) loaded cleanly: `afterLevelLoad level='City Copy'`, scripted quit, shot
`game-copy-direct-s1.png`.

## 5. Per-exit destination
Exits of a class share one XDB string. The exit picker has "Only this exit (its own string)" (default on): it adds a new
string to the level XDB (`StorySession.addDestinationString`, copy of the shared string with the new destination in every
language slot) and gives the exit object its own `Transition To...` parameter pointing at it. The LVL stores that parameter
like the class default: dword with length 0 and the XDB id in the offset half. `lvl-doc.ts`: `isStringRefField`; the document
keeps that id as a number, `writeParams` writes it (cutting the placeholder text words out of the data area), unchanged exits
stay byte-identical, older saves that stored '' keep the id. Tests in `editor-core/test/lvl-doc.test.ts` and
`app/test/story-content.test.ts`. The pack carries the new entry as `new/LEVEL1ON.XDB/0400_53_1.bin`.
Proof in the game (pack made by the editor UI: duplicate the exit "Level Transition from 1 to 2" of Surdana, retarget the
duplicate to the Inquisitor's Lair with the picker, Export):
| run | result |
| --- | --- |
| `DRAKAN_LEVELCHANGE=99` | exit list: exit 0 (original, string 0x80005) "Level 2 - Shadowmire", exit 1 (duplicate, string 0x80035 = new id 53) |
| fire exit 1 (`pj2-fire1`) | `afterLevelLoad level='Level 1E - Inquisitor's Lair'` (shot `game-perexit-duplicate-to-lair.png`) |
| fire exit 0 (`pj2-fire0`) | `afterLevelLoad level='Level 2 - Shadowmire'`: the shared string is untouched |
The host does not name the new exit (`dest 80035 ''`): the pack's destination hook only reads `overrides/`, not `new/` entries.
Fire it by index.

## Not proven / open
* Editor-launched game on a copy: no clean in-world run (see 4).
* The Logic picker can take minutes while the level's models decode in the worker (the real fix is a worker priority lane).
* Drag-through of the duplicate (Ctrl+D) worked in the browser; a stray key-event loop in my desktop script made 31 extra
  duplicates in the throw-away project.

## Browser fallback result
Own Playwright context on a production build: Project tab > Create a project > Add level > Save downloaded
`browser-proof.drakan-project.json` (name, level list); a project's "Open..." with that file restored name "Browser Proof"
and the level (`b2-opened.png`). With no project open the browser panel only offers Create; Open is on the project card.
Checks: `npx vitest run` 63 files / 579 tests pass; cargo build (debug, custom-protocol) passes; the TypeScript check of
my files is clean, `npm run typecheck` only fails in other agents' files at the time of writing.
