# Ghost spike S3: engine lifecycle events (level load, save write, save restore, mount) - 2026-10-31

Spec: `actor-spawn.md` section 8 "S3, lifetime" (ghosts must never survive a level change and never end up in a save).
Result: a general listener layer, `ps2xRuntime/src/lib/drakan_lifecycle.{h,cpp}`, that the ghost spawner (and later editor
hot reload and multiplayer) subscribe to, plus a harness (`scripts/harness/lifecycle_run.py`) that checks the events in
the running game. PAL (SCES-500.06) only; other disc profiles log "not installed" (the USA profile needs address and
layout entries first). Tags: **[game]** measured in a run, **[dec]** read in the decompile only.

## 1. The events and the guest functions behind them

| Event | Hook (PAL) | Notes |
| --- | --- | --- |
| `beforeLevelLoad` / `afterLevelLoad` | `Level_Load` `0x1c94c8(path, flags)` | `path` = `cdrom0:/English/Levels/<n>/<n>.lvl`; `level` = the stem. `flags` 0 for New Game and level exits (via `0x13b7c0`, engine vtable slot 0xd), 1 or 3 for the load inside a save restore (3 when the saved level has the same name as the loaded one). `fromSave` = flags != 0. Return 0 = ok. **[game]** (intro then Surdana at New Game) |
| `beforeSaveWrite` / `afterSaveWrite` | `0x169c28(engine, ?, path, buffer)` (engine vtable +0x18c) | The serialiser itself, called from the save screen handler `0x443ac0` for the menu save, the quick save (F5, which drives the real pause/save screen) and the level-change save. `path` = `BESCES-50006Slot33/<level>.rsg`. Returns 0 = ok. **[game]** menu save (`Slot1`), quick save (`Slot33`), 4A |
| `beforeSaveRestore` / `afterSaveRestore` | `0x16adb8(engine, path)` (engine vtable +0x194) | The loader; it reads the save, calls `Level_Load` itself and then recreates the actors (`0x16c590` per saved actor). Quick load (F8), title Load / Continue. Returns 0 = ok. **[game]** F8 twice |
| `mountChanged` | polled once per frame at `PadMgr_Update` `0x1e3970` and after every level load / restore | Controlled actor `ctx+0x4b0`: class of its resource record (`+8`) `0x10009` Rynn, `0x1001c` Arokh. Fires only for Rynn <-> Arokh; a temporary second Rynn body (the free-cam clone, a ghost) keeps the class and is no change. **[game]** Rynn -> Arokh |

Ordering inside a restore: `beforeSaveRestore`, `beforeLevelLoad(fromSave)`, `afterLevelLoad(fromSave)`, `afterSaveRestore`
**[game]** (harness check). A `mountChanged` that comes from a level load is delivered right after `afterLevelLoad`.

The save write and the restore **yield**: another guest thread runs inside them (the first trace showed `v0` of
`0x16adb8` printed while the real work continued on the resumed thread). A plain before/after wrapper would lose the
"after". The hooks therefore redirect `$ra` to a host trampoline in a free function-table slot (the dual-wield pattern):
the "after" event fires when the function really returns, inline when it returns without a switch, and a resume
(`c->pc != entry`) never fires a second "before". `transitionActive()` is true between the two; anything that spawns
through `postGuestCall` (a dispatcher thread) must stay off while it is set.

### Why not other points

* `0x443ac0` (save screen handler) has early returns (no selection, overwrite question): it is not "a save happens".
* `ctx+0x4b0` has no setter in practice: `0x142c60` has no caller and no vtable entry. The inline stores are in `0x44d3b0`
  (the controlled actor becomes the actor linked at `+0x4d8`), `0x456de0`, `0x243620` (free-cam clone, restores it), `0x362310`
  and the restore. A hook on them would have to stay in sync with each; the per-frame poll does not.
* `0x270d20` is Rynn's own init (event 1), not a mount (`actor-spawn.md` section 6 listed it as a writer: it stores
  `ctx+0x4b0` when the player is created).

## 2. Is destroying an actor through `0x1413c0` inside a "before" event safe?

Short answer: **yes for `beforeSaveWrite` without anything else; yes for `beforeLevelLoad` / `beforeSaveRestore` only
together with a synchronous flush** (`drakan_lifecycle::destroyActor(e, actor, true)` = `0x1413c0` then `0x142e50`).

Evidence:

* `0x1413c0(engine, actor)` only sets bit 31 of `actor+4` and appends to the kill list `engine+0x440` (count `+0x448`).
  The actor is freed by the flush `0x142e50`. **[dec]**
* Callers of the flush: the two per-frame engine updates `0x140ba8`/`0x141148` (the tail of the frame) and the **first call of
  the save writer `0x169c28`**. Nothing else, in particular not `Level_Load` and not the restore. **[dec]**
* Save: the writer flushes before it walks the registry. Test aid `DRAKAN_LIFECYCLE_KILL=1000a` destroys the first live
  elixir (class `0x1000a`) in `beforeSaveWrite` with the plain `0x1413c0`:
  `killList 0 -> 1 alive=0` before, then the log shows `kill-list flush: 1 entries, ra=169c68` (inside `0x169c28`) at the same
  timestamp, and after the write `killList=0 victim alive=0 slot=0` (the registry slot is cleared), save ok. **[game]**
  So the victim is gone before the serialiser starts. Destroy-then-flush is not needed there.
* Level load: the flush counter does not move between `beforeLevelLoad` and `afterLevelLoad` (harness check, e.g. 187 and
  187 at the Surdana load). **[game]** The loader tears the pools down (`0x182860`, `0x18e448` skip only the player) and itself
  queues destroys that are flushed at the end of the next frame (`kill-list flush: 6 entries` right after the load). A ghost
  that was only *queued* before the load would survive into the teardown and be flushed against a rebuilt registry; an
  actor still live at the load is freed with the pool. So destroy with the synchronous flush before `Level_Load`.
* The restore is the same: `0x16adb8` -> `Level_Load` with flags 1 or 3, and the flush counter does not move between
  `beforeSaveRestore` and `afterSaveRestore` either (e.g. 1544 at both). With flags 1 / 3 `Level_Load` skips the pool filter
  `0x18e448` (it only runs for flags 0); how the restore clears the old actors was not traced, so a ghost has to be gone
  before it regardless. **[dec]** for the flag test, **[game]** for the counter.
* Kill test before a restore (`--kill 100d3:restore`, one level-geometry handle, synchronous flush): `killList 0 -> 0
  alive=0` right after `destroyActor(.., true)` (the flush emptied the list and the actor), the restore then ran to `ok`,
  and the registry slot of the victim holds a new handle afterwards (the pool block was recycled). No crash. **[game]**
* Before `Level_Load` of a New Game the registry is already empty (the intro level is torn down before the loader runs,
  `--kill "*:level"` found no live handle at either load), so the same call could not be exercised there; the restore case
  above runs the identical nested `0x1413c0` + `0x142e50` on a live registry at a function boundary.

Flush from a listener runs nested on a copy of the context without preemption (`callGuest`). `0x182ee0` takes the heap
semaphore `0x515260` while it frees: safe when no other guest thread holds it (the kill-list is usually empty and the
semaphore free at these boundaries). If a ghost owner ever sees a stall, defer the flush to the next `PadMgr_Update`
instead.

### Recommended policy for a ghost owner

* `beforeLevelLoad`, `beforeSaveRestore`: `destroyActor(e, ghost, true)`, forget the handle. Respawn on `afterLevelLoad`
  (not while `transitionActive()`) or later when the net state asks for it.
* `beforeSaveWrite`: `destroyActor(e, ghost, false)` is enough (the writer flushes), `true` is harmless. Respawn after
  `afterSaveWrite`: a ghost is therefore visible-gone for the length of the save (about 0.7 s).
* `mountChanged`: re-evaluate whatever depends on the local class (clone model choice, camera mode word).
* Never queue a spawn (`postGuestCall`) while `transitionActive()`.

## 3. Verification (harness)

`python scripts/harness/lifecycle_run.py --exe <play folder>` (6 runs; private cards under `build/harness/lifecycle_*/card`,
the player's saves are never touched). Latest: 15/15 checks.

| Phase | What is verified in game |
| --- | --- |
| `boot` | New Game: intro and Surdana are two `Level_Load` pairs, ok, no flush inside. |
| `save_surdana` | quick save: `beforeSaveWrite`, the writer's flush (counter +1), `afterSaveWrite` ok, `Slot33/*.rsg` written. |
| `save_4a` | New Game into 4A (`DRAKAN_START_LEVEL`): `mountChanged Rynn -> Arokh` right after the second `afterLevelLoad`; quick save in 4A. |
| `menu_save` | ESC, Save Game, Enter: the same two save events for the menu path; `Slot1/*.rsg` written. |
| `restore` | F5, F8, F5, F8 in Surdana: both quick loads fire `beforeSaveRestore, beforeLevelLoad(fromSave), afterLevelLoad, afterSaveRestore` in order, both quick saves fire the save events. |

Kill test aid (`--kill <class>[:save|restore|level]`, env `DRAKAN_LIFECYCLE_KILL`): results in section 2.

### Not verified in game

* **Arokh -> Rynn** (dismount). No reachable level lands Arokh (`2026-10-23/world-rate-audit.md`), and the start-level redirect
  also replaces the level of a loaded save by name, so a 4A -> Surdana restore cannot be staged (a save made under the redirect is
  named Surdana; redirecting the intro to 4A never reaches Surdana). The poll is symmetric (same code path as the verified
  Rynn -> Arokh), but treat the dismount event as untested until a dismount run exists.
* A level change through an exit trigger (`0x361f60` -> screen 0x1a -> `0x13b7c0`). The hooked `Level_Load` is the same
  function the New Game transition uses, and `0x13b7c0` calls it with flags 0 (trace: `ra=13b7e0`).
* A cross-level restore (flags 1): the harness only restores the level it is in (flags 3). A save made under the
  `DRAKAN_START_LEVEL` redirect is named after the replaced level, so a cross-level restore cannot be staged with it either.
* A quick save injected into the card from another run: the quick-load existence check answered "no quicksave exists" for a
  folder copied in before the first gameplay frame (stale card scan), so `restore` saves and loads within one run.
* USA disc: addresses and the `ctx+0x4b0` layout are PAL; `install()` refuses other profiles.

## 4. API (drakan_lifecycle.h)

```cpp
auto id = drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, [](const drakan_lifecycle::Event& e) {
    drakan_lifecycle::destroyActor(e, g_ghost, true); // EE thread, nested guest call, flush now
});
```

`Event`: `kind`, `path`, `level`, `fromSave`, `ok`, `result`, `previous`/`current` (mount), `controlled`, plus `rdram`,
`runtime`, `ctx` for helpers. Helpers: `callGuest`, `engine`, `actorAlive`, `destroyActor`, `flushKillList`,
`killListCount`, `transitionActive`, `currentMount`. `DRAKAN_LIFECYCLE_LOG=1` logs every event (`[lifecycle t] kind level path
fromSave ok result mount flushes`) and every non-empty kill-list flush; `DRAKAN_LIFECYCLE=0` disables the hooks.
Subscriptions may be made before or after `install()`. Listeners run on the EE thread, must not block, and must not call
guest functions that wait on a semaphore another guest thread may hold.
