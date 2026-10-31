# Netcode: protocol v4, authority model, `runtime/net`

Status 2026-10-31: the protocol, match state machine, transports and the headless `drakan_server` exist and are tested;
the game side is a first spike (ghosts only, see "Playing over the network (spike)"; plan in
`docs/proposals/level-editor-and-multiplayer.md`). Rationale for the model: `docs/journal/2026-10-31/multiplayer.md`.

## Layout

| Path | What |
| --- | --- |
| `runtime/net/include/net/Transport.h` | Abstract transport: `listen`, `connect`, `send` on `Channel::Reliable` (ordered, exactly once) or `Channel::Unreliable`, `poll` events (`Connected`, `Disconnected`, `Message`), opaque `PeerId`s. Messages <= `kMaxPacketPayload` (1100 bytes). |
| `net/Protocol.h` | Messages, `ByteWriter` / bounds-checked `ByteReader`, `encode<T>` / `decode<T>`. Explicit little-endian; `f32` is IEEE bits; non-finite floats fail the reader. |
| `net/MatchServer.h` | Authoritative match: roster, phases, damage, score, respawn. `tick(nowMs)` with an explicit clock (deterministic in tests). |
| `net/MatchClient.h` | Handshake, roster, per-player snapshot buffer with interpolation, event queue (`std::variant`). |
| `net/LoopbackTransport.h` | In-process; `LoopbackNetwork` has a virtual clock, optional one-way latency and unreliable loss. |
| `net/UdpTransport.h` | IPv4 UDP, non-blocking. Winsock on Windows, BSD sockets elsewhere (kept out of headers). Not built for Emscripten. |
| `runtime/net/server/` | `drakan_server --port --max-players --mode dm\|tdm --level` (also `--frag-limit --time-limit --auto-start --tick-rate --profile-hash`). No game, no ISO. |
| `runtime/net/tests/` | `net_protocol_test`, `net_match_test`, `net_loopback_e2e_test`, `net_udp_smoke_test` (exit 77 = skipped when sockets are unavailable). |

A browser build needs a WebSocket / WebRTC `Transport` in `src/`; nothing above `Transport` may rely on more than the
two channel guarantees. Unwritten.

## UDP wire layer

Datagram = `'D' 'K'` + type byte. Types: Connect, ConnectAck, Disconnect (empty), Unreliable (payload), Reliable
(`seq u32` + payload), Ack (`next expected seq u32`, cumulative, doubles as keep-alive every 1 s). Reliable channel:
per-peer sequence, receiver accepts only the next sequence, sender resends unacked packets every 150 ms (window 128,
excess queued). Connect retried every 250 ms for 5 s; a peer silent for 10 s is dropped.

## Protocol v4

Application message = 1 type byte (`MsgType`) + body; every decode requires the exact length. Integers little-endian.
`str` = `u16` length + bytes (per-field maximum enforced). Reliable unless marked.

| Type | Dir | Body |
| --- | --- | --- |
| 1 Hello | C>S | `u16 protocolVersion`, `u64 profileHash`, `str name` (<=24) |
| 2 Welcome | S>C | `u8 player`, `u8 tickRateHz`, `u8 phase`, `u32 serverTimeMs`, MatchConfig body |
| 3 Reject | S>C | `u8 reason` (0 version, 1 profile, 2 full, 3 name), `u16 serverVersion`, `str message` |
| 4 MatchConfig | S>C | `u8 mode` (0 DM, 1 TDM), `str level` (<=64), `u16 fragLimit`, `u16 timeLimitSec`, `u8 maxPlayers` (1..16), `u16 respawnDelayMs`, `u16 startHealth` |
| 5 PhaseChange | S>C | `u8 phase` (0 lobby, 1 loading, 2 playing, 3 results), `u32 serverTimeMs` |
| 6 PlayerState | C>S, unreliable | `u8 player`, `u32 sequence`, `u32 tick`, `f32 pos[3]`, `f32 yaw`, `f32 vel[3]`, `u16 animId`, `f32 animTime` (lower body), `u16 upperAnimId`, `f32 upperAnimTime`, `u32 lowerMask` (bit i: skeleton channel i plays the lower clip; see `world-model.md` section 6), `u32 weaponRight`, `u32 weaponLeft` (resource record ids of the held items, 0 = empty hand; v4), `u8 mounted` (0/1), `u32 levelHash` (FNV-1a of the sender's level name, 0 = loading; v3); 66 bytes |
| 7 Snapshot | S>C, unreliable | `u32 serverTimeMs`, `u8 count` (<=16), count x PlayerState body (live players with a known state) |
| 8 Hit | C>S claim, S>C accepted | `u8 attacker`, `u8 victim`, `u8 weapon`, `u16 damage`, `u16 healthAfter` (client sends attacker 0) |
| 9 Death | S>C | `u8 victim`, `u8 killer`, `u8 weapon` |
| 10 Respawn | S>C | `u8 player`, `u8 spawnIndex` (server rolling counter; client maps to a level spawn point), `u16 health` |
| 11 Chat | both | `u8 player` (server fills), `str text` (<=128, control chars stripped) |
| 12 Score | S>C | `u8 player`, `u16 kills`, `u16 deaths`, `u16 teamKills[2]` |
| 13 PlayerJoin | S>C | `u8 player`, `u8 team` (0/1 or 255 none), `str name` |
| 14 PlayerLeave | S>C | `u8 player` |
| 15 LoadDone | C>S | empty |

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
  victim is another player, `1 <= damage <= maxDamagePerHit`, victim not under respawn protection, no friendly fire in
  team deathmatch, attacker under the per-attacker rate limit (`minHitIntervalMs`), and attacker and victim last
  accepted positions are within `maxHitRange`. Accepted hits are broadcast with the resulting health; at 0 health the
  server broadcasts Death and two Scores, then Respawn after `respawnDelayMs`.
- Movement checks: stale sequence numbers are dropped, non-finite or huge coordinates and speeds above `maxSpeed`
  (units/s between accepted states, default 30000; Rynn runs ~2500) are discarded. The first state after (re)spawn is exempt. A dropped teleport
  heals itself as the elapsed time grows.
- Remote players: the client buffers snapshot states per player and renders `interpolationDelayMs` (100) behind the
  estimated server clock, interpolating position, velocity, yaw (shortest arc) and animation time; discrete fields come
  from the nearer sample; no extrapolation. A Death or Respawn clears that player's buffer.
- Known limits: the hit range check trusts both clients' positions; there is no lag compensation, no encryption or
  authentication, no NAT traversal, and the sanity limits are untuned guesses until real game units are known.

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

Not done: no damage or
hits, mounted (dragon) players are hidden, no sector update for the ghost.

## Tests

`ctest --preset runtime-tests -R net_`. Everything is deterministic except the UDP smoke test, which uses localhost
sockets with wall-clock time.
