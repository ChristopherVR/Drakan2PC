# Dr. Evil ending from real kills, and creature close-ups (2026-11-02, track C4)

## Dr. Evil: the fourth head
Rift World's four Dr. Evil heads stand up to ~23000 units apart (PC objects 108-111, positions 335829/174124, 333780/191445, 351105/175530,
348715/189739). In the earlier run (`ootf-state-proofs.md`) head 109 never moved: it woke (flags changed) but its yaw stayed 2.41 for the
whole run. Cause: the heads are clones of the PS2 Primitive Giant, whose own sight values (Detect Radius 25, Hearing Distance 8, Field of
Vision 120 degrees, Break Off Radius 50, all `lu`) leave a head that faces away from the altar blind and deaf. A head that never notices the
player never walks, so the DeathCounter (needs all four death messages) never fires.

Fix (`ootfclass.ts` `CLONE_ADJUST` for engine class 92): `Detect Radius`, `Hearing Distance`, `Break Off Radius` = 400, `Field of Vision` = 360,
written as float bits (`floatBits`). formats 6e4f627.

Evidence (campaign run, `OOTF_TEST_WAKE=4 OOTF_TEST_HEADHP=3`, `DRAKAN_PLAYTEST_GOD=1`, `DRAKAN_PLAYTEST_POS`, attack taps, world dumps):
* player at the altar centre (342357, 182720): heads 22 / 23 / 24 all walk in, hp goes negative and they die; head 21 stops 5100 units away
  on a ledge 2200 units above the floor (y 102.7k vs 100.5k) and stays alive, so the counter is short by one. In real play the player moves.
* player placed next to that head (339500, 182000): all four heads reach her, all four die (hp removed from the dump), and the screenshots from
  42 s on show the ending credits (World 0 fps, scrolling "Art / Animation" over the Wartok farm). The counter -> end timer -> credits chain
  fires from real kills, not only from the earlier timer test.

Harness notes: `ootf_level_run.py` hands its own environment to the converter, so converter test aids (`OOTF_TEST_*`) must be exported in the
shell, while `--env` only reaches the game. `DRAKAN_WORLD_DUMP_FRAME` counts samples (~56 per second here), not recipe milliseconds.

## Creature close-ups (Succubus Lair, `DRAKAN_DBG_PULL` + `DRAKAN_DBG_PULLAT` + `DRAKAN_PLAYTEST_POS` at the start with yaw 0 = +x)
Method: one creature placed 260 units in front of Rynn, four shots. Control: the Succubus appears in front of Rynn with wings and hair
(`c4_succ`), so the method works.
* **Queen, Navy Hueges, Tick: not visible** even at 260 units (4x brightened shot checked), although the world dump has them alive at the
  pulled position (Queen hp 300, lists 0 like every dormant creature in the Lair). The class plan says `Queen -> cloned Armor Gryphon`,
  `Navy Hueges -> Stomp Sand Dragon`, `Tick -> Spider`, models converted ("model=ok", 0 skipped); PC models parse (Queen 433 polygons, 39 joints,
  38 clips). Template differences to the working Succubus (Gryphon) are only in radii, health and clip ids; the Queen's head / neck channel is 27.
  Not found yet: why the skinned body does not draw. Next experiments: force a template with the exact rig size (Blade Dragon (Crow), 39
  joints), dump the display list of model 6683, check whether the idle clip is the Queen's own.
* **Bats** (Mines, start is mounted on Arokh): small dark winged specks around the dragon in four shots; plausible bats, no close-up possible
  while mounted.
* **Ebon Dragon, Drone**: not repeated (Islands also starts mounted); see `ootf-state-proofs.md`.

## Tool
`ootf-convert.ts --audit <regex>` prints the class plan row of every PC class whose name matches (outcome, PS2 template, model state), e.g.
`--audit 'queen|hueges|^tick'`.
