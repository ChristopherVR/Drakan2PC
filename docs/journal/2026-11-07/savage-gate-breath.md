# 4B Savage Gate: breath tests (2026-11-07)

Own exe copy `build/gaps3/play` (the 16:27 build, same as `dist`), recipes `build/gaps3/r_gate1.txt` and `r_gate2.txt`
(not committed). Run through `play_run.py --level 4b`, Arokh mounted with the documented `place` and E taps, then
`place` in front of the gate.

## Data side: where message 24 could come from

* In `LEVEL4RA` nothing sends message 24 (`Shoot`): no object has a `Trigger Message` or `Message To Send` of 24. Across all
  13 level files only Kragmor 12868 (Primitive Giant), Andrellian Isles (conversation handlers, countdown 11622) and the
  Valley countdown 9777 send 24, all to their own scene timers, none in 4B.
* Timers 5728, 7795, 7796 and 7798 (all `Trigger On Message 24` and `Disable/Re-Enable Message 24`) have no incoming
  links, so the message is engine-generated or arrives by a group broadcast. Timers sit at y 142215 in a row at x 643000 to
  648000, z 1730300 (5728 at 628394 142215 1723776). Wheel 1959 (class 0x10023, "Single Land Link? 1", "Run Only When
  Visible? 1") is tied with the morph model 6822 by Object Grouper 6823 (644864 139996 1738240); Detector 7652
  (653451 144922 1744655, One Way) links to object 8 only.

## Runs

| Run | What | Result |
| --- | --- | --- |
| g3_gate1 | Arokh at 640000 144800 1744655 facing +x, SQUARE held 15 s (breath at the wheel, screenshot `a1`: flames and glow on the wheel), then W | Breath works and lights the wheel. Arokh stops at x 646780 (speed 12288, no motion), 4000 short of the wall 7527, the wheel keeps its pose. Gate not opened |
| g3_gate2 | Breath held while flying through the timer row (626000 142600 1724000 to 643084 147127 1732680), then the same approach | No change, same stop at x 646780 |

Conclusion: aimed fire breath at the wheel and flying through the timer positions with breath do not send message 24 (or
not in a form that reaches those timers). Still open: whether the 1959 wheel needs a hit by something other than breath
(arrow or sword from Rynn on foot, not reachable from the Arokh side), or the Stomp Player scene 5513 (621397 140387
1726379, countdown 5512 `Reset Message 25`, fed by timer 5511 on message 27) must play first. Next step needs the guest
message dispatch for `Shoot` (who calls it), not more recipes.

## Not done in this session

The Valley fight and lever, 1A Lair 12479 and portal 18028, the forward New Game chain and the undriven exits were not
advanced: each one's blocker (what enables Zola Dane, how Arokh reaches the player in 1A, the 1A cave floor that `place`
refuses) needs guest code reading rather than another run, and the data above did not give a lead.
