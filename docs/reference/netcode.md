# Netcode: protocol v5, authority model, `runtime/net`

Status 2026-10-31: the protocol, match state machine, transports and the headless `drakan_server` exist and are tested;
the game side has ghosts, a Multiplayer title menu with host / join / lobby (see "Multiplayer menu") and LAN discovery (see
"Playing over the network (spike)"; plan in
`docs/proposals/level-editor-and-multiplayer.md`) and PvP combat (see "PvP"). Rationale for the model: `docs/journal/2026-10-31/multiplayer.md`.

## Layout

| Path | What |
| --- | --- |
| `runtime/net/include/net/Transport.h` | Abstract transport: `listen`, `connect`, `send` on `Channel::Reliable` (ordered, exactly once) or `Channel::Unreliable`, `poll` events (`Connected`, `Disconnected`, `Message`), opaque `PeerId`s. Messages <= `kMaxPacketPayload` (1100 bytes). |
| `net/Protocol.h` | Messages, `ByteWriter` / bounds-checked `ByteReader`, `encode<T>` / `decode<T>`. Explicit little-endian; `f32` is IEEE bits; non-finite floats fail the reader. |
| `net/MatchServer.h` | Authoritative match: roster, phases, damage, score, respawn. `tick(nowMs)` with an explicit clock (deterministic in tests). |
| `net/MatchClient.h` | Handshake, roster, per-player snapshot buffer with interpolation, event queue (`std::variant`). |
| `net/LoopbackTransport.h` | In-process; `LoopbackNetwork` has a virtual clock, optional one-way latency and unreliable loss. |
| `net/ServerBrowser.h` | LAN / direct discovery client: `queryLan()` (broadcast + loopback, ports 47800..47809), `queryHost()`, `poll()` returns merged `ServerEntry`s (v5). |
| `net/UdpTransport.h` | IPv4 UDP, non-blocking; answers `Query` datagrams through `setQueryResponder` (v5). Winsock on Windows, BSD sockets elsewhere (kept out of headers). Not built for Emscripten. |
| `runtime/net/server/` | `drakan_server --port --max-players --mode dm\|tdm --level` (also `--frag-limit --time-limit --auto-start --tick-rate --profile-hash`). No game, no ISO. |
| `runtime/net/tests/` | `net_protocol_test`, `net_match_test`, `net_loopback_e2e_test`, `net_udp_smoke_test` (exit 77 = skipped when sockets are unavailable). |

A browser build needs a WebSocket / WebRTC `Transport` in `src/`; nothing above `Transport` may rely on more than the
two channel guarantees. Unwritten.

## UDP wire layer

Datagram = `'D' 'K'` + type byte. Types: Connect, ConnectAck, Disconnect (empty), Unreliable (payload), Reliable
(`seq u32` + payload), Ack (`next expected seq u32`, cumulative, doubles as keep-alive every 1 s), Query (7) and
QueryReply (8) (v5, no connection: an encoded `Query` / `ServerInfo` message, see "LAN discovery"). Reliable channel:
per-peer sequence, receiver accepts only the next sequence, sender resends unacked packets every 150 ms (window 128,
excess queued). Connect retried every 250 ms for 5 s; a peer silent for 10 s is dropped.

## Protocol v5

v5 (2026-10-31, after the weapon ids of v4): `Hello.password`, `RejectReason::BadPassword`, `MatchConfig.serverName`, the
lobby `Ready` message and the discovery `Query` / `ServerInfo` messages. A v4 peer's Hello does not parse as v5 (the server
rejects it as malformed). `drakan_server` gains `--name`, `--password` and defaults to port 47800.

Application message = 1 type byte (`MsgType`) + body; every decode requires the exact length. Integers little-endian.
`str` = `u16` length + bytes (per-field maximum enforced). Reliable unless marked.

| Type | Dir | Body |
| --- | --- | --- |
| 1 Hello | C>S | `u16 protocolVersion`, `u64 profileHash`, `str name` (<=24), `str password` (<=24, empty = none; v5) |
| 2 Welcome | S>C | `u8 player`, `u8 tickRateHz`, `u8 phase`, `u32 serverTimeMs`, MatchConfig body |
| 3 Reject | S>C | `u8 reason` (0 version, 1 profile, 2 full, 3 name, 4 password), `u16 serverVersion`, `str message` |
| 4 MatchConfig | S>C | `u8 mode` (0 DM, 1 TDM), `str level` (<=64), `u16 fragLimit`, `u16 timeLimitSec`, `u8 maxPlayers` (1..16), `u16 respawnDelayMs`, `u16 startHealth`, `str serverName` (<=32; v5) |
| 5 PhaseChange | S>C | `u8 phase` (0 lobby, 1 loading, 2 playing, 3 results), `u32 serverTimeMs` |
| 6 PlayerState | C>S, unreliable | `u8 player`, `u32 sequence`, `u32 tick`, `f32 pos[3]`, `f32 yaw`, `f32 vel[3]`, `u16 animId`, `f32 animTime` (lower body), `u16 upperAnimId`, `f32 upperAnimTime`, `u32 lowerMask` (bit i: skeleton channel i plays the lower clip; see `world-model.md` section 6), `u32 weaponRight`, `u32 weaponLeft` (resource record ids of the held items, 0 = empty hand; v4), `u8 mounted` (0/1), `u32 levelHash` (FNV-1a of the sender's level name, 0 = loading; v3); 66 bytes |
| 7 Snapshot | S>C, unreliable | `u32 serverTimeMs`, `u8 count` (<=16), count x PlayerState body (live players with a known state) |
| 8 Hit | C>S claim, S>C accepted | `u8 attacker`, `u8 victim`, `u8 weapon` (which item: 0 right-hand melee, 1 left-hand melee, 2 projectile; the server checks it against the attacker's held `weaponRight` / `weaponLeft`), `u16 damage` (claim: as the engine computed it; broadcast: after `damageScale`), `u16 healthAfter` (client sends attacker 0) |
| 9 Death | S>C | `u8 victim`, `u8 killer`, `u8 weapon` |
| 10 Respawn | S>C | `u8 player`, `u8 spawnIndex` (server rolling counter; client maps to a level spawn point), `u16 health` |
| 11 Chat | both | `u8 player` (server fills), `str text` (<=128, control chars stripped) |
| 12 Score | S>C | `u8 player`, `u16 kills`, `u16 deaths`, `u16 teamKills[2]` |
| 13 PlayerJoin | S>C | `u8 player`, `u8 team` (0/1 or 255 none), `str name` |
| 14 PlayerLeave | S>C | `u8 player` |
| 15 LoadDone | C>S | empty |
| 16 Ready | both | `u8 player` (server fills), `u8 ready` (0/1); lobby only (v5) |
| 17 Query | C>S datagram | `u16 protocolVersion`, `u32 token`; UDP datagram type 7, no connection (v5) |
| 18 ServerInfo | S>C datagram | `u16 protocolVersion`, `u32 token`, `u32 serverId`, `str name`, `str level`, `u8 mode`, `u8 phase`, `u8 players`, `u8 maxPlayers`, `u8 passwordFlag`; UDP datagram type 8 (v5) |

Handshake: transport connect, Hello, then Welcome (or Reject, after which the server closes). Welcome is followed by
PlayerJoin for each existing player and the newcomer (the newcomer learns its own team from its own PlayerJoin). The
`profileHash` is the disc profile hash; the server compares it when it was started with `--profile-hash`.

## Authority model

The recompiled game is not deterministic, so each client runs its own world. The server owns **match state**: who is
in, teams, health, kills, deaths, phase, respawns. Clients own **their own movement** (PlayerState); the server never
simulates the game and needs no level data.

- Phases: Lobby, then `startMatch()` (or `--auto-start N` countdown), Loading (clients load the level and send
  LoadDone; ends when all report or after `loadTimeoutMs`), Playing, Results (`resultsDurationMs`), back to Lobby.
  Ends on frag limit (deathmatch: a player's kills; team deathmatch: team kills), time limit, or `endMatch()`. The
  last player leaving resets to Lobby. Joining mid-match is allowed and spawns at once.
- Damage: clients send Hit as a claim; the server accepts it only if the phase is Playing, both players alive,
  victim is another player, `weapon <= 2`, `1 <= damage <= min(maxDamagePerHit, weapon table entry)` (`weaponMaxDamage`:
  record id of the item the attacker holds in the claimed hand -> maximum; `unarmedMaxDamage` for an empty hand; the
  left-hand entry for projectiles), victim not under respawn protection, no friendly fire in team deathmatch, attacker
  under the per-attacker rate limit (`minHitIntervalMs`), and attacker and victim last accepted positions are within
  `maxHitRange` (melee) or `maxHitRangeRanged` (projectile). Accepted hits are scaled by `damageScale`, broadcast with the
  resulting health; at 0 health the server broadcasts Death and two Scores, then Respawn after `respawnDelayMs`. Every
  rejection is logged with its reason ("hit rejected: out of range (1447 > 900)", "victim dead", ...).
- Movement checks: stale sequence numbers are dropped, non-finite or huge coordinates and speeds above `maxSpeed`
  (units/s between accepted states, default 30000; Rynn runs ~2500) are discarded. The first state after (re)spawn is exempt. A dropped teleport
  heals itself as the elapsed time grows.
- Remote players: the client buffers snapshot states per player and renders `interpolationDelayMs` (100) behind the
  estimated server clock, interpolating position, velocity, yaw (shortest arc) and animation time; discrete fields come
  from the nearer sample; no extrapolation. A Respawn clears that player's buffer. A **dead player keeps publishing**
  (the server accepts its states and snapshots keep carrying them): the others see its death animation through the
  replicated clips; the server forgets the corpse's last state at the respawn (`hasState = false`) so the new life does not
  start at the old pose.
- Known limits: the hit range check trusts both clients' positions; there is no lag compensation, no encryption or
  authentication, no NAT traversal. The PvP limits are tuned in game units (see "PvP"); the movement limit is not.

## Playing over the network (spike)

Status 2026-10-31: two game instances on one PC see each other's Rynn as a ghost. Code:
`ps2recomp/ps2xRuntime/src/lib/drakan_netplay.{h,cpp}` (GS-renderer builds, PAL disc profile; it needs `drakan_ghost` and
`drakan_lifecycle`). Results and limits: `docs/journal/2026-10-31/netplay-spike.md`.

Environment (all opt-in; also set `DRAKAN_GHOST=1`, the ghosts are `drakan_ghost` actors):

| Variable | Meaning |
| --- | --- |
| `DRAKAN_NET_HOST=<port>` | Listen server: a `MatchServer` runs in the game process (UDP `<port>`, auto-start with one player, no frag / time limit) and the game joins it over `127.0.0.1`. |
| `DRAKAN_NET_CONNECT=<host:port>` | Client of a listen server or of `drakan_server --port <p> --auto-start 1 --time-limit 0 --frag-limit 0`. |
| `DRAKAN_NET_NAME=<name>` | Player name (default `Rynn`, 24 characters). |

How it works: sockets, the embedded server and the `MatchClient` live on one network thread that never touches guest
memory. At each `PadMgr_Update` the EE thread reads the controlled actor with `world::readPlayerPose`, converts it with
`toPlayerState` and drops it into a mutex-protected slot; the network thread sends it at 20 Hz. Raw received snapshot samples
go the other way into a `world::PoseBuffer` per player, replayed on the sender's world-tick clock (`PlayerState.tick` is the
sender's PadMgr counter; playback advances once per receiver tick); the ghost is spawned (`drakan_ghost::spawn`) when the remote player is Playing, mounted-off, and in the same level, and driven with
`drakan_ghost::setPose` (position, yaw and clips) every frame. It is removed when the player leaves, changes level (their `levelHash` goes to 0 while
loading, then to the new level's hash) or when this instance loads a level or writes a save (`drakan_lifecycle`).

Mismatch rules: the disc profile (hash of the serial and save id) is sent in `Hello`; a server started with that hash (the
listen server always is) rejects other discs with `ProfileMismatch`, and a different protocol version with
`VersionMismatch` (logged as `REJECTED by the server`). The level is deliberately **not** a join check: players change
level during a match, so every `PlayerState` carries `levelHash` (v3, FNV-1a of the level name from `afterLevelLoad`) and
ghosts only show between players in the same level. `MatchConfig.level` records the host's level at start
(informational).

Run both instances with `python scripts/harness/netplay_two_instances.py` (`--mode server` for a `drakan_server`, see its
`--help`). By hand: two play folders, two `DRAKAN_MEMCARD_DIR`s, instance A `DRAKAN_NET_HOST=47800`, instance B
`DRAKAN_NET_CONNECT=127.0.0.1:47800`, both `DRAKAN_GHOST=1`. The game logs `[netplay ...]` lines to stderr: connect and
reject, ghost spawn / removal, and every 5 s `stats: rtt .. ms, snapshots ../s, states sent ../s`. The RTT is measured
from our own PlayerState's send time to the snapshot that echoes its sequence (`MatchClient::rttMs()`), so it includes up
to one 50 ms snapshot interval.

Safety rules in `drakan_netplay`: the opening cut scene (`Level 0 - Intro`) counts as "no level" (a ghost spawned there
made the guest jump to a null target and hang the intro); ghosts are only spawned 1.5 s after a level load; a ghost
that `drakan_ghost` drops by itself is retried with a back-off (2 s, 4 s .. 10 s) instead of every frame; a remote
level / mounted / not-playing change hides the ghost after 0.5 s (a menu opening makes the pose read fail for a
frame), our own load or save hides at once. `python scripts/harness/netplay_two_instances.py --reject` checks the
profile rejection against a `drakan_server --profile-hash 1`.

Not done here: mounted (dragon) players are hidden (no PvP on Arokh). Hits, damage, deaths and respawns: see "PvP".

## PvP (deathmatch, 2026-10-31)

Status: verified in two game instances (`python scripts/harness/netplay_two_instances.py --pvp --card <mc0 with the dagger>`:
A swings Rynn's dagger at B's ghost, B loses health, dies, respawns three seconds later, scores 1/0, 2/0, 3/0, the match
ends at the frag limit with results). Code: `drakan_pvp.{h,cpp}` (combat glue), `drakan_ghost` (hittable ghosts),
`drakan_netplay` (claims, events, scoreboard API), `runtime/net` (`MatchServer`, `Scoreboard.h`). Findings, evidence and
limits: `docs/journal/2026-10-31/pvp.md`. On by default with the multiplayer module; `DRAKAN_PVP=0` turns it off.

**Hits on ghosts.** A blade sweep (`0x4979a8`, then `0x498020` per contact) finds actors through the 2048-unit collision
cells of the level object. A host write of a ghost's position bypassed the engine's mover, so the ghost stayed in its spawn
cell and nothing could hit it. `drakan_ghost` now does what the mover does every tick the ghost moved: clears the stale
bounding sphere flag (`0x104` bit 11), removes the actor from its cell list (vtable `+0x84`, only while listed: `0x104` bit
28) and files it again (`+0x8c`, `0x1496a0`). Then the engine's own hit application runs on the ghost: blood spray, sound,
the contact record. The ghost's swallowing handler drops the damage message (the local HP block is untouched; the hook
checks it and restores it if it ever changed). `drakan_pvp` wraps `0x498020`: when the victim is a ghost and the blade's own
hit list grew (one per victim per swing: `blade+0x1a0`) it sends a claim: weapon = the hand of the blade (right / left
holder), damage = `*(contact+0)`, which is `item property 0x1a x blade+0x194` (the dagger: 4).

**Server resolution.** `MatchServer` validates as in "Authority model". Tuning (`applyDrakanPvpTuning`, used by the listen
server and `drakan_server`): melee reach 900 (the dagger hit ghosts 90..260 units away; the rest is two snapshot intervals
of running at ~2500 u/s), projectiles 20000, one hit per attacker per 100 ms, 2 s respawn protection, dagger limit 16
(damage 4 x blade multiplier), empty hand 8, `damageScale` 3 (the dagger does 4 against a 100 point bar; env
`DRAKAN_PVP_DAMAGE_SCALE`, `drakan_server --damage-scale`), respawn after 3 s (`DRAKAN_PVP_RESPAWN_MS`, `--respawn-ms`;
it must stay below the death clip, ~4.7 s, see below). Scoreboard and results: `MatchServer::scoreboard()` /
`MatchClient::scoreboard()` (sorted by kills, then fewest deaths), `MatchOutcome` (`decideOutcome`: deathmatch winner by
kills then deaths, team winner by team kills, ties; reason frag limit / time limit / ended).

**Victim side** (`drakan_pvp::beforePadMgr`, EE thread). Hit on the local player: the engine's own damage method
`0x314ab0(data, &damage, 0, 0, 0, 1)` (flinch, sound, HUD message) with a damage that cannot kill, **posted** to a dispatcher
thread (a nested call hung the guest once: the method plays sounds and effects behind the heap semaphore), then the HP block
(`dec(S+0xc)`, key `0x4fe86c`) is set to the server's value scaled to Rynn's own maximum. Death: the engine's own death is
not usable for a match (lethal damage hides the body and raises the single-player game over prompt "Press Enter to load the
last saved game", and the level reload behind it hangs the guest when started from outside the game's own flow, runs
pvp13..pvp15). Instead HP stays at 1, the player's behaviour handler (`*(*(actor+0xfc)+0x14)`) is swapped for one that
swallows every event but the destroy (the ghost technique) and the death clip `0x20577` is written into every skeleton
channel each tick; every other player sees it through the replicated pose. Respawn: handler restored, the player is moved
to a spawn point (sector refiled), HP set. Spawn points: positions the local player walked (one per 500 units of travel on
level ground, the level start first; cleared on a new level), choosing the one farthest from the enemies last seen; the
server's `spawnIndex` breaks ties. Level object list start actors are not read (not needed so far). A match ending while
dead gets the player up where she is.

**API for the scoreboard UI** (`drakan_netplay.h`, thread safe): `scoreboard()` returns phase, rows (id, name, team, kills,
deaths, health, alive, self), limits, `timeLeftSec`, own health, `respawnInSec` while dead, `over` / `tie` / `winnerId` /
`winnerTeam` / `reason` in the Results phase, and the last 8 kills (`killFeed`). `sendHitClaim`, `playerOfGhost`,
`ghostOfPlayer` serve `drakan_pvp`. The log has one line per claim, hit, death, respawn and score, and "match over (...)".

Test aids: `DRAKAN_PVP_SELFTEST=hit:<n>,kill:<n>,respawn:<n>` feeds the victim side the events a server would send
(boundaries after the player is ready); `DRAKAN_PVP_TEST_PLACE=<dist>` holds the ghosts in front of / behind the player and
`DRAKAN_PVP_TEST_ANCHOR=1` holds the player (solid ghosts pushed her away, out of the server's melee range);
`DRAKAN_NET_FRAG_LIMIT` / `DRAKAN_NET_TIME_LIMIT` for the `DRAKAN_NET_HOST` shortcut; `DRAKAN_PVP_PROBE=1` logs every hit.

Not done: arrows and bolts (the projectile claim path exists in the server, `kHitProjectile`, but no game hook sends it),
other weapons' limits (only the dagger is in the table: other records fall back to `maxDamagePerHit`), PvP while mounted,
USA disc (PAL addresses; the module reports "no address translation" and stays off), team deathmatch and the time limit in
game (unit tests only), a death screen / kill feed UI, lag compensation.

## Multiplayer menu (2026-10-31)

A **Multiplayer** row on the title menu (after New Game) opens the host-drawn multiplayer pages inside the modern title
menu (`title_menu = modern`, the default); `DRAKAN_MULTIPLAYER=0` removes the row and the whole module. Flow and layout
follow what is documented of Drakan: Order of the Flame (`docs/journal/2026-10-31/ootf-multiplayer-ui-spec.md`: LAN /
Internet connection choice, host game setup, join with a server list, player setup, up to 8 players). The documents give
no pixel layouts, so every position is in `gameui::design::mp` (`runtime/gameui/src/Multiplayer.cpp`), drawn with the
game's own font and skin art (Paint / `GameUiRenderer`); no OotF art is used.

| Page | Contents |
| --- | --- |
| Multiplayer (OotF's player setup screen) | Player Name, Player Color (swatch), Human Skin (Rynn only for now), Dragon Skin (7 names, no effect yet), Connection Speed (Modem 28.8k .. LAN/T1 1M), Host Game, Join Game, mplayer.com (greyed: the service is gone), Back (saves the profile) |
| Host Game | Server Name, Password (blank = open), Level Group (All Levels / Disc levels / Custom levels), Starting Level (14 disc levels, then the levels `ModManager::extraLevels()` reports, marked "(custom)"), Maximum Number of Players 2..12, Kills Limit (No Limit, 5 .. 200), Time Limit (No Limit, 5 .. 45 minutes, 60), Number of Teams (No Teams, 2; 3 and 4 shown "(coming)" and not startable: the server has two teams), Friendly Fire? and Register With Master Servers? (greyed placeholders), Begin Game, Back. There is no mode row: OotF takes the mode from the level type; here teams >= 2 means team deathmatch, otherwise deathmatch (Dragon's Duel / Master of the Dragon are not implemented). |
| Join Game | the server table Server Name / Current Level / Players / Ping sorted by ping (blue = LAN, white "+" = Add Server entry, green = selected, "[password]" marker), Refresh, Add Server (address entry; OK keeps it for this session), Server Info (IP, level, players, mode, password card), Password, Connect; a list row also joins. No Stop / Remove / master server / version-colour rows yet. |
| Lobby | server / mode / map / limits, the roster (ready flags, team names Fire / Water, `[host]`), Ready, Start Match (host), Leave; a Loading card while the match starts. Team select (four teams) is not shown: the server assigns teams. |
| In game | **F4** held = scoreboard with OotF's columns Player / Kills / Deaths / Ping / Time (own ping measured, others "-"; team totals in team games), **F6** = chat (Enter sends, Esc cancels; game input is suspended while typing), a fading chat log bottom left, and a banner (no results screen, as in OotF) while the server shows the results phase before the lobby returns. OotF binds the scoreboard to Tab; here Tab is the inventory, hence F4. |

Navigation: keyboard (arrows / WASD, Enter, Esc), mouse (hover, click, wheel, right click = back) and pad (D-pad, Cross,
Circle). Text fields (server name, password, address, player name) take typed characters; with a pad, Up / Down change the
last character, Right appends, Left deletes. Code: model and drawing `runtime/gameui/{include/gameui/Multiplayer.h,src/Multiplayer.cpp}`
(std only, `gameui_multiplayer_test`), title integration `TitleRow::Multiplayer` / `TitlePage::Multiplayer` in `TitleScreen`,
glue `drakan_settings_host.cpp` (`mpState`, `mpApply`, `mpHudDraw`), network control `drakan_netplay.h`.

### Session control API (`drakan_netplay.h`)

`hostMatch(HostConfig, name)`, `joinMatch("host:port", password, name)`, `leave()`, `state()` (conn, phase, roster, config,
message), `startMatch()` (host, lobby), `setReady()`, `sendChat()` / `chatLog()`, `refreshLan()` / `lanServers()`,
`takeLaunchRequest()` / `launchTaps()` (below), `setLevelRedirectFn()` / `setExtraLevels()` (registered by
`drakan_overrides`). All thread safe: calls queue commands for the network thread, `state()` returns a mutex-protected
snapshot. The module installs without any environment variable now (`drakan_ghost` installs its hooks too; its F9 / F10 /
F1 debug controls still need `DRAKAN_GHOST=1`). The `DRAKAN_NET_HOST` / `DRAKAN_NET_CONNECT` shortcuts issue the same
calls at start (host with `autoStart`, no limits; client with the old retrying connect and no menu launch).

### Starting a match: the safe level path

Posting the level-load function into a running level hangs (lifecycle study), so a match is entered through the title
screen's **New Game** with a level override, the `DRAKAN_START_LEVEL` mechanism made runtime-controllable:

1. Host presses Start Match: the server goes Loading (`MatchConfig.level` = the map, `loadTimeoutMs` 120 s).
2. Every client (host included) sees Loading, calls `requestLaunch(level)`; the title overlay (UI thread) picks it up with
   `takeLaunchRequest()`, which sets `hle::FileService::setLevelRedirect("Level 1A - Surdana", <map>)` (no redirect when
   the map is Surdana) and the glue pushes a `NewGame` title action; the existing title executor presses New Game.
3. The intro cut scene is skipped by START + Cross taps injected in the pad read (`drakan_input_glue.cpp`, while
   `launchTaps()`), which stop when a playable level has loaded.
4. The engine opens `Level 1A - Surdana` paths, the file service rewrites them to the map (the lifecycle hooks still report
   "Level 1A - Surdana"; `drakan_netplay` substitutes the match level when computing `levelHash`, so peers agree).
5. After the load the client sends `LoadDone`; the server enters Playing when all reported; ghosts appear.
6. `leave()` or the match ending clears the redirect. Back in the lobby phase a new Start launches again.

Custom maps: `ModManager::extraLevels()` names (levels registered by mod packs) join the Host level list; they are loaded by
the same redirect (the pack's files are served by the file layer). OotF needs identical level files on client and server: a
client that does not have the match level (not a disc level and not in its `extraLevels()`) refuses the launch with "Level
files mismatch" in the lobby instead of loading something else. File contents are not compared (no per-level hash in the
protocol yet; the disc profile hash in `Hello` covers the disc only). Verified on this disc: Surdana (no redirect) and
Kragmor (redirect), both clients loaded the same level and agreed on the hash. A level where Rynn starts mounted (Kragmor
in this test) hides the other player's ghost by design (mounted players are not shown yet).

### LAN discovery (v5)

`UdpTransport` answers a `Query` datagram (type 7) that reaches a listening socket with a `QueryReply` (type 8) whose
payload is `net::ServerInfo` (`MatchServer::answerQuery`): protocol version, token (echoed for the ping), `serverId`,
name, map, mode, phase, players, max players, password flag, in the spirit of the GameSpy `\status\` query noted in
`docs/journal/2026-10-31/ootf-reuse-study.md`. `net::ServerBrowser` sends the query to UDP broadcast and loopback on ports
`kDefaultPort` .. `+9` (47800..47809) or to one `host:port`, and merges answers by host:port or `serverId` (a server answers
on every local address; the fastest wins). A host takes the first free port of the ten. A server that does not answer is
dropped from the list after 8 s; the list sweeps every 2 s while the Join page is open.

### Checks

`ctest --preset runtime-tests -R "gameui_multiplayer|net_"` (screen model, title row, HUD, protocol v5, password, Ready, status
answer, UDP discovery on loopback). In game: `python scripts/harness/netplay_menu_two_instances.py --screens` (a screenshot
of every page) and `python scripts/harness/netplay_menu_two_instances.py [--level-index N]` (A hosts through the menu, B finds
A in the LAN list and joins, both launch the level and spawn each other's ghost; private play folders `build/mp-ui-{a,b}`).
Details and limits: `docs/journal/2026-10-31/multiplayer-menu.md`.

## Tests

`ctest --preset runtime-tests -R net_`. Everything is deterministic except the UDP smoke test, which uses localhost
sockets with wall-clock time.
