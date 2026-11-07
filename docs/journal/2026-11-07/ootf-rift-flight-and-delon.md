# Issue 5: closed-loop flight into the RibDoor tunnel, the opening scene of Rift World, and why Delon was not fought (items c and d)

Private runs under `build/ootf10/h/` (own copy of the 16:27 executable in `build/ootf10/play`, the `build/harness/ootf7/packe`
pack, direct start of "OotF 12 - Rift World", god mode, `DRAKAN_PLAYTEST_HOLD=1`, one game at a time). The driver is
`scripts/harness/live_drive.py` as a library plus a private flight mission (`build/ootf10/h/fly.py`): it reads Arokh's position and
matrix out of the process and sends only what a player has: W held, SPACE for height, mouse x for the turn, mouse y for pitch.
Nothing is placed, no height is held, no autopilot runs.

## (d) Flying the tunnel with real inputs

Measured controls of the mounted Arokh (probe `probe1`, direct start, arrival 342418 100119 319535 facing north):

* W moves him about 200 units per frame; SPACE climbs about 60 to 100 units per 5 samples; C dismounts (do not hold it).
* Mouse x turns by -1.1e-4 rad per count in total, but slowly: the turn lags by about 20 frames and the turn radius at flight
  speed is tens of thousands of units. A controller that steers on the heading error alone overshoots (the first three attempts
  circled, `fly1` to `fly3`). Steering on the heading error minus 22 frames of the measured turn rate, with up to 400 counts per
  decision, follows a path.
* Mouse y adds about 7.3e-4 rad of pitch per count (it stays until the opposite push), so a pitch hold works from the matrix
  (`m[5]` is minus the sine of the pitch).

Result (`fly5`, waypoints 338500 97500 300000, 335500 96000 288000, 334300 95300 281000, 333800 95000 277500): Arokh dives from the
arrival, crosses the line of the tunnel mouth at x 334,900 and height 95,970 (z 287,700), is inside the ramp at 334,400 / 95,360 /
282,560 after 221 frames and is at 332,500 / 94,790 / 279,900 after 335 frames. He never lands (state word `+0x9c` stays 0). So the
mouth and the first half of the ramp are flown by a closed loop on real inputs, including the 3,000-unit dive from the arrival
height. In the ramp the state word `+0x94` becomes 10 and his speed falls from about 200 to about 12 units per frame (the look-ahead
slows him under the ceiling wedge), so the corridor is crossed slowly. The hall and the shaft were not reached: see the next
section. No person has flown it; this is the nearest thing so far, and it shows no blocker before the ramp.

## A sequence plays about 6 s after Rift World starts and moves Arokh to the altar

New in this pass, and it explains the "jumps" seen in my flights. In a direct start of Rift World (idle, no input, `watch1`), about
370 to 460 input samples after the first gameplay sample the controlled Arokh is moved from the arrival to 341767 94803 183845 (yaw
-0.71, the same point every time, above the altar's hole) and then falls into the rift (y 38,229, the pit floor). The stomp probe shows
what runs: `DRAKAN_STOMP_PROBE=1` logs a sequence with reference `0x00080052` (51 actions) starting 21.3 s after process start, 6 s
after the level's first gameplay sample; the pictures show the camera cut to the altar and Arokh over the rift hole. My flights were
hit at different places (x 322,776, 330,484, 331,242, 329,669 and 332,007 at the moment of the jump), always about 450 frames after
the start. Not found: what starts the sequence (no counter or trigger of the level links to it that I could name), and whether it
also happens after a natural arrival from Volcanic. Earlier runs that placed Arokh repeatedly would not show it as a jump. Next step:
log the registry change at the sequence start (the object that fires it), and run a natural arrival (card from Volcanic, F8) idle
for 15 s.

Consequence for (d): the flight is cut short by this sequence after about 450 frames, and the ramp is crossed at 12 units per frame,
so a player cannot reach the hall in that time. Whether this is the PC level's own opening or a conversion error is open.

## (c) Delon on foot: not fought, and why

* Rynn reaches Rift World mounted on Arokh with an empty inventory (Tab shows no item). No cheat adds one: the debug menu has
  `lvlup`, `heal`, `drop` and so on, but no weapon command; the save editor's Add item needs a class the save already has a record
  for, and a Rift World card has none for a sword.
* The only weapon the campaign gives is Atimar, which comes from the first authored player's loadout carried through the opening
  (`2026-11-04/ootf-starting-inventory.md`) and then travels in the level-change saves. A card that has it in Rift World needs the
  whole chain from the opening (Intro, Ruined Village with its 250 s opening, then Wartok to Volcanic in eight walk-ins,
  `ootf-wartok-chain.md`), played in segments through level-change saves and continued from the title. The title route now works
  for that (`ootf-credits-title-chosen.md`: a save in `mc0_ootf`, "Fortsetzen"), but a save is listed only if it was made with the
  same pack build, so all segments must run on one pack. That is about an hour of game time and was not run in this pass.
* Even with Atimar the fight needs the walk to the altar ring through the sequence above, and Delon's Defense 100 % damage while
  shielding is unmeasured. With breath his hit points went 650 to 185 earlier; a sword kill is not shown.

So (c) is not won. The exact blockers: no weapon source in a direct or natural-arrival Rift World card, and the sequence that moves
Arokh 6 s after the level starts.
