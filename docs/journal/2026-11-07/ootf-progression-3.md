# Issue 5: Rift World, the last level (fresh pack, fire, Ebon Dragons, gate counter, RibDoor tunnel)

Follows [giant, Islands, rift](ootf-giant-islands-rift.md) and [quick-load LRU](ootf-quickload-lru.md). Runs, recipes and
helpers are private under `build/harness/ootf7/` (`run.py` = the ootf-i5 driver on `build/ootf7/play/drakan.exe`, a copy of the
play folder with `build/final-game/bin/drakan.exe`; `go.sh` wrapper, `pcdump.mts` PC object dump with links, `moveobj.mts`
test aid that moves one object in a pack level, `findcls.py` / `dw.py` / `st.py` objects of a class with hit points or
instance words per RAM dump, `montage.py`, `recipes/`, `cards/` saves). Direct starts, god mode.

## Where the chain ends

The campaign has 13 levels (00 Intro to 12 Rift World); the other 10 of the 23 are arenas. **Rift World is the last level**:
there is nothing to walk past it, the ending is inside it. PC logic of the level (`pc-rw.txt`, links only, no incoming
links from outside):

* four Ebon Dragons (527 to 530) each link the counter 558; 558 links the Detectors 367 / 369, which link the RibDoors 191 /
  192 (the hexagonal tunnel gates, a Morph Plug each); timers 447 to 450 drive the vertical gates 439 to 446;
* Delon 161 links the counter 455 (-> Stomp Player 436) and a Death Mage waypoint; four Dr. Evil heads 108 to 111 link the
  Navros DeathCounter 162 -> Ribs 472 -> Stomp Player 473 (the end sequence); Ribs 474 links the heads (an auto-start timer);
* Trigger 183 (mounted) plays 10.1a (proven earlier).

## 1. Fresh pack

`ootf-convert.ts --ootf build/ootf/cab_disc --ps2 build/odbx/data --template LEVEL1NA --campaign 1`, 23 levels, 10,698 files,
2 min 40 s, from formats `f5d3c6b` (the other session's collision-list fix, plus the test aid below only when its env var is set).
Executable: `build/final-game/bin/drakan.exe` (main `7e017f08`).

## 2. A direct start of Rift World has no fire

Direct starts of Rift World (`rw0` to `rw4`) arrive on Arokh with an empty breath orb and breath taps do nothing. The PC
Dragon 240 links a "Fireball Power Up" (563, parked at y 143,863) and the converter drops that class's weapon fields; in
the natural chain the dragon is carried from Volcanic, whose orb is amber. So the real arrival was replayed: Volcanic key,
cage, Runeblade, mount, rift box 2318, exit, load: **Rift World with the amber orb** (`mkrw3`, `mkrw4`).

The Runeblade step is the flaky part of that replay (the cave-mouth dismount: `mkrw`, the Volcanic recipe `vr14` copy, never
landed Rynn in 900 s and the run stopped at the 900 s cap). For these runs a test aid pack moves the Runeblade handle 2239
next to Arokh (`moveobj.mts`: 265000 152300 497200), so Rynn takes it before mounting; the rest is the real route (message
box, save screen, load). The save of that arrival is the card the later runs start from (F8).

## 3. Breath, mana, the Ebon Dragons

* Arokh's breath does 15 damage per hit (150 -> 135); the orb drains in about 10 taps and **refills in about 500 ticks**
  (`mana1`: drained, then full by the first wait shot).
* The four Ebon Dragons idle (positions nearly fixed, no attack on the dormant start) and the breath is a narrow stream: only
  placements about 3,250 units from a dragon, 500 to 1,300 units above it and within about 100 units sideways hit; open-loop
  scripts need many tries (`fire3` to `kill9`, a dozen runs). Not a converter fault; a fight needs a closed-loop driver.
* Test aid `OOTF_TEST_EBONHP=<hp>` (formats `4d21976`, like `OOTF_TEST_HEADHP`): Ebon Dragons with few hit points. With 15
  the four dragons died to real breath hits over staged runs with F5 between stages (`stage1` to `stage4`).
* **The gate counter counts the real deaths**: counter 558 reads 4 at the start and 2, 1, 0 after the kills (`kill9` 2,
  `stage1` 2, `stage2` 1, `stage3` 0 from `dw.py 10007`).

## 4. The RibDoor tunnel (next blocker)

With the counter at 0, Arokh placed at the tunnel mouth flies into the hexagonal tunnel (floor 92,500, ceiling 94,772: 2,272
units tall; at z 281,000 the ceiling is 96,211) only at height 93,700 (at 96,000 the funnel ceiling stops him at z 281,949).
He then stops 2,000 to 3,300 units before the door (x 334,000 to 336,300, z 274,200 to 282,500) facing a rock slab, with the
ornate gate frame to his right. Detector 367 works: when he nears it the RibDoor 191 changes its flags `0x19001045` ->
`0x19009045` (`door7`, `stage4`), but the passage never opens for him in the runs made (`door5` to `door8`, up to 1,600
ticks of W). Superseded by section 5 (the door is not the route). Not known whether the Morph Plug moves its collision, or the tunnel is too tight for Arokh's look-ahead
(the Islands arrival tunnel had the same symptom, see [Arokh flight](../2026-11-06/ootf-arokh-flight.md)), or the plug's
collision hierarchy still wrongly blocks (formats `f5d3c6b`, the collision-list change, was in this pack).

Not reached: the altar, Delon, the four Dr. Evil heads and the ending by natural play (the isolated head fight to credits
of 2026-11-02 still stands).

## 5. Follow-up: the RibDoors are not the route; the tunnel is a flight-clearance problem

The three causes were separated with the dragon look-ahead trace (`DRAKAN_DRAGON_RAY_TRACE=1`, runs `ray1`, `ray2`, `c1`, `w1` to
`w3`) and terrain probes (`col.mts` on the pack, `pccol.mts` / `pcmap2.mts` on the PC level, `pcmap2` prints floor to ceiling gaps):

* **Not the door plug's collision.** Rays in the tunnel hit terrain (`object 0`), never an object; with both RibDoors moved out
  of the level (`packnodoor`, test aid) Arokh stops at the same place (z 279,400, x 335,000) as with them.
* **Not the converter.** The converted terrain matches the PC level at every probed column (`pccol.mts` against `col.mts`:
  identical floors and ceilings, same textures). The tunnel's far end is sealed by the terrain itself in the PC data too
  (floor equals ceiling at x 334,000 to 337,000, z below 272,000): the RibDoors guard side vaults, not the way north.
* **The route** (PC terrain map): from the arrival the central wall (floor equals ceiling, z 279,000 to 283,000 at x 342,418) is
  solid at every height; the way is down the left (or right) tunnel at z 283,000 to 279,000 (x 332,000 to 336,000, a ramp
  rising from floor 81,000 to 92,500), east along the corridor at z 277,000 to 278,000 (floor 92,500, ceiling 94,600 to 95,300:
  about 2,200 units), then north up the open central shaft (x 339,000 to 345,000, ceiling 108,600). Flown from the shaft
  (`s1`: placed at 342,418 98,000 268,000, W) Arokh reaches z 235,000 in 12 s and stops at the first "big spine" prop
  (object 478 at 341,982 99,452 232,363, ray hits the object), which has to be flown around (x 349,000 clears it).
* **What stops him in the corridor**: Arokh lands. Held at a height under 1,400 above the floor he drops to the engine's
  landed height (floor + 741: `93241` over floor 92,500, state word `+0x9c` = 1, forward speed 0, the look-ahead ray points straight
  down), and a landed Arokh does not walk on under W; held above it the ceiling's wedge (ceiling 93,900 at z 278,600, x 335,300)
  ends the look-ahead at 2,300 units. The window between "lands" and "hits the ceiling" is 500 to 700 units wide, which is
  not a window a player could fly through; whether the PC dragon needs less clearance was the open question; section 6 answers it: no, my height
  holds were the cause.

### New test aids (runtime, `drakan_playtest.cpp`, documented in `docs/reference/env-flags.md`, commits `5c270c0a`, `562d9785`)

* `DRAKAN_PLAYTEST_AUTOAIM=<class hex>`: the controlled actor faces the nearest living object of that engine class every
  boundary. Rift World (`aim1`): three of the four Ebon Dragons died in 12 rounds, against a dozen runs of open-loop placement.
  Unit test `autoaim_policy`.
* `DRAKAN_PLAYTEST_ALT=<y>` (height hold) and `DRAKAN_PLAYTEST_WAYPOINTS="x y z; ..."` with `_CLIMB` / `_WP_RADIUS` (autopilot:
  yaw toward the current point, height moves toward its height, the recipe only holds W). `w3` flew the natural start, the left
  tunnel ramp and into the corridor (waypoints 0 to 2 reached) before the landing above.

### War giant

Not re-run: `DRAKAN_PLAYTEST_AUTOAIM=1003b` is now available for it (the earlier note's "reactive driver"); the Alwarren session
needs the hammer route first and was not replayed in this pass.

## 6. The route flown naturally, the altar, the four heads, the ending and the outro

**The landing question is answered: nothing in the converter or the engine differs.** The 2,200-unit corridor was an artifact
of how the map was read: east of the left tunnel (x 331,000 to 335,000, z 283,000 to 279,000, a ramp whose ceiling is 94,000 to
101,000) the route opens into a wide hall (z 277,000 to 274,000: floor 92,500, ceiling 100,700), and the 2,200-unit gap at
z 278,000 is only a sill. Held at 93,800 to 94,100 inside the ramp's tail Arokh lands (the flight tick `0x271e18` keeps the
"landed" state `+0x9c` = 1 when he is within about 1,400 of the floor and the ceiling wedge cuts the look-ahead; the dive
cushion, `0x271e18` lines near `fVar27 < 16384`, scales the descent by `(height - 1024) / 15360`); flown at 95,000 to
98,500 through the hall he does not. Waypoints that keep to the ceiling (95,000 at z 279,500, 95,500 at z 275,800, then 98,500)
fly the left tunnel, the hall and the central shaft with no converter or runtime change (`w6`, `w8`, `a2`, `a5`: all waypoints
reached; the climb must stay behind the ceiling, so waypoint heights matter, and a run in which Arokh climbed too early hit the
wedge at z 277,600 as in `w5`). The shaft opens onto the causeway (floor 98,500 to 100,000, ceiling 108,600 to 120,000 at
x 342,418, a pit on both sides) with the spine props on it; flying over them at 107,800 to 108,800 clears them, and the
altar is at z 183,000 to 190,000 (floor 100,000 at x 342,000, z 186,000).

**Altar and heads (`mkaltar`, `hf2` to `hf4`)**: from the natural arrival save (fire breath) a `place` in the hall, W held and
the waypoint autopilot reach the altar (waypoint 8 of 9, `WP_HOLD` keeps Arokh over it), F5 there. The four Dr. Evil heads
(Primitive Giant clones, 250 hit points each, not lowered) stand around the altar; with `DRAKAN_PLAYTEST_AUTOAIM=1003b` and
`_ALT=101700` and breath taps (22 per round, places at three spots 5,000 to 8,000 units from the heads) **all four died to real
breath hits at their real hit points in about 2 minutes of game time** (head 23: 250 -> 190 -> 40 -> -1 within 600 ticks; the
dead heads fall into the pit, y 47,066). The Navros DeathCounter (162) reads 0 and the ending starts.

**Ending and outro (`end1b`, `end2`, `end3`)**: quick load of the save made during the sequence: the mirror room scene (Delon
falls into the mirror; the German "Vorgang war erfolgreich" quick-save text shows over it: F5 is accepted during the scene),
then the outro on the Wartok farm landscape (`Out.lvl`, "Surreal Software" and the Drakan 2 credits in German, scrolling at the
engine's rate; the stock credits, not an Order of the Flame list), and **at t = 272 s the game returns to the title** (menu
rows: Load, Continue, Quit; "newest save slot 1 (Rift World)"). So the converted campaign reaches its end and its credits
from the last level.

**War giant (`ag-4`, `ag-5`, Alwarren from the `sessA-3` save, German tree, sword equipped with `1`)**: the old open-loop
fights landed 1 to 6 hits and the giant killed Rynn. With `DRAKAN_PLAYTEST_AUTOAIM=1003b` (range 30,000) and W held the facing
error is 0 to 9 degrees all fight, **the giant (150 hit points) dies by 1,800 ticks** (`ag-5`: 150 -> -16 -> -232, i.e. dead
in the first 15 s) before he grabs and throws her to the same corner as before (`ag-4` with a 9,000 range: 150 -> 9, then she
lay 12,600 away and the range left her unaimed). The converter side of the hammer reveal after the kill was not re-checked.

### Not covered

Delon himself (see section 7) was not killed; the Ebon Dragons were fought at 15 hit points only (150 not
retried with AUTOAIM). The corridor ramp's waypoints reach the hall most but not all runs (the autopilot's timing).

## 7. Delon, the Ebon Dragons at 150, flyability, the outro and one continuous session

**Delon survives a quick load (no bug).** I had searched for him in the wrong class: Delon (PC object 161) is engine class
`0x10052` (the Death Mage / NPC class, 650 hit points), not `0x1003b` like the four heads (`findcls.py 10052`: slot 25 in a
direct start, slot 24 after F8, hp 650 in `ram_saved`, `hf2`, `hf4`; the six "Death Mage ghost" clones are the same class with 75).
F8 inside the arena keeps him. He hovers at Arokh's height (y 104,800 to 107,100) within 1,000 to 2,500 units of him, casts a
blue haze over the screen and the breath orb turns white; **breath did not damage him** in three runs even with the new pitched aim
(`dl1` to `dl3`: 650 -> 650), so his fight was not won (an open question: invulnerable until a message, vulnerable to Rynn's
sword only, or the spell disables the breath). The ending does not need him: the heads' DeathCounter (162) alone starts it.

**Ebon Dragons at the real 150 hit points** (`eb1`, pack `packaid`, natural arrival save): `DRAKAN_PLAYTEST_AUTOAIM=1001e` with the
new `DRAKAN_PLAYTEST_AUTOAIM_PITCH=1` (aims up or down as well) and a `place` 3,250 units from each dragon 900 above it: all four
died in 16 rounds of 14 breath taps (16 attempts, 4 dead) and the gate counter 558 reads 0. So kills work without lowering the
hit points. (`OOTF_TEST_EBONHP` is no longer needed for that.)

**Human flyability (`hum1`)**: a plain scripted flight, no height hold and no steering, W held from 286,000 north of the
ramp, placed at six heights over the tunnel mouth. At 94,000 / 94,800 / 95,500 / 96,500 / 97,500 Arokh flies the ramp and enters
the hall (end z 276,100 to 277,600), the state word `+0x9c` stays 0 (never landed); at 98,500 he is stopped by the ramp's ceiling at
z 283,851. So the entrance window is about 3,500 units tall (94,000 to 97,500 at z 286,000; the arrival height is 100,119, so a
player dives about 3,000 units), against the landing snap 1,400 over the floor. The tightest point on the route is the ramp
at x 334,000, z 279,000 (floor 92,500, ceiling 97,262: 4,800 units, 3,400 over the snap); the 2,200-unit sill at z 278,000 is not
on it. Nothing in the converter or the runtime needs to change; the safe corridor is: from the arrival dive to about 95,500, fly
north along x 333,000 to 335,000 (left tunnel), then bear east in the hall (ceiling 100,700) and climb to 99,000 for the shaft.
I could not compare with the PC engine's own landing rule (no PC executable run); the PC level data are identical, and the
route is flyable here with margin, so no fix is scoped.

**Outro**: the PC Order of the Flame install holds no credits content that the pack could carry (no movie, no credits asset, no
credits strings in `Drakan.exe`, `Dragon.rrc` or the resource files: searched). The PS2 outro (`OUT.LVL`, the Wartok farm and
Surreal's German credits scroll) plays as in Drakan 2. Replacing it would need a rewritten `OUT.LVL` text list (the
format of its scroll text was not examined); not done, documented as a known gap.

**One continuous session (`chain2`, Volcanic start, god mode, one process, 14 minutes)**: Volcanic (key, cage, Runeblade handle
moved next to Arokh by the test aid, mount, rift box, exit, save screen) -> Rift World (arrival mounted, fire breath) -> autopilot
route (waypoints, `place` into the hall) -> the four heads killed at 250 hit points (30 rounds) -> mirror-room scene -> outro
credits. Level loads: Volcanic at t = 15 s, Rift World at 188.6 s (about 170 s in Volcanic including the cage scene, the
walk and the exit); the heads fight and the route took about 7 minutes of game time and the credits are still scrolling at the
end of the recipe (they run about 4.5 minutes). Frame rates on the overlay: Volcanic 31 to 46 fps, Rift World 55 to 90 fps,
credits 400+ fps (a static scene). Memory of the host process (sampled every 30 s): working set 1,165 MB at 3 min, 1,211 MB at
6 min, 1,158 / 1,150 / 1,113 MB later, private bytes 1,497 to 1,651 MB then 1,549 MB: no growth over the session. No crash, no
watchdog stall, no guest allocator spin (the one `ray-water` watchdog line of an earlier run was GPU contention from other
sessions). A first attempt (`chain1`) with the autopilot active from boot steered Rynn in Volcanic; `WP_ACTIVATE` fixes that
(committed). The earlier levels were not chained in one process in this pass: their walk-in pairs were checked in separate runs
(Wartok to Grimstone, Grimstone to Grotto, Grotto to Islands, Islands to Alwarren, Rift Crystal to Islands Night, Islands Night to
Succubus Lair, Succubus Lair to Volcanic).

## 8. Delon's data, the credits entries, and the chain attempt

**Delon (PC object 161, class "Delon", engine class 82)**, read from the PC class data (`pccls.mts`): a ground humanoid caster, not
a flier: Initial Health 650, Max Speed 4, Armor % 0, Defense % 100 (he always defends, Defense Reduction 0), Attack Radius 0.5,
Missile (spell) 135864 and Summon 1527 at 50 %, Teleport? 0, Maximum Alpha 0.2 (only used when teleporting), Message To Send 8
(his death message, the same value the heads and Ebon Dragons send); links: Death Mage waypoint 476 and counter 455 -> Stomp
Player 436. The converter maps him to **the PS2 Death Mage** (`--audit delon`: "rfl 82 creature -> cloned Death Mage"), a
hovering teleporting caster, and drops his attack effects ("Delon Attack: Special Effect: no safe target ... omitted", 8 classes).
That explains what was seen: the clone hovers 1,000 to 1,800 units over the altar at Arokh's height, follows him, and its
spell is the blue haze that whitens the breath orb. Breath did not lower his 650 in `dl1` to `dl3`, `dm2`, `dm3` (also pitched
aim, also Arokh landed beside him); Rynn could not be brought to him on foot (the dismount from a landed Arokh at the altar put her
in the pit, y 58,500, `dm3`). The PC data give no vulnerability gate (no "invulnerable" field, no message counter in front of him),
so the likely causes are on the PS2 side (Death Mage damage rules while hovering or "Defense 100 %"), not a lost flag; I did not
read the PS2 Death Mage's own damage handler, so it is not shown. How he is meant to die on the PC: by Rynn's sword, bow or spells
on the altar (his Defense 100 % blocks melee attacks half the time), his death message 8 reaches counter 455 and scene 436 plays.
The converted game ends without him: the heads' counter alone starts the ending.

**Credits text**: the Surreal and Sony credits are two plain ASCII text entries, `0x401` id 8807 (1,575 bytes, "=Heading
Name

"
blocks) and id 8808 (2,148 bytes), in each language's `DRAKAN2.RRC` (ISO sector 3,958 of the English tree). `OUT.LVL` (8 KB) holds only
the Wartok farm scene and does not name them: the engine reads the two ids directly. A pack can replace them with
`overrides/DRAKAN2.RRC/0401_8807_0.bin` (any language tree), but pack entry overrides are global, so Drakan 2 would show the same
text when the pack is installed; a game-scoped credits override needs a small runtime change (a manifest key such as
`campaign_credits` served for those two ids only while the campaign is the active game). Not implemented. What could be written
without inventing names: a short card ("Drakan: Order of the Flame", "Original game: Surreal Software, 1999", "Converted for the
Drakan: The Ancients' Gates engine port"); the PC install has no credits list to port.

**Chain attempt from a fresh New Game (`chain3`, god mode, pack of formats `f5d3c6b`)**: the modern title needs Enter twice (New
Game, then "Allein"). Level loads, seconds from process start: OotF 00 Intro at 30.4 s (auto exit at 74 s), OotF 01 Ruined Village at
74.4 s (the 44 s Intro included). **(Retracted, see section 9: the scene was still running.)** The Ruined Village opening scene seemed not to end by itself: with no input it was still the same
close-up of a villager 240 s after the level loaded (`rv_scene_over` frame of the first run); with Cross taps every 10 s it ended and
control came at about 268 s from start (about 190 s into the level, matching the earlier 145 to 170 s runs that had input). That is
the answer to the open question in the issue #4 note: an idle run does **not** end within 4 minutes, so the opening needs a key
press. Host memory over the 9 minutes (working set): 1,037 MB at 1 min, 1,184 MB at 5 min, 1,183 MB at 9 min (flat). The walk-in to
Wartok was not reached: the recipe's walk from (148,480, 24,700, 216,500) did not hit the converted exit box (the box sits on the PC
trigger, whose bounds I did not read), so the single-session chain stops at Ruined Village. The other pairs were walked in separate
runs in earlier passes; a one-process chain of all 13 levels was not achieved.

## 9. Corrections and fixes: the village scene, Delon, the credits

**Ruined Village opening: no regression, my reading was wrong.** The earlier method (`scene-timing/drive.py`: game choice, no input
after New Game, audio on, `DRAKAN_STOMP_PROBE=1`) differs from my chain run only in the title Enter taps (two, New Game then
"Allein") and audio off; I repeated it on the current exe and the current pack (`idle1`, Enter taps only, audio on, probe on, no
other input for 500 s). The three sequences ran back to back: 1.1 (ref `00080048`) 60.76 s, 1.2a (`00080049`) 51.88 s without any
wait at its trigger (so the 8 MiB texture budget fix holds), 1.3 (`00080047`) 86.9 s and still running at the last probe line;
the villager close-up is the 1.3 part, and the HUD with control appears about +250 s after the level loaded (`idle10.png`), as
in the 2026-11-06 notes (+240 to +250). In the chain run I had waited 240 s from the Intro, which is about 200 s into the
village: the shot showed 1.3 close to its end. The earlier statement in this note that an idle run "does not end within 4
minutes" is therefore wrong; the opening needs about 250 s, and the notes' "+140 to +160" is the 1.2a to 1.3 switch. No bisect
was needed (no regression to find).

**Delon: cause found and fixed.** The PS2 Death Mage template that Delon is cloned from has the field **"Invulnerable?" = 1**
(NPCS.ODB class 6634, field 169; the PC Delon class has no such field), so nothing in the clone could be damaged. It also has
Armor 0 and Defense 0 and is a ground humanoid ("Constrain To Ground?" 1, "Walk on Buildings?" 1): the hovering seen earlier was
Delon standing on Arokh's body. Fix in the converter (formats `5e67f7c`, `ootfpuzzle.ts` `vulnerableBosses`): Delon (PC engine
class 82, name "Delon") gets the object parameter `Invulnerable?` = 0; tests `ootfpuzzle vulnerable bosses`. Result in game
(`dm5`, pack with the fix, direct start, Arokh landed on the altar): his hit points reach 0 (the earlier runs never moved from 650),
he falls into the pit, and his death message decrements counter 455 (1 -> 0, `dw.py 10007`), the link that plays Stomp Player
436. He died to Arokh landing beside him rather than to a blow of mine; sword, bow and spells on foot were not re-run (Rynn on
foot needs a placement on the altar ring or its centre platform, which my 2,000-unit floor map only roughly gives), so "killable"
is shown by the damage path (hp 0 and the counter), not by a played fight.

**Campaign credits: implemented, but not effective in game.** Commit `a79d4c14` adds a pack file `credits/credits.txt`, a
`CampaignCreditsOverride` that serves it for the two credits entries only while the campaign is current (unit test in
`override_test`), the converter writes a short end card without personal names, and `modding.md` documents it. In game the
override is registered (`[game] campaign credits: 1 pack(s)`, `DRAKAN_GAME=d1`) but never asked: while the credits scroll the
engine makes no request for ids 8807 / 8808 through the entry layer (a log line in `wants()` never fired), so the German stock
list still shows. The credits come from the opened `Drakan 2.rrc` by plain reads. A working version patches the file at open (the
file service open filter and `patchSrsc`), which only applies when the game is chosen before the file opens (the title's game
choice comes later), so it needs the open to be redone or the read hooked; not done (`d3d0f6c3` documents the gap).

**Ruined Village exit, trigger bounds.** Exit 1077 is fired by PC object 1075, a "Creature" detector (Task 6 trigger only,
Detect Which 2, "Does Cave Entrance Teleport?" 1, "Dragon Takes Off Upon Teleport?" 1) at 150,528 25,520 211,968, scale (3, 2, 1):
by the Grimstone box (12,288 x 51,200 x 2,048 for scale 3, ?, 1) a slab 12,288 wide in x, 51,200 tall and 2,048 thick in z. Rynn on
foot crossed z 211,968 at x 148,480, y 21,093 (inside it) in `chain3` and nothing fired: the box is a cave entrance meant for
Arokh ("Dragon takes off"), so the exit needs the mounted dragon. The one-process chain therefore stops at Ruined Village. Numbers
from that session: Intro load 30.4 s, village load 74.4 s, host working set 1.04 GB at 1 min, 1.18 GB at 5 and 9 min.

## 10. Delon fought, credits showing, Ruined Village's dragon

**Delon in a played fight (`dl4`, `dl5`, `dm6`).** With the vulnerable fix (pack `packd2`, natural arrival save with fire breath,
`DRAKAN_PLAYTEST_AUTOAIM=10052` with `_PITCH=1`, Arokh placed over the altar) **breath takes Delon's hit points down: 650 ->
560 -> 440 -> 335 -> 185 in four rounds of 15 taps** (`dl4`); a longer run stops at 200 because he ends up standing on Arokh's back
(the robed figure in `dl5` `r6.png`), where the forward breath cannot reach him. So he now takes real blows; the kill itself was
not finished by a played fight (the position problem above). Rynn on foot on the altar ring (`dm6`, placed at 342,044 100,804
184,976 with the finer map below) stood beside him but a direct start gives her no sword (hand word 0: the loadout comes from the
earlier levels), so 200 bare-handed attacks did nothing; a sword, bow or spell test needs a save that carries them.
Finer floor map (`pcmap3.mts`, 500-unit steps, PC terrain): the altar is a ring, solid between x 338,000 and 346,500 for z 178,000
to 186,000 with a hole in the middle (x 341,000 to 343,500, z 180,500 to 184,000) and two outer rims at x 330,000 to 337,000 and
347,000 to 356,000; the rest is the pit. What the PC needs for the ending (read from the PC links): only the four heads
(Dr. Evil 108 to 111 -> Navros DeathCounter 162 -> Ribs 472 -> Stomp Player 473, the end sequence). Delon's death message 8 goes
to counter 455 and plays Stomp Player 436 only; the converted logic matches (the ending played after the four heads without
Delon in `chain2`, and his death moves counter 455 from 1 to 0 in `dm5`).

**Credits are effective now** (`cce1190f`). The credits entries are read with plain file reads, so `FileService::setReadOverlay`
rewrites them per read (evaluated at read time, which also covers a game chosen on the title after the first boot read):
`credits: read at 24536 patched entry 8807 (1567 bytes)` at boot with `DRAKAN_GAME=d1`, and again when the outro starts. In game
(`cred6`, test pack with the ending timer) the end card ("Original game: Drakan: Order of the Flame, Surreal Software, 1999",
"Engine: Drakan: The Ancients' Gates, Surreal Software, 2002") scrolls over the Wartok farm and the game returns to the title. Test:
`override_test` (43 checks, a synthetic RRC, the disc game untouched, partial reads patched).

**Ruined Village's exit and Arokh.** The exit detector 1075 links a "Land Morph" (terrain morph 1076) and the exit 1077; the
village's only dragon is the Dragon object 1011 in the lair at 484,416 24,773 201,196 (with "Dragon Lair", "Dragon Falls Rocks",
the "Dragon Clutch" detector 1089 and an "arokh awakening" scene 1625). Placed beside him facing him (`rv3`: Rynn at 482,600
22,900 201,196 yaw -pi/2, W for 3 s) Rynn walked past him to x 487,205 without mounting, and E did nothing: he is story-gated
(the awakening scene), not a free mount, and I did not find the step that frees him. Therefore the mounted exit, the Wartok arrival
by dragon and the chain from Wartok on were not run in this pass. The `727aaa8e` rule (bring a parked dragon along when Rynn
leaves on foot) is not involved here: the village exit needs the mounted dragon. No per-level table beyond the earlier numbers
(Intro load 30.4 s, village load 74.4 s, host working set about 1.0 to 1.2 GB flat).

## 11. Delon on foot: no weapon source yet (partial)

Tried to get a weapon into Rynn's hand in Rift World (private runner without the slot lock, own exe copy): both the direct
start and the natural-arrival card (`cards/rw-arrive`, F8) start her **mounted on Arokh with an empty inventory** (Tab: no item,
no weapon; the native pad cheat only adds the Spells tab). Holding C dismounts her, `place 342044 100804 184976` then puts her on
the altar ring beside Delon, who walks around her (his hit points 650 / 650 throughout the watch, `d+0x24c` / `d+0x250` in the
`DRAKAN_PLAYTEST_WATCH` data window, `data=600`). 60 attack taps with no weapon and no spell did nothing, as before. A sword is
therefore not obtainable by cheat or card in this level; the key-lock proof (door-proof.md, fifth round) shows that item pickup
by E works in the open, so the next step is a pack that places a weapon item class on the altar ring (needs a weapon item class
present in the Rift World object database, or a spell cast through the inventory flow from `combat_encounter_magic_cast.txt`).
Delon's Defense 100 % and damage while shielding were not measured. Not done: melee kill, counter 455 by a blow.

## Comment-ready summary for issue #5 (not posted)

Pass 3 final status. Rift World is the last level and the converted campaign reaches the credits from it: natural Volcanic
arrival (fire breath carried), the left tunnel, hall and shaft, the altar, the four Dr. Evil heads killed with real breath hits at
their real 250 hit points, the mirror-room scene, the outro credits (about 4.5 minutes, the stock Drakan 2 list) and the return
to the title. One 14 minute process covered Volcanic, the exit and Rift World to the credits with no crash, hang or memory growth
(host working set about 1.1 to 1.2 GB, flat). The Ebon Dragons (150) and Alwarren's war giant (150) die to real hits with
test-only aim aids; the tunnel is flyable by hand (an entrance window about 3,500 units tall); the RibDoors are side vaults. The
Ruined Village opening takes about 250 s and ends by itself (an earlier line here said otherwise; wrong). Delon was invulnerable
because the Death Mage template he is cloned from has "Invulnerable?" 1; fixed in the converter (he now takes damage and his death
message reaches counter 455). Delon takes breath damage now (650 -> 185 in four rounds) and the campaign credits end card shows and scrolls. Open: finishing Delon
with a sword or bow, a chained 13-level single process (Ruined Village's Arokh is story-gated, the exit needs him mounted), and an
unaided human run of the Rift World flight.
