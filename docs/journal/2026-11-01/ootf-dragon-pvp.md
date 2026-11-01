# Dragon-vs-dragon PvP in the air arenas (2026-11-01)

Follows [ootf-mp-arenas-followup.md](ootf-mp-arenas-followup.md) section 2 ("Not done: a way to hurt anyone from a dragon").
Code: `drakan_pvp.cpp` (`scanBreath`, `setDragonHp`, `probeNewActors`), `drakan_netplay.cpp` (test aid), harness
`scripts/harness/arena_dragons.py`. Runs `build/harness/dragons1..7` (pack `build/ootf-run/mp6`, map Arena of Death (air)).

## What the engine's own breath does (not usable here)
* On Arokh the attack command `0x4006` (Square / left mouse) reaches the dragon controller (vtable `0x5268d8`, slot `+0x164` =
  `0x270870`), which only sets bit 1 of `data+0x90` while the button is held (measured: `0x40000 -> 0x40002 -> 0x40000`; Circle /
  right mouse sets bit 5, `0x40020`).
* With a probe on every registry object (`DRAKAN_PVP_DRAGON_PROBE=1`, any kind, not only actors) nothing is created by either button,
  hovering or flying (`W` held), with the mana bar filled (`stats +0x1c/+0x20`, same encoding as health): no projectile actor, no
  visible flame in the screenshots. So the breath of the converted air arenas is not driven by anything the PS2 engine does for an
  arena player (probably it needs the breath item / state a campaign dragon has); the engine's breath code was not traced further.
  The classes the level files carry for breath ("fire ball, lightning, magma, poison, ice" in the MP database) are not instantiated.

## The host breath (implemented)
* While the local player rides a dragon (controlled actor class `0x1001c`) and is alive, bit 1 of the dragon data's `+0x90` (the
  engine's own attack state, so the player's attack button / key binding works unchanged) fires a **breath claim**: every 20
  boundaries (~0.4 s) while held, and at once on the press edge. Targets: every ghost whose origin is inside a cone in front of the
  dragon (heading = row 2 of the 3x3 at actor `+0x20`, the same axis the test placement uses; checked in `dragons7/a/a_fire_01.png`:
  the partner's dragon placed "in front" is seen straight ahead), range 5000, radius 350 + 0.2 x distance. A remote player is two
  ghosts (rider + dragon): one claim per player per breath. The claim is the existing ranged one (weapon 2, damage 4; server x3 = 12
  of the 100 point bar, so ~9 breaths = ~3.6 s of a held breath kill; the server caps ranged claims at 40 and 20000 units).
* No server change: the claim goes through `sendHitClaim`, the server's range / protection / friendly-fire / interval rules apply.
* **Victim side while mounted.** Rynn's damage method is not Arokh's, so no flinch call: the dragon's health float (`data+0x6b4`)
  follows the server's value on each Hit and is set to the Respawn health; Rynn's HP block is still set (it is what the HUD and the
  server's `startHealth` scaling use). A death freezes the dragon with the same swallowing behaviour handler as a ground death (no
  clip: the death clip is Rynn's), the server's Respawn places him on the next dragon pad spawn (`drakan_arena::placeAtSpawn`).
* Ground arenas and co-op are untouched: `scanBreath` only runs mounted, co-op returns before the module.

## Verified
`python scripts/harness/arena_dragons.py --map arena-of-death-air --maps-file build/ootf-run/mp6/map.ini --env DRAKAN_MODS=<abs>/build/ootf-run/mp6/mods --prefix mp6 --name dragons7 --fire 50 --button 1`
(two instances; A holds the attack, the test aids pin B's ghost 1500 units ahead of A and widen the hit range): B `hit by player 1: 12
damage, server health 88 .. 4`, `killed by player 1 (weapon 2)`, respawn at spawn 4 with health 100, the second kill, `frag limit
reached`, `match over: player 1 wins`, results `[1 'Alpha' 2/0] [2 'Bravo' 0/2]`; all 9 checks PASS.

## Not done / limits
* No flame or particle is drawn for the breath (the engine's was not found): the attacker sees nothing, the victim sees his bar fall.
  A visual (an engine fire-ball effect actor spawned at the dragon's mouth) is the obvious next step.
* The dragon's health float is only written on events; the engine's regeneration (1 HP per 0.1-0.8 s) lifts the bar between hits.
* The dying dragon hangs in the air (frozen) until the Respawn instead of falling.
* The heading axis was only verified by the ghost placement test; two players flying at each other were not driven by script.
* `DRAKAN_PVP_MAX_HIT_RANGE` now also sets the ranged hit range (test aid).
