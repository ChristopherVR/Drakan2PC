# Co-op on drakan::net: protocol v6 (2026-10-31)

Task: move the co-op stream off its private UDP socket onto the multiplayer session, give the level change a real message,
and offer co-op in the Multiplayer menu. Reference: [netcode.md "Co-op mode (protocol v6)"](../../reference/netcode.md);
design background [coop-design.md](coop-design.md) sections 7 and 8, [level-change.md](level-change.md) section 4.

## What changed

* **Protocol v6** (`runtime/net`): `MatchMode::Coop`, messages `CoopAuthority` (19), `CoopCreatures` (20), `CoopEvent` (21),
  `LevelChange` (22). `MatchServer` relays them under routing rules (authority-only streams, guest-to-authority claims,
  object events to everybody, targeted Hurt / Respawn), has no PvP and no limits in Coop and appoints the first joiner as the
  creature world's owner. `MatchClient` gains `sendCoopCreatures` / `sendCoopEvent` / `sendLevelChange`, the four events in
  its queue and `coopAuthority()`. `drakan_server --mode coop`.
* **Transport swap** (`drakan_coop.cpp`): `netThread` no longer owns a socket. It converts between the co-op logic's own datagram
  form (unchanged, so `pushReliable`, `hookSend`, the host / client ticks and every helper keep their signatures) and
  `net::CoopCreatures` / `net::CoopEvent`, and exchanges them with `drakan_netplay` (`coopLink`, `coopSend`, `coopPoll`), whose
  network thread sends them through the session's `MatchClient`. `DRAKAN_COOP_HOST` / `DRAKAN_COOP_CONNECT` are gone;
  the module installs with the multiplayer module and `syncLink()` (EE thread, every `PadMgr_Update`) switches it on when the
  session is a Coop match in the Playing phase and off (state forgotten) when it is not.
* **PvP stands down in co-op** (`drakan_pvp.cpp`): no blade claims on the partner's ghost, no server deaths / respawns / phases
  applied while `drakan_netplay::coopMatch()`.
* **Level follow**: the `@@LC` chat line is replaced by `LevelChange`; `destHash` is taken from `drakan_levelchange::exits()`
  (the exit's destination name at the exit position).
* **Menu**: Host Game gets a "Game Type" row (Deathmatch / Co-op, after Starting Level). Co-op greys Kills Limit, Time Limit and
  Number of Teams; the lobby shows only the player count; the server list and HUD know `GameMode::Coop`. The harness recipes
  for the menu flows count one more row (`netplay_menu_two_instances.py`).
* **Harness**: `coop_two_instances.py` starts the host with `DRAKAN_NET_MODE=coop` (new legacy-shortcut variable) and takes
  `--folder <prefix>` for its private play folders; `netplay_menu_two_instances.py` gets `--coop` and `--prefix`.

## Decisions

* Event kind numbers equal the prototype's message numbers, so the conversion functions are trivial and the design notes stay valid.
* The server never takes the authority over when its owner leaves (the new owner's world would have to switch role at runtime;
  the proxies it holds are the old host's world anyway). Documented, not solved; there is no host migration.
* `Hurt` / `Respawn` carry a target player. (Superseded the same day: the host now addresses them to the player concerned,
  see [coop-design.md section 10](coop-design.md).)
* `destHash` was informational here; it is checked by the follower since section 10 of coop-design.md.

## Verification

See the final report of the session and `docs/reference/netcode.md` "Tests": ctest `net_` and `gameui_` pass; in game the
harness folders `build/harness/coopnet-{fight,door,pickup,death2,latejoin}` (all checks of each scenario pass; the first
`death` run, `coopnet-death`, failed the two claim checks because B's swings only met A's ghost in that run, the rerun
`coopnet-death2` passed everything), `build/harness/lcnet-v6` (level follow through `LevelChange`, destination hash equal to the
loaded level's hash).

## Not done

* (Done in coop-design.md section 10.) A third player in a co-op match is relayed but not modelled by the co-op logic.
* Chest / gold / door logic of the other agent still sends through the same helpers; nothing of it was changed here.
* (Done in coop-design.md section 10.) `LevelChange.destHash` is not checked by the receiver.

## Host migration (later the same day)

* **Server**: when the co-op authority leaves or times out, `MatchServer::removePlayer` appoints the longest-connected remaining
  player (`ServerPlayer::joinSeq`, because player ids are recycled: after the first owner leaves, a newcomer gets id 1 and must not
  inherit the world) and broadcasts `CoopAuthority` after `PlayerLeave`. `coopOrphaned_` is gone; `ServerStats::coopMigrations`
  counts. The dedicated server uses the same code. Details and limits in [netcode.md](../../reference/netcode.md) "Authority migration".
* **Game**: `syncLink(rdram)` in `drakan_coop.cpp` sees `link.host` turn true while the module runs as a guest and calls
  `handoverToAuthority`: the proxies' own handlers come back (`restoreHandler`), `hostTick` wraps them with the AI handler and streams
  them, HP stays as the last slice wrote it, creatures already dying keep dying (their deaths are announced by the normal detection),
  a player that was down waiting for the old owner's respawn gets up, queued claims are dropped, the inbox is cleared.
* **Test**: `net_coop_test` `migrationWithLoss`; harness `coop_two_instances.py --scenario migrate` (A and B join a `drakan_server
  --mode coop`, A owns the world and quits, B must take over).
* **Out of scope**: moving a listen server (see netcode.md); replaying claims that were in flight to the old owner (they cannot
  be told apart from ones it already applied, and a doubled hit is worse than a lost one).
* **In game** (`coop_two_instances.py --scenario migrate`, harness folder `build/harness/migrate4`): A and B join `drakan_server
  --mode coop` (A first), A owns 127 creatures and streams, A quits at ~83 s server time; the server logs `co-op authority: player 2`,
  B logs `handover: ... (127 creatures made real, 0 already dying, level keyed)` and then streams `host stats` lines (127 creatures,
  AI running). Not shown with two instances: another guest following the new stream (needs three), claims in flight, and HP continuity
  (B's swings did not reach a creature in that run).
