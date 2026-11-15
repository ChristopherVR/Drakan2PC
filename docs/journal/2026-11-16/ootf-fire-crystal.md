# Order of the Flame: the Fire Crystal does nothing, Arokh has no breath

Two owner reports from the OotF campaign: "Drinking a fire crystal does nothing" and "We are unable to fire Arokh's breath
at all once we have equipped him". Private runs, packs and probes are under `build/review/ootf-crystal/` (`runs/`,
`packs/`, `tools/`). No game assets are in the repository.

## What the PC data says

PC class dumps (`tools/classdump.mts`, `tools/byid.mts` over `build/ootf/cab_disc`):

| PC class (Resources.odb) | Engine | `Effect` | What it is |
| --- | --- | --- | --- |
| Fire Crystal #3158 | Magic Crystal 91 | FX #3079 "Fire Crystal FX" (engine 90) | a ring of fire that runs out along the ground from Rynn: 4,000 wu/s, 1.5 s, 6 damage, Med Burn Effect |
| Ice Crystal #3159 | 91 | FX #3845 "Ice Shield FX" (engine 119) | an ice shield round Rynn for 15 s, 5 damage per second, freezes what it touches (Frozen Sword FX) |
| Lightning Crystal #3157 | 91 | FX #3083 "Lightning Crystal FX" (engine 96) | hold Use to charge (3 s), then a ground lightning bolt of up to 100 damage |

All three have `Quantity` 5, `Player Slot` 1, `Powerup Object` 0. So the crystals are Rynn's own use-items (a spell in
a stone, five charges); they have nothing to do with Arokh's breath.

Arokh's breath comes from Dragon Power-ups (engine 57) linked to the Dragon: the PC "Fireball Power Up" (#1053) has
`Weapon` = "Fire Ball 2" (#5393, engine 48) and `Alt. Weapon` = "Flame Breath Stream" (#3478, engine 21). In the
campaign levels (`tools/dragonlinks.mts`) Ruined Village's Dragon 1011 links Fireball Power Up 3157, Alwarren's Dragon
6 links 155, Rift World's Dragon 240 links 563, and Volcano's Dragon 40 links the Ice, Fireball, Lightning and Poison
Gas power-ups.

## Arokh's breath: cause and fix

The converter keeps a class reference only when the referenced PC class has a PS2 class. Engines 48 (fire ball), 21
(breath stream), 108 (magma shot) and 42 (ice projectile) are in `REFERENCE_ENGINES` (PC-only effects), so the
converted power-ups came out as (decoded from `new/RESOURES.ODB` of the shipped pack, `tools/packrfl.mts`):

| Converted power-up | Weapon / Alt. Weapon before | after |
| --- | --- | --- |
| Fireball Power Up (2 records) | 0 / 0 | 13322 "!! FireBall 3 !!" / 10724 "!! FlameBreath !!" |
| Ice Powerup, Ice Quickening Powerup | 0 / 18244 | 13604 "!! Ice Burst !!" / 18244 |
| Magma Quickening Power up | 0 / 0 | 13322 / 10724 |
| Multi Fire / Magma / Ice PwrUp, QOD Standard Weapon (arenas) | 0 ... | unchanged (see below) |
| Lightning, Poison Gas (both kinds) | 10735 / 10736, 18299 / 0 | unchanged (already carried) |

The fire power-up is the one that gives Arokh his breath for the rest of the campaign (Ruined Village), so with both
fields empty the breath key did nothing on any input. It was not an input binding: Attack (left mouse / Square) reaches
the dragon's attack as before.

Fix (formats `720240a` and `938e188`, `ootfbreath.ts`, hooked into `relocateClassDefaultReferences` as `standIn`): when a Dragon
Power-up's `Weapon` or `Alt. Weapon` points at a PC breath with no PS2 class, it takes the same field of Drakan 2's own
power-up of that element, `#1 Fireball Power Up` for fire ball / breath stream / magma (Drakan 2 has no magma breath;
fire is the nearest) and `#3 Ice Power Up` for the ice projectile. The value is copied only inside the power-up's own
ODB (RESOURES), so the native reference keeps its meaning. The convert log names each stand-in ("PC breath ... has no
PS2 class; the Weapon of Drakan 2's ... stands in"). Arena power-ups ("Multi ... PwrUp", "QOD ...", any class of a
multiplayer ODB; they are placed in the air and QOD arenas) are left as they were (`isArenaPowerUp`): the converted
arenas breathe through the host (`drakan_pvp.cpp`) and a native breath on top of it was never played. Checked on a
re-conversion (`packs/b3`): the campaign power-ups carry the stand-ins, the arena ones are still empty.

### Evidence

Same exe (`build/final-game` ELF copied to `elf/`, play folder copied from `dist/DrakanPC`), Rift World direct start
(arrives on Arokh; its dragon links the Fireball Power Up), ten left-mouse taps and a 3 s hold, through the scripted
host mouse button (the keyboard / mouse path):

| Run | Pack | Breath orb | Breath projectiles (class `0x10008`) born |
| --- | --- | --- | --- |
| `rw-old` | shipped `build/ootf/mods/ootf-levels.zip` | dark, empty | 0 |
| `rw-new` | `packs/b1` (this fix) | amber | 12, fireballs drawn in front of Arokh (`runs/rw-new/a1.png`) |

A quick save made on the old pack (`rw-save-old`) is refused by the new pack ("made with a different version of the mod
pack"), as every pack rebuild does, so an old save with a breathless Arokh cannot carry over; a new campaign (or any
level reached on the new pack whose dragon links a power-up: Ruined Village, Alwarren, Volcano, Rift World) gives the
breath.

Tests: `vitest run` 737 passed (71 files) and `tsc --noEmit` on the committed tree; `test/ootfbreath.test.ts`
(synthetic power-ups: fire, stream, magma, ice; other classes, fields, engines and ODBs untouched; only PC-only engines).

## The crystals: cause, and why it is not fixed yet

The converter clones Magic Crystal 91 (no PS2 class) from the mana power-up 171 (`PS2_SUBSTITUTES`). The clone keeps
the PC icon, quantity and sounds but has no `Effect` field, so the PC effect is dropped; `Mana Boost` stays at the
template's 100 (Fire, Ice) or 50 (Lightning). By the class data, using the crystal consumes one and fills Rynn's mana,
which a campaign with `campaign_spells = false` neither shows nor uses: "nothing happens". (Inferred from the class and
the mana power-up's handler; a scripted use was tried twice, `runs/wc-use1`, `wc-use2`, and did not get to the item: the
first stopped at the Equipment help box, the second opened the modern screen but its grid showed no items to steer to.) The same holds for all three crystals in all
levels (Fire Crystal in Ruined Village, Wartok Canyons, Islands, Mines, Alwarren, Rift Crystal, Succubus Lair; Ice and
Lightning Crystals where placed).

No Drakan 2 item class casts an effect when used: potions (10, 89), mana (171), keys (33), the spell unlocker (178),
books and treasure have no effect-on-use field. The PS2 does have the effects: FX #3079 "Fire Crystal FX" is still a
class 90 ring in `FX.ODB`, the Frost Shield (RESOURES #15338) is class 119, and Drakan 2's own Rynn weapons spawn fire,
ice and crystal rings as their secondary attack (generators 16497 / 16138 / 16709). Engine 96 (the Lightning Crystal's
charged bolt) has no PS2 class.

Probe: the shared item field `Powerup Object` (unused by every PS2 and PC class) was pointed at FX #3079 in a private
pack (`packs/b2`, `tools/patchpack.py`). Result (`runs/wc-pick`, `wc-use1`, Wartok Canyons): the engine spawns that
object at the item at level load (a class `0x1005a` ring at the crystal's exact spot from the start, before Rynn is near
it); it was gone about 3.5 s after Rynn was placed on the crystal (a pickup, or the ring's own 1.5 s life once it woke;
not told apart). Either way it is spawned with the item lying in the world, not when the crystal is used, so it is no
fix.

What a fix needs (engine code, not done in this round):

1. The converter writes `gameplay/crystals.ini` (like `bows.ini`): the converted crystal class -> its PS2 effect class
   (#3079 fire ring; #15338 Frost Shield for ice; a class 58 bolt as an approximation for lightning).
2. A runtime hook that notices a crystal use (the native consume-one path stays, so quantity and saves behave; a
   quantity drop of a carried crystal handle is enough) and spawns the effect at Rynn through the engine's public spawn
   `0x181ad8(ctx, owner = Rynn, transform, 0, 0, resIndex)` (`docs/journal/2026-10-31/actor-spawn.md`) on a guest
   dispatcher thread, as `drakan_arena.cpp` does for its effects but with Rynn as the owner so the ring spares her.
3. A private exe build to verify: ring drawn, an enemy's hit points drop, Rynn's do not.

## Other consumables

Decoded from the re-converted pack against the PC classes: Lesser / Greater Life Crystal (engine 10) keep Health Boost 5 /
10, Potion of Speed (89) FX Type 2 for 5 s, Ring of Invisibility FX 0 for 30 s, Rune of Invulnerability FX 1 for 20 s,
the Dragon health power-ups (138) 50 / 100; the health potions map to the native PS2 potions. Only the Magic Crystals
(91) lose their effect.

## Not played

The Ruined Village route to the mount (the fire power-up there), the Wartok poison pickup, the ice stand-in (13604) and
Volcano with its four linked power-ups; only Rift World's fire was fired in game.

## Rebuilds

* Breath: pack only. Re-convert `ootf-levels.zip` with formats `938e188` or later; the exe is unchanged.
* Crystals: unchanged (still a hidden mana refill).
