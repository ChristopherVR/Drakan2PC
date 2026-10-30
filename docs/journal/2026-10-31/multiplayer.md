# Multiplayer feasibility (Order of the Flame style)

Status 2026-10-31. Desk investigation: repo docs, `analysis/strings_vaddr.csv`, `analysis/sdk_strings.csv`, runtime sources. No game run, no extracted ELF used. No disassembly or generated code is quoted here.

## 1. Dormant multiplayer content in the PS2 build

| Finding | Evidence | Meaning |
| --- | --- | --- |
| Three dormant editor-property labels survive in the PAL ELF: "Multiplayer Energy Recharge", "Extra Multiplayer Effects", "Multiplayer Speed Scaling", plus a property group "Multiplayer Level Properties" (data region around 0x534868..0x547690) | `analysis/strings_vaddr.csv` | The PS2 game shares the Surreal engine's property tables with the PC Order of the Flame (OotF), which had multiplayer. These are tuning knobs, not a mode. Whether any code reads them on PS2 is **not determined**; a cross-reference scan of those string addresses (`gfx_xref.py` style) is the first cheap follow-up. |
| No lobby, session, team, score, frag, deathmatch, "join/host game" strings | grep of `strings_vaddr.csv` (12.9k strings) | No dormant UI or game-mode text. Only spawn/respawn strings for generators and particles. |
| No network SDK libs in the ELF | `sdk_strings.csv`: only libgraph, libdma, libpad, libcdvd, libsdr, libmc, libscf, libipu, libkernl | No libnet/inet/ps2ip/netcnf, no DEV9/SMAP. |
| No network IOP modules | IOP bundle is 7 modules (SIO2MAN, PADMAN, LIBSD, SDRDRV, MCMAN, MCSERV, RSRCIOP) plus the IOPRP23 kernel image; none is DEV9/SMAP/INET (`docs/reference/iop-and-io.md` 1.1) | Nothing network-capable to HLE. |
| Split-screen: no evidence. Pad port 1 is polled by the padmgr (8-deep ring per port) but "not required"; `Player_GatherInput` is per-player | `docs/reference/input.md` | Input plumbing is player-parameterised, but world, camera, HUD and render path assume one local player. The PS2 release was single-player only. |

Conclusion: there is no usable dormant multiplayer. Everything must be built; the only inheritance is engine-level (per-player input struct, property knobs, possibly a latent "other player" notion in the actor system, unverified).

## 2. Determinism

Verdict: **not deterministic; lockstep is not viable without large work.**

* Time base: COP0 Count is wall clock times 294.912 MHz (`recomp-status.md`, COP0 Count fix); the game's ms timer (fades, movie pacing, timeouts) derives from it. Two machines never agree.
* Frame pacing: world rate is configurable (30 / 60 / uncapped); guest vblank edges are scheduled from host deadlines, and gameplay speed corrections (`drakan_frame_rate.h`, `input.md` ~line 375) depend on host rate. Mouse turn rate scales with host frame seconds.
* Threads: guest threads run on host threads; RPC servers, the resource worker (0x1202), sound pump and VU1/GS workers are asynchronous. Resource latency moved to guest cycles (`postAfterGuestCycles`), a step toward determinism, but streaming completion order remains scheduler dependent. `PS2X_FUZZ_UNWIND` shows the scheduler preempts at pseudorandom safe points, so it is not cycle-exact.
* RNG: the game's newlib LCG is now hooked to run on guest state (`journal/2026-10-23/snow-rand.md`), so it is seedable; `stability.md` notes older host-CRT delegation. Other host-side randomness would need an audit.
* Floats: DAZ/FTZ is set, but host x87/SSE versus a future clang/WASM build could differ in rounding; cross-platform lockstep needs bit-exact verification (the difftest harness checks instruction shapes, not whole-game equality).
* Input is sampled by the window thread and published under a mutex (`portability.md`), not at a fixed guest tick.

Making it deterministic needs: a virtual clock (Count from guest cycles only), a fixed 30 Hz world tick decoupled from presentation, cooperative single-threaded scheduling (`portability.md` mentions a cooperative scheduler as a possible form), deterministic resource completion, and input captured per world tick. That is a research project by itself.

## 3. Options

### (a) Input lockstep / rollback of two full instances (GGPO style): effort very high, risk high
* Needs determinism (above) and fast snapshots. The runtime has **no whole-machine snapshot**: "quick save" drives the game's own memory-card save UI (`drakan_quick_save.inl`), which takes seconds and is not a state restore. State to capture: 32 MB guest RAM, GS VRAM, VU1 memory, SPU2 RAM, HLE service state, scheduler/semaphore/thread tables, host caches. A RAM copy is milliseconds, but host stack frames of recompiled code and the unwind/resume machinery make restore the hardest part; per-frame rollback is unrealistic.
* Plain lockstep (no rollback) is cheaper but still needs determinism, and level streaming plus single-player scripting would diverge on any desync. Poor fit for OotF-style modes.
* Verdict: reject.

### (b) State-synchronised ghost players in the guest world: effort medium to high, risk medium
* Authoritative host runs the real game; clients send input and receive actor transforms and animation state (OotF is understood to be client-server: unverified). Or symmetric: each instance runs its own world and injects remote players as extra actors (co-op "ghosts"), no authority.
* Needs: spawn an actor of Rynn's class at runtime through the engine's factory (class/resource ids, allocation, model and animation instances), drive its transform and animation from network data, despawn on level change. Known: controlled-actor identification and per-player input (`input.md`, `drakan_controlled_actor.h`); pose mirroring, dual wield and root-motion hooks already modify per-actor state (`combat-controls.md`). Unknown: the actor creation API, whether combat and damage code can target a second player (enemies assume a single player context global), camera/HUD single-player assumptions.
* "Rynn vs Rynn" needs hit detection between player actors; the damage path targets enemy classes, so expect per-weapon hooks. Dragon (Arokh) combat adds flight-state sync.
* Verdict: feasible as a spike for co-op presence; a full deathmatch is a long tail.

### (c) Multiplayer in a future native engine: effort highest, cleanest result
The sibling `docs/journal/2026-10-31/engine-reimplementation.md` did not exist when this was written. A native engine would own a fixed-tick simulation, so lockstep or server-authoritative play comes naturally, matching OotF's model. It is the right long-term home, but multiplayer should not wait on it; results from spike (b) (actor/class knowledge, animation sync) feed it.

## 4. Transport (WASM-portable)

* Browsers allow only WebSocket and WebRTC data channels, not raw UDP. Define a `Transport` interface with unreliable-datagram and reliable-stream channels, message framing and opaque peer ids.
* Candidates: **ENet** (small C, reliable plus unreliable channels, native only); **GameNetworkingSockets** (Valve; native UDP with ICE/WebRTC NAT traversal, heavy deps, no browser target); **libdatachannel** (WebRTC data channels, native, with a browser JS shim for WASM; unreliable/unordered channels); **WebSocket relay** (simplest for WASM, TCP head-of-line blocking, needs a server). SteamNetworking is Steam-only: skip.
* Recommended: own thin interface; native backend ENet (or GNS), browser backend WebRTC data channels. Spike on ENet only. Keep the protocol transport-agnostic and snapshot/delta based (state sync tolerates loss and reordering).
* Repo layering: a new `runtime/net/` library exposing `drakan::net`; public headers free of Win32/BSD sockets/Emscripten; sockets and JS glue only in `src/`. LAN discovery is a native-only extra (OotF reportedly used TCP/IP and IPX: unverified).

## 5. OotF reference (from memory, unverified, no live lookup done)

Order of the Flame (Surreal, PC 1999) is understood to offer multiplayer over TCP/IP (and IPX) for up to about eight players, with deathmatch-style modes on foot (melee, ranged, spells), dragon-mounted combat and team variants, on a host-authoritative client-server model. Treat mode names, player limits and netcode details as unconfirmed.

## 6. Recommendation and first spike

Pursue (b) incrementally; reject (a); keep (c) as the destination. Spike plan:
1. Cross-reference the "Multiplayer ..." property strings and the level property group to find code that reads them (static, cheap; shows whether engine-level MP hooks exist).
2. Add `runtime/net` with a loopback/ENet transport and a 20 Hz state packet {position, yaw, animation id, weapon state}.
3. Two local instances (separate `mc0` and ports). Each publishes the controlled actor's transform (via `drakan_controlled_actor` and the player context global). The receiver first draws a debug marker, then a clone of the Rynn actor via the engine factory, driven kinematically (no AI/collision) with interpolation.
4. Success: P2's Rynn visibly runs, jumps and swings in P1's world on the same level and survives a level change without a crash. Measure packet rate and injection cost.
5. Only then decide damage authority (host-authoritative hits) and dragon sync.

Risks: actor lifetime across level streaming, single-player globals, camera/mouse assumptions, animation channel layout, per-instance save collisions.
