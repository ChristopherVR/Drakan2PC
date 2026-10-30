# Deriving a reusable engine from the port: assessment — 2026-10-31

Question: what would it take to go from "recompiled MIPS in an emulated PS2 runtime" to a native engine with editable
content, usable by a level editor and multiplayer? Read-only assessment from the docs and code layout; nothing was built
or run. Addresses are PAL (SCES-500.06); other regions go through `drakan_region::guestAddress()`
(`docs/reference/regions.md`). Evidence tags: **[doc]** stated in an existing doc, **[code]** seen in the tree,
**[inf]** my inference.

## 1. Current architecture

### 1.1 What runs where

| Layer | Where | Nature |
| --- | --- | --- |
| Gameplay, world, entities, AI, physics, animation, scripting, UI logic, camera, level load | 12,173 recompiled guest functions (`configs/drakan.toml`, `functions_csv`, ~107-161 MB generated C++, 82 TUs) | **Guest code**, never reimplemented; all state lives in emulated RDRAM |
| EE kernel, syscalls, DMA, scheduler, SIF RPC, COP0 | `ps2recomp/ps2xRuntime` | Native runtime emulating PS2 hardware semantics |
| Disc/resource IOP (RSRCIOP, SIDs 0x1201/0x1202/0x1203) | `runtime/hle` (`drakan::hle`) | Native HLE: ISO9660 reader, SRSC index, resource requests, completions posted back to EE servers |
| Sound | `runtime/spu2` | Native SPU2/ADPCM mixer behind the 0x1203 sound commands |
| Video (GS, VIF1, VU1, GIF) | `runtime/gs` (`gsr`), `runtime/vu1ref`, `runtime/gpu` | Native renderer fed by the **unchanged** game packet stream; VU1 microprogram interpreted/functionally replaced, GS state emulated, D3D11 / portable GPU back ends |
| FMV | `runtime/fmv` | Native decode of the IPU/MPEG streams, HD movie pack |
| Input | `runtime/input` + `drakan_input_*` glue | Native; synthesises pad bytes and, better, calls guest command handlers (`Game_OnInputCommand` 0x23ce18, `InputDev_InvokeCallback` 0x131fa8) and writes camera/player fields directly |
| Settings, pause, inventory, map, save UI | `runtime/settings`, `runtime/ui`, `runtime/gameui`, `drakan_*` glue | Native, host-drawn (ImGui / `gameui` draw lists) reading guest memory through `IGuestMemory`; actions go back via hooks |

[doc] The game is "guest game logic + native hardware/IO/UI shell". The shell is large and well engineered (about 24k
lines in `ps2xRuntime/src/lib/*.cpp` alone, plus the `runtime/*` libraries), but **no game-logic subsystem has been
replaced**; only observers, adapters and tweaks exist.

### 1.2 Hook / override mechanism

* **Descriptor registration** [code]: `PS2_REGISTER_GAME_OVERRIDE(name, elf, entry, crc, applyFn)` in
  `ps2xRuntime/include/game_overrides.h`. `drakan_overrides.cpp` is the game's `apply`: it skips `LoadIopModules`,
  installs `HleRpcHooks {canBind, handle}` for the IOP services, binds stub handlers (`bindAddressHandler`), and
  installs all feature hooks.
* **Function table** [code]: `PS2Runtime::registerFunction(address, fn)` / `lookupFunction` / `hasFunction`
  (`ps2_runtime.h`). Generated code calls through `dispatchGuestBranch`, so any address can be re-pointed at a host
  function. `drakan_dual_wield.cpp` uses it to place a host trampoline at an otherwise empty slot.
* **Stub list** [code]: `stubs = ["name@0xADDR", ...]` in `configs/drakan.toml` binds SDK functions (libdma, libpad,
  libmc, libcdvd, libm, sceSdRemote) to runtime handlers at recompile time (about 200 entries).
* **Hook vocabulary** [doc, `input.md` section 8]: *replace*, *before*, *after* hooks around guest functions, wired per
  feature (input, dual wield, frame-rate fixes such as the wind gust hook at 0x141d80, inventory hooks at 0x342f90,
  skinning pre-run at 0x1165b0, blade window hook at 0x49c590). Each feature owns its install code; there is no single
  declarative hook table and no typed "native function with C++ arguments" wrapper [inf].
* **Calling guest from host** [doc, `input.md` section 11]: nested call on a copy of the context with `pc = entry`,
  `$ra = 0`, and a no-preemption guard in `EeScheduler::checkpointDue`. HLE handlers that may block run on a dispatcher
  guest thread (`EeScheduler::postGuestCall`).
* **Guest memory access** [code]: `IEeMemory`, `gameui::IGuestMemory` (`FlatGuestMemory` also reads RAM dumps offline).
  Layout knowledge is encoded as address/offset constants (`gameui/Addresses.h`, `drakan_controlled_actor.h`).

Consequence: replacing a leaf function is cheap. Replacing a subsystem is hard because every data structure is a raw
guest-memory layout that other guest code still reads and writes. A native replacement must either keep the guest
layout bit-exact (Ship-of-Harkinian style) or replace every reader at once.

## 2. Strategy options

### (a) Incremental decompilation behind the hook mechanism

Replace guest functions subsystem by subsystem with hand-written C++, keeping guest layouts until a whole subsystem is
native, then move its state out of RDRAM.

* Pros: always playable; each step verifiable against the recompiled original (differential testing via
  `docs/reference/stability.md`); reuses the existing shell; matches existing practice (all hooks are already tiny
  replacements). Output is original, cleanly structured C++.
* Cons: about 12k functions, all unnamed (`FUN_xxxxxxxx`; ~6.8k have no direct callers, i.e. vtable/callback targets).
  Decompile output must stay in private tooling; **the repo may not carry verbatim disassembly or translated code**, so
  each function must be rewritten from understanding. Pointer-heavy guest layouts (32-bit pointers, `dec()`/`enc()`
  obfuscated fields, 0x24-byte resource records, `+0xfc` inner pointers) leak into native code until a subsystem is
  complete, and 32-bit guest pointers fight a 64-bit native engine.
* Effort: months per major subsystem (world/entity, AI, physics, animation, scripting); years for all. Early value only
  for leaf and I/O-shaped subsystems.

### (b) Clean-room engine reading original data formats, recomp as oracle

New engine (renderer, entity model, AI, physics) loading SRSC containers (`.LVL .ODB .MOD .TXD .ADB .SDB .XDB`)
directly; behaviour compared against the recompiled game on the same level.

* Pros: no guest layout constraints, native 64-bit, clean API for editor and networking, portable to clang/WASM, best
  legal posture (new code, user supplies disc data). The recomp stays a ground-truth oracle (RAM dumps, GS captures,
  `scripts/harness`).
* Cons: formats only partly decoded (section 3). `.ODB` holds behaviour parameters that refer to code semantics, so a
  loader without the behaviour code reproduces appearance, not gameplay. Largest effort; parity is a long tail; nothing
  playable until late.
* Effort: roughly 1-2 years for a viewer-grade "walk around, render, play sound" engine; gameplay parity (combat, AI,
  spells, Arokh flight) is substantially more [inf].

### (c) Engine as host: keep the recomp, expose a native API layer

A stable native API (entity list, level info, camera, input, resource requests, events) implemented by reading/writing
guest memory and hooking guest functions. Tools, mods, overlays and netcode talk only to that API.

* Pros: cheapest and immediately useful; builds exactly the knowledge needed for (a) and (b) (every API field is a
  documented layout, every event a named hook). Half done already: `gameui` models (inventory/shop/map/pause/save),
  `drakan_controlled_actor.h`, portable camera, scene-model asset reader, object-motion tracker, `ModelExchange`
  double buffers. Low regression risk.
* Cons: not a "reusable engine" by itself. Content authoring is limited to what the guest can load; new levels need data
  the guest accepts (overlay packs through the HLE `FileService` are feasible because the disc is already virtualised).
  Multiplayer is hard: the simulation is not designed for replication; realistic modes are host-authoritative snapshot
  reads or lockstep, requiring determinism work on `rand` and time sources.
* Effort: weeks per API slice.

### Recommendation

Do **(c) first, deliberately shaped as the seam for (a)**; run **(b) only for read-only parts** (asset viewers and
exporters, level inspection) where payoff is immediate and gameplay parity is not required. A full clean-room engine is
a separate multi-year product; do not start it before the formats are decoded. Build each (c) slice as
`native model + guest adapter`, as `gameui` already does (std only, platform free, adapter reads guest memory). When a
slice's adapter becomes the native source of truth, that subsystem has migrated under (a) with its API unchanged.

## 3. What is understood and what is opaque

| Subsystem | Understanding | Evidence in docs |
| --- | --- | --- |
| GS / GIF / VIF1 / DMA | Well understood; replaced natively | `graphics.md`, `gs-capture.md`, `runtime/gs` |
| VU1 microprogram | Fully disassembled: one 1523-instruction "uber vertex shader + clipper + packet builder" | `graphics.md`, `vu1-correctness.md`, `vu1ref` |
| Display driver / VU renderer | Located: "Surreal PS2 Display" object 0x4e8ef0 (ctor 0x1d8930, buffer mgr 0x1d93c8/0x1dacb8), VU renderer global 0x4e9ac0 | `graphics.md` |
| Sound | Well understood end to end: 0x1203 commands, SDB/SSD, SPU2 mixer | `sound-debug.md`, `runtime/spu2` |
| Disc / resource system | Well understood: SRSC container (16-byte header, 14-byte index entries `{type,id,sub,offset,size}`), RSRCIOP RPCs, resource wire entry | `iop-and-io.md` 3 and 5.4, `runtime/hle` |
| Input | Well understood: PadMgr 0x4e90c0, `PadMgr_Update` 0x1e3970, `Player_GatherInput` 0x44c2f8, `Camera_UpdateLook` 0x44e4a8, command layer, keymap | `input.md` |
| FMV | Understood: `.VDB`, IPU path, HD replacement | `fmv.md` |
| Saves / memory card | Understood: 16 slots, `sceMc*` | `save-slots.md`, `iop-and-io.md` 5.6 |
| UI / menus / inventory | Mostly understood: screen 0x15 inventory model, controller at `player+0x584`, builders 0x342f90 / 0x433850 | `settings-menu.md`, `mouse-menus.md`, `journal/2026-10-24/tab-rework-data-model.md` |
| Engine globals | Engine object `*0x5565d4 = 0x4de5e0`; tick time `+0x24`, dt `+0x2c`; `Engine_Tick` 0x135c40; player handle `+0x4b0`, camera handle `+0x4b4`; resource db `+0x320` (0x24-byte records) | `world-rate-audit.md`, `recomp-status.md`, tab-rework note |
| Actor / object model (partial) | Handle: type at `+0xc`, resource index `+0x10`, property block `+0x54` (getter 0x192e48), instance data via `+0xfc`; obfuscated HP/ammo (`dec`/`enc`, key `0x4fe86c`); equipment slots `player+0xa48`, hand holders `+0xe90/+0xeb4`; class ids (e.g. `0x1001c` dragon) | `combat-controls.md`, tab-rework note, `drakan_controlled_actor.h` |
| Animation / skeleton (partial) | Pose update 0x103268, hierarchy walk 0x104678, IK 0x117cd0, skinning 0x1165b0, attach 0x13b878 / detach 0x13b8c8, root motion 0x1198d0 | `combat-controls.md` |
| Combat (partial) | Blade sweep 0x4979a8, damage 0x498020, hit windows 0x49c3e0 / 0x49c590, player attack vtable 0x307198 | `combat-controls.md` |
| Timing model (partial) | Frame-time based; 354 functions use tick time; 30 Hz assumptions catalogued and fixed | `world-rate-audit.md` |
| World / level streaming | **Opaque.** `.LVL` (spawn/sector tables) and `LEVELLST.TXT` known by name only; loader, sector/streaming and scene-list/visibility logic undocumented | `iop-and-io.md` 5.5 |
| AI / behaviours | **Opaque.** Only black-box observation of a patrolling NPC and a hostile pair | `world-rate-audit.md` |
| Physics / collision | **Opaque** beyond collision joints in skinning (`sk+0x128`), jump velocity cap 0x325d30, landing code 0x330f78 / 0x2ddef8 | `world-rate-audit.md` |
| Scripting | **Opaque.** `.ODB` = object/behaviour definitions; `.SSD` sound scripts (SRSC 0x310-0x312) are the only script-like format identified; no behaviour model or bytecode known | `iop-and-io.md` 5.5 |
| Data formats | SRSC known; `.TXD` decoded (some compressed terrain tiles rejected); `.MOD`, `.ODB`, `.LVL`, `.ADB`, `.HSD` payloads **not decoded** | `iop-and-io.md`, `hd-textures.md`, `ui-prompts.md` |

Tools that exist: Ghidra project and `functions.csv` (`docs/reference/ghidra.md`), a private full decompile
(`build/recomp_ui/alldecomp`, not committed), RAM-dump probes, `scripts/harness` recipes, GS captures,
`analysis/scripts/iop_srsc.py` and `txd_decode.py`. Binary function names are all `FUN_xxxxxxxx`.

## 4. Riot Engine lineage (public knowledge; unverified)

* Surreal Software developed the proprietary Riot Engine (1996) and it is listed as the engine of Drakan: Order of the
  Flame (PC, 1999). Sources: [Surreal Software](https://en.wikipedia.org/wiki/Surreal_Software),
  [Order of the Flame (ruwiki)](https://ru.ruwiki.ru/wiki/Drakan:_Order_of_the_Flame).
* A web search found no source naming the engine of
  [The Ancients' Gates](https://en.wikipedia.org/wiki/Drakan:_The_Ancients%27_Gates). Whether it descends from the Riot
  Engine is **unverified from public sources**.
* Circumstantial evidence inside this repo (not confirmation): disc `.TXD` files identify as "Riot Engine Texture
  Database"; a `RIOT.HSD` exists on disc; `.ODB/.MOD/.ADB` naming, a reference to `Drakan 2.rfl`, and a frame-time based
  tick (`world-rate-audit.md` calls it "PC-lineage") fit shared code or tooling.
* Upside if true: Order of the Flame community tools and format notes (`.rfl`, models, level data) might document the
  `.ODB/.MOD/.LVL` layouts and behaviour model. Verify by comparing headers and field layouts of the PC game's files
  against the PS2 SRSC types (never commit the data).

## 5. Phased roadmap

**Phase 0 (1-2 weeks): groundwork.**
* A typed guest-view library `runtime/world` (std only, platform free, same pattern as `gameui`): `GuestPtr<T>`,
  per-region offset tables via `drakan_region`, `dec`/`enc` helpers, bounds-checked reads. Consolidate the scattered
  address constants into it.
* One declarative hook table (address, kind before/after/replace, region, owner feature, enable flag) so hooks are
  discoverable and can be switched off for differential runs.

**Phase 1 (2-4 weeks): first milestone, native entity-list introspection overlay.**
* Locate the actor/scene list(s) from the engine object (0x4de5e0), the handle table and the resource db; enumerate
  actor handles with class id, type, position, model id, owner and property block.
* Expose a read-only `world::Snapshot` (double-buffered like `gameui::ModelExchange`) and draw it in the F1 ImGui
  overlay: list, class filter, click-to-highlight in the 3D view (reuse the portable camera projection), property dump,
  and live edit of a couple of fields (position, HP) to prove write-back.
* Exit criteria: stable across level load and save/load; layouts recorded as tables in a new
  `docs/reference/world-model.md`; JSON dump for offline diffing.

**Phase 2 (1-2 months): content decoding and export.**
* Decode `.LVL`, `.ODB`, `.MOD`, `.ADB` into native structs in a std-only `runtime/content` library over `hle::IsoFs`;
  validate each decoder against guest RAM records. Compare with Order of the Flame tooling.
* Model/animation viewer and a level layout dump (JSON / glTF).
* Let `FileService` overlay mod packs from a user folder so edited `.LVL/.ODB` data reaches the guest.

**Phase 3 (2-4 months): level editor and mod API.**
* Editor built on Phase 1/2 libraries; edits written as overlay packs and hot-reloaded by a hooked level-load entry
  point (the level load path is opaque today; budget a dedicated reverse-engineering sprint).
* Stable C++ API over `world::Snapshot` plus events (spawn, damage, level load, input command).

**Phase 4 (open ended): native subsystem replacement (strategy a).** In order of risk: camera, animation sampling,
pathing/AI leaves, collision queries, then entity update loop and spawn/despawn. Each lands behind an enable flag with a
differential test against the guest function (`stability.md`). Reassess (b) once `runtime/content` decodes all formats.

**Multiplayer (parallel research after Phase 1).** Determine whether the 30 Hz tick can be made deterministic (guest
`rand`, the ms timer 0x4af730 and COP0 Count are host-driven today). Prototype snapshot replication of the entity list as
a spectator mode before co-op. Lockstep is impractical until the simulation is native.

## 6. Risks

* Repo policy: no verbatim disassembly or translated code in docs or commits; decompile output stays under `build/`.
  Native rewrites must be original code.
* Region differences: every new offset table needs PAL and USA entries (`regions.md`).
* Obfuscated fields and 32-bit guest pointers complicate bidirectional bridging; keep write-back narrow.
* Portability (`portability.md`): new libraries stay std only and keep Win32/D3D out of public headers.
