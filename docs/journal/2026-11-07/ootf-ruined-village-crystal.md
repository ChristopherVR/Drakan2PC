# Issue 5: what frees Arokh and leaves Ruined Village

Follows [progression 3](ootf-progression-3.md) section 10, which could not find the step that frees Arokh. Runs are
private under `build/rvq/` (own copy of the 16:27 executable, pack of `build/ootf8/pack`, direct start of
"OotF 01 - Ruined Village", god mode, the 250 s opening skipped with Cross taps). Nothing from the game is quoted.

## The step: the Soul Crystal in the lair

The PC links, read with the formats loader (`loadPcLevel` on the PC `Ruined Village.lvl`, objects by id):

| Object | Class | Position | Links / notes |
| --- | --- | --- | --- |
| 3345 | Creature detector (Task 6), scale 5 x 0.25 x 5 | 487424, 22114, 200704 | -> 1078 |
| 1078 | Creature detector (Task 6, Initial State 1) | 487424, 23328, 200704 | -> 1625 |
| 1625 | Stomp player, the "arokh awakening" scene | 482347, 45364, 189483 | plays the scene |
| 2856 to 2858 | Dragon Lair, Soul Crystal Base and Top | 487424, 22000, 200704 | the crystal Rynn walks onto |
| 1011 | Dragon (Arokh) | 484416, 22614, 201196 | linked from a Fireball Power Up (3157) and a timer (1434) |
| 1089 | Dragon Clutch | 442878, 35024, 228222 | only a map trigger (link 1999, Map Trigger): not the gate |

So nothing needs pressing beside Arokh. The detector 3345 is a slab about 20,000 units wide and 1,000 tall on the crystal
base; Rynn standing on the base fires it, and its chain plays scene 1625. Earlier tries (E beside Arokh, `rv3`) stood on
the ledge next to him, not on the crystal, which is lower (y about 22,100 against the ledge at 24,800).

## Played

* `c2`: placed on the crystal (487424, 22300, 200704): the awakening scene plays (Arokh, then Rynn, then the crystal).
* `c4`: placed on the ledge at 482600, 25200, 201196 facing east, W held. About 2 s of walking later the scene starts
  (screens `w2` to `w8`): the crystal is reached on foot from the ledge, no placement on it needed.
* `c3`: placed on the crystal, Cross taps every 4 s. The scene ends with the engine's save prompt ("must be saved to
  continue"), the save menu, and then `exit taken in 'OotF 01 - Ruined Village'` with the player at 149879, 25211, 211978
  (the cave entrance, where the PC exit 1075 sits) and **OotF 03 Wartok Canyons loads at 277 s** from process start.
  This is the mounted exit: Arokh is freed by the scene, Rynn leaves with him and the exit fires without a walk
  to the detector. Rynn is on Arokh after the load (dragon HUD, camera at the neck); the first frames are dark close-ups
  of him.

Not found by this pass: a natural walk from the village start to the lair ledge (the runs place Rynn at the lair).

## Not done in this pass

* The one-process chain from Wartok on (a per-level time, FPS and memory table past the Intro and Ruined Village).
* Delon on foot, RibDoor flyability by a person, the credits after a title-chosen game in game. The credits path is
  evaluated per file read (`cce1190f`, `FileService::setReadOverlay` calls `CampaignCreditsOverride::patchRead`, which asks
  `drakan_game::currentModId()` each time), so a game chosen on the title after the first read is covered by the code and
  `override_test`; it has been seen in game only with `DRAKAN_GAME=d1`.
