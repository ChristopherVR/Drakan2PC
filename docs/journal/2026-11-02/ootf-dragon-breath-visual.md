# Dragon breath visual in the air arenas (2026-11-02)

Follows `ootf-state-proofs.md` ("Breath visual: not done") and `2026-11-01/ootf-dragon-pvp.md`.

## What was missing
The host breath (claims along a cone) drew nothing. The engine's own breath never creates an actor in the arenas, so there is no
effect to piggy-back on; an effect actor had to be made by the host.

## How
* **Resource table probe** (`DRAKAN_ARENA_RESDUMP=1`, `drakan_arena.cpp`): logs every record of the level's resource db (`ctx+0x320`:
  id, type, class, props, model). The engine class of a particle emitter (formats rfl 43) is `0x1002b` (149 records in the Arena of
  Death air level); a resource id is `db<<16 | ODB class id`, and db 4 is `FX.ODB` (`0x41875` = FX class `0x1875`).
* Names from `FX.ODB` (`parseOdbClasses`, the `zz_c5_names.ts` idea): `Part: BaseFire [n]` / `FireWisp [n]` / `BaseFireBlend [n]` /
  `SmokeAdd [n]` are the fire families used by the camp fires.
* **`drakan_arena::spawnEffect(rdram, resId, pos, rot, lifeTicks)`** queues a spawn through the existing factory path (`0x181ad8`, the
  loadout / pickup machinery, a new `kEffectSpawn` kind) and remembers the actor handle; `beforePadMgr` kills it (`0x1413c0`) when its life
  is over. No ghost / pickup bookkeeping touches it.
* **`drakan_pvp.cpp` `spawnFlame`**: while the attack bit is held (`scanBreath`, now also without any ghost in the match) one effect
  every 6 pad updates at the dragon's mouth (`+700` along the heading, then 1150, 1600, 2050, repeating), rotation = the dragon's 3x3.

## Findings (runs `build/harness/c5*`, two instances, `arena_dragons.py`, all 9 checks still PASS)
* `Part: BaseFire [n]` (class `0x1002b`, model `0x412e2`) draws a **camp-fire log pile with a white flame**: the emitter's model is the
  logs. Not usable for a breath.
* The model-less (`model 0`) classes are pure particles: **`Part: BaseFireBlend [2]` `0x418a5` = orange flame**, `Part: FireWisp [2]`
  `0x418a4` = white sparks. Their pair is the breath (screenshot `build/harness/c5h1/a/a_fire_05.png`: a column of flames along the line
  between the two dragons).
* Blindly sweeping model-less 1002b resources also spawns non-particle objects (a giant's belt, a sail): not a safe way to find effects;
  use the FX names.
* Cost: 28 to 48 fps while firing (the arena normally runs ~50); the effects are killed after 40 pad updates.

## Limits
* Needs the FX resources in the level's table: present in the converted air arenas; ground maps / Drakan 2 levels were not checked (the
  call returns false when the id is absent: no flame, claims unchanged).
* The flame is world-space particles: it does not follow the head, and the campaign dragon (engine breath, co-op) is untouched.
* Nothing is networked: the opponent does not see the attacker's flame (only the victim's bar falls). Sending a breath event is the next step.
* `DRAKAN_BREATH_SWEEP_ONCE=1` (long-lived single pass) is an experiment aid.
