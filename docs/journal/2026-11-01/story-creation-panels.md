# Story creation panels (2026-11-01)

New Story pages: New dialogue, New cut scene, Quest marker (`editor/packages/app/src/story/compose.ts`, logic in
`editor-core/src/story-create.ts`). The Story pack now carries content-session entries (dialogue, sequences) next to strings and
quests, so one export holds everything. Undo labels name objects (template setups, link/unlink, link message, resize, position,
rotation, scale).

## Disc census (all 202 sequences, English)
* SSD header word is 20/21/60/62/124/126, not the duration (126 on 92, 60 on 56). The helper's 60 is a disc value.
* Marker action: value 0 opens and 1 closes an actor (540 / 419); the old doc said always 0.
* Sound params 0x82710 on 809 sound actions; the old helper wrote 0.
* Camera / Rynn actor refs are per-level object ids (910/908, 4/3, 360/359, ...). A default of 908/910 would be wrong elsewhere.

## Browser proof (real disc copy, Surdana, dev server)
Screenshots in `build/`: `create-1-dialogue-form.png`, `create-2-dialogue-added.png`, `create-3-cutscene-form.png`,
`create-4-cutscene-created.png`, `create-5-quest-form.png`, `create-6-quest-placed.png`, `create-7-history.png` (labels name
objects), `create-8-pack.png`. The pack lists `new LEVEL1ON.ODB 0102/2/172`, the 0x0103 lists, `new LEVEL1ON.SSD 0311/1411/2`,
`new SYSTEM.ODB 0102/4000/...`, XDB string and the handler override.

## Game
`build/create-play/mods` (script `build/scratch/create/mkpack2.mts`): new line class 2 on the Surdana farmer object #7806 (via its
`.LVL` override), `Start Conversation` volume, subtitle on sound 33. Result: pack loaded, ODB rebuilt (+198 bytes), level ran, no
crash. The conversation never started (same as the 2026-10-31 attempts; `DRAKAN_PLAYTEST_POS` printed nothing in this exe), so the new
line being spoken is NOT proven. Shots: `build/create-9-game-sheet.png`.

## Found
A class-level phase edit does not reach a placed handler that overrides its array (all 52 in Surdana do). The panel can target a
placed object; that edit lives in the level pack.
