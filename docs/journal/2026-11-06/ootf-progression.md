# Issue 5: OotF progression past Ruined Village (walk-in exits)

Goal of this pass: extend verified progression past Ruined Village level by level through walk-in transitions, check
native F5 / F8 in a new level, and fix every blocker at its root. Pack: the full 23-level conversion (`ootf-convert.ts
--campaign 1`, template LEVEL1NA, all levels), regenerated under `build/harness/ootf-progress/pack*/` (private). Runs:
`dist/DrakanPC/drakan.exe` with a private ini, input ini, card, mods and logs per run under
`build/harness/ootf-progress/runs/<run>` (driver `run.py`, walk-in recipe `pair.py`; private, not committed). Each run
starts the level with `DRAKAN_START_LEVEL` (+ `_FROM=Level 0 - Intro`, game d2, as the editor's Play does), puts Rynn
a few thousand units in front of the exit's trigger with `DRAKAN_PLAYTEST_POS`, holds a movement key into it, confirms
the native message box (screen 2) and save screen (screen 5), follows the load into the next level, walks, quick-saves
(F5), walks away, quick-loads (F8) and walks again. `DRAKAN_PLAYTEST_GOD=1` keeps Rynn alive through the ambushes.
`DRAKAN_LEVELCHANGE_TRACE=1` logs the exit touch handler. The approach to each trigger is placed, not walked from the
level start; the trigger, the prompt chain, the load and the arrival are the real game path.

## Root cause 1: the converter's exit detectors sent the wrong message

Every exit detector on the disc sends **ON** (message 1) to its exit. Census of the disc templates (`Trigger Message`
of every object linking to an engine-55 exit): LEVEL1NA 9 detectors, LEVEL2RE 1, LEVEL3ES 1, LEVEL4LD 2, all ON (only
Timers send 5). The PC levels' own exit detectors send ON as well (their class default). The exit's touch handler
(`0x361440`, decompiled this pass) queues the level change and opens the save prompt on event `0x1001` (ON) only;
`0x10a3` (what message 5 arrives as) just sets a state bit, and `0x1002` disables the exit.

The November 4 change (`ootf-exit-detector-message.md`) made the converter's added exit detectors send 5 explicitly,
from a misread of the FX class default. Every box the converter added has been inert since; the one proven walk-in of
that day (Ruined Village to Wartok) was fired by the PC level's own detector (object 1075, ON) that the walk crossed
first. Trace evidence:

| Run | Pack | What fired | Result |
| --- | --- | --- | --- |
| `w03-04d` | added boxes send 5 | event `10a3` only | no prompt |
| `g04-05` | PC detectors retargeted to 5 (an intermediate attempt) | 10 x `10a3` while Rynn stood in the box | no prompt |
| `w03-04j` | added boxes send ON | event `1001`, exit handler msg 4 | prompt, save screen, Grimstone Mines loads |

Fix (formats `exitDetectorParams`): exit detectors send ON (`EXIT_DETECTOR_MESSAGE = 1`); test updated.

## Root cause 2: the PC levels fire exits from their own boxes, not from the exit object

Every PC exit of the campaign is fired by a linked object, never by touching the exit object: a detector box for 14 of
them, a Timer for the two Book exits, the `ship2` mover in the Succubus Lair. Those boxes lie up to 75,000 units from
the exit object: Wartok's mine-mouth "Creature Detector" 7,400 units from the exit, Grimstone's 20,000, the Islands'
rift detector 75,700 units above the exit object (which sits at height 0, 77,000 below the island), Volcanic's
mount-only detector 75,000 units away and disabled until the Runeblade's timer enables it. The converter put its boxes
on the exit object, which is unreachable in the Islands and skips the Runeblade gate in Volcanic.

The converted PC detectors already work for Rynn on foot (they send ON), but their "Detect Which?" = 2 (Both) does not
take the exit for Rynn on Arokh on PS2 (only "Rynn On Mount" does, 2026-11-01 finding). Fix (formats
`levelExitTriggers`): each PC detector that links to a campaign exit gets copies on the same box (position, rotation,
scale and every parameter, so Initial State and Detect Only Once stay) sending ON to the exit only, one for Rynn on
foot and one on a mount, or only the one its PC "Detect Which?" names. Objects that enable or disable the PC detector
link to the copies too (Volcanic: Runeblade -> Timer 2331 -> detector 2318 and its copy). Boxes on the exit object are
kept only for exits no PC detector fires (the way back from Wartok to Ruined Village, the Succubus ship exit, the
intro hand-over). Test: `puts the walk-in detectors on the level's own exit detector box, keeping its gating`.

## Root cause 3: Maintain Player Position was lost on two exits

The transition keeps the player's offset from the exit object when "Maintain Player Position" is 1 (PS2 Exit class
default). The PC sets 0 on Rift Crystal 308 and Volcanic 1237, but the converted exits took the PS2 class and lost it.
Volcanic's trigger is 75,000 units from its exit object, so the player would arrive in Rift World 75,000 units off the
marker. Fix: `CampaignExit.maintainPosition` carries the PC value and `applyCampaignExits` writes it when it differs
from the class default. Test: `keeps a PC exit's Maintain Player Position 0 instead of the PS2 default 1`.

## Engine hazards around arrival (runtime guard)

Several PC levels have an arrival marker inside the detector box of the way back (Grotto 312 and 405, Ruined Village
1077, the Islands 1169 and 3148, Alwarren 154). Starting inside such a box does not fire it on PS2 (`g05-start`: direct
Grotto start at the Rynn start inside Creature box 397, no touch in 15 s; arrivals also stay quiet). Two cases did fire:

* `w03-04j` (exit-object boxes, before the move to the PC boxes): Grimstone Mines' opening scene moved Rynn away for about
  110 s and put her back on the arrival spot inside the way-back box; that counts as entering, and the way-back prompt
  opened. With the boxes on the PC detectors this particular box is gone (Grimstone's way-back detector 2135 does not
  contain the marker), but any opening scene that puts the player back into a way-back box would do the same.
* `w03-04k`: the play-test placement moved mounted Rynn next to Wartok's mine-mouth box while the loading screen was
  still up (55 s load). The exit was queued and its message 4 arrived under the loading screen, so the save prompt never
  opened; a queued exit ignores every later touch, so this exit was dead for the rest of the run.

Runtime guard (`drakan_exit_guard.h`, used by `drakan_levelchange.cpp`, policy test `exit_guard_policy`): in campaign
pack levels an exit touch (event `0x1001`) is ignored under the loading screen and, in gameplay, until the player has
walked 1,500 units from where she got control (the spot is re-taken while not in gameplay and for 60 ticks after, so
respawns and cut-scene put-backs do not count as walking; touches during cut scenes pass, for cut-scene levels). Also
in the runtime: the November 1 rule that takes an exit for a message arriving more than 8,000 units from the player
(meant for the Intro / Book timers, which send 5) now applies only to events other than the touch, so a walked-in PC
detector far from its exit object (Grimstone 20,000 units, Volcanic 75,000) keeps the native save prompt in the Order of
the Flame game instead of being host-fired without one.

## Mounted Rynn and Wartok

Wartok Canyons starts with Rynn on Arokh. Holding Cross (C) for 2 s dismounts (`probe-dismount`); E (L1) does not.
A mounted Arokh placed in the mine tunnel does not move. Mounted Rynn crossing the old 5-message boxes produced only
`10a3` events.

## Level results (pack with all four fixes unless noted)

| Run | Level and exit | What happened |
| --- | --- | --- |
| `w03-04j` (exit-object boxes sending ON, before the box move) | Wartok Canyons to Grimstone Mines | Placed mounted 4,900 units short of the mine exit, Cross held 2 s to dismount, W walked 2,300 units into the box: event `1001`, message box, save screen, "exit taken", Grimstone Mines loads. Its opening scene then moved Rynn away for about 110 s and put her back on the marker, inside the way-back box: the way-back prompt opened (see the arrival hazards above). |
| `g04-05b` | Grimstone Mines to Grotto | Placed in front of the PC "Creature" box (rotated 230 degrees, 12,288 x 51,200 x 2,048) at the corridor: `1001` at once, prompt, save, Grotto loads (arrival offset kept: 23,000 units from the marker, as on PC). In Grotto: W 3,297 units, F5 (screen 23, save), S 4,742 units away, F8 (screen 23 -> 4, `fromSave=1`): position after load (350791, 27606, 369662) equals the saved one exactly; movement works after the load. |
| `g05-06`, `g05-06c` | Grotto to Islands | The PC "On Trigger" box 313 fires `1001` and the prompt opens as soon as Rynn is placed at the corridor (the box is larger than its 1 x 8 x 7 scale suggests; the exit list confirms the Islands exit, not the way back). Both recipes then waited for 200 gameplay frames that never came (the prompt holds the game): harness deadlock, the transition itself was not confirmed. |
| `i06-08` | Islands to Rift Crystal | Mounted placement at (656384, 76500, 469112) beside the rift detector: the world stopped drawing (World 0.0 fps, a blank blue screen) while the engine kept ticking; no touch. Placements elsewhere on the Islands worked on November 1. |
| `r08-09` | Rift Crystal to Islands Night | The detector sits in the whirlpool basin; a straight walk at the rim slid around it and ran into the wall. The whirlpool is always spinning (two start timers), nothing gates it; the way in is the "WhirlpoolSteps". |
| `n09-10` | Islands Night to Succubus Lair | Real walk: placed 4,000 units east, W 3,061 units west into PC detector 4928 (default 1-cell box): `1001`, prompt, save, Succubus Lair loads at the marker offset. W 4,260, F5, S 2,766 away, F8: back at (60421, 221498, 167939) exactly; D 3,238 after the load. Clean scripted quit. |
| `s10-11` | Succubus Lair to Volcanic | Placed on the ship's deck inside "Detect and Execute" 3181 (no walk): the PC chain runs (counter 3184 to `ship2` and `fade1`, the screen fades), the ship sends ON to exit 1394 (`1001` from another message source), prompt, save, Volcanic loads at its Rynn start. In Volcanic: W, F5, S 2,763 away, F8: back at (269341, 151498, 501686) exactly; D 3,226 after the load. A German quest line of the Wartok farmer ("Rette die Frau des Bauern ...") was on screen right after the Succubus start: not checked whether a Succubus object shows the wrong text. |
| `g05-06d` (recipe waits only 20 gameplay frames first) | Grotto to Islands | The PC box fires as soon as Rynn stands at the corridor: prompt, save, exit taken at the exit object, Islands loads with Rynn on Arokh at the marker offset (331681, 21067, 819064). W 982 units, F5, S did not move the flying dragon, F8 (`fromSave=1`), D 1,000 units after the load. Clean scripted quit. |
| `w03-04l` (final pack and runtime) | Wartok Canyons to Grimstone Mines | Arokh placed in the mine chamber facing east (yaw -pi/2), Cross held 2 s: Rynn dismounts 1,300 units ahead, W walks 4,800 units east into the PC mine-mouth box copy: `1001`, prompt, save, exit taken, Grimstone Mines loads 6,500 units off the marker (Maintain Player Position, as on PC). The opening scene runs about 120 s; no way-back touch afterwards. W, F5, S 2,767 away, F8: back at (169801, 32966, 287928) exactly; D works after the load. Clean scripted quit. |
| `r08-09b`, `r08-09c` | Rift Crystal to Islands Night | Placed at the "WhirlpoolSteps" facing the basin: Rynn stops at the basin rim (`r08-09b`). With two jumps while walking (`r08-09c`) she clears the rim and falls through the whirlpool into the blue void under the level; no touch from the PC box 2,000 units below the surface (box 4,096 x 1,024 x 4,096, PC Detect Which "Both"). The converted terrain reports the basin floor at 205,300 (the PC level's), so the fall points at the basin's collision flags or the missing water volume; not traced further. Blocker for the main chain. |

## Where the chain stands

Verified on the final pack and runtime, each by a walked or stood-in PC trigger with the native message box, save
screen and load: Wartok Canyons to Grimstone Mines (`w03-04l`), Grimstone Mines to Grotto (`g04-05b`), Grotto to Islands
(`g05-06d`), Islands Night to Succubus Lair (`n09-10`), Succubus Lair to Volcanic (`s10-11`). Native F5 / F8 restored the
exact saved position in Grimstone Mines, Grotto, Succubus Lair and Volcanic (Islands: saved and reloaded, but the dragon
did not move in between). Ruined Village to Wartok is the November 4 walk-in through the same PC detector 1075.

Open for the main chain:

1. Islands to Rift Crystal: a mounted placement beside the rift detector stopped the world drawing (`i06-08`); the rift
   was not reached by flying. Next: fly from a nearer placement, or place with `DRAKAN_PLAYTEST_SOFT=1`.
2. Rift Crystal to Islands Night: Rynn falls through the whirlpool basin (`r08-09c`). Next: check the converted basin
   triangles' collision flags and the PC water volume of the whirlpool.
3. Volcanic to Rift World: the mount-only rift box stays disabled until the Runeblade's timer enables it (gate kept by the
   copies); picking up the Runeblade and flying to the rift was not run.
4. The quest route inside each level (items, puzzles, the Ruined Village house and book) is still only partly walked;
   these runs place Rynn near each trigger.

Harness notes: a direct start of Wartok Canyons puts Rynn on Arokh, and holding Cross 2 s dismounts her facing the
dragon's heading; `DRAKAN_PLAYTEST_POS` yaw `y` makes W walk along (-sin y, -cos y); a recipe that waits for many gameplay
frames before anything else deadlocks when a trigger opens the prompt first (wait for 20 frames).
