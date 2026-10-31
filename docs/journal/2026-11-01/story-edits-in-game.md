# Story edits in game: attempt (2026-11-01)

Goal: show in the running game that an edited subtitle, a swapped dialog line sound and a pressure plate -> door chain made in the
editor take effect, and check the message ordinal mapping. Result: **not proven**; what was established is below.

## Done and proven

* The packs are built by the editor's own code, not by hand: `app/src/story/content.ts` `ContentSession` (driven in node over an
  `isoStoryFs`: `setText`, `setLineSound`, `changes()`) and `buildStoryPackFiles`; plate chain through the `pressure-plates` template
  and `applyDocumentToLvl` + `buildModFiles` (LEVEL4LD, plate -> counter -> door 1420, message Unlock). Scripts are scratch
  (`build/scratch/story2/make_conv.mjs`, `make_plate.mjs`).
* In game (private play folder `build/story2-play`, exe from `build/final-game/bin`, `dist/DrakanPC` could not be redeployed while other
  runs held its DLLs): the packs load (`mods: loaded 'story.proof.conv' ... 2 entries`), the LVL / ODB containers are rebuilt with the
  entries (`LEVEL1ON.ODB rebuilt with 1 entry patch(es)`, `LEVEL4LD.LVL rebuilt`), and `DRAKAN_PLAYTEST_POS` places the player
  (`placed after 1 try (0 units from the point)`), also 160000 units away in 1 to 10 tries.
* **Yaw convention of the play-test was wrong** (fixed): `DRAKAN_PLAYTEST_POS` yaw 0 faces **-z**, -pi/2 faces +x, +pi/2 faces -x
  (the Surdana farmer 1200 units to +x was on the right of the view at yaw 0 and straight ahead at -pi/2; shots
  `build/harness/s2-conv/c600.png`, `s2-conv2/c600.png`). The editor assumed yaw 0 = +x, so "Play from here" faced 90 degrees off.
  `yawFromDirection` is now `atan2(-dx, -dz)` (test updated), header comment of `drakan_playtest.h` too.

## Not proven (and why)

* **Conversation / subtitle / sound swap.** Standing next to the farmer (NPC 5102, 1200 units, handler 7806) with E (Use = L1) taps, with
  a template volume sending Start Conversation (28) or On (1) to the handler (#7806, LVL override), no conversation started: no new
  `0304` sound request, no subtitle (`DRAKAN_SUB_LOG=2` prints nothing but the intro), shots show idle animations. The farmer's
  `Detect Radius` is 0 and the handler has `Distance From Rynn 0.4`; whatever gates the start (NPC state, handler "Initially active", quest
  state) was not found. Teleports to the palace monk (#7770 / handler 7771) landed Rynn inside a wall or reset the game to the title;
  a teleport next to Gus (#7890, Detect Radius 1, handler 7891) put her on a wooden floor with no villager in view. The edited
  subtitle and swapped sound packs load, but no frame shows them. Sounds of conversation lines are preloaded at level load
  (`LEVEL1ON.SDB 0304/721` is requested at load in stock and modded runs alike), so a missing `0304` request does not prove
  absence of speech either: an audio trace is needed (`DRAKAN_AUDIO=0` was used; use the SPU key-on log from `sound-debug.md`).
* **Pressure plate -> door.** Ravenshold (LEVEL4LD, the only level with a door near the start and a plate class besides Stratos)
  starts Rynn on Arokh in flight, so a plate / door at the start is not walkable (`build/harness/s2-plate/sheet.png`). Needs an on-foot
  start (dismount, or a level where the player starts on foot with a plate class: only Stratos has plate instances, and it has no doors).
  The earlier Stratos attempts stay unproven too.
* **Message ordinals.** No change to the editor mapping. The ordinal -> engine id conversion was searched again in the ELF (no
  `addiu 0x1000` that builds a message id, the registration function at 0x2cd700 only stores per-class accepted ids into tables at
  0x50bc50 / 0x50bd48). Evidence still: quest start (1) and complete (18) work in game, door data (3 = Unlock, from the key lock
  `Trigger To Send 3`), 4 = Play Sequence in game. Ordinals above 7 other than 18 are untested; a runtime trace of the send function
  (`DRAKAN_TRACE_FN`) is the next step.

## Housekeeping

* `Build-LocalGame.ps1` deploy fails while other agents run games out of `dist/DrakanPC` (access denied on `avcodec-61.dll`); the
  compile itself succeeded. Concurrent agents also left a literal newline in `project.ts` and `drakan_settings_host.cpp` for a while,
  which blocked node loads and the build for ~20 minutes.
