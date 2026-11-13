# Order of the Flame: item requirements (round 3)

Owner: "Item requirements do not translate well, e.g. the chain mail armor you can find early in the game we can't wear
for some reason." Private runs and evidence under `build/review/ootf-items/` (`runs/`, `evidence/`, `packs/`); no game
data in this note.

## Reproduction (real input, one placement)

Own play folder (a copy of `build/review/baseline-play`: the owner's `drakan.ini`, HD textures, DLSS, exe `4fddfbc8...`,
private ELF copy), the current pack from `dist/DrakanPC/mods/ootf-levels.zip` (sha256 `ccc7e58a...`). New Game on an
empty card, the Intro and the Ruined Village opening played out by the auto taps until control. Then one test-aid
placement next to the first Chain Mail of Ruined Village (PC object 2498, in the cave west of the village), E picks it up
(real key), Tab, Right, equip (Enter on the modern screen, Square on the native one).

| Screen | What the game says (`evidence/before_*`) |
| --- | --- |
| Modern (owner's setting) | "Rusty Chain Mail", Exp. Level Needed 3, Armor Rating 20, Durability 15/100, red "Requirement not met"; Enter gives "That can't be used right now." and nothing is equipped |
| Native Tab screen | "Exp. Level Needed: 3" in grey (not met); Square does nothing |

Rynn in that run: character level 1, experience 0, skills Melee 1/8, Archery 1/8, Magic 1/8 (stats block `0x4fbac8`).

## How requirements work

* **Order of the Flame (PC):** no item class has a requirement field. Armour (engine 45) carries Damage Prevention,
  durability and the Armor Model; weapons (14), bows (79), potions (10, 89), crystals (91), power-ups (57), keys (33) and
  quest items (117) have no Skill Level, level or strength field either (checked on every class of every PC ODB).
* **Drakan 2 (PS2):** item classes have `Skill Level` (property `0x13`). The equip check `341858` (called from the
  equip path `31e460`) compares it for weapons (object type 10) with the Melee skill row (melee 14) or the Archery row
  (Bow 79), for spell scrolls (178) with the Magic row, and for armour (`0x1002d`), shields (`0x10090`) and `0x100c7`
  with the **character level** (`dec(S+0x28)`). Both inventory screens show the value ("Skill Needed", "Exp. Level
  Needed") and draw it greyed or red when it is not met. Drakan 2 has no strength stat.
* **Converter:** a PC item class is either mapped by name onto a disc class or cloned from a disc class of the same
  engine id, and the clone keeps every template field the PC class has no field for. Since 5 November the inherited
  Skill Level of weapons and bows is cleared (`inheritedWeaponSkillField`), armour was not: each PC armour got the
  level of whichever disc armour the template choice picked (Chain Mail from "Chain Armor": level 3), and the PC
  "Dragon Armor" was mapped by name onto the disc Dragon Armor itself (level 8, unbreakable, Drakan 2 values).
* **Rynn in the campaign:** starts at level 1 with the three skill rows at 1. She gains experience by the Drakan 2 rule
  from the creatures' template "Experience Given By Death" values (the PC creatures have none), so levels do grow, but on
  a Drakan 2 curve the PC game does not have.

## Fix (formats)

`ootfclass.ts`: the inherited requirement rule now covers Armor (45) as well as melee (14) and Bow (79): a converted
item whose PC class has no Skill Level gets 0 in the template's single integer Skill Level field. A PC armour with the
same name as a restricted disc armour (Dragon Armor) is cloned from it instead of being mapped onto it, as was already
done for weapons, so Drakan 2's own Dragon Armor keeps level 8. Drakan 2 classes and packs are unchanged; only the OotF
conversion changes. Potions keep the disc value 1 (the check never reads it for them). Spell scrolls (178) are left to
the campaign spell work.

Pack diff against the same converter without the fix (both converted here, full 23-level campaign): the class sets are
identical except one new RESOURES record (the cloned "Dragon Armor"); its id shifts later clone ids, so a new game is
needed (the game refuses saves from another pack build anyway).

## Audit: every equippable campaign item

PC: requirement in the PC data (none for all). Before / after: the converted Skill Level (for armour the needed
character level, for weapons the skill row). "Early / mid / late" is whether an early (level 1, skills 1), mid and late
OotF player could equip it before the fix; after the fix every row is yes for all three.

| Item (PC class) | Engine | Placed in | PC | Before | After | Before: early / mid / late |
| --- | --- | --- | --- | --- | --- | --- |
| Chain Mail | 45 | Ruined Village (2), Wartok Canyons (2), Grimstone Mines, Alwarren | none | level 3 | 0 | no / depends on XP / yes |
| Scale Mail | 45 | Wartok Canyons, Grimstone Mines, Grotto | none | level 4 | 0 | no / depends / likely |
| Plate Mail | 45 | Islands Night, Rift Crystal, Succubus Lair, Volcanic | none | level 6 | 0 | no / no / depends |
| Banded Mail | 45 | Grotto, Islands (2), Alwarren, Rift Crystal | none | level 7 | 0 | no / no / depends |
| Dragon Armor | 45 | Volcanic | none | level 8 (disc class itself) | 0 (own clone) | no / no / depends |
| Atimar Blade, Short Sword, Long Sword, Mace, Scimitar, Battle Axe, Battle Hammer, 2-Handed, club, Troll Axe, slave pick, Magic Short / Long / 2 Handed Sword, Magic Mace / Axe / Hammer, Flame Sword, Ice Sword, Lightning Axe, Speed Scimitar, Death Mace, Runeblade | 14 | all levels | none | 0 (since 5 Nov) | 0 | yes |
| Long Bow, Heavy Bow, Speed Bow, Energy Bow | 79 | Wartok, Grimstone, Grotto, Islands, Succubus, Volcanic | none | 0 | 0 | yes |
| Health Potion (Red / Blue), Invisibility Potion, Invulnerability Potion | 10, 89 | everywhere | none | 1 (not checked) | 1 (not checked) | yes |
| Crystals, power-ups, keys, Bell Hammer, Soul Crystals, Succubus Mirror | 91, 57, 33, 117 | | none | no field / not checked | unchanged | yes |

"Depends" means the level the Drakan 2 experience curve gives by then; it was never measured. The multiplayer armours of
the arena levels (Banded, Plate, Dragon) also go to 0.

Look and effect: the converted armour shows the disc armour model of its template on Rynn (Chain Mail: the disc chain
armour), as before; the PC armour skins are not converted. Damage Prevention, durability and the per-object values come
from the PC (Chain Mail 0.2, shown as Armor Rating 20, the "Rusty" one 15/100).

## Proof by play (fixed pack `packs/fix1`, sha256 `60276717...`)

Same play folder and route, New Game each time:

* Modern screen (`evidence/after_modern_*`): the Chain Mail shows Exp. Level Needed 0, no red line; Enter equips it
  ("Armour: Rusty C...", the inventory portrait wears it); Atimar's Blade equips into the right hand; Duncan's Battle Axe
  (placed next to it, picked up with E) equips into the right hand. Armour slot before: none (Armor Rating 0); after:
  Rusty Chain Mail, Armor Rating 20. In the world at the starting house Rynn wears the chain armour
  (`after_modern_a08_start_armour.png` against `before_modern_a_state1.png` at the same spot).
* Native Tab screen (`evidence/after_native_*`): "Exp. Level Needed: 0" in the normal colour, Square equips it, the
  portrait wears it.

Tests: formats suite 61 files, 623 tests pass (two new cases: armour 45 in the inherited-requirement rule, the
same-name Dragon Armor clone; both fail without the fix); `tsc -p` and Prettier clean.

## Not verified

* That the armour actually reduces damage in a fight (Armor Rating read from the screen only).
* Scale, Plate, Banded and Dragon Armor in game (checked in the pack only); only the Chain Mail was equipped.
* The Drakan 2 experience curve in the campaign (what level Rynn reaches by each level).
* The rebuilt dist pack and the full-build exe: proof ran on the baseline exe with a pack converted without HD sources.
* The "0" requirement line still shows on both screens (as for weapons since 5 November); hiding a zero requirement would
  be a runtime UI change.
