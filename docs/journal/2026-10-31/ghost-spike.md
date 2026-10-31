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

## S2 integration: animation drive and mirror mode

- `drakan_ghost::setAnim(id, channel, clip, time)` (channel -1 = all) and `setPose(id, const world::PlayerPose&)`.
  Each tick at the PadMgr boundary the host writes clip id and time into the ghost skeleton's channels
  (`sk = *(h+0x50)`, `+0xe8`, stride 0x80, clip `+0x50`, time `+0x58`); channels in `lowerMask` get the lower clip,
  the rest the upper clip.
- Writing clip id and time every tick is enough: no clip-start call is needed. The skeleton is still evaluated and drawn
  although the ghost's handler swallows the tick event; readback shows the engine advancing time slightly past the
  written value.
- `DRAKAN_GHOST_MIRROR=1` (implies `DRAKAN_GHOST`): records the local pose with `world::readPlayerPose` into a
  `PoseBuffer` (1.0 s delay, 512 samples) and drives an auto-spawned ghost with the pose from 1 s ago, offset +300 x
  (`DRAKAN_GHOST_OFFSET=x,z` overrides; not yet exercised in a run). It respawns after level loads and saves.
- Verified in game (run `ganim1`): the ghost runs in stride along the player's path, passes the player and decelerates.
  It was destroyed and respawned once across the intro → Surdana level change and animated normally afterwards.
- Not verified: attack and jump clips on the ghost (it was off screen at +300 x during those frames), and the
  dedicated lifecycle checks (destroy before exit / New Game / menu save / F5, no second `0x10009` in the save,
  respawn after load).

### Follow-up runs (offset 120, lifecycle checks)

Play folder `build/ghost-anim-play`, runs `ganim2`, `glcA`, `glcB`, `glcC` under `build/harness/` (recipes `mirror.txt`,
`lcA/B/C.txt` in the play folder; registry scan `build/ghost/regscan.py` over `ram_*.ram` dumps).

- **Jump (`ganim2`, `DRAKAN_GHOST_OFFSET=120,0`):** the ghost is visible behind/beside the player and shows the jump pose
  (arms out, off the ground) about 1 s after the player's jump. **Attack:** the player is unarmed; the ghost overlaps the player
  in the attack frames and no weapon clip was distinguishable from the screenshots, so an attack clip on the ghost is
  *not* confirmed (needs an armed save).
- **Save while a ghost exists (F9 ghost, no mirror):** F5 (`glcA`, Slot33) and menu Save Game (`glcB`, Slot1) both log
  `[ghost] destroyed id 1 before beforeSaveWrite`; the registry scan one frame later shows a single `0x10009` actor. After
  F8 the loaded world (`glcA ram_loaded`) has exactly one `0x10009` (the player, `+0x4d8` = 1), so the written save contains no
  ghost. Player HP block unchanged.
- **Respawn after save/load (mirror, `glcC`):** the ghost is destroyed before the save, auto respawned (id 3) after
  `afterSaveWrite`, then F8: `ram_loaded_early` / `late` hold the player plus one new ghost (id 4, new handle, ref 4611);
  no stale second ghost, no crash.
- **Level change (New Game, intro -> Surdana):** the mirror ghost spawned in the intro (id 1) is gone at the next
  `beforeLevelLoad` (no "destroyed" line: `destroyActor` found it already dead, the engine tears the pools down before
  `Level_Load`) and the ghost list is cleared and respawned (id 2). The restore load (fromSave=1) behaves the same: ghost 3 is
  not reported destroyed by our listener, yet it is absent afterwards. So the listener is a safety net; observed teardown is
  done by the engine before `Level_Load`.
- Not verified: a level change through an exit trigger, an armed attack clip on the ghost.

## Ghost attack clip

Play folder `build/ghost-attack-play`, runs `gatk1` / `gatk2` under `build/harness/` (recipe and private card copy of the
Level 1 save in `build/ghost-attack/`; `play_run.py --save`, `DRAKAN_GHOST_MIRROR=1 DRAKAN_GHOST_OFFSET=150,0 DRAKAN_GHOST_LOG=1`).
The save has Rynn armed only after the Tab equip (TAB, ENTER, E, TAB); that recipe equipped the **bow** (not the dagger),
so the "attack" is the bow aim/draw clip, still an upper-body clip on a separate channel.

- `drakan_ghost.cpp`: added two log lines under `DRAKAN_GHOST_LOG`: `[ghost] player lower/upper/lowerMask` (every 15 ticks)
  and `[ghost] id N channels <clip ch0> <clip ch1>` (the ghost's real channel clips, every 30 ticks). No behaviour change.
- **Log evidence (`gatk2`):** the player skeleton has two channels. During the draw the player pose reads
  `lower 20895 upper 20894 lowerMask 22c19` (bit 0 set, bit 1 clear), otherwise lower == upper and mask `ffffffff`.
  About 1 s later the ghost's channels read `20895 20894`, i.e. channel 0 lower, channel 1 the upper clip, held for the draw
  duration (~1.8 s), then `20897 20897` / `20895 20895` after release. Upper clip is not overwritten by the engine.
- **Screenshots (`a0_g1300.png`):** with the actors 150 units apart both are visible; the ghost shows an arm-raised draw pose
  while its legs idle; it holds **no weapon** (no bow geometry) and no projectile; the player's bow is attached.
- Not verified: the melee (dagger) swing clip; the equip sequence picked the bow. Attacks 2 and 3 in the recipe were taps
  with the bow (no draw), producing only 20896/20897 lower-channel clips.
