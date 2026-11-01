# OotF creature classes: the rig invariant (Volcanic hang, Rift Crystal crash), arrival marker, duplicate ids

## Volcanic hang / Rift Crystal crash: template fields that name the template's rig

Ebon Knight (PC rig 32 joints, model 6688) on the template "Savage Leader - Maulgak" (55 joints) hung Volcanic at pc 0x4ac960; Crimson Knight
on the same template crashed Rift Crystal; the same pairs passed in other levels. Test aids added: `DRAKAN_DBG_CLSFIELDS=1` (with
`DRAKAN_DBG_CLSINFO`) lists every field of a synthesized class with its final value, `DRAKAN_DBG_TPLRIG=1` lists the template candidates of a
converted skinned class with their joint counts and flags word. Every rfl-68 template has 45 to 60 joints against the knights' 32, so no
template can match the rig; the invariant has to be on the class data instead.

Dumping the final Ebon Knight class showed two kinds of fields that still belonged to the TEMPLATE's rig, because `mergeDefaults` only visits
fields the PC class has:

* channel (joint id) fields the PC class lacks: `Body Attachment Channels [8, 24, 42, 25, 9, 50, 44, 3]` (joints 42, 44, 50 of a 32 joint rig);
* animation fields the PC class lacks or could not convert: Hit Rxn, Dodge, Taunt, Victory, Strafe, Impale, Special (clips 5095..5313 of the
  template's rig, which has another track count than the converted rig's clips).

Both index past the creature's per-joint arrays, so the outcome depended on what the heap held next to them (hence "(model, template) pair" and
level dependent). Fix in `mergeDefaults` (formats 4e2a8bd): with a converted skinned model, template channel values >= the converted rig's joint
count become 0 (root), and animation fields not converted from the PC class get a converted clip of that class (its Base Idle, else the lowest
converted id; empty arrays stay empty). Template selection itself is unchanged (creatures keep their templates).

Result (working tree = commit 4e2a8bd): Volcanic reaches control (frame 14989) and Rift Crystal (frame 13274) in the same build, creatures
exempt from the flag filter, no per-level code. See the table below for the 13 level re-run.

## Arrival marker after a level exit

The campaign synthesizes an arrival exit at the destination's Rynn start using the raw PC position, while the converted level puts Rynn 400
units over `terrainGroundBelow`. The marker now uses the same height (formats f76c579; Ruined Village raw y 19259, grounded 19150). The
player's arrival position in the run was already 19150 (the engine places her at the level's own Rynn start for a host fired exit), so this
did not change that run. The "dark rock over flat sky" picture of `build/ootf-run/e2e/exit0/x3_later.png` does NOT reproduce with the current
formats: a rebuilt campaign pack (`build/ootf-run/camp2/mods`, 8778 files against 8654) and the same exit (`DRAKAN_GAME=d1 DRAKAN_LEVELCHANGE=0`,
run `e2e/exit3`) shows Rynn and a villager inside the village cave, lit, terrain complete. The old pack predated the terrain / class fixes of
the day, so the camera problem was in that pack's terrain, not in the arrival placement. (Harness note: `run.sh` sets `DRAKAN_AUTOPRESS=0`; the
menu run needs `--env DRAKAN_AUTOPRESS=3000 --env DRAKAN_AUTOPRESS_UNTIL=100000 --env DRAKAN_INPUT_AUTOSTART=1` after it.)

## "duplicate object id 77 at index 129" (Alwarren)

`convert.ts` numbers objects in list order: the first EMITTED copy of an id keeps it and owns `idMap[id]`, later copies get fresh ids, and a link
to the id resolves to that first emitted copy (a copy dropped for an unmapped class never claims the id). So object 1389's link to 77 reaches a
live object when either copy survives. The two records are identical (class 3249, same position), so which copy a link meant does not matter.
No change needed.

## 13 level re-run (`full.sh r3`, formats snapshot f76c579 via git archive, `PS2X_WATCHDOG=60`)

Control = `[in] autostart: player under control` in the log. Twelve of thirteen levels reach control, including Volcanic and Rift Crystal together
and Alwarren; the Ruined Village Book has no player (cut scene level, the same watchdog at pc 0x1d7a04 as in every earlier table).

| Level | Control | | Level | Control |
| --- | --- | --- | --- | --- |
| Intro | yes | | Islands | yes |
| Ruined Village | yes | | Alwarren | yes |
| Ruined Village Book | no player (cut scene) | | Rift Crystal | yes |
| Wartok Canyons | yes (slow load, one 60 s watchdog line at 0x182b40, then control) | | Islands Night | yes |
| Grimstone Mines | yes | | Succubus Lair | yes |
| Grotto | yes | | Volcanic | yes |
| | | | Rift World | yes |
