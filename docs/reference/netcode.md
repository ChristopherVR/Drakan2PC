# Netcode: protocol v2, authority model, `runtime/net`

Status 2026-10-31: scaffold. The protocol, match state machine, transports and the headless `drakan_server` exist and
are tested; nothing is wired into the game yet (that needs the native world view, see
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

## Protocol v2

Application message = 1 type byte (`MsgType`) + body; every decode requires the exact length. Integers little-endian.
`str` = `u16` length + bytes (per-field maximum enforced). Reliable unless marked.

| Type | Dir | Body |
| --- | --- | --- |
| 1 Hello | C>S | `u16 protocolVersion`, `u64 profileHash`, `str name` (<=24) |
| 2 Welcome | S>C | `u8 player`, `u8 tickRateHz`, `u8 phase`, `u32 serverTimeMs`, MatchConfig body |
| 3 Reject | S>C | `u8 reason` (0 version, 1 profile, 2 full, 3 name), `u16 serverVersion`, `str message` |
| 4 MatchConfig | S>C | `u8 mode` (0 DM, 1 TDM), `str level` (<=64), `u16 fragLimit`, `u16 timeLimitSec`, `u8 maxPlayers` (1..16), `u16 respawnDelayMs`, `u16 startHealth` |
| 5 PhaseChange | S>C | `u8 phase` (0 lobby, 1 loading, 2 playing, 3 results), `u32 serverTimeMs` |
| 6 PlayerState | C>S, unreliable | `u8 player`, `u32 sequence`, `u32 tick`, `f32 pos[3]`, `f32 yaw`, `f32 vel[3]`, `u16 animId`, `f32 animTime` (lower body), `u16 upperAnimId`, `f32 upperAnimTime`, `u32 lowerMask` (bit i: skeleton channel i plays the lower clip; see `world-model.md` section 6), `u8 weapon`, `u8 mounted` (0/1); 55 bytes (v2) |
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
  (units/s between accepted states) are discarded. The first state after (re)spawn is exempt. A dropped teleport
  heals itself as the elapsed time grows.
- Remote players: the client buffers snapshot states per player and renders `interpolationDelayMs` (100) behind the
  estimated server clock, interpolating position, velocity, yaw (shortest arc) and animation time; discrete fields come
  from the nearer sample; no extrapolation. A Death or Respawn clears that player's buffer.
- Known limits: the hit range check trusts both clients' positions; there is no lag compensation, no encryption or
  authentication, no NAT traversal, and the sanity limits are untuned guesses until real game units are known.

## Tests

`ctest --preset runtime-tests -R net_`. Everything is deterministic except the UDP smoke test, which uses localhost
sockets with wall-clock time.
