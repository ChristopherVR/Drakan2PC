# Co-op stand-ins hold the partner's weapon - 2026-11-01

## Finding: the weapon node is the same on every roster model

`startWeaponChange` (drakan_ghost.cpp) read the hand bone from the model's property block (`*(actor+0x54)+4`, property
0x45 / 0x46 for Rynn). An NPC body has a different block, so the read returned garbage (`0x3f8ccccd`) and the weapon was
skipped ("weapon record ... unusable").

The 0x0208 node lists (`parseSkeleton` of `@drakan/formats`, read from the player's ISO; only names and indices noted here)
show the attach point is a node named `Rweapon` / `Lweapon`, a child of the hand node, and its **joint index is what
`0x13b878(model, item, bone, 0, 0)` takes**:

| Model | Rweapon joint | Lweapon joint |
| --- | --- | --- |
| Rynn (SYSTEM.MOD 3877) | 37 (property 0x45) | 19 (property 0x46) |
| all 30 usable roster models (NPCS.MOD) | 30 | 13 |

All 30 usable roster models share it (the skeletons have 57 or 60 joints; 4402 / 4544 / 4700-4706 have 60, the others 57,
the weapon joints are the same). So no per-model table is needed: `kNpcWeaponJoint = {30, 13}` in `drakan_ghost.cpp`,
used when `Ghost::character != 0`.

## Replication

No protocol change. `PlayerState.weaponRight / weaponLeft` (v4, resource record ids of the held items) are already on the
wire in v8 (78 bytes) and `Ghost::wantRec` is set from them for every ghost kind (`setPose`); `startWeaponChange` drops a
wrong item (detach 0x13b8c8 + the engine kill list) and spawns the wanted one, one item at a time. A stand-in now goes through
the same path as a Rynn ghost, with the NPC joints above. Attach call confirmed as 0x13b878 (log
`[ghost] weapon record 33cbc -> item ... (ghost 1 hand 0 bone 30)`).

## Verified

* **[run] attach**: `scripts/harness/coop_standin.py --char 1798 --acts idle,attack` (build/harness/wsin2): Capn Serris holds
  Rynn's Dagger in the right fist and the blade follows the swing (contact sheet `sheet.png`).
* **[run] two instances**: see the section below.

## Not verified

See the two-instance section for what the run showed. The weapon swap on a change of the partner's weapon uses the unchanged
Rynn-ghost path (detach, destroy, respawn); it was not re-run separately on a stand-in.

## Two-instance run (build/harness/wsd4, port 27800)

`python scripts/harness/coop_two_instances.py --scenario death --folder coopws --port 27800 --swing-hold 300 --b-delay 8
--env-b DRAKAN_COOP_CHARACTER=1798 --env DRAKAN_GHOST_LOG=1` (B announces Capn Serris, so A shows B as the stand-in).

* **[run]** A: `[ghost] weapon record 33cbc -> item ... (ghost 2 hand 0 bone 30)` for the stand-in; B (Rynn ghost of A):
  `bone 37`. Screenshots (`sheetA.png`) show the stand-in with the dagger in the fist, swinging.
* **[run]** Stand-in clips on A: attack clip `0x50df5` repeats with B's swings, death `0x50df4` plays when B dies (B died and
  respawned several times: "the partner's death reached the host", "respawn granted" PASS), the orcs hit the stand-in's ghost
  ("ghost ... (player 2) hit by ... for 10") and a host creature died.
* **Not proven**: B's own swings did not produce a claim on an orc in any of 4 runs ("client: claim on creature" absent;
  the harness's `claims sent` / `claims applied` checks FAIL). B plays its attack clip (20691) but the hits never register as
  claims on this loaded machine; with the 60 ms click it failed the same way, so this is not specific to the stand-in.
  The "partner swings -> orc is hit" link is therefore not demonstrated; the swing, the stand-in's hits taken and its death are.
  Also found: B's inventory equip can fail under load ("That can't be used right now", wsd3), so a run may have no weapon.
* The harness got `--swing-hold <ms>` (the attack button hold, default 60).
* The weapon swap on a change (drop + respawn) was not run on a stand-in.
