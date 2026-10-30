# Level editor (Three.js) and multiplayer layer

Status: proposal, 2026-10-31. Background: `docs/journal/2026-10-31/` (asset-format-census, level-editor,
engine-reimplementation, multiplayer, save-editor).

## Decisions

| Topic | Decision |
| --- | --- |
| Reverse engineering | Python dump tools under `analysis/scripts/` (`lvl_dump.py`, `odb_dump.py`, ...) write to `build/`. |
| Editor stack | TypeScript + Vite + three.js in a new top-level `editor/` workspace. |
| Editor data source | The player's own ISO, parsed in the browser (ISO9660 → SRSC → formats). Nothing is uploaded and nothing ships in the repo, so the editor can be hosted on GitHub Pages. |
| Edits | Written as SRSC entry overrides in the runtime's existing `overrides/<FILE>/<type>_<id>_<sub>.bin` layout (`DRAKAN_OVERRIDES`), exported as a mod pack (zip + manifest pinned to a disc profile hash). The ISO is never modified. |
| Multiplayer | Listen server (host from the game) and a headless dedicated server, both built from the same C++ code. Join by address or invite code. A public server browser (master server) comes later. |

## Editor architecture

```
editor/
  package.json            workspace root (npm workspaces), vitest, eslint/prettier
  packages/formats/       pure TS, no DOM: Iso9660, Srsc, Txd, Xdb, LevelList, later Lvl/Odb/Mod
                          runs in the browser, in Node (CLI / tests) and in a worker
  packages/app/           Vite + three.js: ISO picker, level browser, resource inspector, 3D viewport
```

- `formats` is the single JS implementation of each format. Python scripts stay the research tools; when a
  layout is understood, it is written up in `docs/reference/` and ported to `formats` with tests.
- Tests use synthetic fixtures built by the test code itself. Never commit real game bytes.
- Phases:
  1. **Inspector** — open ISO, list levels from `LEVELLST.TXT`, browse each file's SRSC entries (type/id/sub/size,
     zlib detection, hex view), show TXD textures.
  2. **Viewer** — render level geometry and entity markers once `.LVL`/`.ODB`/`.MOD` are decoded. Interim
     option: load geometry exported from a runtime GS capture (`docs/reference/gs-capture.md`) as glTF, so the
     viewport and camera work can proceed before the formats are decoded.
  3. **Editor** — select/move/add/delete entities, edit properties via `.ODB` schemas, export overrides.
  4. **Geometry/terrain editing** — after the derived data (collision, VU1 packets, lighting) is understood.
- Round-trip gate: `formats` must re-serialise every untouched entry byte-identically before an editor feature
  writes it.

## Multiplayer architecture

The recompiled game is not deterministic (`multiplayer.md`), so lockstep/rollback is out. Each client runs its own
world. The server owns **match state** (players, teams, scores, health, respawns, damage resolution); clients are
authoritative for their own movement, which the server sanity-checks. Remote players appear in each world as
kinematic proxies created through the game's own actor factory.

```
runtime/net/              drakan::net (std-only public headers)
  include/net/Transport.h     unreliable + reliable channels, connect/accept, poll
  include/net/Protocol.h      versioned binary messages + serialiser
  include/net/MatchServer.h   lobby → loading → playing → results state machine, modes, scoring
  include/net/MatchClient.h   connect, snapshot buffer, interpolation, event queue
  src/                        UDP (Winsock/BSD), loopback; later ENet, WebSocket/WebRTC for the WASM build
  server/                     drakan_server: headless dedicated server (no game, no ISO)
```

- **Listen server** = `MatchServer` running inside the game process next to a local `MatchClient`.
- **Dedicated server** = the same `MatchServer` in `drakan_server`. It never runs the game simulation, so it needs
  no ISO; it validates the level name and disc profile hash that clients report.
- **Protocol v1**: `Hello/Welcome/Reject` (protocol version, disc profile hash, player name), `MatchConfig` (mode,
  level, limits), `PlayerState` (20 Hz: position, yaw, velocity, animation id/time, weapon, mounted dragon),
  reliable events (`Hit`, `Death`, `Respawn`, `Chat`, `Score`), `MatchPhase`.
- **Modes** (OotF-inspired): deathmatch, team deathmatch, dragon combat, co-op later (needs shared AI/world state).
- **Transports**: UDP on native first; WebSocket for the browser build (the server listens on both). WebRTC data
  channels later if browser latency needs them.
- **Game integration** needs the native world view from `engine-reimplementation.md` phase 1 (`runtime/world`):
  find the local player, read pose/animation, spawn and drive proxy actors, apply damage. This is the critical
  path; `runtime/net` and `drakan_server` can be built and tested without it.

## First scaffold (parallel)

1. `editor/`: workspace, `formats` (Iso9660, Srsc, Txd, level list) with synthetic-fixture tests, `app` inspector.
2. `analysis/scripts/lvl_dump.py` / `odb_dump.py`: per-level SRSC entry dumps, type histograms, zlib/float-run
   heuristics into `build/lvl-dump/`.
3. `runtime/net` + `drakan_server`: protocol, match state machine, loopback + UDP transports, ctest coverage, a
   loopback "two clients, one server" test.

Next after the scaffold: `runtime/world` entity introspection (F1 overlay), then a two-instance ghost-player spike.
