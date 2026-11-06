# Issue 5: a Drakan 2 quest line on arrival in Succubus Lair

Run `s10-11` of [ootf-progression.md](ootf-progression.md) showed "Rette die Frau des Bauern, die in einer Höhle
nördlich seines Hofes Pilze suchte." right after Succubus Lair started (game language German). The line is not Order of
the Flame text (the converted pack carries only the English install's text) and not Wartok's: it is the disc's own
System.XDB string 8, the optional Surdana quest of Drakan 2 (the farmer's wife in the cave).

## Cause: the converter, not a stale runtime entry

Pack `build/harness/ootf-progress/pack5/ootf-levels.zip` (probe `build/harness/fixes3/markers.mts`, private):

* Every converted map marker of every level was an object of one SYSTEM.ODB class, `0102_4366` ("Map Object",
  group 0xbef "1 - Surdana"). The PC marker classes (engine 104) are cloned from a PS2 quest class with `reset: true`
  (`CLONE_ADJUST`), so all clones had identical data and the class plan's signature folded them into one record. Its
  defaults were the template's: Type 1, SubQuest? 1, Journal Entry 8, Map Text 0.
* The game keys a quest by its System.ODB class id (save row `+0x18`) and draws its journal text, Type and SubQuest?
  from the class record (decoded in [save-editor.md](../2026-10-31/save-editor.md)). The converter wrote the text only
  as overrides on the placed marker objects, which the quest table never reads. So whichever marker Succubus Lair's
  opening touched announced class 4366's text: string 8, in the game's language.
* The object overrides were broken as well: `setNamedParam` set Journal Entry and Map Text as plain string ids
  (dword = id, length 0) and then rebuilt the parameter list through `objectParamSpecs` / `setObjectParams` for the next
  scalar (Map X). That decodes a length-0 string as empty text and writes it back as a one-word inline string, so both
  ids became 0 (`Journal Entry` dword `0x10006` pointing at a zero word, in all four Succubus markers).

Quest rows do persist across levels by design (the save's table, type 0x401 id 893); nothing in the runtime kept a
wrong entry. With one shared class, every converted quest was also the same quest to the game.

## Fix (formats)

* `ootfclass.ts` `markerClassIdentity`: a named PC marker class gets its own clone; markers of the same name share one
  (the same quest on two levels stays one quest); generic names (`Map Trigger`, `Map Object`, which the puzzle pass
  leaves out) share one record.
* `ootfpuzzle.ts` `markers()` passes Type, SubQuest?, Journal Entry, Map Text and the map position to the new
  `PuzzleEnv.setClassDefaults`; the pipeline writes them into the marker's synthesized record
  (`ootfclass.ts` `setSynthesizedDefaults`, first value per field wins, a later different text is a warning).
* `setNamedParam` sets scalars with `setPlainParam` (keeps the other parameters word for word), so a string id set
  before survives.

Tests: `test/ootfpuzzle.test.ts` (string id kept after a scalar, one record per marker name, journal text / type /
SubQuest? in the record for a quest and for a place label, conflicting text keeps the first). All four fail on the old
code.

## Result

Fresh full conversion (`build/harness/fixes3/packA/ootf-levels.zip`, private): 22 marker classes in SYSTEM.ODB, each
with its own text (`Map Trigger : Get Arokh` -> Journal Entry 0x6010 "Get Arokh", `Flame Fort Icon` -> Type 0, Map
Text "Flame Fort", and so on); the Succubus markers carry Journal Entry 0x600d..0x6010 as objects and classes.

In game (exe of this pass, German, same start and placement as `s10-11`: `DRAKAN_START_LEVEL=OotF 10 - Succubus Lair`,
`DRAKAN_PLAYTEST_POS=236000 220500 247723 1.5708`, run `build/harness/fixes3/runs/succ-A`): the arrival line now reads
"Get Arokh" (`placed.png`), the Succubus Lair quest of the English install. No Drakan 2 text.

## Not covered

* The journal page itself (Tab, native inventory) was not opened in this run; it reads the same class fields.
* Converted quest text is the install's language in every game language (slot 0, issue 3 journal).
