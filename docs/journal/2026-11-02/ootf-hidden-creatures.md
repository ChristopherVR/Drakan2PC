# Queen, Navy Hueges, Tick "not drawing": they start hidden (2026-11-02, track C6)

Follow-up to `ootf-drevil-ending-and-creature-closeups.md`. The bodies were never a model problem.

* **Cause:** PC object flag bit 0x01 is "visible" (`level-format.md`). Creatures with it clear start hidden. The PS2 engine honours it. A
  campaign-wide count (13 levels, creature engine classes): Death Mage 20 of 23 hidden, Tick 24 of 29, Spider 49 of 66, Ebon Dragon 16 of 29,
  Succubus 11 of 18, Queen 2 of 2, Navy Hueges 1 of 1. The control that fooled the earlier close-ups: the Succubus that drew was object 590
  (flags 0x101, visible); the others pulled had flags 0 or 0x100.
* **Proof:** `DRAKAN_DBG_PULLFLAGS=1` (new test aid, ORs object flags of the pulled objects) made the Queen (winged, horned, green), Navy Hueges
  and Tick (spider) draw at 260 units in the Succubus Lair. A ring run without it shows none of the four.
* **How the PC shows them:** hidden creatures are linked from `Detect and Execute` objects (classes 61, converted to Trigger Detector) or timers.
  Proven in game by teleporting onto the detector (no pull): Death Mages (detector 2600) and Navy Hueges (3531) become active (lists 21-26)
  and move. The Queen has no links at all (`DRAKAN_DBG_LINKS='^queen'` shows `-> [] <- []`), so nothing could ever show her.
* **Fix (formats 10f4c65, `ootfpipeline.ts`):** a creature object (engine kind `creature`) with the visible bit clear that no object links to
  gets the bit set. Linked ones stay hidden until their trigger fires. The Succubus Lair prints "1 hidden creature(s) nothing links to made
  visible" (the Queen); with the rule and no pull flag the Queen draws at 260 units (`c6_qf`).
* **Not resolved:** teleporting Rynn to (126944, 209400, 152576) (Detect and Execute 1691/1702, which link to Tick 1692/1703) crashes the
  guest at control (`missing-target` JALR 0 at 0x10822c). Bisected with `DRAKAN_DBG_DROP=<class regex>` (new test aid, removes matching
  objects): it still crashes with the Ticks, the Tick way points, all Detect and Execute, all timers, the Evil Ruin / sconce scenery or the
  music / sound objects removed, 4000 units away, and 400 units higher, while five other detector spots (1773, 2600, 3531, 1605, 1713) do
  not. So it is a property of that area (terrain / teleport), not of the Tick class. Tick activation by its detector is therefore still
  unproven; Tick draws when visible.
* Close-ups of the Ebon Dragon, Drone and bats still need a dismounted start (Islands / Mines start mounted).

Harness: parallel runs without the game lock work when every run has its own `--name` (own run dir) and no shared world dump dir.
