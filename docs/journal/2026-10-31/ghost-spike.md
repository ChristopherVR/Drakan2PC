# Ghost spike S0/S1 - 2026-10-31

Code: `ps2recomp/ps2xRuntime/src/lib/drakan_ghost.{h,cpp}` (`DRAKAN_GHOST=1`, F9 spawn, F10 destroy, F1 World panel buttons,
`DRAKAN_GHOST_FWD=<hex event-bit mask>`, `DRAKAN_GHOST_LOG=1`). Registration: ps2xRuntime CMakeLists source list,
`drakan_ghost::install` next to `drakan_world::configure`, `drakan_ghost::beforePadMgr` before `inventoryViewsTick` in the PadMgr hook,
`drawControls` in the World panel. `DRAKAN_TRACE_FN` now also prints a4/a5. Runs: `build/harness/ghost{1..6}` (git-ignored).

## S0
NOT done as specified: the engine's own camera-mode-7 clone (0x243620 via the console command) was not triggered in a run.
What was observed instead is the factory call made by the ghost code with the same shape (see S1).

## S1 (observed)
* `0x181ad8(ctx=0x4de5e0, 0, &xform(0x30 bytes on the dispatcher stack), 0, 0 [a4], resIndex [a5])` posted with `postGuestCall` to a host
  trampoline works: returns a handle, a second class 0x10009 actor appears in the registry (ref 3005, res index 2 = player's, model 0x20f25).
  Spawn took the PadMgr boundary + dispatcher thread without deadlock or crash.
* Setting the camera mode word (`camdata+0x14c`) to 7 before the call does NOT keep Rynn's init (event 1) on the early-out path:
  after the factory `ctx+0x4b0` had been overwritten with the new actor (the "player" became the ghost, camera went blank) and the mode
  word read 6. Not a thread switch (same result with ps2xNoPreemptEnter/Leave around the call). Workaround in the code: save `ctx+0x4b0/0x4b4`
  before the factory and restore them right after (stage 2). With that, world JSON shows player 0xc29cb0 and camera 0xc29b90 unchanged, HP 100/100,
  plus the ghost handle. Decompile of 0x30ea30 event 1: the 7 test happens after 0x44b490/0x44b7e8 and skips the sector insert;
  what writes `ctx+0x4b0` and resets the mode to 6 is still unidentified (suspect 0x44b490/0x44b7e8 or the camera's own tick).
* Handler swap by writing `*(actor+0xfc)+0x14` works. Events seen on the ghost per tick: only 4 and 8. Forwarding 8 to Rynn's real handler
  ("all but 4") left the ghost's world-JSON position NaN (pos None) in one run; with `DRAKAN_GHOST_FWD=2` (event 2 only) no dump was
  captured (run too slow under load), so the NaN cause (event 8, mode word, or transform copy) is not isolated.
* Entering a guest function through a host trampoline slot: set `c->pc` to the real function address before calling it
  (otherwise "bad-resume ... entered at pc=0x10010c"). Stage return via `ra` = second trampoline slot works.

## Not verified
Screenshot with two visible Rynns (ghost at +300 x was off-screen or NaN), destroy path (F10), dragon link (`player+0x4d8`), level change behaviour,
`ctx+0x4b4` unchanged only checked through the world JSON camera field. Machine was heavily loaded (several game instances); dist/DrakanPC
was locked by another run, so runs used `build/ghost/play` (copy of dist top-level files + `build/final-game/bin/drakan.exe`).

## Follow-up (same day)
Runs `build/harness/ghostB` (scripts: `build/ghost/ghost2.txt`, registry scanner `build/ghost/regscan.py` over `ram_*.ram` dumps).
* **Visibility was a missing world insert.** With camera mode 7 Rynn's event 1 returns before the sector insert (`0x30ea30` event 1:
  `0x44b490`, `0x44b7e8`, then the mode-7 test, return 0). The ghost was registered but not drawn. Stage 3 of the spawn trampoline now calls the level object's
  insert the way event 1 does (`obj = *(*(ctx+0x31c)+0x24b4)`, fn = `*(obj+0x74)`, `this = *(ctx+0x31c) + (s16)*(obj+0x70)`, args `(actor, -1)`);
  returns 0x550000 (ignored). Screenshot `ghostB/spawned.png`: two Rynns, the clone beside the player.
* **Destroy must not go to Rynn's real handler.** Forwarding event 2 to `0x30ea30` on the clone crashes (`missing-target` JALR through a null sub-object at `0x31b12c`: the
  init early-out left the +0x57c controller sub-objects unattached). The host handler now sends event 2 to the free-cam body's handler `0x2a3378`
  (world remove + `0x44c108`), exactly what the engine pairs with this configuration. F10 then: `0x1413c0` queued, ghost absent from the registry after the flush (`ram_destroyed`),
  no crash.
* Player (`0xac9f20`), camera (`0xac9e00`), the stats block `0x4fbac8` words and the dragon link word `player-data+0x4d8` (= 1) are identical before spawn / spawned / after destroy.
* Position: with the transform rewritten every tick (hold) the ghost sits at the asked position (player x + 300; pos right after the factory is exact). Default forwarded events are
  now none (event 2 goes to the free-cam handler, 4 / 8 swallowed).
* Lifecycle: `drakan_lifecycle` exists; the ghost code subscribes `BeforeLevelLoad` and `BeforeSaveWrite` and destroys with flush (`destroyAll`). Not exercised in a run yet.
* **NaN position (isolated):** the transform is exact right after the factory (`pos right after the factory` = asked), so the factory's transform copy and the mode-word write are not
  the cause. Run `ghostC` (hold off, event 8 forwarded to Rynn's real handler, world insert present): position stays exactly +300 at later samples, so event 8 is not the cause either.
  The NaN seen in `ghost3/5` happened with the actor not inserted in the world (before stage 3) and events 8 forwarded; most likely event 8 acting on a world-less actor. Not reproducible now.
  Hold (rewrite every tick) stays on by default (`DRAKAN_GHOST_HOLD=0` disables, for tests).
* **`ctx+0x4b0` write:** not `0x44b490`/`0x44b7e8` (they write the Rynn instance data only) and not the setter `0x142c60` (traced in `ghostC`, never called during the spawn); the writer was
  not located statically. The engine's own mode-7 recipe (`0x243620`, decompile) does the same dance: sets mode 7 (saving the old one at `+0x150` and running `0x21f8e0`), spawns with
  `(0, &xform(copy of player+0x14, 0x30 bytes), 0, 0, u16 *(player+0x10))` via `ctx+0x520` vtable `+0xfc` (this = `ctx + (s16)*(vt+0xf8)`; the same function as `0x181ad8`), writes the new
  handle into `ctx+0x4b0` itself for `0x219948`, then restores `ctx+0x4b0 = player` and swaps the handler (`0x283ae0(actor, 0x2a3378)`). Restoring `4b0/4b4` after the spawn is therefore the engine's own convention.
  Safe by observation: stats block words, `player-data+0x4d8` (dragon link = 1) and player/camera handles identical before/after spawn and destroy. Not checked: HUD message state.
* S0 is satisfied from the decompile (the arguments above), not from a trace of the console command.
