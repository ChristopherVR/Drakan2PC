# Order of the Flame: PC object behaviours the conversion lost (round 7 audit), and the felled tree as on the PC

Owner follow-up to [boulders and tree](ootf-ruined-boulders-tree.md):
1. The felled tree in Ruined Village must behave as in Drakan 1: what a strike does, how it falls, the orc it lands on,
   and a trunk walked onto and over with no running jump.
2. Audit the whole campaign for other PC object behaviours that the PS2 engine lacks or the conversion drops, fix the ones
   with a clear PS2 mechanism and list the rest.

Fixed in the converter (formats `0afdb74`, `7266f98`); the runtime is unchanged. Scratch, runs and evidence are under
`build/review/rv-physics/` (`audit/`, `runs/tr*`, `runs/br1`, `evidence/`). Nothing from either game is quoted here.

## 1. The tree as the PC level chains it

What the PC data says (Ruined Village, PC ids):

| Step | PC objects | Behaviour |
| --- | --- | --- |
| Rynn comes out on the ledge | Detector 2622, Timer 2623 (0.1 s), message 7 "Move to WayPoint" | orc 2612 walks to waypoint 2621 on the far ledge, by the far end of the gap, and taunts there (its waypoint plays six taunt clips and sounds and keeps him there until he sees Rynn) |
| Rynn strikes the tree | "push tree" 1106: Push Over Mode 5, Fall When Dead on, health 0, "Message To Send When Pushed" On, links Timer 2624 and Triggered Sound 2455 | the tree goes over and the falling sound plays (sound 2455 on On) |
| 1.65 s later | Timer 2624, message 9 "Gib" to orc 2612 | the orc is gibbed: the tree lands on him. The timer's 1.65 s is how long the fall takes on the PC |
| afterwards | the trunk lies over the 4,000 unit gap | the way on; the tree's class also names a "Hit Ground Sound" |

How many strikes the PC needs and which way mode 5 throws the tree are not stored. One strike (the tree has no health on the
PC and the push follows the blow) and the direction from the terrain (the gap, world -x) are what the converter uses.

What changed (`ootftree.ts`, on top of the round 7 fix):

* **The fall** takes 1.6 s (it accelerates for 1.4 s), matching the PC Timer that gibs the orc at 1.65 s. The PS2 Mover is now
  Type 4 (turns and slides at once, as the disc's elevator cog and Bonegrinder door): while it turns it sinks so that the
  trunk's top near its foot is 200 units over the ledge (`TRUNK_STEP`; the converter reads the trunk's lower fifth from the
  model: 469 units for this tree). The trunk lay with its full thickness, about 630 units, over the ledge before.
* **The roots** at its foot ("pushtreebase" 1105, same spot) collide only with their polygons up to 150 units over the
  ledge (`requireLowCollision`, `lowCollisionBounds` in `ootfassets.ts`). Their flares stand 320 units high on the ledge
  side and stopped Rynn short of the trunk in every walk-on (`tr1`, and the round 7 runs), which is why only a running jump
  got her up.
* The orc's chain (detector, timer, waypoint, gib timer) is the PC's, converted as it was; it needed no change.
* Sound: the PC "Hit Ground Sound" is offered to the Mover's stop sound, but the converter has not converted that sound (the
  PS2 Ground Object has no such field, so nothing asks for it); the falling sound 2455 plays on the strike as on the PC.

Played (own play folder, owner's ini, windowed, exe `a3b2703c...` = current dist exe; pack `packs/p7`, the push-over code of
`0afdb74` before its rebase over the unrelated `6141d3c`; a pack of `7266f98`, `packs/final3`, has the same Ruined Village
level and differs only in the Alwarren level and the prop models of `6141d3c`; New Game, Atimar equipped, then placements and real input), run `tr4`:

| Step | Input | Result |
| --- | --- | --- |
| detector | placed 600 units east of detector 2622, W 0.8 s through it | the orc walks to the far end of the gap (316019, 196966) |
| strike | placed 1,300 east of the tree, W to 600 east, left mouse once | the standing tree goes, the Mover swings down and sinks 469 units |
| the orc | none | 1.5 s after the strike the orc is at 0 health and lies dead by the trunk's far end |
| cross | placed 1,300 east, W only (mouse steering along the trunk, no Space) | first try: over the roots, onto the trunk (24,734 on the trunk over the gap), across to the far ledge (316263, 24451, 197179) |

Contact sheet `evidence/tree_pc_chain.jpg` (`runs/tr4`).

How reliable the W-only walk is, on a pack of the committed formats (`packs/final3`, `runs/sw1`): one strike, the orc
killed again, then six walks from 1,300 units east of the foot, starting 200 north to 300 south of the trunk's line, W with
the mouse steering along the trunk: 4 crossed, 2 did not. Both failures got onto the trunk and stopped on its north half
over the gap (318857, 197733 and 319392, 198474): a branch of the PC model lies there on the trunk's north side (its
collision box rises about 470 units over the trunk between 319060 and 320670). `tr3` and `tr4` (same Ruined Village data)
crossed on their first try, `tr2` (the same pack as `tr3`) stuck on the trunk near its foot in all three tries, on its north
half as well. So: no jump is needed, but a player who keeps to the north edge of the trunk meets a branch. The branches'
collision was left as the PC model has it.

Before the roots change (`tr1`) Rynn stopped at the roots.

## 2. How the audit was made

`audit/ootf-audit.ts` runs the converter over all 23 levels (`--only classes,models,singletons,sequences`) and, for every PC
object, compares each scalar field (int, float, bool, enum) of its PC class or own parameter with the converted object:
`missing` (the PS2 class has no field of that name), `differs` (the field exists, the value differs) or `dropped` (the
object was not converted). It also lists links to objects that were not converted. 68,101 rows before the fixes
(`audit/a1.json`), 68,010 after (`audit/a2.json`); summaries `audit/missing_summary.txt`, `audit/differs_summary.txt`,
links `audit/a3.links.json`. Field values were then read by hand per class, against what the PS2 engine does with them.

## 3. Fixed (clear PS2 mechanism)

| What | Objects (level: count) | PC | PS2 before | Now |
| --- | --- | --- | --- | --- |
| Push-over Ground Objects (Push Over Mode not 0) | Ruined Village tree 1106; Wartok Canyons Push Pillar 1198 with Push Brace 1253; Alwarren Brick 1197, 1998; Islands Goblin Pillar 2603 | knocked over, sends "Message To Send When Pushed" (tree: On to the orc's gib timer; pillar: a sound; brick 1197: Off to two magic flames; goblin pillar: On to the timer that drops a cage) | static props, health 0, nothing happens | one blow destroys the standing object and sends the PC message; where it bridges a gap (the tree, the Wartok pillar, its brace falling with it) a hidden Mover with its model falls and sinks into place, the props at its foot collide only low ("Push Base" 1252 in Wartok Canyons); otherwise it just breaks |
| Name-mapped Ground Object health | Islands: Barrel 16, Crate 14; Alwarren: Barrel 23, Crate 4; Islands Night: Barrel 2, Crate 2; Succubus Lair: Barrel 3; Wartok Canyons: Crate 5, Crate Short 4; Grimstone Mines: Crate Short 2; Ruined Village: Crate 1; Grotto: Crate 1 | 10 or 20 | 5 (the disc's Barrel, Crate, Crate Short) | PC value (`keepsPcDefaults` now covers engine 17) |
| Name-mapped boulders | Ruined Village 3, Alwarren 4, Islands Night 1, Wartok Canyons 1 | damage 50 / 5, damage player on, friction | disc Boulder: damage 1, off | fixed earlier in round 7 (`dd830b1`, `e29c435`) |

Equivalent, left alone: BrickPile (Islands 5, Alwarren 10, Islands Night 5) PC health 99,999 against the disc's 0 (both never
break); Wartok Canyons' Shield PC 0 against 999,999.

## 4. Differences left, with the reason

| Area | Objects | Difference | Why not changed |
| --- | --- | --- | --- |
| Creatures mapped onto Drakan 2 creatures | Orcs (Ruined Village, Wartok Canyons, Grimstone Mines, Islands, Alwarren, Succubus Lair, Volcanic), Trolls (Wartok Canyons, Grimstone Mines, Grotto, Alwarren), Giants (Grimstone Mines to Succubus Lair), Spiders (Ruined Village, Wartok Canyons), Scavengers, Skeleton Dragons (Volcanic) | health, armour, damage, speeds, flee limits (e.g. PC yellow orc 25 health against 40, PC war giant 450 against 150, PC spider speed 1.4 against 0.5) | combat balance: they play on the disc creatures' rigs, clips and AI, tuned with the disc values; Scavenger health is the earlier flee stand-in. An owner decision, not a dropped behaviour |
| Creature fields with no PS2 counterpart | all creature classes | "Defend Radius", "Defense Attack %", goblins' "Fire Range" | the PS2 AI has no such setting |
| Break-away floors | 190 pieces in 6 levels | "Pause" before a piece drops | the PS2 class has no such field; floors drop on the PS2 timing (round 4 break-away floor work) |
| Flame and particle emitters | 128 cloned "Burning Flame", "Alwarren Magic Flame", "death trigger" | "Particle Life Time" PC 0, PS2 0.5 | flame work belongs to the flames agent (round 7); told the coordinator |
| Torch / sconce flames | 416 | light colour, flicker, radius | the PS2 flame class has no light fields |
| Switches | 45 cloned switches | "Switch Type" | the PC class lists "Switch Type" twice (1 and 0); the PS2 gets the second. The switch, door and lever work of 7 November proved them in play |
| Scene detectors | 18 (Islands, Islands Night, Volcanic, Wartok Canyons, Grimstone Mines, Rift World, Succubus Lair) | "Task" 4 (Play Sequence) to 6 (Trigger Only) | deliberate: the scene is relayed through a Stomp Player (`relayDetectorSequences`) |
| Stomp players, death counters, level transitions, air currents | 18, 1, 2, 71 | play order, counted message, "Maintain Player Position", current speed | deliberate translations (`ootfstomp.ts`, death counters, exits, Air Currents tuned on the PS2 default) |
| Sounds mapped by name | Grimstone Mines 2, Grotto and Succubus Lair 3, multiplayer gusts 31 | "Location", "Play Mode", period | small audio differences; the sound agent's area |
| Items | potions, weapons, keys, armour, bows, crystals | inventory slot and bitmap numbers, bow accuracy and auto-aim cone, crystals' "Hold Use Button To Charge" (3) | the PS2 inventory and bow use their own scheme (`gameplay/bows.ini`) |
| Classes left out | 207 objects | arena spawns (exported to the map ini), sky and fog switches, music triggers (played by the music trigger rewrite), generic map triggers, three waterfall models with no source ("Fall01", linked from an Alwarren Visibility Toggler) | deliberate omissions (`deliberateOmission`) |
| Links to objects not converted | 82 arena weapon links to launch pads, 3 Alwarren toggler links to "Fall01" | | arena exports; the waterfall pieces have no model |

No PC object with health 0 is breakable on the PC either (0 means immortal on both). No timer, mover or gib was found
whose target was dropped other than the rows above. The method sees scalar fields and links to objects that were not
converted; it does not show whether a PS2 handler honours a message that arrives (the orc's Gib was checked in play).

The push-over objects that break now have health 1: any damage breaks them, not only Rynn's blow (a boulder, an explosion
or a creature's swing would). Alwarren's second brick (1998) links to nothing and stays a prop (`7266f98`). The low
collision is keyed by model: the roots model of the tree and Wartok's "Push Base" model are used by no other object of
their levels.

One audit row is an artefact: Grimstone Mines object 286 ("Large Tree (Snowy)") shows a creature class because a creature
of the converted level has the same id; not a conversion fault.

## 5. Played and not played

* Ruined Village tree, orc and crossing: played as in section 1 (`tr4`; `tr3` too).
* Alwarren brick (`br1`, `br2`, direct start of Alwarren, one placement): the inventory of a direct start is empty
  (`br2` opened it: no weapon), so the clicks did not break the brick and the break was not seen. The ring of magic flames
  round the barrier crystal showed no flame and Rynn walked through it to the crystal unharmed. In the PC data 95 of
  Alwarren's 100 "Alwarren Magic Flame" objects, these included, have no visible flag (flags 0x100), and nothing but the
  brick links two of them; whether they burn on the PC was not established. Passed to the coordinator for the flame work.
* Wartok Canyons' pillar and the Islands goblin pillar: not played (both levels start on Arokh; no weapon in a direct
  start). The converter logs: pillar falls toward world -x over the gap, 4,531 long, sinks 106, with its brace.
* Barrels and crates with PC health: not played (a value change only). The flame work's barrels are among them.

## 6. Not verified

* What the PC push-over modes (1, 4, 5, 7) mean, and how many blows the PC needs: inferred, the PC game was not run.
* The tree's "Hit Ground Sound" is not played (not converted).
* The Wartok Canyons bridge, the Alwarren brick and the Islands goblin pillar in play (see 5).
* A campaign save after Heron's Crystal on the new pack: none; the runs start from New Game with placements.
* Owner's full-screen settings, sound (off in the runs), the German tree; Drakan 2 is not reached (converter only).
