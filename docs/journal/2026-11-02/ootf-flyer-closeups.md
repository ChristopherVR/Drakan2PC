# Ebon Dragon, Drone and bats close-ups (2026-11-02, track C8)

Goal: pictures of the creatures that only show up while the player is mounted (Islands, Mines). No dismount aid was needed: the
converter test aid `DRAKAN_DBG_PULL=<class regex>` (+ `DRAKAN_DBG_PULLFLAGS=1`, `DRAKAN_DBG_PULLDIST=<d>,<dy>`, exported before
`ootf_level_run.py`) pulls matching objects around the start, and the mounted camera sees them. Recipe: `scripts/harness/recipes/ootf_creature_ring.txt`.
Runs were started with `--campaign --env DRAKAN_PLAYTEST_GOD=1`, one run name each, no game lock.

* **Ebon Dragon (Islands): draws and animates.** Pulled at 1500 units, the black bat-winged dragons are in frame around Arokh in
  every shot, in different wing poses between shots (`c8_eb` f0, f1, f3, u2). Class plan: `cloned sand dragon 2 DONT USE` (the template
  name is a PS2 leftover, the model is fine). Visible-flag check: 16 of 29 Ebon Dragons start hidden (`ootf-hidden-creatures.md`); the
  pulled ones had the bit forced, unpulled hidden ones wake from their Detect and Execute.
* **Drone (Islands): draws and moves.** Small red-winged crow-sized fliers with an orange body near the dragon in three runs
  (`c8_dr3` f0 / f2, `c8_dr4` u1) and fire / flame effects from them (their attack). Class plan: `cloned Blade Dragon (Crow)`.
  Two earlier Drone runs "failed": the guest sat in the allocator (`pc=0x1e2318` / `0x1e3290`, `[stall]` after the level loaded). The same
  stall hits control runs; three reruns (two Drone, one without the pull) all passed, so it is the known intermittent heap stall
  (`ootf-native-mips.md`), not the Drone.
* **Bats (Mines): alive, not photographed.** `Bats` is one PC object (id 2630, flags 0x01, no links) of engine class 0x10041 (resource
  0x51b94, model 0x31ab0, template `Butterfly`): a flock of 11 entities (leader flags 0x7001b, followers flags 0 with lists 16) that exists
  in the world dump in every run and flies off at once: about 5000 units from the start after 7 s. Pulled to 500 units they still leave
  before the first screenshot. A brightened screenshot shows a line of tiny white dots in the cave, the likely flock, no clear body.
  The Mines are dark and the model is tiny, so this is no close-up. `DRAKAN_PLAYTEST_POS` did not move the player in the Mines runs
  (identical frames), which blocked staging a shot next to the flock; a camera or teleport aid for that level is the missing piece.
* Harness note: parallel runs with their own `--name` work without the lock. World dumps land in `dist/DrakanPC/dev/world`, shared by all
  runs, and the file name holds a global frame counter: compare dumps by modification time, one run at a time.
