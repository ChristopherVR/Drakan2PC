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

## Play test (new, second round)
Right-click in the viewport or an Outliner row, the drop-down beside Play, palette commands, F5 (desktop only).
- **Play from here / from camera / from this object**: writes the author's edits (if any) as the temporary pack
  `editor.playtest`, starts the level with `DRAKAN_START_LEVEL` and `DRAKAN_PLAYTEST_POS="x y z yaw"`. Moving the Rynn
  object in the level does NOT move the start, so the runtime moves the player: `ps2recomp/ps2xRuntime/src/lib/drakan_playtest.{h,cpp}`
  (called from `drakan_input_glue.cpp` after the arena hook): 240 boundaries after the player appears it writes
  `player+0x14` and the rotation matrix, files the actor again (`drakan_ghost::refileActor`), repeats until it stuck, then holds
  the position and heading for 240 boundaries (without the hold she fell through terrain that was not resident yet and the
  engine put her back at the start), and logs `[playtest] ...` (position and yaw every 120 boundaries afterwards).
  Proof (game log + the game's own `GSR_WINSHOT` screenshot, `playhere-yaw-final.jpg`, `game-playhere-yaw.log`): a viewport
  click gave `target 460903 179256 610505 yaw set 1.571`, `placed after 1 try (0 units)`, and for the next 1800 boundaries
  the player stays at `460903 179440 610505` with `yaw 1.571` read back from the rotation matrix (`atan2(m[0][2], m[0][0])`),
  Rynn stands in a narrow passage far from the start. The camera heading is the game's own; only the actor's yaw is proven.
- **Bad picks are refused in the editor** (`standableProblem`): no terrain under the point, or no level object within 6000
  units of it ("That spot is far from everything in the level ..."). The game undoes a teleport onto ground it has no floor
  for (a first pick at 489472 253320 ended with the player at the start again). Verified: the camera-centre pick over the
  void is refused with that message. Object picks (Play from this object) are not checked.
- **Play this cut scene**: copy of the document plus the "cutscene-trigger" template (10 x 8 x 10 cell volume, message Play
  Sequence, Detect Both, once) around the Rynn object, linked to the chosen Stomp Player, and the player is put there
  (`DRAKAN_PLAYTEST_POS`). Findings: setting `Initial State` to On starts nothing; a Timer (Run Instantly, Play Sequence)
  linked to the Stomp Player is never instantiated by the game (the dump shows 111 timers of the editor's 175, none ours);
  a small 6 x 4 x 6 volume with the player teleported into it did not fire; the 10 x 8 x 10 volume did: in the control run
  (no trigger) Rynn stands alive at the same spot for 1800+ boundaries, with the trigger she is pulled into the scene's pose
  and ends dead on the ground by frame 6100 (`cutscene-proof-contact-sheet.jpg`, frames 5200 to 6700, the orc ambush of
  Surdana's #7809 presumably kills the 24 HP the new game starts with, not seen; in one run the game was back at the title by frame 11500).
  Not proven: a frame of the sequence itself (the camera cut is not in the captures).
- **Play test arena (host / host + second player)**: `editor/arena/install.ts` registers `setArenaPlayProvider` (pack
  built from the open map without a preview picture, mode dm/tdm, refused with the first validation error). Run from the app
  on an authored map "Test Meadow" (Surdana, 8 suggested spawns, `mp-test-meadow` pack): the pack was written and enabled, the
  host started with `DRAKAN_MP_MAP=test-meadow`, 15 s later the app started a second instance with `DRAKAN_NET_CONNECT`
  (`game_launch(extra = true)`): log `arena world mode on: Test Meadow`, `player 2 'Tester 2' joined`, `phase -> 2 (match level
  'test-meadow')`, `player placed at spawn 1: 224186 159616 339675` (`game-arena-second-instance.log`; both instances share one
  `drakan.log`, so only the last writer survives). The 15 s wait is a fixed delay.
- In the browser build the entries are shown disabled with "Available in the desktop app".
- After the game exits the editor sets `editor.playtest = off` in `mods.ini` (checked).

## Desktop shell checks (second round)
- **Native dialog automated**: `invoke('iso_pick')` opens the Windows "Open your Drakan disc image" dialog; UI Automation set the
  file name and invoked Open, the command returned the `IsoInfo` (3453943808 bytes).
- **Window state**: `CloseMainWindow()` (a graceful close) writes `%APPDATA%/dev.drakan.editor/.window-state.json`; moved to
  120,70 and sized 1333x811, the next launch opened at 120,70 with width 1333 and height 781 (30 px shorter: the menu bar is
  counted differently when the size is restored). Killing the process by PID writes nothing, which is why the first round saw no file.
- **Hazards found**: other agents' desktop apps share `%APPDATA%/dev.drakan.editor/settings.json` and the WebView2 profile.
  A second app with `--remote-debugging-port` fails to start while another uses the default profile (HRESULT 0x8007139F): set
  `WEBVIEW2_USER_DATA_FOLDER`. One agent's save of its settings replaced the private `gameDir` and one of my Play clicks wrote
  `mods/editor.playtest` and ran `dist/DrakanPC/drakan.exe` (cleaned: the `mods` folder and my world dump were removed from
  `dist/DrakanPC`). Check `settings_get().gameDir` before every Play (`chk.mjs`).

## API added
`registry.ts`: `ContextItem.unavailable(t)` (reason string greys the entry, shown as its hint; `objectmenu.ts` maps it).
`platform.launchGame(env, extra = false)`; `modio.playInGame(..., extraEnv)`; Rust `game_launch(env, extra)`,
allow-list gained `DRAKAN_PLAYTEST_POS`. `playtest.ts` (pure: `yawFromDirection`, `withCutScene`, `copyDocument`; flow:
`playFromHere`, `playCutScene`, `playArena`, `setArenaPlayProvider`), `editor/playtestui.ts` (`installPlayTest`).

## Not done
The save editor items (a separate agent has them) and the add-item investigation. A frame of the cut scene sequence itself.
The arena play test was run with both instances in one folder (shared log); a pickup-less map only.
