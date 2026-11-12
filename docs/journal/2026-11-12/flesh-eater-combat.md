# "Flesh eaters" that cannot be hit: the Scavenger, measured (2026-11-12)

Report: "Can we also investigate combat with the flesh eating spiders, we can't seem to hit them", then "I'm not talking
about the cave spiders, I'm talking about the flesh eaters running around."

**Outcome, revised by the follow-up at the end: with god mode off and Atimar's Blade, Rynn loses to the Scavengers that attack (her swings are cut by their hits); one converter change went in after the second follow-up (new games only). The sections up to the follow-up were written before that and used god mode in most runs.** In those runs the miss was not reproduced. The creature is identified, a visible Scavenger is hit and killed in every
condition tried, and no combat code or converter rule was changed. What exists now is a diagnostic
(`DRAKAN_MELEE_TRACE`) to capture the engine's own view of a swing when it happens in real play. Probes, runs and
pictures are private under `build/review/flesheaters/`; nothing from either game is quoted here.

## Which creature

* No class, model or text of either game contains "flesh eater" (byte and UTF-16 search of the unpacked Order of the
  Flame install and of the disc data; "flesh" only names the Flesh Mage, banners, gib models and impact sounds).
* Order of the Flame Ruined Village, where all of the owner's campaign saves are (`iso/mc0_ootf`, last written
  2026-11-09 15:05), places 16 `Spider` (engine class 40, all at cave height: the cave spiders) and 19 `Scavenger`
  (engine class 41, on the surface). Scavengers feed on corpses and walk about in the open, so "flesh eaters running
  around" is the Scavenger. The other creatures of the level are orcs and slaves.
* Scavengers per Order of the Flame level: Ruined Village 19, Wartok Canyons 13, Succubus Lair 8, Grotto 6, Grimstone
  Mines 3, Alwarren 2, Volcanic 1.
* It is the creature the conversion swaps: the installed pack (`mods/ootf-levels.zip`, build `d248f6c701888bf8`) places
  18 objects of disc class `Scavenger` (NPCS 2296, runtime class `0x10029`) in Ruined Village, and the owner's saves of
  12:22 and 14:57 hold that class id. So the 2026-11-09 swap is in the installed pack.
* The same disc class is in Drakan 2: Surdana 16 (all in the Scavenger cave), Kragmor 5, Shadowmire 9, Ravenshold 21,
  Northern Tundra 9, Shiverbane 6, Valley of the Fallen 12, Stratos 12.

## Setup of the runs

`build/review/flesheaters/tools/fe.py` on top of `scripts/harness/live_drive.py`: it places Rynn 2,600 units from a
Scavenger, walks her in, keeps her facing it, clicks attack at a fixed period and reads the creature's hit points out of
the process. The executable is the owner's (`dist/DrakanPC/drakan.exe` and `build/review/baseline-play` are the same
file, sha256 `4fddfbc8...`), with the installed pack and a copy of `drakan.ini` / `drakan_input.ini`
(`world_rate = uncapped`, `dual_wield = 1`, direct mouse look). Cards are copies of the owner's saves: the 15:05 save
(Atimar's Blade in the right hand, Mace in the left) and the 14:59 save (Atimar's Blade only).

Differences from the owner's play, all runs unless said: a 960 x 540 window instead of borderless full screen, DLSS off,
sound off, the modern title menu, a direct start in Wartok Canyons followed by a quick load of the save, placement of
Rynn near each creature, god mode (`DRAKAN_PLAYTEST_GOD`), scripted clicks and a facing kept by the driver. The runs
named `fresh*` kept the owner's display block (DLSS quality, 1280 x 960 window) and went through New Game instead of a
save. The machine was shared, so frame rates were 20 to 60 per second outdoors.

## What was measured

A "click in reach" is a click with Rynn on the ground within 1,000 units; a hit is a loss of 4 hit points or more before
the next click. Atimar's Blade does 5, the Mace 10, a Scavenger has 40.

| Condition (baseline build and pack) | Hits / clicks in reach | Killed |
| --- | --- | --- |
| Atimar's Blade only, click every 0.6 s (`baseC1`, `baseC2`) | 31 / 49 (63 %) | 4 of 4 |
| the same at a fixed 30 Hz world rate (`rate30_*`) | 22 / 36 (61 %) | 3 of 3 |
| the same at a fixed 15 Hz world rate (`rate15_*`) | 24 / 38 (63 %) | 3 of 3 |
| the same in a 320 x 240 window, cheapest settings (`fast*`) | 23 / 49 (47 %) | 3 of 3 |
| both weapons, delayed and noisy aim, random strafing, 3 clicks a second (`humA*`) | 15 / 51 (29 %) | 3 of 4 |
| Atimar's Blade only, the same driver (`humB1`) | 8 / 15 (53 %) | 1 of 1 |
| both weapons, 8 clicks a second (`spam_0.12`) | first Scavenger dead after 19 clicks, 2.4 s | 1 of 1 engaged |
| every Scavenger of the level in turn, both weapons, 10 clicks each at 1 a second (`all1`) | of 13 shown ones, 11 took damage; the other 2 stood on a level the placement did not reach (never within 300 units of height) | 7 killed within their 10 clicks |

Further single-factor runs, both weapons, one click a second, two Scavengers each:

* **Facing error** held at 0, 10, 20 and 30 degrees: all eight Scavengers killed in 5 to 18 clicks (`ao_scav_*`).
* **Camera pitch** from 0.24 rad up to 0.44 rad down: killed in 3 to 10 clicks at every pitch (`p_*`). Lock-on
  (`t1lock`): 8 hits in 10 clicks.
* **No god mode**, Atimar's Blade only (`ng1`): the first Scavenger died after 12 clicks in reach.
* **Current tree's build** instead of the owner's (`cur1`): two Scavengers killed in 2 and 4 clicks.
* **Fresh New Game**, Atimar's Blade (`fresh3`): 5 hits in 15 clicks and 2 in 4 on two Scavengers, then Rynn died.
* **Ambush Scavengers**: 7 of the 19 start hidden and are shown by a trigger box, a timer or a pickup (the flame book
  shows the one beside the village). After walking Rynn through the box of two of them, in a fresh game (`fresh4`) and
  in a loaded save (`load4`), both pairs fought and all four died to 7 or 8 hits of the blade.
* **Native Drakan 2** Scavengers in the Surdana cave with the owner's club, Rynn standing 900 units away: 10 hits in 67
  swings uncapped (`d2a`), 18 in 55 at 30 Hz (`d2a30`). These two runs did not walk her in, so they are not comparable
  with the table.
* **Larger hit volumes** (test pack, every hit sphere of the disc Scavenger 1.6 to 2.5 times its radius): 32 / 45
  (71 %) against 63 % (`sphC*`). No material difference, so the volumes are not what limits the rate.

What a hidden Scavenger looks like to the game: it is not drawn, does not move and takes no damage (0 hits in 42 swings
at the one beside the village in a fresh game, `fresh2`; the pictures show Rynn swinging at empty grass).

Geometry, for the record: the disc Scavenger carries 25 hit spheres, the leaves 51 to 164 units in radius on head,
chest, arms, legs and tail; it stands on its long arms with the body at Rynn's head height and attacks from about 900
units (class `Attack Radius` 1.0). A swing passes among thin limbs, which fits a hit rate near two in three rather than
every swing.

## Traps in this kind of measurement

Two things looked like a reproduction and were not:

* **The placement hold.** After `place`, the play-test aid keeps putting Rynn back on the spot (240 boundaries by
  default, longer in wall time when screenshots slow the game). She hangs in the air, her attacks are air attacks and
  the creature walks under her: 0 hits in 40 swings in the first runs. `DRAKAN_PLAYTEST_HOLD=30` and a start 2,600
  units away removed it. A run is only valid where the player state word (`+0x94`) has bit `0x10` clear.
* **Hidden creatures as targets.** A registry scan finds them with full hit points and an idle clip; they are invisible.

The last trace runs (see "Not established") never engaged and are not used for any hit rate.

## What was changed

* `ps2recomp/ps2xRuntime/src/lib/drakan_melee_trace.h` (new) and two lines in `drakan_overrides.cpp`:
  `DRAKAN_MELEE_TRACE=1` logs every swing of the player as the engine runs it (how long the blade was live, in how many
  ticks, how many contacts the blade sweep `0x4979a8` handed to `0x498020`, how many became hits) and every hit with the
  victim's class and the contact height; `2` adds every tick and contact. It reads only. Off by default.
  `docs/reference/env-flags.md` has the row.
* Found while writing it: the blade's hit list count (`+0x1a0`) does not grow for a victim it still holds from an earlier
  swing although that swing's first contact does damage, and the owner reference (`+0x60`) is cleared at times; the trace
  therefore takes a hit from the victim's hit points and finds the player's blades through the hand holders.
* No converter change, no combat change, no test added (the hook has no unit test).

## Not established

* What the owner did when the swings missed. Nothing here reproduces a visible Scavenger that takes no damage.
* The owner's real frame rate and pacing (full screen, DLSS, sound, no other load). Fixed 15 and 30 Hz and 20 to 60
  frames a second behaved alike; rates of several hundred a second were not reached.
* Hand play. Every run was driven by a script, most with god mode and a driver-kept facing.
* Scavengers of the other six levels, and Scavengers in co-op.
* When the observation was made. The saves show play at about 05:46 and 12:22 on 2026-11-09 with an earlier pack build
  (`4025048fd8e6928f`, no copy left) and from 14:49 to 15:05 with the installed one. The 12:22 save already names the
  disc Scavenger class; the pack of 05:46 is unknown and may have had the converted PC model, which could not be hit.
* The trace's final form in a fight. An earlier build of the hook logged real fights (`trB2`, `trB3`: live spans of
  0.12 to 0.17 s, 9 to 16 ticks each, 2 to 9 contacts in most swings at a Scavenger, contact heights 25 to 186 units
  above its origin) but counted hits by the hit list and so under-reported them (3 lines for 8 damage events). The
  corrected build (hits by the victim's hit points) only ran in sessions where Rynn never reached the creature: in six
  runs on that build (`trB4`, `hf1` to `hf6`) the placement did not take and she stayed 45,000 units away, 381 swings
  with 0 contacts in the last two. Whether that is the faster machine at that hour or another change in the tree that
  build picked up was not looked into. The trace has no unit test.

## How to settle it

Play to the Scavengers with `DRAKAN_MELEE_TRACE=1` set and keep `drakan.log`. A swing line with 0 contacts while the
blade is visibly in the creature means the blade never met a hit volume (geometry); contacts without a hit mean the
engine refused them; no swing lines at all mean the attack never armed the blade (no weapon in hand: a new Order of the
Flame game starts with Atimar's Blade in the inventory, not equipped, and an unarmed click does nothing).

## Follow-up the same day: fights without god mode

The owner's answer to the above: "try hitting scavenger without god mode." Done, and it changes the picture: against the
Scavengers that attack, **Rynn with Atimar's Blade loses**. Nothing above this heading had tested that (one unhurt run,
and fresh-game runs that ended with her dead and were not followed up).

Method: `build/review/flesheaters/tools/duel.py`. One process, many fights: quick load, the creature comes or Rynn walks
to within 650 units, a player-like driver (looks at the creature every 0.25 s with an 80 unit aiming error, strafes at
random where noted, clicks every 0.4 s with jitter), until the creatures or Rynn are dead or 60 to 75 s pass. Her hit
points are read from the stats block and never written; no god mode. Owner's executable and installed pack, 960 x 540
window, DLSS and sound off, direct start in Wartok Canyons then quick load. The saves are copies: the owner's own with
Rynn moved next to the creatures on the baseline executable (`near_s3`, `near_s33`), the owner's own with a placement
after each load (hill), and a save made in a fresh New Game with Atimar's Blade equipped (`fresh_base`).

| Fights (baseline, no god mode) | Rynn won | Rynn died | Hits / clicks | She deals | She takes |
| --- | --- | --- | --- | --- | --- |
| The Scavenger beside the village, Atimar's Blade, 10 fights (`dA1`) | 10 | 0 | 79 / 159 (50 %) | 6.3 hp/s | 0 |
| The same one, both weapons, strafing, 10 fights (`dG1`) | 10 | 0 | 54 / 147 (37 %) | 6.8 hp/s | 0 |
| Hill Scavengers (shown from the start), owner's 14:59 save, Atimar's Blade, strafing, 5 fights (`dE_s3`) | 0 | 5 | 32 / 244 (13 %) | 1.2 hp/s | 2.5 hp/s |
| Hill Scavengers, fresh New Game save, Atimar's Blade, strafing, 8 fights (`B_fresh`) | 4 (first Scavenger killed) | 4 | 50 / 715 (7 %, includes clicks at a second, distant one) | 0.6 hp/s | 1.1 hp/s |
| Native Drakan 2, Surdana cave, club, 3 usable fights (`dD3`) | 2 | 1 (started at 4 hit points) | 10 / 24 (42 %) | 8.8 hp/s | 2.7 hp/s |

* The village Scavenger (the one the flame book shows) never hit Rynn in 20 fights and stood still for 45 s with her
  4,450 units away (`dP1`, `dP2`). The hill ones attack at once: 64 hits on her in 132 s of `dE_s3`, 5 hit points each.
* In `dE_s3` she died in all five fights in 12 to 54 s, with the Scavenger at 5 to 15 of 40 hit points. In `B_fresh`
  the four kills cost her 30 to 60 of 65 to 75 hit points.
* **Her swings are cut by its hits.** Of 61 attack spans (attack flag, player `+0x94` bit 7) in `dE_s3` she was hit
  during 38, and only 10 of those 38 dealt damage; 22 of 61 dealt damage in all. Of the 64 hits on her, the attack flag
  was still set at the next poll in 1: a hit clears it. In `B_fresh`: hit during 64 of 122 spans, 13 of those dealt
  damage. So what the owner sees is real: most swings near an attacking Scavenger come to nothing, and the dagger's 5
  points against 40 take longer than she lives.
* The same happens in native Drakan 2 (hit during both spans measured), but the club's 8 points per hit killed the
  Scavenger in five hits and 3 to 6 s. Three fights only: that save had Rynn at 4 to 39 hit points.
* Dual wield on the hill (`dE_s33`): the first Scavenger died to 5 or 6 hits without hurting her in six fights; the
  rest of each fight was the driver clicking at a second Scavenger out of reach, so those hit ratios mean nothing.
* Frame-rate caveat: fight 0 of `dE_s3` and of `B_fresh` took a screenshot every 0.4 to 0.5 s and ran at 4 to 9
  frames a second; the other fights ran without screenshots and ended the same way.

Class values of the creature:

| | PC Order of the Flame `Scavenger` | Disc `Scavenger` (what the pack plays) |
| --- | --- | --- |
| Initial Health | 35 | 40 |
| Flee Health Limit | 20 | 0 |
| Damage To Inflict | 10 | 5 |
| Max Speed (lu/s) | 3.2 | 1.4 |
| Attack Radius / Attack Speed | 1.0 / 0.9 | 1.0 / 0.9 |
| Delay Between Attacks | no such field | 0.4 s |

Atimar's Blade does 5 in both games. Rynn's own hit-reaction settings in the campaign against Drakan 2 were not compared.

### A fix that was tried and is not in the tree

Giving each placed Scavenger the PC values of `Initial Health` and `Flee Health Limit` as object parameters (a
`keepOotf` list on the swap row; 51 creatures in six levels got them, Volcanic's one did not). On a pack converted with
it (`packs/fix1`, build `b812405f22183dac`, same executable, fresh New Game save, same driver), 9 fights (`F_fresh`):
6 first-Scavenger kills and 3 deaths, against 4 and 4 in 8 baseline fights. The health of 35 is applied (the second
Scavenger reads 35), but in the three lost fights the Scavenger stood at 20, 20 and 15 hit points and went on
attacking, so the flee limit did not visibly work, and 6 of 9 against 4 of 8 is inside the noise of this driver. That
is not a proven fix, so the converter change and its three tests were taken out again. They are kept as
`build/review/flesheaters/patches/scavenger-keep-ootf-values.diff` and `pipeline-block.ts.txt`.

### The trace, corrected

`DRAKAN_MELEE_TRACE` now counts damage from the hit points of the victims the player's blades touched, compared at every
sweep call (`[melee] damage ...`), finds her blades through the hand holders, ends a swing after 0.12 s without a live
tick, and logs hits on Rynn and attacks whose blade never went live. Checked against the probe's own reads in five
fights (`dT2`): 46 damage lines for 44 damage events seen from outside (the trace also sees 2 point steps the 8 ms poll
merges), 5 hits on Rynn in both. The "attack ended" lines did not appear in that run (the flag did not fall between
clicks 0.9 s apart), so the cancelled-swing line is untested. Builds from the current tree have a `place` that no
longer moves Rynn to the target (seen in builds from about 11:15 on; the owner's executable is fine); the trace runs
use saves made next to the creatures instead.

### Not established after the follow-up

* A fix. The cause is narrowed to: the hill Scavengers hit every two seconds or so, each hit cancels Rynn's swing, and
  the dagger needs eight hits. Which lever is right (their attack delay, their damage reaction, health, the flee
  behaviour that did not show, or a starting weapon with more damage) was not tested beyond the one attempt above.
* Whether the PC original behaves like this: only its class values were read, the PC game was not run.
* Fights against three at once, hand play, the owner's display settings and sound in these fights.
* Whether the flee limit works at all for this class in the PS2 engine.

## Second follow-up: the levers, and the change that went in

Order of the coordinator's list. God mode off in every fight; one probe (flee) used it and says so.

**1. Flee.** The PC Scavenger runs away at 20 of 35 hit points; the disc Scavenger class has the field (`Flee Health
Limit`) at 0. Of the 155 disc classes that carry the field, 90 set it, all of engine classes 49 (orcs, trogs, Wartoks: 2
to 40), 157 (villagers: 20), 66 and 30; no class of the Spider / Scavenger engine classes (40, 41) does. With the PC
limit written as an object parameter (pack `fix1`), a Scavenger hit slowly under god mode from 35 down to 0 stayed
within 1,100 to 1,600 units and went on attacking in 4 of 4 fights (`flee1`; a run clip showed in one). So the limit
has no visible effect on this class. The engine code that reads the field was not located, so "class 41 never flees" is
shown by behaviour and by the disc's own data, not by the code. Nearest equivalent the engine does support: end the
fight after the same number of blows, see "The change".

**2. Attack rhythm.** PC: one attack clip, `attacklow`, 0.60 s, class `Attack Speed` 0.9, no delay field, 10 damage.
Disc: `attacklow` 0.70 s and `attackbite` 0.97 s, `Attack Speed` 0.9, `Delay Between Attacks` 0.4 s (+- 0.1), 5 damage.
Read from the clip headers and class data; the PC game was not run. On paper the PC creature can strike about every
0.67 s for 10 (about 15 hit points a second at most), the disc one every 1.2 to 1.5 s for 5 (about 4 at most; measured
1.1 to 5.8 on Rynn, depending on how many reach her). The converted creature is already slower and weaker per hit than
the original, so there is nothing to slow down on that reference, and matching the PC interval would raise the damage.
Not changed.

**3. Rynn's hit reaction.** PC Rynn (`Begin Health` 100) has six hit-reaction clip fields (`HitRxn HSL / HSH / OSL / OSH
/ FSL / FSH`), so the original also plays a reaction when she is hit; whether it cancels a swing in progress cannot be
read from the data and the PC game was not run. In the PS2 engine a hit clears her attack flag: of 473 hits on her in
the series below, the flag was still set afterwards in 2. Native Drakan 2 does the same (both swings measured). It is
engine behaviour for every enemy, not a converted setting; no change proposed without the PC behaviour.

**4. Starting weapon.** The PC player object links Atimar's Blade (object 1059) as inventory; nothing in the PC data
read here says whether the original puts it in her hand at the start. Not changed. In the port a new game starts with it
in the inventory, unequipped, and an unarmed click does nothing (100 clicks, attack flag never set).

**5. The village Scavenger.** PC object 885 carries no parameters at all (class defaults, the same as the hill ones) and
has the visible flag clear; the flame book links to it. So it is an ambush creature that the book shows, not one with
lost aggression settings. Why it stands idle after being shown in the owner's save was not found.

### The change (formats, in the tree)

The swap row of the Scavenger gets `healthUntilFlee`: each placed Scavenger receives, as an `Initial Health` object
parameter, the damage the PC one takes before it runs (PC health minus PC flee limit, from the object's own PC values,
else the class: 35 - 20 = 15). 48 creatures in six levels (Ruined Village 19, Wartok Canyons 10, Succubus Lair 8,
Grotto 6, Grimstone Mines 3, Alwarren 2); a creature placed dead and one whose PC values give no flee keep the disc
health. Three dagger hits now end the fight, as three dagger hits drive the original off. It is a stand-in for
fleeing, not the original behaviour: the creature dies instead of running, and gives its experience.

Balanced against: the number of blows the first weapon of the campaign needs in the original. Not touched: damage,
attack rhythm, speed.

### Proof, and its limits

Same driver as above (0.4 s clicks, 0.25 s look delay, 80 unit aim error, strafing), owner's executable, hill
Scavengers, Atimar's Blade only.

| Series | Scavenger health | Fights | First Scavenger killed | Rynn died | Time to kill, median (range) | Hit points lost up to the kill |
| --- | --- | --- | --- | --- | --- | --- |
| Fresh game, installed pack (`B_fresh`) | 40 | 8 | 4 | 4 | 23.1 s (18.5 to 44.2) | 30, 55, 60, 45 |
| Fresh game, changed pack (`F2_fresh`) | 15 | 11 | 10 | 1 | 8.3 s (2.6 to 13.4) | 10, 15, 35, 35, 55, 25, 5, 80, 45, 30 |
| Owner's 14:59 save, installed pack (`dE_s3`) | 40 | 5 | 0 | 5 | none | |
| Owner's save again (`B_own`) | 40 | 15 | 10 | 5 | 9.1 s (4.8 to 21.0) | 20, 55, 45, 40, 10, 35, 5, 5, 50, 25 |
| Owner's save on the changed pack (`F2_own`) | 40 (see below) | 12 | 4 | 8 | 11.6 s (5.8 to 12.7) | 50, 55, 50, 20 |

* **A save keeps the old creatures.** In `F2_own` the Scavenger still had 40 hit points (8 hits to kill, 30 to 40 left
  when Rynn died): the owner's save restores the creature with the health it was saved with. The change reaches new
  games, not existing saves of a level. That series is therefore a third baseline, not a test of the change.
* **The driver is noisy.** The same condition (owner's save, 40 hit points) gave 0 of 5, 10 of 15 and 4 of 12 kills.
  Pooled over all 40-point series: 18 kills in 40 fights; with 15 points: 10 in 11.
* Hits per click and cancelled swings do not change with the health and are not expected to: `B_own` 98 of 339 clicks
  hit (29 %), Rynn was hit during 59 of 76 attack spans and 24 of those still dealt damage; `F2_fresh` 30 hits (the
  click count there includes a minute of clicking at a distant second Scavenger).
* It does not flee in any series.
* Not run: dual wield on the changed pack (on the installed pack the first Scavenger already died to 5 or 6 hits
  without hurting her), three at once, hand play, the owner's display settings and sound. One creature at a time
  reached her in most fights; a second one joined in a few (the 80 point loss).
* Pictures: `evidence/no-god-mode/fixed-fight/` (brightened three times; the first fight of `F2_fresh`, which took a
  screenshot every 0.4 s and ran at 10 to 50 frames a second).

### `place` on builds of the current tree

On my build of about 11:15 `place` did not move Rynn (7 of 7 runs: `try N: player 51321 units from the point`); on the
build of about 12:20 it works, with and without `DRAKAN_LAYER_SEAM_FIX=0`, `DRAKAN_LOD_DISTANCE=1 DRAKAN_DRAW_DISTANCE=1`,
`DRAKAN_RES_CHAIN=0` and `DRAKAN_SCENE_CAPTURE_MOTION_ONLY=0` (one run each). The failing executable no longer exists,
so the switch that would have restored it is unknown.
