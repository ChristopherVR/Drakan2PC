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

## Ladders, doors, switches in game (same session, game lock, Ruined Village / Alwarren, pulled objects)

| Run | Setup | Result |
| --- | --- | --- |
| dr3 / dr4 (Alwarren) | "House Ladder" #2551 (MOUNTAON clone of "Shaft Ladder01", rfl 15, one field) pulled 450 units in front of Rynn; key E tap | ladder draws (wooden rungs with the converted model); the use key alone does nothing |
| **dr6** (Alwarren) | same ladder at 900, W held from 2.6 s | **Rynn walks into the ladder and climbs it** (climb animation, rising each shot, `build/ootf-run/dr6/run/s0..s11.png`). Ladders (rfl 15) work in game |
| dr2a / dr2b (Ruined Village) | "Inner Door R/L" #916 / #917 with and without the "Generic Detector" #2142 that links to all the village doors | level loads, doors draw; the detector (Task 6, Detect Which 2) makes Rynn flinch (arms over head, dr1 c5..c9, dr2b s6) but no door state change is visible (the doors are seen edge-on) |
| dr5 / dr7 | "Switch" #1911 (+ a test link Switch -> Inner Door L, `DRAKAN_DBG_LINKADD="1911>917"`) 700 and the door 1050 units in front, `DRAKAN_PLAYTEST_POS` yaw set, E tap at 3.5 s | Rynn plays the use animation with her hand on the object in front of her (dr7 s2, s3) but the door slab is in the same place in s0 and s9 (dr7 `build/ootf-run/dr7/run`): **door opening not demonstrated**. Open: the switch -> door message chain (rfl 13 sends which message, does rfl 19 open on it), and whether "Inner Door" needs its own Initial State / Open fields from the PC class (the clone only copies same-named fields) |

Ropes (rfl 73, Grimstone Mines "ropes" #2263 / "Bridge Rope") were converted (packs build) but not run in game in this session; the lock was contended for
most of the time (about 1.5 h of waiting for ~8 short runs).

Harness notes: `build/ootf-run/dr_run.sh` (lock with trap, 2 s polling, `PT=` for `DRAKAN_PLAYTEST_POS`), recipes `dr_walk` / `dr_use` / `dr_sw` /
`dr_wlad`, `dr_sheet.py` (contact sheet, brightened). Use is a tap: `key down E` then `key up E` 60 ms later.

## Follow-up: doors open by proximity, ropes

**Door class (rfl 19, clone of WORLDCON "MW Wood Door01") fields** (converted "Inner Door L", all copied from the PC class, coverage 1.00): `Locked 0`,
`Auto-Open? 1`, `Auto-Open Distance` 2.0 lu (= 4096 units), `Auto-Close Distance` 3.0 lu, `Open Direction`, `Opened Rotation Angle` ~1.9 rad,
`Msg Sent When Opened 1`, `Msg Sent When Closed 0`. The village doors therefore do not need a switch: they swing open when Rynn comes within
~4096 units. The "Generic Detector" #2142 (links to 13 doors) is a separate puzzle trigger (its messages are not what normally opens a plain door).
The switch (rfl 13, clone of LEVEL1ON "Button", `Switch Type 3`, `Message to send when on 1`, off 0) sends message 1 to its links, and a door's
`Msg Sent When Opened` is also 1. Links survive the renumbering (checked with `DRAKAN_DBG_LINKS`: Generic Detector #2142 -> doors 916, 917, ...).

**Demonstration (dr8 / dr11m, `build/ootf-run/dr11m/run/s0..s13.png`, "Inner Door L" pulled 6000 units in front of Rynn, `DRAKAN_DBG_PULLROT=-90`
so it faces the camera, Rynn runs at it, 500 ms per shot).** Shots s0..s3 (distance 6000 to ~4500): the carved door face is turned toward the
camera; s4 (about 4100 units, the auto-open distance): the door has swung 90 degrees and is a thin edge-on slab, and stays so while Rynn passes it.
With the original yaw (dr8) the sequence is mirrored (edge-on first, face after the threshold). The earlier "no change" runs had the door already
inside the open distance from the first frame, so it had opened before the first screenshot (that is why it looked edge-on in every shot).

**Ropes.** The Grimstone Mines "ropes" #2263 / "Bridge Rope" #1938 are engine class 17 (static scenery, cloned from "spear" with the PC model),
not rfl 73; they load (dr9) but sit in a dark cave, nothing to climb. The rfl 73 ropes are Islands "Swing rope" #3031.. / "Cagerope", Ruined Village
"Rope" (hangs the crates, name-mapped disc class) and Wartok "Rope". dr10 (Islands, two "Swing rope" pulled 800 units in front): level loads, the
rope draws as a striped vertical rope and is solid (Rynn runs against it, `build/ootf-run/dr10/run`). Swinging on it (jump, grab) was not tried.

New test aid: `DRAKAN_DBG_PULLROT=<deg>` (adds yaw to pulled objects).
