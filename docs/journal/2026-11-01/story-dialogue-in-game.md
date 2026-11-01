# Story dialogue proven in game (2026-11-01)

Result: a real Surdana conversation is started with input, and an edited subtitle, a swapped line sound and a brand new
dialogue line created through the editor's own code are shown / played in it. Harness: `scripts/harness/story_dialogue.py`
(pack builder `scripts/harness/story_dialogue_pack.mts`, 18 asserted checks, all pass). Details and decoded gates:
`docs/reference/level-format.md` section 14 "Starting a conversation".

Why earlier attempts failed (three independent causes):
1. The E tap was held 20-30 frames. The Use handler (`FUN_003168a0`) only acts on a release inside 0.5 s of game time.
2. The player stood 1200-2500 units from the NPC / not in front of it. The NPC's talk gate needs distance <= 2048 and mutual facing.
3. Messages were sent to the handler by a volume: the handler's start path expects the NPC (`0x116d` is forwarded by the NPC).

Also found: the full ordinal -> engine message table (`FUN_002cd648`); `Start Conversation` (28) = `0x116d`, `Quest Complete` (18) = `0x116e`.

Tools added: `DRAKAN_TRACE_FN=4149a0` now prints the subtitle text (`[subtitle-set]`), `46b368` dumps the talk gate's NPC
pose (both in `drakan_overrides.cpp` trace hook). Editor writers needed no fix: pack content decoded and applied as authored.

Not proven: audible playback (no audio capture; evidence is the sound request `0304/<id>` plus the sound's subtitle),
the `Start Conversation` template volume (the handler needs the NPC as sender), other languages.
