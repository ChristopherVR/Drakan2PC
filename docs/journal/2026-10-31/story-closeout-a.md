# Story close-out A: quest and dialogue proofs in game (2026-10-31)

Method: private play folder `build/content2-play` (exe copied from `build/final-game/bin`), packs built by the editor's own code
(`build/scratch/content2/make_pack_*.mjs`: `questChanges`, `stringChanges`, the `volume-message` template, `applyDocumentToLvl`,
`buildModFiles`, `writeSdbText`, `setActionField`, `newSequence`), `scripts/harness/play_run.py` with `DRAKAN_MODS`. Shots in
`build/scratch/content2/`. The story files of Surdana (LEVEL1NA.LVL) live in `LEVELS\LEVEL1ON\LEVEL1ON.{ODB,SDB,SSD}`.

## Proven in game

* **A new quest started and completed by level objects.** Pack: new class 4000 (copy of class 4169, journal string 114, map text 115),
  a quest object of class 4000 placed in Surdana (clone of the Farmers Quest object #8666 with the class id changed), two
  `volume-message` templates at Rynn's start: `On` (message 1) and `Quest Complete` (18), both linked to the quest object. In the
  native journal (`quest_journal_done.png`) the new text is listed under Optional Quests with a **ticked box**: the object
  registered the quest (1 = start) and 18 completed it. Log: `SYSTEM.ODB/XDB`, `LEVEL1NA.LVL` rebuilt.
* **An SSD sequence override plays changed.** Replacing sequence 484 ("Meeting Zola Dane", 22 s, started by a template cut scene
  player at the start) with an emptied sequence built by `newSequence` + `addActor` removes the scene: `seq_empty_vs_base_240.png`
  (left stock: Rynn in the palace corridor with Zola; right: Rynn stays on the Surdana path with the quest hint). So the 0x1202 path
  reads `0311` overrides and the new-sequence writer produces a record the game accepts.

## Authored, not proven

* **Edited subtitle (SDB 0302):** the pack loads (`0302_119_15`), but cut-scene sound actions show no subtitle bar even in the stock
  game (checked 24 frames of the Zola scene, stock and modded, `build/harness/c2-z-*`): the bar is fed by the conversation speech
  controller (subtitles.md), not by sequence sound actions. A conversation has to be started for this proof; Surdana's farmer handler
  #7806 is 190000 units from Rynn's start, a `Start Conversation` (28) volume at the start did nothing (handler needs its NPC /
  distance), and the coop teleport aid `DRAKAN_COOP_TEST_GOTO` does nothing outside a coop session. Open: start the proof next to the
  farmer (a start position override, or walk there) and look at the top bar; `make_pack_conv.mjs` is ready (subtitle of sound 721,
  Rynn's reply line class 875 re-pointed to sound 33).
* **Swapped line sound (class patch) and swapped / retimed sound action in an SSD sequence:** packs built (`mods_conv`, `mods`), the
  retimed and swapped sequence is the one the game reads (see above), but no audio capture was made, so "plays another sound" is
  not shown. A camera keyframe edit (y + 1500 on all 7 camera keys of sequence 484) changed nothing visible: the scene is not
  viewed through the sequence's `Camera` actor in this port (or the keys are not what drives it).
* **Map marker of an edited quest class:** class 4295 (Palace Icon, X/Y 110/100) patched to 300/330 via `editQuestRecord`:
  the dot stayed at the same spot (`map_marker_base.png` / `map_marker_moved.png`), and the pure `0102` override produced no
  `rebuilt` log line (the quest pack with `new/` entries did). Either the class record is read once before the override layer, or
  the dot comes from the placed object. A new quest of Type "both" listed under "Optional objectives" in the modern map side panel
  but got no dot, in both completed and start-only variants (`map_quest_start_only.png`). Open.
* In the modern panel the new quest is struck through even in a start-only pack; the native journal for the start-only pack was not
  captured, so the "On = start only, no complete" half of the semantics is untested (the complete case is proven).

## Code added

`formats/src/odbwrite.ts` `setClassArray` (replace an array field with any length; elements appended to the data area, the field
word points at them); `formats/src/story/ssd.ts` `freeSequenceId`, `sequenceNameLen`, `newSequence`, `addActor`;
`formats/src/story/conversation.ts` `newDialogLineRecord`, `handlerPhaseRecord`; `editor-core/src/story.ts` `NewDialogLine`,
`dialogLineChanges` (new line classes + 0x0103 list); `odb.ts` exports the two field-count offsets. Tests in
`formats/test/story.test.ts` and `storywrite.test.ts` (synthetic). The in-game acceptance of `setClassArray` / new dialog line
classes was not run (no panel UI was added for creating lines / editing phase arrays; the functions are the groundwork).

## Round 2 (map marker fix, conversation attempt)

**Map marker override: cause and fix (proven in game).** The game never requests `SYSTEM.ODB` (or any `.ODB`) entries through the
0x1202 resource layer: with `DRAKAN_HLE_LOG=2000000` the log has `file open ...System.odb` and no `resource: ...ODB 0102/...` line,
so the per-request override layer (`DirectoryOverride` / `ModPack::entries()`) was never consulted and a pure `overrides/SYSTEM.ODB/0102_*`
had no effect (the earlier quest pack only worked because its `new/` entries forced a container rebuild). `ModPack::isPlainReadContainer`
now treats `.ODB` like `.LVL`: entry overrides of an ODB are patched into the rebuilt file (log: `mods: ...SYSTEM.ODB rebuilt with 1
entry patch(es)`). `runtime/hle/tests/mod_test.cpp` checks the ODB override now shows in the rebuilt container. In game, class 4295 (Palace
Icon) X/Y 110/100 -> 200/160 moves the yellow dot from (710,450) to (1082,635) on the native map page
(`map_marker_base_r2.png`, `map_marker_moved_200_160.png`). The same fix means the class patch of `LEVEL1ON.ODB` (swapped line sound)
is now applied (`LEVEL1ON.ODB rebuilt with 1 entry patch(es)`); before it was silently ignored.

**New quests do get a map dot.** The earlier "no dot" was only an out of range position: the map plots X 0..~260 / Y 0..~190 (about
3.7 / 2.8 screen px per unit at 1080p); the test used X = 300. With X/Y = 200/160 the new quest (class 4000, started by a template
volume, `make_pack_quest.mjs ... start 200 160`) shows a dot at the same spot as the moved marker (`map_new_quest_dot.png`), drawn dark
(inactive / done colour) and struck through in the modern panel even in the start-only pack; why it counts as done is still open.

**Conversation proof: not achieved.** `DRAKAN_PLAYTEST_POS` works (the player stood 0 to 350 units from the farmer handler #7806 /
130 units in one run). With the `Start Conversation` volume and with E (Use) taps near the handler, no conversation started in 2100 frames
(the frames show idle animations only; `LEVEL1ON.SDB 0304/720, 721` are requested at level load in both the stock and the modded run, so that
is preload, not speech; no `0304/153` or `/33` request). The handler's NPC is probably the wandering villager #5102 and the handler needs
Rynn to face it; the yaw sign of DRAKAN_PLAYTEST_POS (tried 2.47 toward the villager) put the camera away from it. Next attempt: stand at the
villager's position with yaw pointing at him, tap E, run with `DRAKAN_SUB_LOG=2`. The packs (`build/content2-play/mods_c_conv`, `mods_c_base`)
are ready and now apply the ODB class patch.
