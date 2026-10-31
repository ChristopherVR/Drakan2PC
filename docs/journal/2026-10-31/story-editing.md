# Story editing (2026-10-31)

Strings, quests and the pack format are written up by the strings / quests part of this task (append those sections here).
Reference for the formats below: [level-format.md](../../reference/level-format.md) section 14.

## Dialogue and cinematics

Goal: make conversations and cut scenes editable. Everything was decoded against the player's PAL disc (`build/scratch/story/*.mts`
are the throwaway probes; nothing of the disc is stored in the repo, tests use synthetic fixtures).

### What a cut scene and a conversation are

* A cut scene is a **Stomp Player** object (class 0x10033) whose `Sequence List` names sequences of the level's `.SSD`. A sequence
  is a list of actors (camera, Rynn, NPCs, props, effects) with timed actions. 84 of 84 placed players resolve.
* A conversation is a **Conversation Handler** (class 0x100b4) with five phase arrays of **dialog line** classes (0x100ba) of the
  level ODB. A dialog line is the speaker's class: sound (`Sound`), animation, camera position. Its subtitle is stored in the
  sound's SDB descriptor, so there is no separate dialogue table (the earlier subtitles research already said so).
* The first guess in the old notes ("no sequence reference on the disc") was wrong: sequence refs live in the object parameters
  of the players, not in class defaults.

### How it was decoded (short)

1. Class field names of 0x10033 / 0x100b4 / 0x100ba came from the ODB field lists (they are descriptive).
2. SSD: the record header (`length`, 0, `slots`, `actors`) and the actor header (`name, slot, ref, mode, actionCount`) fell out of
   the small records (`New Group`, `Preload Blacksmith`, `bellows`). The next actor is found by scanning for a plausible actor
   header, then the action grammar was solved from actors with one action (their byte length is the action size): types 0, 1, 3, 5,
   7 first, then 4, 6, 9 from actors with two actions, then 8 and 10 by inspecting a camera actor of LEVEL6EN
   (type 10 is just a time and is followed by an ordinary keyframe at the same time). Result: all 1264 actors of the 202 sequences
   of the 14 English `.SSD` files are consumed exactly (10041 actions).
3. Type 3 payload checked against the level SDB: `1.01 Jade` plays `101JA04-A`, `101JA04-B`, ... with the subtitles of the cut
   scene text. That fixed the sound reference as `(db << 16) | id` with db 0 = the level SDB.
4. SDB 0x0302 write-back: the 12 byte tail and the "empty text still has the flag" case were found by round trip failures
   (3398 of 3398 descriptors now rewrite byte for byte).

### Proven and inferred (short; full tables in level-format.md)

Proven: sequence record and actor layout, action sizes and the time field, transform keyframe layout (camera path), sound action
ref, SDB subtitle layout and padding, `Sequence List` = SSD id, handler phase arrays hold dialog line class ids, dialog line
`Sound` / `Animation` ids.
Inferred: names of action types 1, 4, 5, 6, 7, 8, 9, 10, `length` units, `mode` / `ref` of actors, keyframe interpolation, message
number meaning (`0x1000 + enum`), which handler phase plays when, `Sound Play Mode`, camera enums.
Not decoded: types 4, 6, 8, message targets, handler item / quest logic beyond the field names.

### Editors

* `formats/src/story/`: `ssd.ts` (parse / write sequences, actor scan, keyframe scan), `ssdactions.ts` (typed actions, edit, insert in
  time order, remove), `sdbtext.ts` (subtitle read / write, timed pages), `conversation.ts` (dialog line and handler decode, sound edit),
  `cutscene.ts` (player params, camera path, sampling, sound and message cues), `load.ts` (reads one level folder through a
  `StoryFs`). Tests: `formats/test/story.test.ts`.
* `app/src/story/content.ts`: session with per level edits and the export (`StoryEntryChange[]`, `buildStoryPackFiles`, size rule
  of `checkEntrySize`); `dialogue.ts`: lines and conversations (speaker, sound id, subtitle text with page timing, play, used by);
  `cinematics.ts`: sequence list by folder, timeline, camera path (top view), scrubber and real time playback with subtitle timing and
  recordings (host `play`), action tables with editable time, sound / message / animation / position and add / delete;
  `contentregister.ts` (`initContentEditors`) registers both panels; `storyfs.ts` adapts an `Iso9660`.
* Screenshots: `build/scratch/story/dialogue-lines.png`, `dialogue-edit.png`, `dialogue-handlers.png`, `cinematics-jade.png`
  (harness in `build/scratch/story/harness`, real disc over HTTP range requests).

### Not done

* Not proven in game: that an edited subtitle, sound ref or sequence appears in a played conversation / cut scene (the packs are
  authored and the entries decode, but the runtime path for SDB / SSD overrides was not exercised).
* Camera path is shown top down in the panel; the `showPath` host hook is there for the 3D view but not wired.
* Placed handler instances (the object parameters) are read through `host.placed`; the panel has no editor for the phase arrays
  themselves, only for what each line says. No creation of new sequences or lines.
* Types 4, 6 and 8 are shown as raw numbers; keyframe quaternion editing is not offered (positions only).

## Strings and quests (editor panels, mod pack, in-game proof)

**Mod pack coverage (goal 1).** The resource override layer sees every 0x1202 request, so `overrides/<FILE>/<type>_<id>_<sub>.bin`
already replaced single entries of ODB, XDB, SDB and SSD files; `new/` adds entries to them. No C++ change was needed; `mod_test`
gained a "content files" block (124 checks) proving per-request replacement with a different size, the `x` sub, untouched
neighbours (a `0304` audio entry beside an overridden `0302`), `new/` on four containers, replacing the `0103` field list, and
that a pack which only adds also works. modding.md has the table of keys.

**Editor.** formats: `xdbwrite.ts` (cp1252-correct entry encoder, `diffXdbStrings`, `nextStringId`, `classStringRefs`),
`odbwrite.ts` (record split/join with the disc's name-area rule, `patchClassData`, `encodeFieldList`, `appendFieldList`),
`questwrite.ts` (`QuestDraft`, `editQuestRecord`, `freeClassId`, `questPlacements`). editor-core `story.ts`
(`buildStoryPackFiles`, `stringChanges`, `questChanges`). App `app/src/story/`: `session.ts` (state, undo-free edits, export),
`strings.ts` (all XDBs of the disc: search, language slots, new string with fresh id, reference count + class list),
`quests.ts` (quests by chapter, kind / optional / journal text / map text / draggable marker on a 512x512 plot of the chapter's
markers, new quest from a template, "Scan levels" = where each quest is placed and which objects start / complete it),
`packpanel.ts` (changed-entry list, export zip / write to mods folder), `register.ts` (three dock panels through
`registerPanel`; `main.ts` calls `initStoryEditors` and `setDisc`). Worker RPC: `loadStoryEdit`, `scanQuestUsage`.
Tests: `formats/test/storywrite.test.ts`, `editor-core/test/story-pack.test.ts` (synthetic only).

**Proof in game** (private play folder `build/scratch/story/play`, `DRAKAN_MODS=build/scratch/story/mods`, save card copy of
`build/harness/journal2`, `DRAKAN_INVENTORY_SCREEN=native`, journal page; shots in `build/scratch/story/shots/`):
pack `story.proof` built by the same functions the panels call: string 22 rewritten, new string 114, new class 4000 (copy of the
Inquisitor class 4169 with journal id 114, field list appended to 0x0103); a save copy whose quest row 5 points at class 4000
(`scripts/savetool` helpers, class word +0x18). The baseline journal shows the stock sentences; the modded one shows
"EDITED BY STORY EDITOR: find Lady Myschala at the palace." and "A BRAND NEW QUEST: carry the lantern to the eastern gate."
the second under Optional Quests, where the template class puts it (`SubQuest?` is 1 there). Log: `mods: loaded 'story.proof'` and
`SYSTEM.ODB / SYSTEM.XDB rebuilt with 1 entry patch(es)`.

**Not proven.** A new quest started by a level object (the save row was patched; placing the quest class in a level and linking a
trigger was not run in game); the map marker position of an edited class on the map page; the editor panels were type-checked and
the logic unit-tested, but not screenshotted in a browser (the shared Playwright browser was in use by another session).
Messages 1 / 18 as start / complete are inferred. Strings of level objects (level-local ids) are not in the reference count.
