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

## The crystals: cause

The converter clones Magic Crystal 91 (no PS2 class) from the mana power-up 171 (`PS2_SUBSTITUTES`). The clone keeps
the PC icon, quantity and sounds but has no `Effect` field, so the PC effect is dropped; `Mana Boost` stays at the
template's 100 (Fire, Ice) or 50 (Lightning). Using a crystal consumed one and filled Rynn's mana (seen: mana 20 -> 100
on the use, `runs/wc-use4`, and `cr-off` below), which a campaign with `campaign_spells = false` neither shows nor uses:
"nothing happens". The same held for all three crystals in every level that places them (Fire Crystal in Ruined
Village, Wartok Canyons, Islands, Mines, Alwarren, Rift Crystal, Succubus Lair; Ice Crystals in Islands and Mines;
Lightning Crystals in Islands, Islands Night and Volcano).

No Drakan 2 item class casts an effect when used: potions (10, 89), mana (171), keys (33), the spell unlocker (178),
books and treasure have no effect-on-use field. Pointing the shared item field `Powerup Object` at FX #3079 (`packs/b2`,
`runs/wc-pick`) is no fix: the engine spawns that object at the item at level load, while it lies in the world.

## The crystals: fix

Formats `ootfcrystal.ts` writes `gameplay/crystals.ini` into the pack (crystal class resource id -> effect resource id,
like `bows.ini`; reference `modding.md` "Magic Crystals"). The runtime (`drakan_crystals.cpp`, PAL only) hooks the mana
power-up's message handler `392aa0` (vtable `0x533250` slot 3, event 32 of the class handler `392978`; it is where the
Mana Boost is added, for message `0x10cb` with a user). For the use message of a listed crystal it queues one effect;
when the game is back in play (inventory closed) the effect is spawned at Rynn's transform through the public spawn
`0x181ad8(engine, owner = Rynn, transform, 0, 0, resIndex)` on a posted guest call (a host trampoline puts the transform
on the dispatcher stack, as `drakan_arena.cpp` does). The native use is untouched: one charge is consumed. `ModManager`
collects the files (`crystalFiles()`). `DRAKAN_CRYSTALS=0` turns it off; `DRAKAN_CRYSTALS_TRACE=1` logs every call of the
handler.

| Crystal | PC effect | What it casts in the converted game |
| --- | --- | --- |
| Fire | Fire Crystal FX (engine 90) | the same class (native in FX.ODB): a ring of fire from Rynn that sets what it reaches burning |
| Ice | Ice Shield FX (engine 119): a shield round Rynn for 15 s | Rynn's Ice Ring (RESOURES #16137, the Maulgak Maul's secondary ring): freezes and hurts what it reaches. The PS2 Frost Shield (#15338) spawned alone, and through its generator #15568, drew nothing and hurt nothing (`runs/cr-ice1`, `cr-ice2`); Drakan 2 only makes it through the Ice Blade's secondary attack |
| Lightning | Lightning Crystal FX (engine 96, none on the PS2): hold to charge, then a ground bolt | the Lightning Blade's secondary-attack generator (#15567): two seeking bolts from Rynn, no charge. The converted PC bolt (engine 58) spawned alone struck nothing (`runs/cr-lt1`) |

### Evidence

Private exe: scratch build tree `build/review/ootf-crystal/gb` linking a copy of `build/final-game`'s
`ps2_generated.lib` (the ponytail agent's way; `tools/configure.sh`, `tools/build.sh`), run from a private copy of
`dist/DrakanPC` with its own ELF copy. Pack `packs/c3` (full conversion with this converter). Runs drive the native
Tab screen (`DRAKAN_INVENTORY_SCREEN=native`: Tab, Cross for the help box, Square = use), god mode off, Rynn's hit
points from the stats block, creatures' from their instance data (`tools/crydrive.py`).

| Run | Level, setup | Seen |
| --- | --- | --- |
| `fin-fire` | Wartok Canyons: Fire Crystal (2) picked up, Rynn placed by Orc 135 | log `Fire Crystal ... used`, `effect 40c07 spawned at Rynn ... (ok)`; no ring at level load (`0x1005a` count 0 from the start); the orc burns 40 -> 29, Rynn 100 throughout; the crystal count 2 -> 1 (`cr-fire4` `invc.png`). Ring and burning orc: `runs/cr-fire4/r0.png` |
| `fin-ice` | Grimstone Mines: Ice Crystal (5) picked up, Rynn by Troll 1651 | `effect 33f09 spawned`; troll 100 -> 37 and frozen blue (`runs/cr-ice3/i1.png`), Rynn 100; count 5 -> 4 (`fin-ice/invc.png`) |
| `fin-lt` | Volcano: Lightning Crystal picked up, Rynn by Ebon Knight 2699 | `effect 33ccf spawned`; two bolts (`fin-lt/lt_05.png`), knight 275 -> 272 -> 260, Rynn 100 (the health orb flashes violet for a moment) |
| `cr-off` | as `fin-fire`, `DRAKAN_CRYSTALS=0` | `[crystals] off`; mana 20 -> 100 on the use (the native refill), no ring, the orc stays at 40 |

Two uses in a row spawn two effects (`cr-ice1`). The camera in the Volcano shots sits in front of Rynn (the placement,
not the crystal).

Tests: formats vitest (`test/ootfcrystal.test.ts`: fire native, ice and lightning stand-ins, namespace and missing
effect omitted), runtime `crystals_policy` (table parsing, use message) and `mod_test` (enabled packs only, load order).

## Other consumables

Decoded from the re-converted pack against the PC classes: Lesser / Greater Life Crystal (engine 10) keep Health Boost 5 /
10, Potion of Speed (89) FX Type 2 for 5 s, Ring of Invisibility FX 0 for 30 s, Rune of Invulnerability FX 1 for 20 s,
the Dragon health power-ups (138) 50 / 100; the health potions map to the native PS2 potions. Only the Magic Crystals
(91) lost their effect.

## Not played

* Breath: the Ruined Village route to the mount (the fire power-up there), the Wartok poison pickup, the ice stand-in
  (13604) and Volcano with its four linked power-ups; only Rift World's fire was fired in game.
* Crystals: the modern Tab screen's Use and the quick-slot keys (the native screen was used); a crystal used while
  mounted (the effect is dropped with a log line unless Rynn is the controlled actor); the multiplayer "FIRE!" crystal of
  Hill of Fire (omitted: its effect has no PS2 class); Drakan 2 (the hook only acts on resource ids listed by a pack).

## Rebuilds

* Breath: pack only (formats `938e188` or later).
* Crystals: pack (formats with `ootfcrystal.ts`) and exe (`drakan_crystals.cpp`): re-convert `ootf-levels.zip` and run
  `Build-LocalGame.ps1 -Incremental`.
