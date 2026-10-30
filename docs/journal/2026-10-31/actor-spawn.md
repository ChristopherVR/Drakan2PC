# Guest actor creation / destruction API, and a spike plan for a multiplayer "ghost player" - 2026-10-31

Goal: find the engine's own way to create and destroy an actor so a network-driven, kinematic clone of Rynn can be
spawned (`multiplayer.md` option (b)). Research only; no runtime code changed. PAL (SCES-500.06) addresses; other regions
go through `drakan_region::guestAddress()`. No disassembly is reproduced, only semantics.

Evidence tags: **[run]** observed in a traced run of `dist/DrakanPC` (`DRAKAN_TRACE_FN`, New Game on Surdana, outputs under
`build/harness/actorspawn{,2,3}/`, git-ignored), **[dec]** read in the private decompile (`build/recomp_ui/alldecomp`),
**[ELF]** read from the ELF data, **[inf]** inference, **[?]** unknown.

Read first: `engine-reimplementation.md` (hook mechanism), `docs/reference/world-model.md` (registry, handle layout; written
concurrently by the `runtime/world` work; I only read it), `docs/reference/debug-menu.md`.

## 0. Summary

* There is exactly one construction path for every game object: **create a 0x120-byte "actor" (handle) from a resource
  record, then attach a class "behaviour" (an event-handler function plus instance data) chosen by the record's class id.**
  The behaviour is a plain function `handler(handle, event)`; class id to handler is a 155-entry table in the ELF data.
* The engine itself already spawns a second Rynn-class actor: the free-camera code (`0x243620`) copies Rynn's transform,
  spawns an actor with **Rynn's resource index**, and then swaps its behaviour handler for a minimal one. That is the exact
  recipe a ghost needs, in the game's own words. Rynn's init handler even has an early-out for that case (camera mode 7).
* The spawn function to call is `0x181ad8` (`this` = `0x4de5e0`). Destruction is `0x1413c0` (queue, flushed next tick).
* Hard problems are not creation but the **single-player globals** (135 functions read the controlled-actor handle at
  `ctx+0x4b0`; player HP is a global block, not per actor), so a ghost must be inert: no AI notices it, no damage reaches it.

## 1. Object model

### 1.1 Layers

| Layer | What | Where | Tag |
| --- | --- | --- | --- |
| Resource db | `ctx+0x320` = global `0x4db4c0`; records 0x24 bytes at `*(db+8)`, count at `db+0x10` (index limit inclusive). Record: `+4` type, `+8` class id, `+0xc` property block, `+0x10` model id, `+0x18`/`+0x1c` flag words, `+0x20` optional init blob | `world-model.md`, [dec] | [RAM] |
| Actor ("handle", 0x120 bytes) | vtable `0x51b8b0` (ctor `0x178d98`, dtor `0x178f28`). `+4` flag word (bit 31 = dying, bits 2-3 / 4-5 select registry lists, bit 22 "has owner", bit 16 pooled), `+0xc` type, `+0x10` u16 resource index, `+0x12` own registry ref, `+0x14` position (3 floats), `+0x20` 3x3 rotation, `+0x50` model/skeleton instance, `+0x54` property block, `+0x7c` sector index, `+0x9c`/`+0x9e` list indices, `+0xfc` behaviour handle, `+0x104` flags (bit 28 = needs sector update) | [dec] + `world-model.md` | [dec]/[RAM] |
| Behaviour handle (0x18 bytes) | `+4` alive, `+8` back pointer to the actor, `+0xc` vtable `0x5278d8`, `+0x10` **instance data** (class-specific struct, size from the class table + 4), `+0x14` **handler function** | ctor `0x283720`, dtor `0x283818` | [dec], [run] |
| Class table | 12-byte entries `{class id, handler, instance size}`, 155 entries, `0x4fca40`..`0x4fd184` (e.g. Rynn `0x10009` -> handler `0x30ea30`, size `0x111c`; Arokh `0x1001c` -> `0x270ba0`, `0x990`; camera `0x1001b` -> `0x2180b0`; spell projectile `0x10008` -> `0x3971f8`; arrow `0x1003c` -> `0x41a1e8`; weapon `0x1004f` -> `0x20e130`). Lookup `0x283ee8` (linear, cached last hit, bound 0xe6); size getter `0x283eb8`. Entries with a null handler (e.g. `0x10036`, `0x1002e`) fall through to a generic switch in `0x3a8580` | [ELF], [dec] | high |
| Class factory | singleton `0x556750` (static init `0x33a300`, vtable `0x527970`), held by the "RFL" database objects (size 0x124, `+0x120`) which the class-db manager `0x4deb08` indexes by class id (`0x1cc9f8`) | [run] `a0=556750` on every create | high |

The behaviour handler is what the 2026-10-17 journal called the "factory" (0x3971f8, 0x41a1e8, 0x270ba0): it is called with
`(handle, event)`. Events seen in the handlers: **1** init, **2** destroy, **4** per-tick update, **8** second update/enable
step, **0x10** and **0x20** per-frame prepare with the spawn/frame context at `ctx+0x4e0`, plus **0x80**, **0x100** (send
wrappers `0x283988/0x283a28/0x283b48/0x283be8`, which save/restore the context fields around the call). Meaning of 8/0x10/0x20
is [inf]. Return 0 = ok.

Pools: actors come from a pool owned by the resource db (`db+0x14` block array, blocks of 0x80 actors = 0x9000 bytes,
`db+0x20/0x28` free list; grow `0x182c80`, teardown `0x182860`). Model instances (0x1a8 bytes, `actor+0x50`) come from a
second pool of 0x40 per 0x6a00 block (`0x183148` inline). Everything else uses the guest heap behind the malloc semaphore
`DAT_00515260` (WaitSema/SignalSema around every allocation).

### 1.2 Registries

* Master slot table `ctx+0x7c` (see `world-model.md`): `0x168388(table, actor)` returns/allocates the 1-based ref
  (`actor+0x12`) and bumps its refcount; `0x168040` releases. Ref to handle: `0x1093c8`.
* Update/draw working sets on `ctx`: `+0x44c` (count `+0x454`, cap `+0x450`; actors whose flags select "dynamic"),
  `+0x464` (count `+0x46c`), `+0x458` / `+0x470`. Added by `0x141578` (engine vtable slot at `0x51a854`), removed by `0x141778`
  (slot `0x51a85c`); the actor stores its 1-based index at `+0x9c` / `+0x9e` and removal is swap-with-last. `0x452190(classId)`
  scans `+0x44c` for the first actor of a class (used by Rynn's init to find Arokh). [dec]
* Spatial structure: `*(ctx+0x31c)+0x24b4` is the level/sector object (vtable `0x51ac38`; 2048-unit cells, 0x164-byte cell
  records). Its methods: insert `0x1499c8(world, actor, -1)` (vtable `+0x74`), remove `0x149c08` (`+0x7c`), **update after a
  move `0x1494c8` (`+0x84`)**. [dec]
* Deferred kill list `ctx+0x440` (cap `+0x444`, count `+0x448`). [dec]

## 2. Creation

### 2.1 The creation stack (runtime spawns: projectiles, effects, items, summons, free-camera body)

| Step | Function | Meaning | Tag |
| --- | --- | --- | --- |
| Public spawn | **`0x181ad8(this, owner, transform*, extra*, extraLen(u16), resIndex)`**, `this = 0x4de5e0`, engine vtable slot `0x51a874` (also reached through the sub-object at `ctx+0x520`, vtable `+0xfc`). `transform` = 12 floats = `{pos[3], rot[3][3]}`, the same 0x30 bytes as `actor+0x14`. `owner` may be 0; `extra` null = defaults built on the stack. Returns the new actor handle pointer or 0 | [run] 190 calls from ~25 call sites in a short play session; [dec] | high for the shape |
| Simpler variant | `0x181aa8(this, transform*, extra*, len)` forwards through `ctx+0x520` vtable `+0x100`; this is what the level loader uses | [run] 5039 calls | high |
| Spawn core | `0x181c50(this, transform*, extra*, extraLen, resIndex, preAllocatedActor)` (engine vtable `0x51a87c`): saves the spawn context (`0x142cd8/0x142ce0` read it, `0x142ce8` sets `ctx+0x4e0` = `extra`/len), calls `0x183148`, then `0x183808`, then the engine's "register in lists" (`ctx+0x520` vtable `+0xdc`, which reaches `0x141578`), sets flag bit 22 if an owner/extra was given, restores the context | [run] all 5245 `0x183148` calls come from here | high |
| Allocate + fill | **`0x183148(db=0x4db4c0, resIndex, actor-or-0, transform*, doInit, notify, bypass)`**: bounds check on the index (inclusive), refuses records flagged "level-only" unless `bypass`, allocates an actor from the pool when none is given, writes `+0x10` index, `+0xc` type, copies the property block (`0x193088`) and a 0x40-byte init blob from the record, creates the model instance when the record has a model (`0x101ea0`), derives the list flags (`+4` bits) from record flags, copies `transform` into `+0x14` (`0x13d8a0`) and the model instance, sets `+4 |= 0x40`, optionally runs `0x183808`, and optionally posts the "created" notice `0x11bc68(ctx, record, actor)` | [dec] | high |
| Attach behaviour | `0x183808(db, actor)` -> `0x179060(actor)`: reads class id from the record, `0x17e220(actor, classId, 1)` -> `0x1398d0(0x4deb08, actor, classId)` -> class db -> factory `0x283cc0(factory, actor, classId)`: looks the class up, allocates the 0x18-byte behaviour handle and `size+4` bytes of instance data (`0x283720`), sets `actor+0xfc`, then runs the handle's init (event 1, vtable `+0x1c` -> handler(actor, 1)). Unknown class ids go to `0x3a8580`. On failure the actor is returned to the pool | [run] `a1=actor, a2=classId`, 2574 calls | high |
| World insert | done by the class handler's event 1 (Rynn: through the level object vtable `+0x74`) | [dec] | high |

A zero-argument `0x13b598(resIndex, transform*, doInit)` also exists (db `0x4db4c0`, no list registration, no
callers seen): do not use it.

### 2.2 Level load (instances from the level data) - two different paths

* **New game / level change**: `0x1c94c8(path, flags)` ("Level_Load") opens the `.lvl` stream, loads the sector/geometry
  object `0x4dc110` (`0x14db68`), loads the resource db (`0x4db4c0`, 3897 records in Surdana, 4465 in the next level seen) and
  then, if `flags & 3 == 0`, runs `0x18e448(0x4df4d8, stream)`, which grows the pool (`0x182c80`) and instantiates every placed
  object through **`0x181aa8` -> `0x181c50`** (5039 spawns in Surdana). [run] The per-instance placement comes from the stream;
  the class comes from the resource record, i.e. the ODB-derived database. The `.ODB` record layout itself is still undecoded.
* **Save restore** (`.rsg` state): `0x16c590` (vtable slot `0x51a934`, not hit in a New Game run) calls `0x183148` per saved
  actor with its saved transform, then the per-actor state loader `0x179170` (reads flags, transform, property overrides,
  attached behaviour data via the handle's vtable `+0x54`), then `0x183808`. Serialisers: actor `0x179170`/class `0x380f70`
  (load), `0x37fcb0` (save); both have a `case 0x10009`. [dec]

### 2.3 Projectiles, enemies, dropped items, dragon

All use the same `0x181ad8`/`0x181c50` stack with the effect's or creature's resource index (there is no separate API per
kind): the 190 runtime calls in a 12 s session come from about 25 distinct call sites (`0x11c038`, `0x468778`, `0x409a60`,
`0x20cae0`, ...). Dropped items and spells use `extra` (spawn context in `ctx+0x4e0`) to pass parameters to the handler's
event 1 (the spell handler `0x3971f8` reads `ctx+0x4e0` in its first event). The existing dragon is not summoned: Arokh is a
placed level actor (class `0x1001c`) and Rynn's init links to it (`0x452190(0x1001c)` then `0x27dfa0`). [dec]

## 3. Destruction

* **Public: `0x1413c0(this, actor)`** (engine vtable slot `0x51a884`): sets bit 31 of `actor+4` and appends it to the kill list
  (`ctx+0x440`). The list is flushed by **`0x142e50`** (called from the per-frame engine update `0x140ba8`/`0x141148`), which
  calls `0x182ee0(db, actor)` for each. [dec] (33 + 372 `0x182ee0` calls in the runs, from `0x1363c0`/`0x182860`.)
* `0x182ee0(db, actor)`: `0x17a030(actor)` (behaviour dtor: handler event 2, free instance data, `0x141778` unregister from
  the lists, release the registry ref, free skills/animation lists, `0x1930a8`, free model instance) then returns the actor
  block to the pool free list and decrements the live count. [dec]
* Rynn's event 2 removes her from the sector object (vtable `+0x7c`), runs `0x310490` / `0x44c108` and releases her sub-objects.
* **Level unload**: `0x182860` tears the whole pool down; `0x18e448` / `0x1c9dc8` filter the preserved set (they skip the
  player actor, `DAT_004dea94`) and free the rest with `0x182ee0`. The registry is rebuilt by the level loader
  (`0x16adb8` writes new `ctx+0x4b0/0x4b4`). [dec]

## 4. Rynn: class, subsystems, single-player assumptions

* Class **`0x10009`**, handler `0x30ea30`, instance data 0x111c bytes (+4), resource record id `0x20a50`; model/skeleton from
  the record. Init (event 1) sets up in this order: zeroes and constructs the controller and equipment sub-objects at
  `+0x57c/+0x580`, `+0x768`, `+0x798`, `+0x9c8`, `+0xa14`..., `0x3a5a88`, stores the **actor back pointer at `data+0x80`**
  (this is what the dual-wield code calls "model"), `0x44b490` (model/animation setup, shared with the free-cam body),
  `0x44b7e8` (Rynn specific), **then: if the camera's mode word (`camera data +0x14c`) equals 7 it returns early**.
  Otherwise it inserts her into the sector object, finds Arokh (`0x452190(0x1001c)`) and links the dragon to this actor
  (`0x27dfa0`), sends HUD message 0xf (`0x2ca958`) and attaches the camera. [dec]
* **Camera mode 7 is the engine's "second Rynn body" mode.** `0x243620` (called by the console command with string id
  `0x240b`/`0x240d` in `0x240480`, which the debug menu's "Run" path also reaches): saves the camera mode in `+0x150`, sets
  `+0x14c = 7`, copies 0x30 bytes from the player actor's `+0x14`, calls the spawn with the player's own resource index
  (`*(player+0x10)`), temporarily writes the new handle into `ctx+0x4b0` to run `0x219948` (camera attach), restores
  `+0x4b0`, and replaces the new actor's behaviour handler with `0x2a3378` through **`0x283ae0(actor, newHandler)`**
  (writes `*(actor+0xfc)+0x14`). That handler handles only events 1 (`0x44b490` + world insert), 2 (world remove +
  `0x44c108`) and 4 (reads the input controller at `ctx+0x358`, integrates velocity straight into `actor+0x14..0x1c`,
  rotates with `0x13edd8`, then calls the **sector update `0x1494c8`**). [dec]
  So: a second class-`0x10009` actor with a custom handler is an engine-supported configuration. [dec] (not observed live:
  the run did not trigger the console command).
* **Player globals (where the game assumes one player)** [dec]:
  * `ctx+0x4b0` controlled-actor handle and `ctx+0x4b4` camera handle: **135 / 223 functions read them** (AI `0x240480`,
    `0x2aaa18`, `0x362d38`, controllers, HUD, inventory ...). Writers: level load (`0x16adb8`), dragon mount/dismount
    (`0x270d20`), `0x243620`, `0x362310`, setters `0x142c60`. Nothing but those switches the controlled actor.
  * **HP, mana and the stats are global**: Rynn's damage handler `0x314ab0` subtracts from the block `0x4fbac8` (getter
    `0x3d56b0`, setter `0x3d5710`), not from the actor. Equipment slots, inventory UI, quest/save state are likewise global
    or on the controller at `data+0x57c`.
  * Class-id compares against `0x10009` ("is this Rynn") exist in about 40 functions: a ghost of that class is treated as Rynn
    by them (targeting rules, parry/block bits at `data+0xab0`, shield handling). `0x4dea94` keeps the player actor alive
    across level unload.
  * Input/camera binding is not part of creation; it is `ctx+0x4b0/0x4b4` plus Rynn's init non-mode-7 branch.

## 5. Driving a spawned actor kinematically

Everything below is from the engine's own free-cam handler, the dual-wield / pose-mirror hooks and `world-model.md`.

| What | How | Tag |
| --- | --- | --- |
| Position | write 3 floats at `actor+0x14` (x, y, z) | [dec], [RAM] plausible |
| Orientation | 3x3 at `actor+0x20` (rows; `model+0x20` convention of `combat-controls.md`) | [dec] |
| Tell the world | after writing, call the sector object's update `0x1494c8(world, actor)` (`this = *(ctx+0x31c)+0x24b4` object; the free-cam body calls it through vtable `+0x84`); flag `actor+0x104` bit 28 marks "needs sector update"; flag `+4` bit 18 is set by it. Without this, collision/visibility cells go stale | [dec] |
| Skeleton | `sk = *(actor+0x50)`: channels at `sk+0xe8` (count `sk+0xf0`, stride 0x80): `+4` node, `+8` attach, `+0x18` export, **`+0x50` clip id, `+0x58` clip time (float)**; node-to-channel u16 table at `sk+0xdc`; pose records `sk+0x10c` (count `sk+0x114`, stride 0x6c); pose update `0x103268(sk, force)` advances clips, walks the hierarchy, IK, then skinning `0x1165b0` | `combat-controls.md`, `drakan_dual_wield.cpp` constants |
| Animation id/time | write the clip id and time per channel (root, spine, legs, arms: nodes 0..5 in the dual-wield code) on each net update and let the pose update interpolate. How clips are *started* natively (an anim-play call) is not found yet | [?] open |
| Root motion | the pose walk moves the actor through `0x17ea70(model, world delta, 0)` (called from `0x1198d0`). For a ghost either leave the clips running (the actor will drift by the clip delta between packets, then be snapped) or hook `0x17ea70` and drop the call for ghost skeletons | [dec], `combat-controls.md` |
| Suppress AI / physics | **do not run the class handler's tick.** The handler is the only thing that ticks the behaviour; replace it through `0x283ae0(actor, hostHandler)` (as `0x243620` does). A host handler registered with `PS2Runtime::registerFunction` at an unused guest address (as `drakan_dual_wield.cpp` does for its trampoline) can forward event 2 to Rynn's real handler (so her sub-objects are released) and swallow 4, 8, 0x10, 0x20 and all damage-related events. Which of 0x10/0x20 are needed for the model to draw is [?]; test by forwarding all but 4 first | [inf] from [dec] |
| Property reads | collision uses the model's collision joints (`sk+0x128`, skinning); the ghost is in the sector structure so queries hit it | [dec] |

## 6. Lifetime, safe moments, threading

* **Spawn allocates through the guest heap semaphore (`0x515260`)** and, for a model-bearing record, may wait on the
  resource system. The runtime rule (`input.md` section 11, `save-slots.md`): a nested call on a copied context has no
  preemption and must not block; a nested spawn from a hook is therefore only safe if the heap semaphore is free. Two safe
  routes: (a) `EeScheduler::postGuestCall(pc, a0..a3, done, priority)` (used by the input glue; runs on a dispatcher guest
  thread that may block) with a **host trampoline** registered at an unused guest address that reads the six spawn
  arguments from a scratch struct in low RAM (`0xff000` page is already used for UI scratch; pick another) and calls
  `0x181ad8` with `$a4/$a5` set, since `postGuestCall` only passes four arguments; (b) a before-hook on an engine-frame
  function on the EE thread that tail-calls. Prefer (a).
* **Where**: request from the F1 menu (host thread), execute at the `PadMgr_Update` boundary (existing `beforePadMgr` of the
  debug menu / world adapter) which is early in `Engine_Tick` `0x135c40`, before the actor lists are iterated, so list growth
  (`0x141578` reallocates `ctx+0x44c`) cannot invalidate an iteration. The game itself spawns during ticks, so this is only
  a precaution.
* **Destroy**: `0x1413c0`; the actor is freed at the next flush, not immediately. Never call `0x182ee0` directly from a
  hook.
* **Level change**: pools and registry are rebuilt; any host-held ghost pointer dies. Hook `Level_Load` `0x1c94c8` (or poll
  `ctx+0x98`, the path, plus the `world-model.md` liveness test: vtable `0x51b8b0` and `ref == slot + 1`) and destroy ghosts
  *before* it: `0x1413c0` then `0x142e50(ctx)` to flush synchronously, from the main thread at the call boundary.
* **Save game**: save serialisers iterate the registry (class case `0x10009`), so a ghost could end up in an `.rsg`. Destroy
  ghosts when a save screen opens (or flag them so the serialisers skip them: [?] unverified), and never let ghosts persist
  across a quit.
* Regions: the class table, the handler addresses and every address above need a USA entry before use there.

## 7. Damage path and what a second player-class actor breaks

* Melee/projectile hits are **collision driven**: the blade sweep (`0x4979a8`) and hit application (`0x498020`) take the
  contacted actor from the sweep result and special-case victims whose class is `0x10009` (block / parry state in
  `data+0xab0`). They do not look up "the player" by global, so a weapon can hit a ghost that sits in the sector structure.
  [dec]
* What reaches a ghost then depends on its handler. With Rynn's real handler the hit runs `0x314ab0`, which subtracts
  from the **global** HP block `0x4fbac8`: **the local player would lose HP when the ghost is hit** (and the death/flash
  logic would fire on the local player). With the swapped host handler the hit is swallowed: ghost is inert and immune.
* **Enemy perception and targeting** go through `ctx+0x4b0` (135 readers), so enemies neither see nor attack a ghost.
  Making them target it needs per-AI work or a controlled-actor swap, which breaks input and camera: out of scope.
* For ghost-vs-local-player damage: apply it host-authoritatively by hooking the applying function and routing a hit whose
  victim is a ghost into a net message, and call the local damage entry (`0x314ab0`) only for the local player.
* Items carried by the ghost (weapons are actors of type 10, `data+0xa48` slots, hand holders `+0xe90/+0xeb4`) are not created:
  a ghost has no weapons unless attached separately with `0x13b878(model, item, bone, 0, 0)` (see `combat-controls.md`).
  Skip for the first spike.

## 8. Spike plan

Goal: a static Rynn clone beside the player, created from the F1 menu, then driven; no net.

**S0, confirm in-game (no new code).** Run the debug menu or the console route that reaches `0x243620` with
`DRAKAN_TRACE_FN=181ad8,283ae0,1413c0,17a030` (max 8 functions, `DRAKAN_TRACE_N`) and capture `a0..a3` (+ the 16 bytes behind
pointers) at the free-cam spawn. Expect `a0=4de5e0`, `a2` pointing at a 0x30-byte transform that equals Rynn's `actor+0x14`,
the returned handle in `v0`, and `0x283ae0(handle, 0x2a3378)`. The trace helper does not print `a4/a5`, so extend it for this
step or read `extraLen` and the resource index from a RAM dump of the registry (`world-model.md` snapshot: new class
`0x10009` handle with the same resource index as the player).

**S1, spawn.** New `drakan_ghost.cpp` with an install function and an EE-thread `beforePadMgr` hook (pattern of
`drakan_debug_menu.cpp`):

1. F1 menu button (or `F9`) posts a request. At the next `PadMgr_Update` boundary read the controlled actor via
   `drakan_controlled_actor`-style logic (`ctx+0x4b0`; require class `0x10009`, i.e. not on Arokh).
2. Build the scratch struct in low RAM: `transform` = player `actor+0x14..0x44` with x offset +300 units, `owner = 0`,
   `extra = 0`, `extraLen = 0`, `resIndex = u16(player+0x10)`. Set the camera mode word (`camera data +0x14c`, via
   `*(*(ctx+0x4b4)+0xfc)+0x10`) to 7 for the duration of the call and restore it after (do **not** leave it at 7; that would
   turn on the free-cam camera path). Do this on the EE thread, not while input is read.
3. `postGuestCall` to a registered host trampoline that calls `0x181ad8(0x4de5e0, 0, &transform, 0, 0, resIndex)`; the
   completion callback receives `v0` = handle.
4. In the callback (EE thread again via `postHostTask`): `0x283ae0(handle, hostHandler)`. `hostHandler` forwards event 2 to
   `0x30ea30` and returns 0 for everything else. Store the handle and its `actor+0x12` ref.
5. Verify: the `runtime/world` panel lists a second `0x10009` handle (resource index equal to the player's) beside the
   player; a screenshot (`play_run.py` recipe with `shot`) shows a second Rynn; Rynn's HP, dragon link (`player+0x4d8`),
   `ctx+0x4b0` and `ctx+0x4b4` are unchanged before and after.

**S2, drive.** Each frame (before the pose update) write position/rotation of a recorded or looped transform into
`actor+0x14/+0x20`, call `0x1494c8`, write clip id/time per channel from a recording of the local player's channels (read the
local player's `sk+0xe8` channels at 20 Hz; `drakan_dual_wield.cpp` already knows the offsets) and watch the clone run, jump
and strike. Decide the root-motion policy (hook `0x17ea70`) after seeing drift.

**S3, lifetime.** Destroy through `0x1413c0` on F-key; on `Level_Load` and on save-screen open (see section 6). Test:
spawn, change level, spawn again; spawn, save, load; spawn, mount Arokh, dismount. Success = no crash, no second Rynn in
the save, `ctx+0x4b0` unchanged throughout.

**Risks / open items.** (1) Which events the model needs to draw while the handler is swapped (0x10/0x20?). (2) Whether
Rynn's init for the clone leaks or double-registers anything in global UI/HUD state in mode 7 (the engine's own free-cam path
suggests not). (3) Clip *start* API (we only know where clip id/time live). (4) `ctx+0x520` sub-object adjustment vs a
direct `this = 0x4de5e0`: the traces show `a0 = 0x4de5e0` at the callee, so call `0x181ad8` directly with that value.
(5) Save serialisation of ghosts. (6) USA addresses. (7) Damage routing for ghost-vs-player is a separate spike.

## 9. Method and tools

* Index of the private decompile: split `build/recomp_ui/alldecomp/all_*.c` on its `//// FUNC` headers into one file per
  function in the scratchpad; call graphs came from a small scan for `jal` targets (and data-pointer xrefs for vtable-only
  callees) over the ELF, using `analysis/functions.csv` for function bounds. Class table found by searching the ELF data for
  the already known handler addresses from the 2026-10-17 journal.
* Runs: `python scripts/harness/play_run.py --name actorspawn --recipe <ms recipe> --env DRAKAN_TRACE_FN=<hex list>` (three
  runs, 50 to 60 s each, New Game on Surdana, empty card). Totals of one 50 s run: 2574 factory creates, 5245 `0x183148`,
  5039 level-load spawns through `0x181aa8`, about 190 runtime spawns, 405 frees.
* Nothing else was edited; no commits.
