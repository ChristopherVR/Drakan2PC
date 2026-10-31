# Netplay spike: two game instances see each other's Rynn - 2026-10-31

Goal: first networked spike on top of `drakan::net`, `world::readPlayerPose` / `PoseBuffer`, `drakan_ghost` and
`drakan_lifecycle`. How to run it: `docs/reference/netcode.md`, "Playing over the network (spike)". Code:
`ps2xRuntime/src/lib/drakan_netplay.{h,cpp}`, harness `scripts/harness/netplay_two_instances.py`. Tags: **[game]**
measured in two real instances on one PC (PAL disc, Release build, D3D11), **[test]** unit tests.

## 1. Result

**[game]** `DRAKAN_GHOST=1 DRAKAN_NET_HOST=47800` (A) and `DRAKAN_NET_CONNECT=127.0.0.1:47800` (B), both New Game into
Surdana, on private play folders (`build/netplay-{a,b}`) and private memory cards:

* Both connect (handshake, `Playing` at once with `autoStartPlayers = 1`), each spawns a ghost of the other about 1.5 s
  after its own Surdana load. A walks and strafes by recipe; B's screenshots (`b_before_move`, `b_a_walking`,
  `b_a_strafing`, `b_a_stopped`) show A's ghost standing beside B first, then walking away across the meadow. The ghost
  moves and turns with A; it has no animation yet (position and yaw only, `setTransform`), so it slides in its idle pose.
* A quick-saves (F5) and quick-loads (F8) in Surdana. A's own ghost of B is destroyed by `drakan_ghost` around the save
  write and respawned after it. B logs `ghost of player 1 removed (player in another level)` about 0.5 s into A's load
  (A publishes `levelHash = 0` from `beforeSaveRestore` / `beforeLevelLoad`) and `... spawned` again about 1 s after A is
  back. No crash, both instances exit through their scripted `quit`.
* `drakan_server --auto-start 1` instead of the embedded server (`--mode server`): same result.
* Disc profile mismatch: `drakan_server --profile-hash 1` + one game: `REJECTED by the server: reason 1, 'disc profile
  mismatch'`; the game keeps running without networking (`--reject`).
* **Not verified**: a move to a genuinely different level. `DRAKAN_START_LEVEL` redirects at the file layer, so the engine
  and the lifecycle hooks still report "Level 1A - Surdana" even when Kragmor is loaded (also why `ghost-spike-s3.md`
  could not stage 4A -> Surdana), and no level exit is scripted. The level gate is `PlayerState.levelHash` equality and
  is exercised only by the reload above (hash 0 while loading, then the same hash). A real level exit is the next test.

Network numbers **[game]**, localhost, two games rendering on one GPU (so the frame times are noisy):

| | A (host, client of its own server) | B (client) |
| --- | --- | --- |
| states sent | 18-19 /s (target 20) | 18-19 /s |
| snapshots received | 18-19 /s (server interval 50 ms = 20 /s) | 17-19 /s |
| RTT (own state to its echo in a snapshot, includes up to 50 ms of snapshot interval) | 52-55 ms | 20-50 ms |

The host's 52 ms is the 50 ms snapshot interval alone: the embedded server and client share one process and the
snapshot goes out on the server's next 50 ms tick. Isolated spikes (hundreds to ~2000 ms in one 5 s window, mostly in A,
around the Intro -> Surdana load) are an artifact of the estimator: when the EE thread is in a long guest call, the
published slot is stale, the same sequence is resent, and the echo of an old sequence is read as a long round trip.
The 20 Hz is a ceiling the frame rate sets: the state is only refreshed at `PadMgr_Update`.

## 2. Design

* One network thread (sockets, `MatchServer` when hosting, `MatchClient`, 4 ms loop). It never touches guest memory.
* EE thread, `drakan_netplay::beforePadMgr(rdram)` after `drakan_ghost::beforePadMgr`: `world::readPlayerPose` ->
  `toPlayerState` -> mutex-protected slot (with `levelHash`). The network thread sends the slot at 20 Hz; a slot older
  than 1 s is sent with `levelHash = 0`.
* Remote players: `MatchClient::remoteState` (100 ms interpolation delay) -> mutex-protected map -> per-player
  `world::PoseBuffer` on the EE thread (30 ms jitter delay) -> `drakan_ghost::spawn` / `setTransform`.
* Visible when: the remote is in the match phase Playing, not mounted, `levelHash` equals ours and is non-zero, we are
  not in a transition (`drakan_lifecycle::transitionActive`) and our own pose read is Ok.
* Lifecycle: `beforeLevelLoad` / `beforeSaveRestore` publish `levelHash = 0` at once and drop the ghost ids
  (drakan_ghost destroys the actors itself); `afterLevelLoad` sets the new hash; `beforeSaveWrite` drops ids.
* The level in `Hello`: not done on purpose. Players change level during a match, so a join-time level check would
  reject people that are fine a minute later; `PlayerState.levelHash` is the continuous gate. `Hello.profileHash`
  (FNV-1a of the profile serial and save id) rejects other discs. `MatchConfig.level` is the host's level at start,
  informational.

## 3. Changes in `drakan::net`

* **Protocol v3**: `PlayerState.levelHash` (`u32`, 59 bytes on the wire). Justification: clients run their own worlds and
  the server needs no level data, but a receiver must know whether a state belongs to its level. Tests updated
  (`protocol_test`: field round trip, mounted-flag position); netcode.md updated.
* `MatchServer::onPlayerState`: the speed sanity check is skipped when `levelHash` differs from the last accepted state.
  Without it the first Surdana state after the Intro was a 100k-unit "teleport" and every later state was dropped (A's
  ghost never moved: found in the first run, rtt stuck at 2.9 s). Test added in `match_test`.
* `MatchClient::rttMs()` / `snapshotCount()` diagnostics (additive).

## 4. Findings worth keeping

* **A ghost spawned in `Level 0 - Intro` (the opening cut scene) hangs the guest**: `[guest-branch:missing-target]
  JALR target=0x0 source=0x10822c` right after the ghost's kill-list flush, the game then ran at a fraction of a frame
  per second. Cut scene levels are excluded (`Level 0*` is "no level"); the owner of `drakan_ghost` should decide
  whether the module itself must refuse to spawn while a cut scene runs.
* `drakan_ghost` silently drops a ghost whose spawn did not work (seen with a position that does not exist in the
  loaded level). netplay used to respawn it every frame (2500 ids in 80 s); it now backs off (2 s, 4 s .. 10 s).
* Opening a menu makes `readPlayerPose` fail for a frame or two; ghosts flapped (removed and respawned) until the
  hide was debounced (0.5 s; own transitions are immediate).
* A load blocks the EE thread for about a second, which is about the stale-slot limit; the lifecycle listeners
  therefore publish `levelHash = 0` themselves instead of waiting for the staleness rule.
* Two instances on one PC run (40-100 fps each at 960x720) but their clocks drift against each other by several
  seconds, so the harness recipe times are generous and its checks read the logs, not the clock.
* The harness copies `dist/DrakanPC` (DLLs, ini) into `build/netplay-{a,b}` and junctions `textures`, `movies`, `nr`,
  `ngx_data`; the exe comes from `build/final-game/bin/drakan.exe`.

## 5. Not done / next

Animation (`setPose` + clips from `PlayerState`), a real level-exit test, the mounted (Arokh) case, hit claims, a
spawn position that is valid in the level for the ghost's sector, `DRAKAN_NET_*` in the PC Settings overlay, other disc
profiles (lifecycle is PAL only). The binaries for these runs were built with
`cmake --build build/final-game/runtime-build --config Release --target ps2EntryRunner` (dist/DrakanPC untouched).
