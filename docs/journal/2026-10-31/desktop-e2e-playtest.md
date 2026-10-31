# Desktop app end to end, and the Play test

Private play folder `build/scratch/desktop2/play` (exe + DLLs + ini copied from `dist/DrakanPC`, set as the game folder
through the app's settings file; the user's `%APPDATA%/dev.drakan.editor/settings.json` was backed up and restored).
The debug build of the desktop app was driven over WebView2 remote debugging (CDP, `build/scratch/desktop2/drive.mjs`),
screenshots and game logs are in `build/scratch/desktop2/` (git-ignored).

## Verified in the desktop app
- Last disc reopens at start (range reads through the asset protocol), Level tab opens Level 1A, edit in the Details
  panel (Axe #6568 X 738603 -> 740000), Mod tab lists "Moved Axe #6568", autosave restore prompt appears on reopen.
- **Play wrote the mod and the game loaded it**: `play/mods/editor.level1na-edit/` + `mods.ini` entry, game log
  `[mods] loaded: Level 1A - Surdana edit 1.0.0 - 1 entries`, and the world dump (`DRAKAN_WORLD=2`,
  `DRAKAN_WORLD_DUMP_FRAME=900`, set in the editor's environment, the game inherits it) shows the Axe (resId 0x802d4) at
  x = 740000.0 (`game-run1.log`, `world-run1.json`).
- Host from the app (mode Host deathmatch): game log `[netplay] DRAKAN_NET_HOST: host on UDP 27960`, the host client
  joined; a second `drakan.exe` started by hand with `DRAKAN_NET_CONNECT` joined as player 2 (`game-host-join.log`). A
  second Play click answers "The game is already running. Close it first." (one game per app).
- **Terrain failed to load in the desktop app** ("PS2 terrain data section not found"). Cause: Tauri's asset protocol
  answers a Range request with at most about 1 MiB, the 1.19 MB layer table of Level 1A arrived truncated and
  `remoteBlob` (`worker.ts`) took the short body as the whole answer. Fixed: it now loops until the requested length is
  assembled. Level 1A now shows "Terrain: 483 layers, 374 meshes". (Any read over 1 MiB was affected, not just terrain.)

## Play test (new)
Right-click in the viewport or an Outliner row, the ▾ button beside Play, command palette, F5 (desktop only).
- **Play from here / from camera / from this object**: writes the author's edits (if any) as the temporary pack
  `editor.playtest`, starts the level with `DRAKAN_START_LEVEL` and `DRAKAN_PLAYTEST_POS="x y z yaw"`. Moving the Rynn
  object in the level does NOT move the start (tested: the dump shows the same start), so the runtime moves the player:
  `ps2recomp/ps2xRuntime/src/lib/drakan_playtest.{h,cpp}` (called from `drakan_input_glue.cpp` after the arena hook): 240
  boundaries after the player appears, writes `player+0x14` / rotation, files the actor again (`drakan_ghost::refileActor`),
  repeats until it stuck, then holds the position for 240 boundaries so the terrain under the player streams in (without
  the hold she fell through before it was resident). Logs `[playtest] ...`.
  Proof: click on terrain -> `[playtest] armed: target 489472 172199 253320`; from the object #?: target 564434 180313 534429,
  `placed after 1 try`, `+120..+480 boundaries` player at 564796 174071 536449 (stable), dump frame 900 shows
  564786 174197 536302. A target that is not walkable (the editor's ground pick at 489472 253320 on the first try) is
  undone by the engine: the player ends up at the start again; pick ground inside the level's playable area.
- **Play this cut scene**: copy of the document with the Stomp Player's `Initial State` set to On, same pack. The menu
  entry and pack are verified; **the cut scene itself was not seen starting** (Stomp #7809: the screenshots at frames 11500
  and 12500 show ordinary play). Still open.
- **Play test arena (host / host + second player)**: the entries exist and are greyed ("Open a multiplayer map first")
  until the multiplayer map editor calls `setArenaPlayProvider` (`playtest.ts`); not wired to its pack yet, not run. The shell
  command `game_launch` gained `extra` (second instance beside the running one) for this; not exercised from the UI.
- In the browser build the entries are shown disabled with "Available in the desktop app".
- After the game exits the editor sets `editor.playtest = off` in `mods.ini` (checked), so a plain Play is unaffected.

## API added
`registry.ts`: `ContextItem.unavailable(t)` (reason string greys the entry, shown as its hint; `objectmenu.ts` maps it).
`platform.launchGame(env, extra = false)`; `modio.playInGame(..., extraEnv)`; Rust `game_launch(env, extra)`,
allow-list gained `DRAKAN_PLAYTEST_POS`. `playtest.ts` (pure: `yawFromDirection`, `withCutScene`, `copyDocument`; flow:
`playFromHere`, `playCutScene`, `playArena`, `setArenaPlayProvider`), `editor/playtestui.ts` (`installPlayTest`).

## Not done
Native dialogs, window-state persistence (the app was always stopped by PID, which skips the plugin's save), the save
editor items (in-place save and `.bak` in the desktop app and Chrome, tray, collapsible dock, quest pictures, item
tooltips, journal seen bits) and the add-item investigation were not started.
