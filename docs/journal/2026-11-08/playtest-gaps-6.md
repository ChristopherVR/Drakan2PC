# Base-game playtest gaps, round 6 (2026-11-08)

> **Follow-up (2026-11-08):** `4b-flight-and-savage-gate.md` confirms the 4B result below with real flights and finds what
> opens the gate (sequence 1536 after the 4A conference); `valley-vault-and-exits.md` follows up the Valley items and
> corrects the Valley detectors (18975 / 18376 detect both, on foot is the only way through the vault door).

Follows `docs/journal/2026-11-07/playtest-gaps.md` and `savage-gate-route.md`. Own exe copy `build/gaps6/play` (copy of
`dist/DrakanPC`, the 2026-11-07 16:27 build), recipes and routing output in `build/gaps6/` (not committed), runs
`build/harness/g6_v1` and `g6_v2`. No game or runtime code was changed; nothing crashed or soft-locked in these runs.

## Valley of the Fallen: what enables Zola Dane

Run `g6_v1` (`place` at the Zola Dane object, 428075 231300 96000, after the 110 s opening): Rynn arrives mounted on Arokh
on the flat y 230000 slab with farmers, a hoe and a grey sand dragon (`Stone Sand Dragon` / `Stomp Sand Dragon` props).
Nothing animates over 60 s (identical frames, no Zola, no fight). It is a cut scene set, the same kind of stage as 1A's
y 200000 perch, not a playable arena.

Data (`build/lvl-objects/LEVEL6EN.json`):

* Zola Dane 9274 has no incoming link and sends message 8 (death) to timer 20232 (cut scene 20233), timer 20860 (lever 18379
  -> timer 18378 -> transition 9781 "Desert") and timer 21946 (Valley icon).
* `Gothral` 7546 on the same slab only sends message 8 to 21188 (skeleton archers in the Gothral hall), 18327 (torches),
  21595 (gates) and music. So both are scene actors that a sequence kills; the message 8 chain is a cut scene side effect.
* The real route is east: detectors 10061 and 18974 (x 695k to 745k, z 444k to 455k, Detect Which 1, once) play sequence 434
  (cut scene 18375); detectors 18975 and 18376 (x 973k to 983k, z 303k, once) play sequence 731 (cut scene 18377) next to the
  lever hall.

Run `g6_v2`: `place` into detector 18975 (973398 225900 303182) and 5 s of W, 70 s of watching, then `place` at the lever
(1022825 224900 338500) and E taps. No cut scene, no screen 2, no `exit taken`; Rynn stands in the lever hall next to Arokh
and E does nothing. So the detectors need a mounted player (Detect Which 1 on 10061 and 18974) or the sequence has to
kill Zola for 20860 to arm the lever; on foot the lever stays dead. Whether the mounted entry plays sequence 731 and
sends message 8 was not run (the `place` of a mounted Arokh into 18376 is the next test).

## 1A Lair 12479, portal 18028, Arokh in 1A

* The 1A Dragon (id 2, Initial Mode 1) is on the y 200000 stage and its only incoming links are timer 8388 (Start Mode 0,
  Trigger On Message 0, Time 0) and its own Fireball Power Up 14850. An earlier mount test on the stage
  (`gp_m1a/mount2.png`) shows Rynn, the Empress and Zola Dane and no dragon: it is a frozen scene actor, not rideable. The
  only way the port moves him is `bringDragon()` at a level transition (`kragmor-arrival.md`).
* The Lair cave (y about 150000) shares its level slice with Shadowmire's cave (7969, reached on foot in round 4) and the
  Surdana Falls (waterfall 17520 at 451326 151686 533296, Dragon Barrier 17861, detector 18226 at 452512 152973 521312).
  Detector 8689 (506616 150477 463094) needs the carried item 75999; it feeds 17676 -> 18044 which enables the cave entrance
  detector 17794 (marker 17795 at 423666 160850 512249, 25k from the Lair). So the Lair cave is entered through that
  item gated entrance, and `place` into the cave floor stays lifted to 160498. Not walked.
* 18028 (800541 170412 579164, Detect Which 1) is fed by timer 18110 (413108 173200 593794; Disable/Re-Enable message 17) and
  leads to transition 12829 and Arokh teleporter 18014. It needs a mounted player in 1A, and no mounted player is
  possible in a New Game 1A (see above). Same gap as the exit to 1B, which round 7's dragon-arrival fix covers only for
  the transition itself.

## 4B full flight

Terrain Dijkstra (`build/gaps2/route.mts`, `SLOPE=3 MAXY=176000 ABOVE=14000`, flight ceiling about 178100):

* start (839562 z 1010000) to the ridge approach (943400 z 1433000): a route exists (254 cells, terrain max 143990, waypoint
  string in `build/gaps6/wp4b_176000.txt`); it was not flown in this round.
* start, ridge approach or the west basin to the exit hall (690000 z 1744655), the arrival pair (681003 z 1753089) or the
  exit detector 5595 (681024 z 1765312): **no route under the ceiling**. The pocket is enclosed by terrain above the
  flight ceiling and is entered only through the Savage Gate (closed by wall 7527 until message 24 reaches Vis Toggler 5727).
  So an unbroken start to exit flight is not possible at present; the level's exit needs the gate opened. This confirms
  `savage-gate-route.md` was wrong to say a player never has to cross the gate: the pocket is only reachable through it.
  (Caveat: the heights come from the editor's PS2 terrain parser, 2048 unit grid; a flown test through the pocket ridge was
  not done.)

## Not done

Forward New Game chain beyond 1A, aimed breath at the Savage Gate wheel, level 8 / 3A 8021 / 4A 5132 / Valley 9779 exits,
mounted entry into Valley detectors 18376 / 18975. All need the Arokh in 1A mechanism or a mounted Valley test.
