# Ranged PvP in the arena: bow and spell (2026-11-01)

Harness: `python scripts/harness/arena_ranged.py --port 27200` (map `scripts/harness/maps/arena_ranged_test.ini`, output
`build/harness/arena_ranged/{a,b}/` with screenshots and `summary.json`). Tags: **[game]** measured in two game instances,
**[open]** not proven.

## What was found
* **A bow cannot be given through the loadout [game].** The factory returns an object for the Short Wood Bow (`0x31bc9`, class
  `0x1004f`) but the registry never takes it (self ref `+0x12` = 0, logged now as `never became a live actor ... selfref 0`);
  the arrows (`0x3379d`) return 0. Same limitation as the potions / dagger in `netcode.md` "Which res= resources spawn". The
  archer therefore picks the bow up from the ground: Surdana's own bow lies at (425126, 160500, 441806); the test map puts both
  spawn points a few steps from it and A walks into it (the engine takes it by touch, `pickup 0 ... taken here: claim`). The
  bow brings its own arrows (the HUD shows 194 after the shots).
* **Equip [game].** `DRAKAN_ARENA_EQUIP=bow` makes `armTick` wait for a bow in the inventory (no melee fallback, no right hand
  check: the bow goes to the left hand), raise the skill each weapon asks for (every weapon of the inventory now, melee 1 and
  bow 2; before, the first weapon only) and mark the bow the only quick slot for QUICK1; `armed: the bow (actor ...) is equipped`.
* **Spells [game].** `DRAKAN_ARENA_SPELL=<n>`: the debug console command `shaman` (`0x240480` with the game's own string at
  `0x51e3c8`, posted) unlocks the ten spells at level 1, then the selector `0x3422c8(player+0x584, n, 1)` selects spell n and
  equips it (`+0x4b4` of the inventory controller is the selection, the table is at `player+0x1028`, count at `+0x1030`). A
  caster is not given a melee weapon (armTick stays out). Attack (LMB) then casts: a class `0x10008` projectile per cast
  (mana drops 100 -> 85 ...).
* **Spell damage [game].** The spell projectile's item property `0x1a` reads **1**, which is not its damage (the inventory shows
  Fireball 8): `scanProjectiles` now trusts property `0x1a` for spells only when it is 2 or more, else `kSpellDamage` 10. The
  arrow's property reads 0 (the damage is on the arrow item), so arrows claim 5. Both are capped by the server.
* The old arena loadout code (`DRAKAN_ARENA_LOADOUT="0x.. 0x.."`, a test aid replacing the map's list per instance) works for
  spawnable resources (melee weapons).

## Results (two instances, base port 27200, `build/harness/arena_ranged`, logs `build/arena_ranged_run*.log`)
* Run 11 [game]: A picked up the bow, equipped it (194 arrows left on the HUD), 3+ arrows crossed B's ghost -> `claim: hit on
  player 2, weapon 2, damage 5` -> server `hit: player 1 hit player 2 for 80 (weapon 2), health now 20 / 0` (scale 16) -> B
  `hit by player 1: 80 damage` -> 4 kills credited to player 1 (`death: player 2 killed by player 1 (weapon 2)`), kill feed
  "Alpha killed Bravo (Short Wood Bow)", match over by frag limit. 10 of 14 checks passed (arrows fully, B's spells cast).
* Run 7 [game] (scale 8): arrows as above (3 kills, match over) AND a spell: `[pvp] spell ... hit ghost -> claim damage 10` ->
  server `player 2 hit player 1 for 80 (weapon 2), health now 20` -> A `hit by player 2: 80 damage`. 13 of 14 checks
  (the spell kill itself was not reached because the arrows ended the match first).
* Run 5 [game] (older build, shotgun placement): B's spells killed A: `death: player 1 killed by player 2`, kill credited.
* Screenshots: `a_fire_*.png` (bow drawn, kill feed), `b_fire_*.png` (fireball in hand), `b_scoreboard.png`.
* [open] No single run passed all 14 checks. The spell kill and the arrow kills were seen in different runs. After the
  `heal` refill was added (runs 14-20) B froze at ~900 frames (boot / title phase, before any arena code runs) in every run,
  while A ran normally: not diagnosed (nine other game instances were running on the machine; unclear whether it is
  load, another agent's change in the shared build, or the refill). The refill itself (`heal` every 10 s so a caster keeps
  mana) is therefore unverified.

## Test aids added
`DRAKAN_ARENA_LOADOUT`, `DRAKAN_ARENA_EQUIP=bow`, `DRAKAN_ARENA_SPELL=<n>` (`drakan_arena.cpp`), `DRAKAN_PVP_TEST_FRONT=1`
(`drakan_pvp.cpp`: the ghost test placement stays in front instead of rotating), harness `--scale`, `--frag-limit`.

## Side findings
* The arena's first placement races the bounds check: a map whose spawn is far from Rynn's start gets "back to spawn N" (the
  farthest one) a second after the first placement, because the game's own start position is applied after it. The test map keeps
  both spawn points next to the bow so either outcome works. [open] Not fixed.
* The blacksmith's weapon room at (460731, 515952) holds the other `0x31bc9` bows as display items, behind walls: not a source.
