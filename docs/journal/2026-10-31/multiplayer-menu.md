# Multiplayer menu, runtime session control, LAN discovery - 2026-10-31

Reference text (pages, API, launch path, discovery): `docs/reference/netcode.md`, "Multiplayer menu". This note records what was
built, what was verified in game, and what is open. Tags: **[game]** two real instances (PAL disc, Release, D3D11),
**[test]** `ctest --preset runtime-tests -R "gameui_multiplayer|net_"`.

## What was built

* `runtime/net`: protocol **v5** (Hello.password, BadPassword reject, MatchConfig.serverName, Ready, Query / ServerInfo),
  `UdpTransport::setQueryResponder` (Query datagram type 7 -> QueryReply 8), `net::ServerBrowser` (broadcast + loopback sweep
  of ports 47800..47809, unicast, answers merged by host:port / `serverId`), `MatchServer::info()` / `answerQuery()` /
  `allReady()`, `drakan_server --name --password`, default port 47800. [test] protocol round trips, password and Ready on the
  loopback network, status answer, discovery over real sockets (the broadcast also arrives over the LAN adapters, hence the
  `serverId` merge).
* `runtime/gameui`: `MultiplayerScreen` + `drawHud` (std only), a Multiplayer row in `TitleScreen` (`showMultiplayer`, off by
  default so existing title tests are untouched; the glue turns it on), `UiInput::text` / `backspaces` for text fields.
  [test] navigation, forms, value lists, Add Server, server table scrolling, lobby, focus following rows, mouse, HUD sorting.
* `drakan_netplay.h`: `hostMatch / joinMatch / leave / state / lanServers / refreshLan / startMatch / setReady / sendChat /
  chatLog` plus the launch hooks. The network thread now runs idle from start-up and runs one session per Host / Join command;
  `DRAKAN_NET_HOST` / `DRAKAN_NET_CONNECT` are dev shortcuts that issue the same commands. `drakan_ghost` installs its hooks
  for multiplayer without `DRAKAN_GHOST` (its F9 / F10 / F1 debug controls still need it); `DRAKAN_MULTIPLAYER=0` turns it off.
* Glue: `drakan_settings_host.cpp` (state / action mapping, `multiplayer.ini` profile, in-game HUD), `drakan_input_glue.cpp`
  (START taps to skip the intro while launching), `drakan_overrides.cpp` (registers the level redirect and the mod levels).
* `scripts/harness/netplay_menu_two_instances.py` (`--screens`, default two-instance run, `--level-index N`).

## Level launch (the question from the lifecycle study)

Posting the level load into a running level hangs, so the match starts from the title screen: **New Game with a runtime level
override**. `DRAKAN_START_LEVEL` is `FileService::setLevelRedirect("Level 1A - Surdana", <level>)` at start-up; the same call is now
made when the match enters Loading. New Game plays the intro cut scene (skipped with START taps) and then opens the Surdana
paths, which the file layer rewrites. The engine and the lifecycle hooks keep reporting "Level 1A - Surdana", so netplay
substitutes the match level when it computes `levelHash`. **[game]** Surdana (no redirect) with ghosts both ways; Kragmor
(redirect) loaded on both clients with equal hashes, Playing reached (Rynn starts mounted there, so ghosts are hidden by the
existing rule).

## Verified in game [game]

Screens `build/harness/mp_menu_screens/a/*.png`: title with the Multiplayer row, Multiplayer (player setup), Host Game (levels,
teams "(coming)", greyed placeholders), Join Game (empty and Add Server), colour swatch. Two instances
`build/harness/mp_menu_two/{a,b}`: A hosts through the menu (Begin Game), B finds A in the LAN list (`b_join_list.png`) and
joins (`b_lobby.png`), A starts the match from the lobby, both load the level through the launch path and spawn each other's
ghost (`b_sees_a.png`), scoreboard HUD with the OotF columns (`a_scoreboard.png`, forced by `DRAKAN_NET_SCOREBOARD`; scripted
input cannot press F4). Private play folders `build/mp-ui-{a,b}`.

## Open / limits

* Text entry is verified by the unit tests only; the harness cannot type (scripted input has no characters). The ImGui char
  queue path (`UiInput::text`) and the F6 chat typing were not exercised in game.
* Teams 3 / 4, friendly fire, master servers, dragon modes, team select, Remove / Stop / version colours in Join, results
  summary: shown greyed or absent. The server has two teams.
* The scoreboard is on F4, not Tab (Tab opens the inventory); gamepad has no scoreboard / chat binding yet.
* Custom map check is by name (disc list or the mod layer's `extraLevels()`), not by file hash. Verified only for disc levels.
* The Join table shows one entry per server (merged by `serverId`); entries come from any adapter, so on a PC with several
  adapters the shown address is the first or fastest, not always the LAN one.
* Another instance on the LAN (a concurrent test) joined A's open server once during a run: servers are open unless a password
  is set.
* Combat / kills are the PvP agent's: the scoreboard shows whatever `Score` messages deliver.
