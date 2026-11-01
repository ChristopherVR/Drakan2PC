# Results buttons, custom game modes in the server, dedicated server console (2026-11-01)

Reference text: `docs/reference/netcode.md` "Results screen, custom modes, dedicated server" and "Custom game modes". Tags: **[test]**
unit tests (`net_results_modes_test`, `gameui_multiplayer_test`, `net_protocol_test`), **[ded]** `dedicated_server_check.py`,
**[game]** two real instances (`scripts/harness/mp_results_check.py --port 27600`, output `build/harness/mp_results*/`).

## What was built
* Protocol addenda (no version bump, trailing optional fields): `ResultsAction = 30`, `PhaseChange.byHost`, `Status.host`,
  `Score.score / teamScore[4]`, `MatchConfig.modeId`, `PlayerJoin.team` 0..3. `DRAKAN_MP_PORT` moves the whole rig (server port and
  the LAN sweep) so rigs can run side by side; `net::basePort()`.
* Server: results choices (host only), `returnToLobby`, `advanceMap` over `ServerOptions::rotation` with `modeFitsArena`, custom mode
  scoring / limits on points, 2..4 teams (`fixTeams`), friendly fire, capture and hold objectives on arena markers.
* Client: `hostPlayer()`, `teamScore()`, `teamCount()`, `sendResultsAction()`; scoreboard sorted and decided on points.
* Menu: `RowId::HostMode` (Game Mode row after Game Type), `ModeEntry`, `LevelEntry::modeIds`, `HostForm::modeId`; lobby shows the
  mode name and N team columns. Results screen: `ResultsMenu` + `resultsButtonRects`, SCORE column and 3..4 team cards in `drawHud`.
* Game glue: `mpResultsUp()` holds the guest input while the results are up, the buttons call `drakan_netplay::resultsAction` /
  `leave`; `autoTitleTick` (EE) leaves the match level for the title (START, modern pause Quit to Title) after a `byHost` lobby
  change or Leave.
* `drakan_server`: stdin console (`status` with ping, `kick`, `start`, `end`, `quit`), countdown lines, `--game-mode`,
  `--start-countdown`, `--results-sec`, `--host-kick`, `--status-sec`. Test client `drakan_netbot`.

## Findings
* Script cursor coordinates are PS2 screen units (640 x 512), not window pixels (`cursor 216 124` = window pixel 432, 175 at 1280x720).
* A title / lobby left alone for ~90 s starts the credits (attract mode): long waits in a harness need idle taps.
* Boot times vary from 15 s to over 60 s on a busy PC: B must be started after A listens, and A's Start Match must wait for B.
* `DRAKAN_NO_ACTIVATE=1` windows never reach "free gameplay" (`sync play`); the results harness runs with focus (`0`).
* `build/final-game` deploy to `dist/DrakanPC` fails while another agent runs the game from there (avcodec DLL in use); the
  harness copies the fresh exe from `build/final-game/bin` anyway.

## Verified / not
* [test] `net_results_modes_test` (new), `net_protocol_test`, `net_match_test`, `net_lobby_test`, `net_coop_test`, `net_modes_test`,
  `net_loopback_e2e_test`, `gameui_multiplayer_test` (+ the other gameui tests) pass. `net_arena_test` does not compile: its
  source (another session's edit) has broken string literals at line ~98.
* [ded] `dedicated_server_check.py` passes: countdown lines, player table with pings (40 ms on loopback), the host bot's Rematch,
  `kick Bravo` (Kicked reject on the bot, gone from the table).
* [game] two instances, port 27600 (`mp_results_check.py`, screenshots `build/harness/mp_results/{a,b}/`):
  match 1 by time limit -> A's **Back to lobby** (keyboard) -> both games left the level on their own and resumed the lobby ->
  Start Match; **Rematch** with the mouse (hover + click, `a_m1_results_hover.png`; run of 2026-11-01 07:2x) and later with the
  keyboard; **Next map** -> `server: next map: City of the Damned: Skeleton Square`, the base level was launched; **Leave** on B ->
  `player 2 'JoinRynn' left` on the server; the guest sees the host buttons greyed (`b_m1_results.png`: "Waiting for the host to
  choose"). Different runs proved different steps: one run with all three actions in sequence did not complete (B stuck in a
  level load on the third match; the harness timing is fragile on a busy PC, see Findings).
* Host page: the rules summary overlapped the last row after the Game Mode row was added; moved below the rows (y 774, two
  lines). Checked in game at 1280x720 and 1920x1080 (`build/harness/mp_screens_{720,1080}/a/mp_host.png`).
* Not proven in game: a custom mode match end to end (the Host page of the current exe also carries another session's changes,
  Game Type greyed, so the harness key path for `--custom` no longer reaches the Game Mode row; the server side is covered by the
  unit tests), co-op results buttons, capture / hold in game, the real mouse (only the script's cursor events), 3 / 4 team HUD in game.
