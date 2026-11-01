# OotF flames (particle classes 43 / 71 / 97) in template ODBs: the level-load hang and its fix (2026-11-01)

Earlier note (ootf-template-odb-hosting.md): "Burning Flame (Wide)" cloned from WORLDCON "Part: BaseFire [15]" hung the level load, so particle
classes were kept out of template ODBs (`CROSS_FX_RFL`, `OOTF_XODB_FX=1` opt in).

## Cause

The watchdog (`PS2X_WATCHDOG=40`) showed the main thread spinning in the particle emitter update (caller 0x3b5ee4, the guest RNG 0x4ce028): the
update at ~0x3b5440..0x3b63f0 accumulates `rate * dt` in the emitter (`acc += dt * Particles Per Second`) and spawns one particle per whole
unit in a loop (`while (acc >= 1.0) { spawn; acc -= 1.0 }`, branch at 0x3b63e8). The PC class "Burning Flame (Wide)" has **"Particle Life Time"
= 0** (and 80 particles per second). `mergeDefaults` copied the 0 into the clone; the PS2 emitter never leaves the spawn loop with it. Everything
else of the clone equals the template (Textures, Damage Effect, colour, blend mode come from the template record; coverage 1.00). A run with only
the life time kept at the template's value loaded fine (`afterLevelLoad`, control, 0 watchdog lines).

The 0 is also wrong as a value: the disc's own flame particles live 0.1 to 0.8 s (FX / WORLDCON "Part: BaseFire", scan of all rfl 43 classes).

## Second finding: the model of a particle class is the template's emitter model

With the converted PC model in the clone (`patchRecordModel`) and a life time of 0.015 s (the template "BaseFire [15]" default, which was left in
place) the flames loaded but drew nothing. The disc's BaseFire classes all name the same small emitter model (FX model 4834, `db 2` from WORLDCON /
FX); the PC model of the flame is only an emitter box.

## Fix (formats cc5a60b)

* `planClassMapping`: engine classes 43 / 71 / 97 clone a template of a host or template ODB and **keep the template's own model** (no PC model
  conversion; `OOTF_FX_PCMODEL=1` restores the old behaviour).
* `mergeDefaults`: a PC "Particle Life Time" of 0 becomes 0.5 s.
* `CROSS_FX_RFL` no longer blocks the clone (`OOTF_XODB_FX=0` blocks it).

In game (Ruined Village, `DRAKAN_DBG_NOOBJ` + `DRAKAN_DBG_PULL="Burning Flame"`, 4 flames around Rynn): the level loads and tall orange
flames with the fire texture draw around her (`build/ootf-run/fl4/run/c*.png`; fl2 / fl3 = life 0 fixed but PC model kept: loads, no flames).

## Test aids added

`DRAKAN_DBG_LINKS=<regex>` lists objects with their link targets and linkers (names + ids); `DRAKAN_DBG_PULLIDS=<ids>` restricts `DRAKAN_DBG_PULL`
to those object ids (up to 8, a second ring at 1.5x distance); `DRAKAN_DBG_PULLP` also prints radius / flags / links.
