# Valley vault detectors, the Zola Dane fight and the undriven exits (2026-11-08)

Follows `playtest-gaps-6.md`. Same setup as `4b-flight-and-savage-gate.md`: own copy `build/gaps7/play` (mods off), scratch
in `build/gaps7/` (`valley.py` on top of `scripts/harness/live_drive.py`, `gate_pack.mjs` test packs, runs `runs/g7_*`), the
class handlers traced with `DRAKAN_TRACE_FN` (timer `395e48`, lever `416170`, cut scene player `4091a0`, level transition
`365138`; message event 20, engine id at `a3`). Handler addresses come from the class table at `0x4fca64` (12-byte entries:
class, handler, size). No game, runtime or converter code was changed.

## Valley of the Fallen: vault detectors 18975 / 18376

Data correction to round 6: both detectors use the class default `Detect Which` (Both), not mount only. 18975 (x 973398,
3 x 3 x 3 cells) sends 23 (Load) to cut scene player 18377 and to timer 19281, whose On opens VltDoor01 19016; 18376 (just
past the door, x 982629, thin in y) sends to timers 19279 (door Off), 19280 (Play Sequence to 18377) and 21257 (Blow Up to
Guardian Gate 19457). Sequence 731 is "6.05 Zola Dane's and Rynn" (actors Camera, Rynn, Zola Dane 9274): it brings Zola from
his y 230000 staging slab into the vault hall (986647 224601 303123) and leaves him there to fight. Sequence 434 (detectors
10061 / 18974, mount only) is the Shaza and Tora guardian scene.

| Run | What | Result |
| --- | --- | --- |
| g7_V1 | Rynn mounted, `place` 966000 226000 303182 | The level's own opening (Stomp Player 7577, sequence 497, Initial State 1) was still playing; Arokh stays put. Not a detector test |
| g7_V2, V3 | mounted, drive east at y 225k | Arokh stops at x 968147 (V2) and 972459 (V3); 18975 fires in both (trace: 0x1174 Load at the cut scene player and at timer 19281). Arokh cannot pass the vault door, so a mounted Rynn never reaches 18376 |
| g7_V4 | dismount (hold C; on the ground Cross dismounts), walk east on foot | 18975 fires, then 18376: three timers get On, the cut scene player gets 0x10a1 (Play Sequence); sequence 731 plays (70 s, Rynn held at 985216, Zola moved to 986647 224601 303123) |
| g7_V4, V6 | fight Zola after the scene (`mission_fight`, god mode) | Zola stays at 200 hit points over 515 attack taps (V4, no weapon: a direct New Game start has none) and 387 taps with a Mithril Axe picked up first (V6; picked up per the HUD text, equipping not checked). No death, so no message 8 |

So sequence 731 does not send message 8 itself (it has no message actions); message 8 is Zola's own death message, and his
death was not achieved. Whether he needs a vulnerability trigger, a specific weapon or only an equipped weapon is open.

### The lever chain with a test pack

Pack `pack_6_lever` sets timer 20860 `Start Mode` 0 (run at load instead of on message 8). Runs g7_V7 to V9: the trace shows
lever 18379 receiving On (0x1001) at load, so the 20860 -> lever link works. Rynn on foot at the lever (445 units from it,
g7_V9) tapping E (L1, Use) and pad L1: no further message at the lever or at timer 18378, no transition 9781. The g7_V6
control (Zola alive, no pack) is the same. The lever is `Lever 2000` with `Switch Type` 3; what activates that switch type
(Use, a weapon hit, facing) is not known, and Rynn had no weapon in V7 to V9. Exit 9781 stays undriven.

## The other undriven exits

* **Level 8 Stratos, 10481 (to the outro): works.** Its only source is timer 10482, which sequence 600 "8_07 Jasaad Defeat"
  (Stomp Player 4056, fed by the Jasaad fight) sends On. Pack `pack_8_600` sets 4056 `Initial State` 1; run g7_L8
  (`play_run.py --level 8`): Stratos loads, the level `Out` loads 2.7 s later and the outro movies `level0ro_6` / `_7` play,
  then the title. (The run then loops, because the play test's level redirect starts Stratos again: a harness effect.)
* **Level 8 8143, Valley 9779 ("Achean"), 4A 5132: no sender.** No object links to them and no sequence of the disc sends
  them a message (all type 7 actions of every English `.SSD`, `build/gaps7/seqmsgs.txt`). They are arrival anchors or unused;
  they cannot fire in play, and there is nothing to drive.
* **3A Andrellian Isles 8021: not reached.** Chain: shaman 3889 dies (after the three eyes, Countdown 13151) -> timer 3915
  (Start Mode 2, message 8) enables the mount-only detectors 7318 / 13786 / 13794 -> timer 13610 -> transition 8021
  (`ExitSequence` 125). Pack `pack_3a` sets 3915 `Start Mode` 0; the trace (g7_A3b) shows 3915 running at load and its timer
  targets receiving On. In g7_A3 Arokh flew mounted from the level start through the plane of 7318 (z 598029, y 215k to
  220k, inside its 50 x 30 x 2 cell box) to 879892 219707 613714 with no exit and no screen change; g7_A3b drifted off the
  route and was not a test of the plane. Whether 7318 was enabled and detected the mounted player was not traced (the
  detector class has no handler entry in the table). Open.

## Corrections to round 6

* `playtest-gaps-6.md` says the Valley detectors need a mounted player: 18975 and 18376 detect both; on foot is the only way
  through the vault door, mounted only reaches 18975.
* The Zola Dane slab is a staging area, as stated, but Zola is not only a cut scene actor: sequence 731 moves him into the
  vault hall to be fought.
