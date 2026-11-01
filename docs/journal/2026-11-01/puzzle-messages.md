# Puzzle messages: ordinal -> engine id (2026-11-01)

* Class handlers (`0x4fca40` table of `{class id, handler, size}`) are `handler(handle, event, ...)`; event 1 init, 4 tick, 20 message
  (a3 -> u16 id). `DRAKAN_TRACE_FN="45ba20,45de10,396428,416170,395e48,353550,4091a0,28d430"` with `DRAKAN_TRACE_N=100000` logs them.
* The message list is mapped by a table at `0x4fbc40` (see level-format.md 15.1): confirmed at run time for 0, 1, 4.
* Stratos run with a plate -> counter -> new door pack (`scripts/harness/story_puzzles.py`): door and counter received 0x1001.
* Not done: screenshots of the door opening (the NecroBB door is huge; camera ended inside it), lever / timer / key lock play tests,
  editor templates for timer and key lock, MESSAGE_NAMES needs no change.

## Round 2 (templates, harness)

* New editor templates (`editor-core/src/logic-templates.ts`, tests in `test/logic.test.ts`): `timer-door` (volume -> Timer with Start
  Mode "Run When Triggered By Any Message" -> target), `key-lock` (key lock class sends Unlock, ordinal 3 -> engine id 0x1102), and
  `pressure-plates` gained `spacing` and `axis` (plates in a row along X or Z). Guide text in `guides.ts`, `editor-guide.md` regenerated.
* Harness `scripts/harness/story_puzzles.py plate` (Stratos start canyon, Rynn on foot facing +z, new door `GF Wood Door01` locked with
  Auto-Open on, plate -> counter -> Unlock): the counter receives 0x1001 and the door 0x1102 (Unlock). Unlock = 0x1102 is thereby seen at run
  time, matching the table.
* Not proven: the door's visible opening. The Stratos plate is a tall block that Rynn cannot step onto by walking, and my gate is hidden
  behind it in the shots (`build/harness/puz-plate/`). Counter (2 plates), lever, timer and key lock kinds exist in the author script and
  `KINDS` but were not run; key lock has no key item class in Stratos at all.
* Death with the NecroBB door at +7000 in the plate room was not investigated further; the only observations are that Rynn lay dead after
  walking 4 s with W and that the door (a huge model, rotates 1.73 rad about its origin) sat in her walking line.
* Spawn quirks: a plate scaled 1.2 (stock) is about 4000 wide; a player spawned within it presses it at once.
