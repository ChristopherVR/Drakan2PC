# Drakan 2 characters and weapons for converted OotF levels (2026-11-08)

User request: besides textures, use the Drakan 2 version of enemies, NPCs, Rynn, Arokh and some weapons where the disc has one,
configurable like the texture pairs ([Drakan 2 textures](ootf-ps2-texture-substitution.md)).

Probes and runs are private under `build/ps2mdl/` (class audit of all 23 levels `audit.log`, `wl.ts` / `cls.ts` class lists of
both games, converter scripts `conv*.sh`, recipes `fight.txt`, `dead.txt`, `weapon*.txt`, `fly.txt`, `scene.txt`) and
`build/ootf-run/ps2mdl-*`. Nothing from either game is quoted here; only class names and numbers.

## What was already Drakan 2

The class audit (`ootf-convert.ts --audit .` over all 23 levels) shows how each OotF character is played:

* **Rynn** (engine class 9) and **Arokh** (the player's dragon, engine class 28) are name-mapped onto the disc's Rynn and
  Arokh in every level: the player and her mount are the Drakan 2 models with their own clips. The PC Rynn model is only
  converted as the source of her retargeted scene clips (ootf-rynn-scenes.md), never drawn.
* **Wartoks and orcs**: `Troll Leather / Chain / Plate` (and their flankers) and `Orc Leather (purple)` / `Orc Plate (yellow)`
  were already played by the disc's `LeatherWartock`, `ChainTock`, `PlateTock` and the purple / yellow orc classes
  (`PC_CLASS_ALIASES`), with the PC clips retargeted onto the disc rigs and replacing the disc clips of the same name.
  `Spider` likewise (same 37 joints). `War Giant` / `Primitive Giant`, `Skeleton Dragon`, `Fish`, `Ballista` use the disc model
  and clips already.
* **Own PC models**: Scavenger (the disc has a `Scavenger`, but its skeleton matches under half the PC joints), Death Mage,
  Delon, the Succubus, Queen, Goblins, knights, the slaves, the tuiri lady, dragons other than Arokh, and the scene-only
  characters (`Wartock Leather` etc., rfl 17 props that only scenes move).
* **Weapons**: every PC weapon class is cloned onto a disc weapon template with its converted PC model.

## What changed (formats `ootfps2model.ts`)

`DEFAULT_PS2_MODEL_SUBSTITUTIONS`: rows `{ category, ootf: '<OotF class>', ps2: '<Drakan 2 class>', label, why, defaultOff? }`.

* **Enemies** (the six Wartoks, two orcs, the spider): with the row on (default) the creature stays the disc class and keeps
  the disc's own clips (`enemyAnimations: 'drakan2'`): the retarget step is skipped for it. `'ootf'` keeps the earlier
  retarget. A row turned off converts the PC model with its own rig (`keepOwnModel`). Scavenger is a row that is off by
  default: turned on it is played by the disc's Scavenger class, so its health and attacks would be the disc's.
* **Weapons** (Short Sword, Long Sword, 2-Handed, Scimitar, Speed Scimitar, Mace, Battle Axe, Battle Hammer, Troll Axe, club,
  Goblin Club, the three bows of one PC model, Arrows): the class is cloned as before with its PC data (damage, reach,
  durability, effects); only its model becomes the disc weapon's model, a model of `RESOURES.MOD` named in place
  (`OotfAssetBuilder.useNativeModel`: no conversion, nothing appended to the MOD). Weapon classes cloned into another ODB keep
  their converted model.
* Not in the table: magic, elemental and quest weapons (Runeblade, Atimar Blade, Flame / Ice Sword, Lightning Axe, Death Mace,
  the multiplayer `Frag :` set), NPCs (no Drakan 2 counterpart), shields (Order of the Flame has none).

## In game (private copy `build/ps2tex/game`, `ootf_level_run.py --no-convert --campaign`, god mode, game lock held)

| Run | Pack | What the frames show |
| --- | --- | --- |
| `ps2mdl-fight-on` / `-off` | Ruined Village, four orcs pulled to Rynn (test aids `DRAKAN_DBG_NOOBJ` / `DRAKAN_DBG_PULL*`) | on: the Drakan 2 purple and yellow orcs with tridents stand, walk in and swing (`build/ps2mdl/fight_on_bright.png`); off: the OotF orc models with their hammers (`fight_off_bright.png`) |
| `ps2mdl-dead-on` | same, health set below 0 | a Drakan 2 purple orc ends its death lying on its back with its drops around it and stays down to the end of the run (8 s, `dead_on_big.png`) |
| `ps2mdl-wk-on` / `-off` | Wartok Canyons, four Leather trolls pulled | on: the Drakan 2 Leather Wartoks (maned, bare-armed) walk and attack; off: the OotF trolls with shields (`wk_on_off.png`) |
| `ps2mdl-fly` | Islands (texture pack, Arokh is not in the table) | the Islands arrival scene: the Drakan 2 Arokh flies out of the cave with Rynn on his back, wings beating (`fly_sheet.png`) |
| `ps2mdl-wp3-on` / `-off` | Ruined Village, weapons pulled to Rynn, picked up, the Long Sword equipped in the inventory | on: the inventory preview and Rynn's right hand show the Drakan 2 Steel Longsword (gold guard), still named "Worn Long Sword" with the OotF damage 15; she draws and swings it with a correct grip (`wp3_held_on_off.png`, top); off: the OotF sword (bottom) |
| `ps2mdl-scene-on` | Intro + Ruined Village campaign, New Game | the Ruined Village opening plays through: Rynn walks beside Delon, the Wartok ambush, Rynn draws a long steel sword and fights in her stance with her feet on the ground (`scene_sheet.png`, frames 60 s to 140 s); the scene-only Wartoks keep their converted models |

## How this interacts with the creature fixes

The grunt fixes of `ootf-grunt-pose-fixes.md` (death clips that hold, the folded pass-through joint, the absolute root track)
live in the retarget path. With the default (`drakan2`) the Wartoks, orcs and spider do not take that path: they play the
disc's own clips, which already hold their death pose (the run above). The fixes still apply with `--enemy-animations ootf`,
to every creature that is not in the table (Scavenger, Death Mage, the scene-only Wartoks ...), and to the PC clips of seated or
dice-playing grunts, which the disc clips do not have: with the default those grunts use the disc's idles.

## Open

* Arokh in scenes: his PC scene clips still map through the converted PC dragon's clip ids while the actor is the disc's
  Arokh (the same wrong-database problem the player had); the Islands arrival showed him flying with wing beats, but whether
  those are his scene clips or his own flight was not separated.
* The scene-only characters of the Ruined Village opening (`Wartock Leather` and others) keep their converted PC models: a
  swap would need their scene clips retargeted onto the disc rigs (as for the player) and is not done.
* Death Mage, Navy Hueges, Ice and Crimson dragons have disc classes of the same kind (`Death Mage`, `Purple Orc Center`, `Ice
  Dragon 1`, `Crimson Dragon (Bat)`), but using them would give them the disc's health and behaviour or need a template swap
  of their clip fields; not in the table.
