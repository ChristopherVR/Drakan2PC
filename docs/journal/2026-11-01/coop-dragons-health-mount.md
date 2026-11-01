# Co-op dragons: parked animation, Arokh's health, real mount attempts (2026-11-01)

Follow-up to [coop-dragons-ground](coop-dragons-ground.md) and [coop-party-health](coop-party-health.md).
Code: `drakan_netplay.cpp`, `drakan_ghost.{h,cpp}`, `drakan_playtest.cpp`. Harness: `scripts/harness/coop_dragon_ground.py`
(port 28500 used here), `scripts/harness/coop_dragon.py`.

## 1. Parked dragon plays an idle clip (proven)
* `drakan_ghost::setAnimFree(id, clipLow)` writes one clip (low 16 bits; the level's SYSTEM prefix is added as for any dragon
  ghost) to every channel *once* and then leaves the channels alone: the engine advances and loops the clip itself
  (the rider's own dragon shows clip time cycling 0..0.6 s). The next `setPose` returns to externally driven clips.
* On dismount `drakan_netplay` calls it with `parkedDragonClip(lastClip)`: **`G_Idle_WingsW2` (SYSTEM clip 0x880)** after any
  ground / flight clip, **`F_hover2` (0x663)** when the last mounted clip was a hover (0x663, 0x899, 0x63c, 0x78c).
  Names and the dragon group (ADB group 71 of SYSTEM.ADB: F_* flight, G_* ground, `Ar*` talk) come from `build/adbnames.json`.
* Proof: `coop_dragon_ground.py --scenario mount` (fake mount aid, Surdana): the parked dragon's wings are in a different
  position in four screenshots 3 s apart (`build/harness/dg_mount_c/parked_sheet.png`); spawned 1, parked 2, re-mounted 1 PASS.
  Not separately proven: the hover variant (a real dismount always ends on the ground).

## 2. Arokh's health (field found, sent, shown)
* **Field: float at the dragon's instance data + 0x6b4** (`data = *(*(dragon+0xfc)+0x10)`), 100.0 at full health, and the
  engine regenerates it (1 HP per 0.1-0.8 s after a poke to 40, log `dragon health N`). Evidence: the engine getter `0x3d3b80`
  returns it (-1.0 when no dragon is linked, called with the stats block `0x4fbac8` as a handle to the dragon) and `0x3d3bc0`
  adds a delta to it (callers `0x410468` item pickups, `0x4110b8`). It is **not** Rynn's obfuscated stats block (that stays
  `0x4fbac8`+0xc/0x10). No max field exists: `0x6b8` is a different value (the engine writes -1 there). The max sent is the highest
  value seen during the ride (100 at the start of a ride at full health).
* Read every tick while the controlled actor is Arokh (`readDragonHp`); on foot it is 0 (the wire's "on foot / unknown").
  `setLocalVitals` merges it into the Vitals message (type 29, `dragonHp` / `dragonMaxHp`) when the caller passes 0.
* Test aids (env, `drakan_netplay.cpp`): `DRAKAN_DRAGON_HP_PROBE=1` logs every number-like word of the dragon data / actor
  record once per 3 s; `DRAKAN_DRAGON_HP_POKE=<hex off>:<float>[,..]` writes floats into the dragon data at every probe tick.
* Proof: `coop_dragon.py --name hp5 --env-a DRAKAN_DRAGON_HP_PROBE=1 --env-a DRAKAN_DRAGON_HP_POKE=6b4:40` (Ravenshold, A flies):
  B's party panel shows the thin amber DRAGON bar under "Alpha (Rynn)" (`build/harness/hp5/b/b_s18.png`, bar ~54 %, regenerating
  between the 3 s pokes; undamaged `hp1/a/a_s03.png` has no bar of its own). Damage was injected by poke, not by an enemy: no
  creature hurt Arokh in these runs, so the *natural* damage path is only inferred from the engine code above.

## 3. Real mount in a ground level: NOT proven
* Surdana: Arokh sits at (737879, 200000, 198858) in the level's holding area (every NPC of the level is staged there on a
  floor-less "stage"; the player started at 450762, 171700, 340186). `DRAKAN_PLAYTEST_POS="737550 200000 198858 -1.571"` works:
  Rynn is placed next to him (`placed after 1 try`), stands there while held (screenshots show Arokh's claws and wing and the
  staged NPCs), and falls and dies once the hold ends (no floor). Pressing E (= L1 "Call Arokh / Use", held 1 s) while held did
  nothing visible and no `mountChanged` happened. Surdana's Arokh is probably not mountable (story-gated).
* Northern Tundra (`Level 4B`): Arokh stands 2.5k units from the start in the cave; teleporting next to him works (Rynn right
  at his flank, he idles and turns his head, screenshots `build/harness/dg_real8..10`). E while held / free, with and without
  walking into him, did not mount. Not worked out: the mount condition (probably a facing / zone test in the dragon
  controller `0x271ac0`, or L1 only works with Arokh "available"). Level Tundra, Shiverbane and Kragmor are the near ones
  (Arokh 2.5k / 2.5k / 12.8k from the start); Ravenshold is the only level that starts mounted.
* Tools added: `DRAKAN_PLAYTEST_HOLD=<boundaries>` (hold length), `DRAKAN_PLAYTEST_SOFT=1` (only pull back beyond 400 units, so
  a mount's own movement is not overwritten), and the hold is released if the controlled actor changes (mounted).
  `coop_dragon_ground.py --scenario real --level <name> --pos "x y z yaw"|none --press ms,ms --raw "ms cmd"`.
  Note the machine load made the recipe clock and level load drift by tens of seconds; place keys by the log, not by guess.
* The sender path therefore still rests on the fake-mount aid; the receiver only sees `mounted` flip, which is also what
  Ravenshold's real mount (start state) exercises.
