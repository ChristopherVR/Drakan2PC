# 4B Northern Tundra: closed-loop flight to the exit hall, and what opens the Savage Gate (2026-11-08)

Resolves the contradiction between `../2026-11-07/savage-gate-route.md` (exit 5595 reached from the start without the gate)
and `playtest-gaps-6.md` (no route under the flight ceiling, pocket only through the closed gate). Own exe copy
`build/gaps7/play` (copy of `dist/DrakanPC`, the 2026-11-07 16:27 build, with both mods in `mods.ini` switched off: the
`gaps6` runs had `ootf-levels` on). Scripts and output in `build/gaps7/` (not committed): `fly.py` and `gate.py` (missions on
top of `scripts/harness/live_drive.py`), `route.py` / `minimax.py` (terrain), `gate_pack.mjs` (test pack), `runs/g7_*`.
No game, runtime or converter code was changed.

## Answer

`playtest-gaps-6.md` is right and `savage-gate-route.md` was wrong: the exit hall is a closed tunnel that is entered only
through the Savage Gate. Its g5_a run started inside the hall (`place 690000 146000 1744655`), so it never tested the way in.
But the gate is **not** dead data, as both notes assumed: it is opened by a cut scene, and the game opens it correctly.

## The real flight (start to the gate, no position writes after the start)

`fly.py`: the 4B level start on foot, walk Rynn into Arokh (mounts, class 0x1001c), walk him through the cave mouth (No-Fly
zone, A/D turn him on the ground), SPACE taps take off, then W held, mouse x steers (stick mode, rate), SPACE climbs and
mouse y (negative, a dive) descends on a vertical speed loop. Waypoints from `route.py` (Dijkstra over the decoded terrain
layers, floor of 3 cells around + 6000, cap 176000; the gaps6 file `wp4b_176000.txt` only reaches the ridge approach, so a
new start to basin route was used: 97 points, max floor 153350). A stop is 4 s without 150 units of progress; on a stop it
climbs (SPACE taps) and retries three times.

| Run | Last leg (east along z 1744655) | Result |
| --- | --- | --- |
| g7_F1b | descend to y 144800, through the gate | start to the west basin in about 160 s with no stop (the z 1383k to 1414k ridge band crossed in flight at x 646k to 686k, y 147k to 151k); stops at **x 647042, y 144613**: the Invisible Static Wall 7527 (x 650799) less Arokh's radius. Retries do not lift him (on the ground) |
| g7_F1 | y 167000 (above Dragon Barrier 7827, top 160213, and the wall, top 151458) | over the gate, stops at **x 656717 to 658145, y 167000 to 168281**: the mountain above the tunnel (outer layer 2271, 164k to 176k at x 657k to 661k) |
| g7_F3 | flight ceiling (target 177000) | stops at **x 658775 to 659574, y 176745 to 177160**: the ridge, 182k to 185k from x 661504 |
| g7_F4 | the lowest saddle (see below) at the ceiling | over the waterfall steps, stops at **x 665776 to 666607, z 1705k, y 176922 to 177560**: the 180000 rim from x 668672 |

So the flight from the real start works up to the basin, and every way east stops at the wall (gate height), the mountain
(above the barriers) or the ridge (at the ceiling). The Dragon Barriers 7825 to 7827 were not the stop in any run.

## Why: the hall is a tunnel

The terrain layers (`build/gaps7/t4ra.json`, editor parser) show the pocket as three floor + ceiling layer pairs: 603
(x 651k to 672k, z 1737k to 1753k), 614 (x 672k to 690k, z 1737k to 1769k, holds exit 5595) and 616 (x 672k to 698k,
z 1769k to 1784k), floor 140000, ceiling 142000 to 152000. Walls are where the ceiling meets the floor. The openings: the
west edge of 603 at x 651264, z 1740800 to 1748992 (the gate mouth, ceiling holes) and the east edge of 616 at x 698368,
z 1773k to 1781k, which faces the map edge (no layer beyond). Above the tunnel the outer surface is the 180000 rim
(layer 320) or no surface at all (beyond the map edge) over the closed ceilings. A bottleneck search over all layers
(`minimax.py`) gives 180146 at x 674816, z 1707008 as the lowest way from the start, the basin or the Ravenshold arrival
to anything above the hall: 2000 above the flight ceiling (about 178100), and even past it the ceilings are closed. The
Ravenshold arrival (6451, x 821211) is in the start yard, not a second way in. The level has three transitions only:
5592 (to 4A, the yard), 5596 (Shiverbane arrival pair 7773 / 7774 in the hall) and 6451.

## What opens the gate: the "Zeggoro Lands" cut scene

The earlier notes looked for a sender of message 24 (Shoot) in level fields and engine code and found none. Two facts were
missed:

* The gate timers' **Start Mode** is the class default **1 = run on any message** (`build/odb-fields/LEVEL4ON.json`,
  Timer 3888: Start Mode default 1). Only 7796 sets 0. So `Trigger On Message 24` is irrelevant: any message starts
  5728 (Off to Vis Toggler 5727, which hides wall 7527, and to Disable Detector 7775), 7795 and 7798 (gate rings).
* **Cut scene sequences send messages** (`.SSD` action type 7, engine ids). A scan of every sequence of the English
  disc (`editor/scripts` scratch, `build/gaps7/seqmsgs.txt`) finds no Shoot (0x1175) anywhere, but sequence **1536
  "4.7c Zeggoro Lands New"** (LEVEL4ON.SSD) sends On (0x1001) to its actors "Trigger Timer" 5728, "Switch Gate Ring" 7795
  and "Switch Gate Ring 2" 7798, and 0x10a3 to "Morph Gate" 6822 (the opening animation).

The story chain, read from the data:

1. 4A Ravenshold: the four items (Blood 2286 and Ashes 2772 in 4A, Eye Amethyst 2481 and Blade 6944 picked up in 4B via the
   0x100dc forwarders "Picked Up Eye / Blade") count Countdown 2771 down, which enables detector 2624 at Zeggoro's tower:
   Stomp Player 2625 plays sequence 813 "4.7 Conference w/ Zeggoro". It sends On to timer 6686 (Play Sequence to 6687
   "Play Stomp Part B", class 0x100dc, Level Number 4 / Handler ID 5) and to timer 6688 (On to transition 6497: back to 4B,
   arrival 6451 in the start yard).
2. 4B: the twin 0x100dc object 5598 "Play Stomp Part B" (no incoming link, the cross-level side) sends to Stomp Player 5599,
   sequence 1342 "4.7B Confronting Savages": Rynn, Ranwulf and the Northmen ride to the gate, the five Savage footmen
   4252 to 4256 come out of it, and at 36.7 s it sends On to timer 5511 (Attack Linked AI to footmen and Northmen).
3. Each footman's death (message 8) counts Countdown 5512 (5) down; at zero it plays Stomp Player 5513, sequence 1536
   (Zeggoro lands and opens the gate), turns on the hall props (7655 -> Vis Toggler 7653) and the combat music.

### Game test of step 3 (test pack)

`gate_pack.mjs` builds a pack (`build/gaps7/pack_1536`, an LVL object-list override) that only sets Stomp Player 5513
`Initial State` to 1, so sequence 1536 plays at load. Run g7_G2 (`gate.py --kill 0 --pre-wait 70 --mods
build/gaps7/pack_1536`, `DRAKAN_TRACE_FN=395e48` on the timer handler):

* The trace shows three timers receive message 0x1001 at load (the sequence's three timer actors).
* The scene plays (Zeggoro, Ranwulf, Arokh landed at the gate), then the ring gate stands open (`runs/g7_G2/gate0.png`).
* Rynn placed at 638000 140800 1744655 and walked east: she walks into the landed Arokh (mounts), Arokh walks through the
  open gate along the tunnel and at **681493 142423 1763799** the game raises the level exit box (gui screen 2, "must be
  saved to continue", `exitbox.png`): exit 5595 fires.

The closed-gate controls are F1b above (Arokh stops at 647042) and `savage-gate-shoot-dispatch.md` g4_foot1 (Rynn on foot
stops at 649609). Killing the five footmen where they wait (their y 200000 stage, x 742k to 746k, z 1795k to 1805k) does not
work: in g7_G1 about 1700 sword strikes left the stage footman at 100 hit points. They are sequence actors until 1342 moves
them.

## Is anything at fault

No game or converter bug was found: the gate opens when the sequence plays, the wall and rings respond, and the exit fires.
A `--level 4b` start (New Game into 4B) lacks the 4A conference, so the pocket is closed there by design; the earlier
playtests all started that way. Not run end to end: the real story path (4A items, the conference, the 4B arrival, the
footmen fight after sequence 1342). The cross-level step (the two 0x100dc "Play Stomp Part B" objects, Level Number 4 /
Handler ID 5) is read from data only; it is the one link of the chain that a real playthrough still has to show.
