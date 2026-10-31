# Netcode: protocol v7, authority model, `runtime/net`

Status 2026-10-31 (later): protocol v6 adds the **co-op game mode** and a real `LevelChange` message (see "Co-op mode
(protocol v6)"); co-op rides the same session as deathmatch and the hidden `@@LC` chat line is gone. Earlier the same day: the protocol, match state machine, transports and the headless `drakan_server` exist and are tested;
the game side has ghosts, a Multiplayer title menu with host / join / lobby (see "Multiplayer menu") and LAN discovery (see
"Playing over the network (spike)"; plan in
`docs/proposals/level-editor-and-multiplayer.md`) PvP combat (see "PvP") and the whole menu -> deathmatch -> results -> next match flow with an in-game HUD (see
"Match flow and HUD"). Rationale for the model: `docs/journal/2026-10-31/multiplayer.md`.

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
| `runtime/net/server/` | `drakan_server --port --max-players --mode dm\|tdm\|coop --level` (also `--frag-limit --time-limit --auto-start --tick-rate --profile-hash`). No game, no ISO. |
| `runtime/net/tests/` | `net_protocol_test`, `net_match_test`, `net_coop_test` (v6: relay rules, co-op session, level change, version mismatch), `net_loopback_e2e_test`, `net_udp_smoke_test` (exit 77 = skipped when sockets are unavailable). |

A browser build needs a WebSocket / WebRTC `Transport` in `src/`; nothing above `Transport` may rely on more than the
two channel guarantees. Unwritten.

## UDP wire layer

Datagram = `'D' 'K'` + type byte. Types: Connect, ConnectAck, Disconnect (empty), Unreliable (payload), Reliable
(`seq u32` + payload), Ack (`next expected seq u32`, cumulative, doubles as keep-alive every 1 s), Query (7) and
QueryReply (8) (v5, no connection: an encoded `Query` / `ServerInfo` message, see "LAN discovery"). Reliable channel:
per-peer sequence, receiver accepts only the next sequence, sender resends unacked packets every 150 ms (window 128,
excess queued). Connect retried every 250 ms for 5 s; a peer silent for 10 s is dropped.

## Protocol v7

v7 (2026-10-31, after v6): `PlayerState.character`, a trailing `u16` (the co-op stand-in character: the NPC class id from 
`drakan_coop_character::roster()`, 0 = Rynn). PlayerState is 68 bytes on the wire. The server relays it unchanged (it is 
part of the state the client sends and the snapshot echoes); a receiver spawns the ghost of that player as the character 
and respawns it when the value changes. A v6 peer is rejected with `VersionMismatch` (`serverVersion` 7). 
See [coop-characters](../journal/2026-10-31/coop-characters.md).

## Protocol v6

v6 (2026-10-31, after v5): `MatchMode::Coop = 2` (MatchConfig / ServerInfo `mode` accept 0..2) and four messages,
`CoopAuthority`, `CoopCreatures`, `CoopEvent` and `LevelChange`, documented in "Co-op mode (protocol v6)" below. A v5
peer's Hello is rejected with `VersionMismatch` (`serverVersion` 6), its discovery `Query` is not answered. Everything else
is the v5 wire format.

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
| 6 PlayerState | C>S, unreliable | `u8 player`, `u32 sequence`, `u32 tick`, `f32 pos[3]`, `f32 yaw`, `f32 vel[3]`, `u16 animId`, `f32 animTime` (lower body), `u16 upperAnimId`, `f32 upperAnimTime`, `u32 lowerMask` (bit i: skeleton channel i plays the lower clip; see `world-model.md` section 6), `u32 weaponRight`, `u32 weaponLeft` (resource record ids of the held items, 0 = empty hand; v4), `u8 mounted` (0/1), `u32 levelHash` (FNV-1a of the sender's level name, 0 = loading; v3), `u16 character` (stand-in character id, 0 = Rynn; v7); 68 bytes |
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
| 19 CoopAuthority | S>C | `u8 player` (0 = none; v6) |
| 20 CoopCreatures | both, unreliable | `u32 levelHash`, `u32 sequence`, `u16 count` (<= 45), count x {`u32 key`, `f32 pos[3]`, `f32 yaw`, `i32 hp`} (v6) |
| 21 CoopEvent | both | `u8 kind` (2..10), `u32 levelHash`, `u8 player`, then per kind: CreatureDied / ObjGone `u32 key`; Claim / ObjState `u32 key`, `i32 value`; Hurt `i32 value`; ObjMsg `u32 key`, `u16 msgType`, `i32 w[4]`; PlayerDied / Respawn / SyncReq nothing (v6) |
| 22 LevelChange | both | `u8 player` (server fills), `u32 destHash` (FNV-1a of the destination level name, 0 = unknown), `f32 exit[3]` (the exit trigger's world position) (v6) |

Handshake: transport connect, Hello, then Welcome (or Reject, after which the server closes). Welcome is followed by
PlayerJoin for each existing player and the newcomer (the newcomer learns its own team from its own PlayerJoin). The
`profileHash` is the disc profile hash; the server compares it when it was started with `--profile-hash`.

## Co-op mode (protocol v6)

`MatchMode::Coop`: the match is played together instead of against each other. The server relays, it never sees creatures
(the same rule as the rest of the layer: clients run their own world). Server behaviour in Coop (`MatchServer`):

* **No PvP, no limits.** A `Hit` claim is rejected (`co-op: players cannot hurt each other`), frag and time limit never end
  the match, there are no teams (`kNoTeam`), no scores.
* **Authority.** The first player to join a Coop match owns the creature world (`MatchServer::coopAuthority()`,
  `MatchClient::coopAuthority()` / `isCoopAuthority()`); the server tells everyone with `CoopAuthority` (to a newcomer on its
  join, to all when it is assigned or changes; switching a lobby to Coop appoints the longest-connected player, switching away
  clears it). On a dedicated server the first client to join is the host of the world; on a listen server it is the host's own
  client.
* **Authority migration** (no protocol change). When the authority leaves, times out or is kicked, `removePlayer` appoints the
  remaining player that has been connected longest (`ServerPlayer::joinSeq`; ids are recycled, so "lowest id" would hand the world
  to a recent joiner) and broadcasts `CoopAuthority` right after `PlayerLeave` on the same reliable channel, so every client
  sees the old owner go before the new one is named; `ServerStats::coopMigrations` counts them. The server is the single
  serialisation point, so there is no window with two owners: the authority-only checks switch in the same call, a `CoopCreatures` /
  CreatureDied / Hurt from the old owner that arrives later is refused (its peer is gone), and a late joiner is simply told the
  current owner. Reliable events already relayed are delivered exactly once as before (the channel is per peer and the relay is
  per message); what is lost is what only the old owner held: claims (`Claim`, `PlayerDied`, `SyncReq`) that were relayed to it but
  not yet processed, and its newest creature slice. The new owner continues from the HP of the last slice it received. Nobody
  takes over when the match is empty; the next joiner is the owner. The game side (`drakan_coop.cpp handoverToAuthority`)
  turns the proxies of the new owner into the real creatures and starts the stream; the other guests keep their proxies (keyed by
  resource + ordinal, identical on every instance) and follow the new stream.
* **Listen-server host leaving.** The host's `MatchServer` runs inside its process, so when that player quits the session ends
  for everyone (the clients' connections time out); the authority migrates only when the owner is a guest of a dedicated server
  or the host is not the owner. Moving the server itself needs the clients to reconnect to a new address and the whole match state
  (phase, roster, scores, pickups, the co-op owner's world) to be rebuilt, and in a co-op match the dying host owned the creature
  world; it is out of scope. Use `drakan_server` for sessions that must survive a leaving player.
* **Relay rules** (phase Playing only, otherwise dropped and counted in `ServerStats::coopRejected`; the server stamps
  `CoopEvent.player` / `LevelChange.player` with the sender):

| Message | Allowed sender | Goes to |
| --- | --- | --- |
| `CoopCreatures` | authority only | every other player, unreliable, forwarded byte for byte |
| `CoopEvent` CreatureDied | authority only | every other player |
| `CoopEvent` Hurt, Respawn | authority only | the player named in `player`, or every other player when 0; an unknown target or the sender itself drops it |
| `CoopEvent` Claim, PlayerDied, SyncReq | not the authority | the authority only (nobody there: dropped); Claim `value` must be 0..10000 |
| `CoopEvent` ObjGone, ObjMsg, ObjState | anyone | every other player |
| `LevelChange` | the authority (Coop) / the lowest player id (other modes: the host of a listen server) | every other player; exit coordinates beyond 1e6 are refused |

* **Event kinds** are the ones the first prototype used on its private socket (the numbers match, see
  `docs/journal/2026-10-31/coop-design.md` sections 7 and 8): Claim = a hit of a guest on a creature proxy (the authority
  validates it and applies the damage to the real creature), CreatureDied = the authority reports a death, Hurt = a creature hit the
  guest's ghost, PlayerDied / Respawn = the guest's player is down / may get up at the authority, ObjGone = a pickup was taken
  (either side), ObjMsg = a door / lever message to replay, ObjState = a door or chest changed state (late-join catch-up),
  SyncReq = the guest keyed the level, send the catch-up. Every event and creature slice carries the sender's `levelHash`; a
  receiver on another level ignores them.
* **Game side.** `drakan_coop` keeps its game logic and internal datagram form; its transport (`netThread`, the conversion
  functions `eventFromLegacy` / `eventToLegacy` / `creaturesFromLegacy`) now exchanges `net::CoopCreatures` / `net::CoopEvent`
  with `drakan_netplay` (`coopLink()`, `coopSend()`, `coopPoll()`), whose network thread sends them through the session's
  `MatchClient`. The private UDP socket and the `DRAKAN_COOP_HOST` / `DRAKAN_COOP_CONNECT` variables are gone. The module
  installs its hooks with the multiplayer module (`DRAKAN_COOP=0` leaves them out) and switches on when the session is a Coop
  match in the Playing phase (`CoopLink.active`), as the host when `CoopLink.host`. `drakan_pvp` stands down while
  `drakan_netplay::coopMatch()` is true (no blade claims, no server deaths / respawns / phases applied). The co-op logic is
  N-player since 2026-10-31 (coop-design.md section 10): one ghost per remote player (`drakan_netplay`'s ghosts, player id <->
  ghost via `playerOfGhost` / `ghostOfPlayer`), a per-player slot on the host (alive / down, respawn timer), creature and door
  targeting among the host's body and every live ghost, `Hurt` and `Respawn` addressed to the player concerned, claims
  validated against the claimant's own ghost. The co-op logic's internal datagram carries the player id after the level hash
  (the sender for events the server stamps, the target for Hurt / Respawn).
* **Starting a co-op game.** Multiplayer > Host Game > Game Type: Co-op (kills limit, time limit and teams are greyed), Begin
  Game; others Join Game like a deathmatch (the server list shows the mode). Dev shortcut: `DRAKAN_NET_HOST=<port>
  DRAKAN_NET_MODE=coop` and `DRAKAN_NET_CONNECT=<host:port>` (the match starts at once, a late joiner enters Playing directly).
  `drakan_server --mode coop`.

### LevelChange

The host's level change used to travel as a hidden chat line `@@LC x y z`. It is now `LevelChange{player, destHash, exit[3]}`:
`drakan_netplay` installs the `drakan_levelchange::setExitListener` callback (EE thread, fired at the level-change save with the
exit's world position), looks up the destination name in `drakan_levelchange::exits()` for `destHash` (0 when the exit list does
not know it) and queues it; the network thread sends it when this player leads (co-op authority, otherwise the lowest id). The
server relays it to everyone else; a receiver calls `drakan_levelchange::followExit(exit)` exactly as for the old chat line (the
same exit is fired in its own copy of the level after 120 frames in a level). A guest's level change is not sent (the server would
refuse it). **Follow check** (`drakan_netplay::serviceFollow`, EE thread): on receipt the follower records `destHash`, its own level
hash and the number of level loads so far. After the next playable level load it compares the loaded level's hash with `destHash`:
equal (or `destHash` 0) ends the check ("level follow: arrived"); different is reported (net log `level follow: MISMATCH`, a local
chat line "Level mismatch: ...") and the follower searches `drakan_levelchange::exits()` of the level it landed in for an exit whose
destination name hashes to `destHash` and takes it (up to 3 attempts); with no such exit it gives up (log + chat line). A follower
whose exit never fires (no level load 45 s after the message) fires it again, 3 times at most. There is **no direct load of an
arbitrary level** as a last resort: the engine's level-change entry hangs when called from outside its own transition, so a
destination that is not reachable through the exit lists is reported, not forced. The ghosts stay hidden until the levels match by
themselves (a remote player is shown only while its published level hash equals ours). Test aid `DRAKAN_NET_TEST_LC_DEST=<level
name>` makes a follower expect that level instead (exercises the mismatch path).

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

**The host's level changes pull the clients along** (2026-10-31, [level-change](../journal/2026-10-31/level-change.md)).
`levelHash` alone only hides ghosts; it cannot name the level. When the host's game takes an exit (walked into or fired,
`drakan_levelchange::setExitListener`, read at the level-change save), `drakan_netplay` sends a `LevelChange` message (v6, see
"LevelChange"; before v6 it was a hidden chat line `@@LC x y z`) with the exit trigger's world position. A client that receives it
calls `drakan_levelchange::followExit`, which fires the exit at that position in its own copy of the
level (both load the same level data, so positions match to the float) once the level has run 120 frames; the game then does its
own transition and the ghosts reappear through the `levelHash` gate. Clients' own exits are not sent (co-op "exit = ready-up" is
another design). Limit: a client that is not in the host's previous
level finds no exit at that position and stays (logged `no such exit here`).

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
(damage 4 x blade multiplier; every melee weapon of `net/WeaponTable.h` gets 4 x its own damage, see "Weapons and damage"), empty hand 8, projectiles at most `kMaxProjectileDamage` (40), `damageScale` 3 (the dagger does 4 against a 100 point bar; env
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

### Weapons and damage

`runtime/net/include/net/WeaponTable.h` lists every weapon the player can hold, by resource record id (what
`PlayerState.weaponRight / weaponLeft` carry). It comes from the live weapon records of 2875 local RAM dumps
(`python analysis/scripts/weapon_table.py build`: registry handles of type 10 / 11, record `rec+0xc` property block, see
`docs/reference/world-model.md`), so it holds the game's own numbers, not guesses:

| Class | Record fields used | Table |
| --- | --- | --- |
| Melee `0x1000e` (swords, axes, clubs, hammers, the dagger) | property `0x1a` = the "Damage" line of the inventory (dagger 4, Wood Club 8, Iron Axe 9, Mithril Short Sword 11 ... 28 for the strongest hammer), `0x1e` = stance (0 one handed, 1 two handed, 2 hammer / axe), `0` = name string id | 40 records, damage 1..30. The server accepts 4 x the item damage for a claim in that hand (the blade multiplier is 1.0 .. 2.0 observed). Names resident in the dumps (Rusty / Iron / Steel Short Sword, Rusty / Iron Axe, Wood Club, Iron Club, Rusty / Iron / Steel Scimitar, Rynn's Dagger) show in the kill feed; the rest read "sword" / "two-handed sword" / "hammer". |
| Bows `0x1004f` | properties `0x1a` .. `0x1f` are floats (`0x1a` 0.4 .. 2.0, `0x1e` 40.0), `0x16 / 0x17` arrow count 200; **the damage is the arrow's** ("Wooden Arrows" show Damage 5 in the inventory of every bow, not a bow property) | 20 bow records, ids and names only. A projectile claim is 5 (the starting arrows) and capped at 40. |
| Spells (scroll items `0x100b2`, projectile class `0x10008`) | the dumps carry no damage field for them | A spell projectile claims 10 (`kSpellDamage`, a design value: the real numbers were not decoded), capped at 40 like arrows. Casting was never verified in game here (earlier journals could not cast reliably). |

Ranged hits (`drakan_pvp.cpp` `scanProjectiles`): the engine's projectile code was not traced; instead, each PadMgr
boundary the actor registry is scanned for projectile actors (arrow: type 9, class `0x1005f`, found in the `bow5` /
`audio77` RAM dumps; spell: class `0x10008`) that first appear within 900 units of the local player (the shot; other
arrows are never claimed), and the path each one flew since the last boundary is tested against the vertical capsule of
every ghost (radius 150, from 60 below to 460 above the ghost's origin). A crossing is one claim with `weapon = 2` per
projectile. Run: `python scripts/harness/netplay_two_instances.py --pvp --weapon bow --card build/harness/gw-net/a/card/mc0`
(see the results in `docs/journal/2026-10-31/pvp.md`).

Not done: PvP while mounted, USA disc (PAL addresses; the module reports "no address translation" and stays off), team
deathmatch in game (unit tests only), lag compensation, arrow / spell damage per ammunition type and per spell (the claim
is the starting arrow's 5 / a flat 10), real spell casting verified in game, other players' ping (the protocol has no
ping field, the scoreboard shows "-" for them).

## Match flow and HUD (2026-10-31)

Status: verified in two game instances, menu -> deathmatch -> results -> next match
(`python scripts/harness/netplay_menu_two_instances.py --pvp`, private play folders `build/mp-full-{a,b}`, output
`build/harness/mp_full/{a,b}`). Journal with the evidence and what failed on the way: `docs/journal/2026-10-31/pvp.md`
section 6.

**Data.** The HUD (`mpHudDraw` in `drakan_settings_host.cpp`, drawn by `gameui::mp::drawHud`) reads
`drakan_netplay::scoreboard()`: rows (id, name, team, kills, deaths, health, alive, **ping**, **time**) most kills first,
limits, `timeLeftSec`, own health / `selfAlive` / `respawnInSec`, the results (`over`, `tie`, `winnerId`, `winnerName`,
`winnerTeam`, `reason`) and the kill feed (killer, victim, `weaponName`). `pingMs` is measured for the own player only,
others show "-" (no ping field on the wire); `timeSec` counts from when this client saw the player join.

| Element | Behaviour |
| --- | --- |
| Limits chip (top centre) | "8:40 left   First to 3": the time limit counts down on the client from the server time of the Playing phase, the kill limit is shown while playing. |
| Kill feed (top right) | "A killed B with Rynn's Dagger" / "... with Short Wood Bow" / "B died", the last 5 kills for 7 s, fading; lines with the local player are outlined. The weapon is the killer's held item in the struck hand (`net::weaponDisplayName`). |
| Death overlay | While the local player is dead: dimmed red screen, "You were killed", "Killed by A (Rynn's Dagger)", "Respawning in N" (the server's `respawnDelayMs`, 3 s). Gone at the Respawn event. |
| Scoreboard | **F4** held (Tab is the inventory): server name, mode, level, limits, time left, the table, team totals in team games. |
| Results | When the server enters the Results phase (frag limit, time limit or `endMatch`): banner "Match over - HostRynn wins (frag limit)" ("Draw", "Team Fire wins"), the final standings stay up without F4 held; the server shows them 15 s, then returns to the Lobby phase. |
| Back to the lobby | In the level the banner then reads "Match finished - Esc > Quit to Title returns to the lobby for the next match". Quit to Title (pause menu, Quit, Quit to Title) opens the title menu, which now **resumes on the Multiplayer lobby page** while the session is up (`TitleScreen::resumeMultiplayer`); the host presses Start Match again, every client launches a New Game with the match level as before (scores restart at 0; the level hash of the old match is cleared at Loading so the server waits for the real load). |
| Chat (F6) | F6 opens the line ("Say: "), typed characters go to it (the screen layer takes the window messages while it is open, `g_mpChatOpen`; the game's own input is suspended), Enter sends, Esc cancels; messages show bottom left for 8 s ("name: text", system lines without a name). The F4 / F6 key state of the OS only counts while the window has the focus (`drakan_win::focused()`), so a second instance on the same PC does not react to the other's keys. |

**Time limit in game.** The server enforces it (`MatchServer::tick`: `now - phaseStart >= timeLimitSec` ends the match with
reason `TimeLimit`); the menu offers 5 .. 60 minutes. For tests `DRAKAN_MP_TIME_LIMIT_SEC` / `DRAKAN_MP_FRAG_LIMIT` override
the host's limits (`--time-limit-sec N --frag-limit 0` in the harness).

**Typing verification.** The game's input script has DirectInput-style key names only: no F4 / F6 and no characters. The
harness therefore sends real keyboard input with `SendInput` (`scripts/harness/winkeys.py`: the window is focused with
the AttachThreadInput trick, F-keys as virtual keys with scan codes, text as `KEYEVENTF_UNICODE` events = one `WM_CHAR`
each) from a thread that waits for the games' log lines. Messages typed on B arrive in A's log (`chat: <JoinRynn> ...`)
and the other way round. The keyboard is shared with whoever uses the PC: the harness checks that each message arrived
and retries. `DRAKAN_NET_CHAT_DEBUG=1` logs `[chat] input= chars= enter=` per typed character.
Text fields of the title pages use the same char queue (`UiInput::text`).

**Launch taps.** The New Game intro movie (screen `0x1d`) needs START / Cross taps, then the cut scene level behind it
needs a START tap once it has been playing for 6 s (about 10 s from the press to the playable level). Taps during the
loading screens around the movie opened the pause menu instead, a second tap closed it again and the intro never ended
(a 120 s load timeout in several two-instance runs); without any tap the cut scene never ends either. See
`drakan_input_glue.cpp` `hookPadRead`.

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
| Host Game | Server Name, Password (blank = open), Level Group (All Levels / Disc levels / Custom levels), Starting Level (14 disc levels, then the levels `ModManager::extraLevels()` reports, marked "(custom)"), Game Type (Deathmatch or Co-op, v6: co-op greys the limits and teams and hosts a `MatchMode::Coop` match), Maximum Number of Players 2..12, Kills Limit (No Limit, 5 .. 200), Time Limit (No Limit, 5 .. 45 minutes, 60), Number of Teams (No Teams, 2; 3 and 4 shown "(coming)" and not startable: the server has two teams), Friendly Fire? and Register With Master Servers? (greyed placeholders), Begin Game, Back. OotF takes the mode from the level type; here Game Type = Co-op means co-op, else teams >= 2 means team deathmatch, otherwise deathmatch (Dragon's Duel / Master of the Dragon are not implemented). |
| Join Game | the server table Server Name / Current Level / Players / Ping sorted by ping (blue = LAN, white "+" = Add Server entry, green = selected, "[password]" marker), Refresh, Add Server (address entry; OK keeps it for this session), Server Info (IP, level, players, mode, password card), Password, Connect; a list row also joins. No Stop / Remove / master server / version-colour rows yet. |
| Lobby | server / mode / map / limits, the roster (ready flags, team names Fire / Water, `[host]`), Ready, Start Match (host), Leave; a Loading card while the match starts. Team select (four teams) is not shown: the server assigns teams. |
| In game | see "Match flow and HUD": **F4** held = scoreboard (Player / Kills / Deaths / Ping / Time), kill feed, death overlay with the respawn countdown, limits chip with the time left, **F6** = chat, results banner with the final standings, then the lobby page again after Quit to Title. OotF binds the scoreboard to Tab; here Tab is the inventory, hence F4. |

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
The whole flow (host with a 3 kill limit, join from the LAN list, fight with chat / F4 / kill feed, results, back to the
lobby, next match): `python scripts/harness/netplay_menu_two_instances.py --pvp` (about 6 minutes; `--time-limit-sec 50
--frag-limit 0` ends the match by time instead). Ranged: `python scripts/harness/netplay_two_instances.py --pvp --weapon
bow --card build/harness/gw-net/a/card/mc0 --port 47920`.

## Multiplayer maps (arena world mode, 2026-10-31)

Drakan: Order of the Flame had dedicated multiplayer maps: no story, no AI creatures, player spawn points, weapon and item
pickups that come back, no saving. Until now a PvP match here was a New Game on a campaign level with everything still in
it. Deathmatch and team deathmatch now run in the **arena world mode**; co-op keeps the campaign world (`MatchMode::Coop`
never activates it). Code: `ps2xRuntime/src/lib/drakan_arena.{h,cpp}` (game side), `runtime/net/include/net/ArenaMaps.h`
(map format, std only), `MatchServer` arena options, protocol messages `PickupClaim` / `PickupState`. Evidence and what
failed on the way: `docs/journal/2026-10-31/mp-arenas.md`.

### Straight into the map

A menu launched deathmatch no longer goes through the intro: `drakan_netplay` redirects **`Level 0 - Intro`** (the level
New Game opens: movie player, cut scene, the transition to Surdana) to the map's base level in the file layer
(`hle::FileService::setLevelRedirect`, the `DRAKAN_START_LEVEL` mechanism with `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`), so
the New Game from the title loads the arena level itself: no movie, no cut scene, **no Start / Cross taps** (`launchTaps()`
stays false for these launches; the load took about 5 s on the dev PC instead of ~10 s plus the intro). The lifecycle hooks
still report `Level 0 - Intro`; `drakan_netplay` substitutes the real level name for the hash exactly as it did for
Surdana. Co-op keeps the old route (redirect of `Level 1A - Surdana`, intro, taps). The redirect is cleared when the match
goes back to the Lobby phase, so a New Game from the title in between is an ordinary one.

### What counts as singleplayer only (removed)

`drakan_arena::beforePadMgr`, once per load (12 boundaries after the player is ready, `drakan_ghost::ready()`), then every
20 boundaries for 7 s and every 120 boundaries after that (stragglers: a spawner that runs later), destroys through the
engine's own entry `0x1413c0` (queued kill, flushed in the frame tail, what co-op and the ghosts already use):

| What | How it is recognised | Why |
| --- | --- | --- |
| Creatures and NPCs | every handle with creature hit points (`world::HpKind::Creature`: types 6 / 7, `*(q+0x23c) == handle`): Surdana 137 of them (guards, orcs, scavengers, spiders, skeleton dragons, wartock shamans, villagers, ballistae, catapults) | no AI in an arena; they would also chase only the local Rynn |
| Cut scene player | class `0x10033` ("Stomp Player", 18 in Surdana) | a cut scene takes the controlled actor and the camera |
| Conversation handlers | class `0x100b4` (52 in Surdana) | dialogue |
| Movie player | class `0x10016` (the intro level) | |
| Level exits | class `0x10037` ("Level Transition", "Portal", 5..11 per level) | no level change, no level change save |
| Quest items | class `0x10021` (runes, amulets, journals, keys, black powder) | story objects |
| Treasure | class `0x1006d` (gold sacks, goblets, chalices) | no economy |
| Chests | class `0x100c5` | loot that is not replicated |
| Loose weapons and items | types 10 / 11 (swords, bows, potions, power ups, scrolls) not parked near the world origin, **during the first 420 boundaries only**, except the ones within 2500 units of where the player stood when the level came up (her own gear: the dagger lies at her start) and the ones at a map pickup | the arena's pickups are the map's list |

Kept on purpose: Arokh (class `0x1001c`, type 3: Rynn's init links to him; a level where Rynn starts mounted, Kragmor, still
hides the other player's ghost), timers and trigger detectors (`0x1003e`, `0x1003d`: message logic and doors; with the story
objects gone they only message nothing; a creature a trigger would spawn is caught by the creature sweep), doors, scenery,
sounds, lights, Air Currents, Dragon Barriers.

Also in arena mode: **saving is vetoed** (`drakan_lifecycle::setSaveWriteVeto`: the save writer `0x169c28` returns 1 at once, so
no menu save, quick save or level change save can write; quick save / load keys log "ignored", the pause menu's Save / Load rows
show disabled), the **journal and the map markers stay empty** (the tab and pause screens clear `snapshot.quests`), and an exit
cannot be taken because there is none. The player's own `mc0` is never touched by a match.

### Spawn points and respawn

The server owns the choice (`ServerOptions::arena`, `MatchServer::chooseSpawn`): a (re)spawn takes the spawn point **farthest
from every other living player** (their last published position, or the spawn point they were just given), never the one of
the previous life while another exists. `Respawn.spawnIndex` is that index into the map's `spawn` list (v6 wire format
unchanged; without a map it is the old counter). The client (`drakan_pvp` -> `drakan_arena::placeAtSpawn`) moves the player
there (y + 20, refiled in the collision cells like a ghost) at the match start and after every death; with no map the
walked-track crumbs of PvP are still used. A far teleport can fail the first time (the engine puts the player back where she was while the floor at the target is not resident yet): the placement is watched and repeated every 40 boundaries up to 6 times (`[arena] placement of spawn N took after K tries`; measured 1..5 tries across 46k units). A player outside the map's `bounds` for 4 s, or fallen 6000 units below the lowest spawn point (maps on a cliff top), is put back at a spawn point. The `yaw`
column of a spawn is stored but not applied (the player's heading is the camera's).

### Pickups (host authoritative)

The map's pickup list is the same on every machine, a pickup is its index. Two kinds, both turned into `drakan_arena::Pick`s once
the level is up: **adopted** (no `res=`: the level's own loose item nearest to the position, within 600 units, becomes the
pickup; its position is its home) and **spawned** (`res=0x...`: the resource is created through the engine's factory `0x181ad8`
posted to a guest dispatcher thread, like the ghost's weapons, and found again in the registry at the spot). Per pickup the
client watches its actor: gone from its home (the engine moved it into an inventory) without the server having said so is a
**claim** (`PickupClaim{index}`, reliable); the server (`MatchServer::onPickupClaim`) refuses a claim when not Playing, the
player is dead or farther than `pickupClaimRange` (4000) from it, or it is already taken; otherwise it marks the pickup taken
for its `respawnSec` and broadcasts `PickupState{index, taken, player}`. Every other client **destroys its own copy**; when
the time is up the server broadcasts `taken = 0` and every client spawns a fresh copy at the home position. A new Playing
phase broadcasts `index = kAllPickups` (everything available), and a late joiner is sent the taken ones. The two messages
(23 and 24) are a separate block at the end of the v6 enum; peers that do not know them drop them.

### Map definitions

`configs/mp_maps.ini` (committed, compiled into the game by `runtime/net/CMakeLists.txt` as the default list, no game data
beyond numbers and names), the player's own **`mp_maps.ini` next to the exe** (or the file named by `DRAKAN_MP_MAPS`) and the
**`maps/*.ini`** of mod packs (`ModManager::mapFiles()`; a pack can also ship a custom level with `levels = [...]` and
`files/...` and a map on top of it) use one format, an arena id defined again replaces the earlier one:

```ini
[arena surdana-meadow]                  ; "arena " + a short unique id
name = Surdana Meadow                   ; display name, also MatchConfig.level on the wire (<= 64 chars)
level = Level 1A - Surdana               ; base level (LEVELLST title: disc level or mod pack level)
description = ...
modes = dm, tdm                          ; supported modes
players = 8                              ; 2..16
health = 100                             ; start health bar in server points
bounds = 398000 543000 432000 576000     ; playable rectangle x0 z0 x1 z1
loadout = 0x3372b                        ; resource ids every player gets at the start (optional)
spawn = 412221 173800 556494 [yawDeg]    ; at least 2, at most 32
pickup = 412921 173800 556494 30 res=0x3372b name=Steel Club   ; x y z respawnSec [res=] [name=], at most 64
```

`net::ArenaCatalog::add(text, source, &errors)` parses it (errors name `source:line`, an invalid arena is dropped as a whole);
`find(idOrName)`. The menu reads the list through **`drakan_arena::maps()`**: `MapEntry{id, name, level, description, source,
deathmatch, teamDeathmatch, maxPlayers, spawns, pickups, hasBounds, bounds[4], installed}` (`installed` = the base level exists on
this disc or in a loaded pack). Hosting a map: put `MapEntry::name` (or the id) into `HostConfig.level`; `drakan_netplay` resolves
it (`drakan_arena::findMap`), gives the server the arena (`ServerOptions::hasArena` / `arena`, start health, player cap),
and the clients resolve `MatchConfig.level` themselves; a client that does not have the map or its base level refuses the launch
with "Level files mismatch". `drakan_server --level "<map name or id>" [--maps file.ini]` does the same headless.

Authoring: `analysis/scripts/arena_candidates.py` proposes spawn / pickup lines from the decoded level (`lvl_objects.py`:
ground decor and loose items inside a circle); `python scripts/harness/arena_tour.py --map <id>` loads the map in one instance
(`DRAKAN_ARENA_FORCE`), visits every spawn point (`DRAKAN_ARENA_TOUR=<boundaries per stop>`, logs where the player stands
afterwards, screenshots) and dumps the world.

Test aids: `DRAKAN_ARENA_FORCE=<map>` (arena mode without a session), `DRAKAN_ARENA_TOUR=<n>`, `DRAKAN_ARENA_LOG=0`,
`DRAKAN_MP_MAP=<map>` (the host uses this map whatever the menu chose), `DRAKAN_MP_MAPS=<file>` (extra map file).

### Authoring keys (spawn team, marker, keep/remove)

Added to the arena format for the level editor's multiplayer map mode (`ArenaMaps.h`, tests `net_modes_test`). All are
parsed and exposed on `ArenaDef`; the status column says what the game does with them.

| Key | Meaning | Game today |
| --- | --- | --- |
| `spawn = x y z [yaw] [team=1..4]` | team spawn (0 / omitted = any team) | position used; yaw and team stored only |
| `marker = base\|flag\|zone x y z [radius=R] [team=N] [name=Label]` | objective positions (radius default 600 base / zone, 150 flag; at most 32) | authored only |
| `keep = x y z`, `remove = x y z` | override the arena world filter for the object at that world position | authored only (`drakan_arena` does not read them) |
| `modes = dm, tdm, <id>` | an id other than dm / tdm names a `[mode <id>]` (`ArenaDef::customModes`) | stored only |

### Custom game modes ([mode] descriptor)

`[mode <id>]` sections live in the same INI text as the arenas (built-in `configs/mp_maps.ini`, the player's `mp_maps.ini`,
mod packs' `maps/*.ini`); `ArenaCatalog` skips them, `ModeCatalog` (`net/GameModes.h`, `GameModes.cpp`) parses them
(later sources replace an id, an invalid mode is dropped with a `source:line` error). The game fills
`drakan_modes::customModes()` (`drakan_modes.h`) from the same three sources, so a menu can list them.

| Key | Default | Range / values |
| --- | --- | --- |
| `name`, `description` | required name | name <= 40 characters |
| `base` | `dm` | `dm` or `tdm`: the engine rule set it builds on |
| `teams` | 0 (2 with tdm) | 0, or 2..4 (needs `base = tdm`) |
| `score_kill` / `score_death` / `score_objective` | 1 / 0 / 0 | -1000..1000 |
| `frag_limit` | 20 | 0 none .. 1000 |
| `time_limit` | 600 | seconds, 0 none .. 7200 |
| `respawn_delay` | 3 | seconds 0..60 |
| `friendly_fire` | off | on / off |
| `start_health` | 0 (the map's) | 0..1000 |
| `loadout` / `weapons` | empty | resource ids (<= 16); loadout is added to the map's, weapons empty = all |
| `objective` (repeatable, <= 8) | | `capture` or `hold`, `[points=N] [seconds=N] [radius=N]`; hold needs `seconds` |

`capture` = carry a `flag` marker to the team's `base` marker; `hold` = stay inside a `zone` marker for `seconds`.
Example: `instagib-lite` in `configs/mp_maps.ini`.

| Field | Honoured by the runtime today? |
| --- | --- |
| `base`, `frag_limit`, `time_limit`, `respawn_delay`, `start_health` | yes, through `applyModeToConfig(mode, MatchConfigData)` into the match config (the server already has these options); nothing calls it from a menu yet |
| arena `players`, `health`, `spawn` positions, `pickup`, `loadout` | yes (the arena mode) |
| `score_*`, `teams` beyond tdm's two, `friendly_fire`, `weapons`, mode `loadout`, `objective`, markers, spawn `team` / yaw, `keep` / `remove` | authored only: parsed, listed, not executed |
| the menu | lists maps from `drakan_arena::maps()`; custom modes are available from `drakan_modes::customModes()` but no menu row shows them yet |

Map pictures from mod packs: `previews/<safe map name>.png` in a pack (<= 8 MiB) is copied to `previews/mods/` next to the
exe at startup; the menu looks at `previews/<name>.png` first, then `previews/mods/<name>.png`.

### Authoring in the level editor (Multiplayer Map)

The level editor's **Multiplayer Map** dock tab (`editor/packages/app/src/editor/arena/`, pure logic in
`editor-core/src/{arena,gamemodes,arenapack}.ts`) writes exactly this format, so a map no longer has to be typed or taken
from `arena_candidates.py`.

* **Start**: pick any campaign level (its terrain loads) or import an `mp_maps.ini` / pack `maps/*.ini` and edit one of its
  arenas; the draft is kept in the browser. Map tab: name, id, description, supported modes (dm, tdm and your custom modes),
  max players, start health, loadout (item picker with the Resources thumbnails), bounds (typed, drawn on the terrain, or
  moved with the two corner handles; the rectangle is drawn draped over the terrain).
* **Place tab**: spawn point, pickup, flag, base and zone tools (click the terrain; positions snap to the editor grid and sit on
  the ground), a move / rotate gizmo (yaw uses the angle snap), per item fields (position, team, facing, item, respawn
  seconds, radius), "Suggest spawns" (spread out points on flat ground inside the bounds, up to the player count) and
  "Alternate teams". Every edit is one History row (Ctrl+Z).
* **World tab**: the preview of the class filter above. Red dots in the viewport are what `drakan_arena` removes (creatures,
  cut scene players, conversation handlers, movie players, exits, quest items, treasure, chests, loose items away from the
  player start and from adopted pickups), counted per class; each object can be kept (`keep = x y z`) or marked for
  removal (`remove = x y z`). The editor guesses creatures and loose items from class names, the game decides by hit points
  and entity type, so the counts can differ slightly. `keep` / `remove` are authored only (see the table above).
* **Modes tab**: create, copy and edit `[mode]` descriptors (score values, limits, respawn delay, friendly fire, loadout,
  allowed weapons, capture / hold objectives); a table next to the form says what the game runs today. Modes are
  stored in this browser and written, together with the arena, into every pack that uses them.
* **Export tab**: pack id / name / version / author, a top-down preview capture (orthographic render of the terrain over the
  bounds with spawns and pickups drawn on it, saved as `previews/<safe map name>.png`), then download a zip, write
  into the game's `mods/` folder (browser folder picker, or straight to the remembered game folder in the desktop app) or
  download the map INI alone. The pack holds `mod.toml`, `maps/<id>.ini` and the preview; no level file is needed
  because the filter runs in the game.
* **Validation** (the Validation tab and a strip in the panel, errors block the export): id / name / level / player count,
  at least 2 spawns, spawns fewer than players (warning), spawns and pickups outside the bounds, over no terrain or floating
  above the ground, spawns closer than 400 units, a team without a spawn point, adopted pickups with no level item
  within 600 units, pickups that cannot be reached on foot from the spawns (coarse flood fill over the terrain, steps over 450
  units block; jumping is not modelled), objectives without the markers they need, unknown mode ids.

### Starting loadout, health, limits found

* A New Game that opens a level other than the first has **no weapon**: the dagger is an object of Surdana's list, not part of the game state. `loadout` spawns the listed resources at the player's feet once the campaign sweep is over and presses the game's Use (L1) through the input glue (`takeUseTap`) while one lies within reach (Use with nothing near calls Arokh, so it is never pressed blindly); the items land in the inventory, equipping stays the player's. Start health is `health` (server points, scaled to Rynn's maximum by `drakan_pvp`).
* Ghosts of players 46k units away show and move normally (3 ranges tested, 10k / 21k / 36k / 46k); the limit is the floor residency of a *teleport*, handled by the retry above.
* A pickup is taken with Use, walking over it does nothing.

Not done: spawn yaw, team spawn sets, a menu row (the menu agent reads `drakan_arena::maps()`), loadout auto-equip, arena maps for levels where Rynn starts mounted, the USA disc (PAL addresses).

## Tests

`ctest --preset runtime-tests -R net_`. `net_coop_test` covers the v6 relay rules (authority, routing classes, targets, range
checks, no PvP / limits in Coop), the authority's lifecycle (late joiner, leaving, empty match, mode switch in the lobby), host migration (the oldest remaining player
takes over, also over 40 ms / 15 percent loss with the old owner leaving, its disconnect possibly lost so the timeout covers it: the announcement arrives once, events and claims
continue exactly once and in order, a late joiner after the migration, a chain of three migrations with a recycled player id),
LevelChange (Coop and deathmatch leaders, guests refused, bad coordinates), a v5 peer rejected with `VersionMismatch`, and a
co-op session over 40 ms latency with 15 percent unreliable loss (reliable events exactly once and in order, creature slices
arriving in part); the encode / decode round trips of the new messages are in `net_protocol_test`. In game:
`scripts/harness/coop_two_instances.py` (all scenarios) and `netplay_two_instances.py` (level follow), see the 2026-10-31
journal `coop-net.md`. Everything is deterministic except the UDP smoke test, which uses localhost
sockets with wall-clock time.
