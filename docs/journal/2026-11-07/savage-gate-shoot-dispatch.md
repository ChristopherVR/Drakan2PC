# 4B Savage Gate: where message 24 (Shoot) can come from (2026-11-07)

Analysis of the guest (PAL ELF, own disassembly, nothing verbatim kept) plus one on-foot run in `build/gaps4/play`.

## Dispatch findings

* Ordinal 24 maps to engine id `0x1175` through the table at `0x4fbd48` (filled by `0x2cd648`; lookup `0x2cd9c8` / `0x2cd9e0`).
* A scan of the whole .text for the constant `0x1175` finds only: the table fill, a range check in the timer handler
  (`0x40e320`, which has no Shoot case of its own) and a receiver at `0x1f7940` (handles `0x1174` Load and `0x1175`).
  No code builds a message with `0x1175`; all 26 callers of the lookup `0x2cd9e0` and 51 of `0x2cd9c8` translate an
  ordinal read from an object field (the sender or the receiver side). So Shoot is never raised by the engine on a hit,
  breath or arrow: it exists only when a level or ODB field holds 24. The ODB class defaults hold it only for one
  LEVEL6EN timer (Disable/Re-Enable), nothing in 4B.
* Timers compare the incoming id with the id of their `Trigger On Message` field (`0x40e660`), so timers 5728, 7795, 7796
  and 7798 can only fire from a link source; none exists (checked again: no object links to them or to 7775). Wheel class
  `0x10023` (ODB "Savage GateWheel", 37 fields) is a looping rotating mechanism (Damage, Looping, Collide with player 1) with no
  hit or message reaction, and wheel 1959 has no links.
* Wiring read from data: 7796 (Start Mode 0) sends Off to Vis Toggler 7794 at load (hides the "NoSpin" wheel 7793), 7795 on
  Shoot sends On to 7794, 7798 on Shoot sends Off to 7797 (toggles the spinning wheel 1959), 5728 on Shoot sends Off to 5727
  (toggles wall 7527) and to 7775 (a Disable Detector at 465920 0 634538). I.e. the design was "a Shoot stops the wheel and
  drops the wall", with the sender missing from the shipped data. 4B has exactly one exit (detector 5595 behind the gate).
* Other levels use Shoot only as data (Kragmor 12868, Andrellian conversation handlers, Valley countdown 9777), so the same
  dispatch knowledge gives no lead for Zola Dane (9274) or for Arokh in 1A: those need a data sender that exists, which
  none of the message scans found (Valley/1A not run this session).

## Run g4_foot1 (Rynn on foot, `place 640000 140500 1744595`, W for 18 s, SQUARE tap)

Rynn walks east at ground level and stops at x 649609 (z 1744606, y 140510), directly under the wheel (650275, 144967), 1200
short of the wall 7527; sword swing does nothing measurable, wheel 1959 data unchanged. Not reachable on foot by sword
(wheel centre about 4500 above her). No bow is carried in 4B, so the arrow test was not run (needs a bow pickup from 1A).

## Dragon barriers near the gate (lead, untested)

Dragon Barriers 7825/7826/7827 (class `0x10076`) box the gate approach: 7825 spans about x 646661 to 656901, and Arokh's stop at
x 646780 matches its near face, so the stop may be the barrier and not wall 7527 (7825 at 651782 150122 1733813 scale 5 10 5;
7826 at z 1755317; 7827 at y 155657). Unverified because the z/y ranges do not obviously contain Arokh's path; worth testing
Arokh at other heights and z before more Shoot hunting.
