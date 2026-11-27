# Order of the Flame: the Sword of Flame's special attack hurts nobody

Owner report: "Sword of Flame special right-click ability doesn't seem to be damaging any enemies". Private runs under
`build/harness/fs-*`, the decoding script and the patched pack under `build/flame-sword/`. No game data in this note.

## Not the input

Right click is Circle (`MOUSE2 = CIRCLE`, the player's `drakan_input.ini`), command `0x4007`, the same command a pad's
Circle sends. With a melee weapon drawn, the game runs that weapon's special attack. It spends one of the weapon's
`# of Secondary Charges` (the number in the right orb) and spawns the class named by `Secondary Attack Object`
(field 31 of a melee weapon, engine 14). In the runs below the charges count down and the effect is drawn. So input,
hooks and the campaign's no-spells switch (`[spells] Circle: no cast` appears only with empty hands) are not involved.

## Cause: the converted special names the effect, not a generator

Decoded from the installed pack (`new/RESOURES.ODB` records with the field lists of its `0103` override) against the
disc's `RESOURES.ODB`:

| Class | `Secondary Attack Object` | What it is |
| --- | --- | --- |
| Disc Fire / Ice / Lightning / Earth Blade, Blade of Darkness, Flamestrike, Maulgak Maul, BoneGrinder Staff | `!! ... Secondary Gen !!` / `!! ... Ring Gen !!` | always an Object Generator (engine 63), which spawns the effect with `Parts Inherit Links From Parent` = 1 |
| Converted Flame Sword (17843) and both Runeblades (18000, 18100) | `0x20c07` = FX #3079 "Fire Crystal FX" | the ring effect itself (engine 90), named straight from the PC class |

When the PS2 weapon spawns the engine 90 ring directly, the ring runs out from Rynn and burns along the ground, but it
hurts nothing. When a generator spawns the same kind of ring, it hurts: Drakan 2's own Flamestrike special is
`!! Fire Ring Gen !!` (16497), which spawns `!! Rynn's Fire Ring !!` (16139). That ring has the same engine, damage 6,
speed 4000 and life 1.5 s as the PC Fire Crystal FX.

## Evidence

Owner's play folder `dist/DrakanPC` (exe of 27 Nov 21:20, owner's `drakan.ini` and `drakan_input.ini`, HD textures,
DLSS). `play_run.py` with `DRAKAN_GAME=d1`, start in Wartok Canyons, Ctrl to dismount, placed on the Sword of Flame
(picked up: "Sword of Flame: 15AP (150/150)"), drawn through the native Tab screen (`DRAKAN_INVENTORY_SCREEN=native`,
Tab, Enter, E). Then placed 1,300 units from LeatherWartock ref 218 and two right clicks (a tap and a 1 s hold).
Hit points come from `DRAKAN_PLAYTEST_WATCH=class=10031,dataoff=24c` (data + 0x24c, max at + 0x250). Recipe:
`build/flame-sword/recipes/repro4.txt`.

| Run | Pack | Seen |
| --- | --- | --- |
| `fs-repro4` | installed `ootf-levels.zip` | charges 5 -> 4 -> 3, wall of fire through the Wartok (`s9_melee.png`); Wartok 0x32 (50) on every line from before the first click to the end |
| `fs-gen1` | the same zip with field 31 of the Flame Sword set to 16497 (`build/flame-sword/mods-gen`, `DRAKAN_MODS`) | Wartok on fire (`s8.png`); hit points 50 -> 44 -> 41 -> 38 -> 35 -> 34 over the next seconds |

An earlier attempt without dismounting (`fs-repro1`) fired Arokh's breath with the right click instead. That one does
hurt (50 -> -25) and is not this bug.

## Fix (converter, formats `ootfsecondary.ts`)

`relocateClassDefaultReferences` takes a new `replace` hook for references that did resolve. The pipeline uses it so
that a melee weapon whose PC special names an effect that is not a generator takes the disc generator listed for that
effect's engine: engine 90 (ring) -> `!! Fire Ring Gen !!`. It looks only in the weapon's own ODB, so the native
reference keeps its meaning. The convert log names each swap ("PC special attack ... is not a generator ...; Drakan 2's
... stands in"). A PC special that already is a generator, or an engine with no listed stand-in, keeps its converted
reference. Tests: `test/ootfsecondary.test.ts` (stand-in, untouched cases, the relocation through the hook);
formats `vitest run` 817 passed, `tsc --noEmit` clean.

The pack must be converted again (editor Order of the Flame import, or `ootf-convert.ts --campaign 1`). The exe does not
change.

## Not verified

* A full re-conversion with this converter: the Order of the Flame install is not on this machine. The in-game check
  used the installed pack with the single dword the converter now writes (16497) patched in by hand.
* The Runeblades (same reference, same fix) were not played. Whether a re-converted pack keeps the save build
  (`save build ... kept`) was not checked; only a class default changes.
* The converted Ice Sword and Lightning Axe have no special at all (`Secondary Attack Object` 0 in the pack). Whether
  their PC classes name one that has no PS2 class was not checked.
* The modern Tab screen's equip and the pad's Circle were not played (both send the same command `0x4007`).
