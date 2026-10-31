# OotF STOMP sequences -> PS2 SSD (2026-10-31)

Code: `editor/packages/formats/src/ootfseq.ts`, test `formats/test/ootfseq.test.ts` (synthetic). Probed on all 16 OotF `.ssd`
(110 sequences, 700 actors, 7727 actions: every record parses, every converted record re-parses with `story/ssd.ts` +
`ssdactions.ts`) and the 8 PAL `.SSD` (202 sequences). Layout of the PC side: OpenDrakan `doc/riot_database_format.txt`, `Sequence.cpp`.

## Verified (PC data vs PS2 data)

* **Header words are identical**: PC `flags, modifyRunState, unk2, actorCount` = PS2 `length, unknown1, slots, actors`. Flag values
  60 and 126 occur on both (PS2 "length 60 = empty default" is the PC flags field, 0x3c). `unk2`/`slots` = highest actor id + 1.
* **Actor ids = PS2 slots** (kept as is, so `attach` targets and transform `relativeActor` need no remap). PS2 `ref` is the **level object
  id** (55/55 in LEVEL5NE, 222/302 for the shared LEVEL1ON.SSD against LEVEL1NA's list; the rest belong to sibling levels).
  PC actor = `name, id, type(1), assignedAs, objectId, n`; the PS2 record has no `type`/`assignedAs` word.
* PS2 `mode`: 0 on 846 object actors, 7 on 184 NPC/prop actors, 1 on Rynn/Arokh (137), 2 and 5 rare. What 0 vs 7 changes is **not
  known**; the converter writes 0 (1 for Rynn / Arokh).
* Action payloads: transform = PC 36 bytes verbatim + scale 1,1,1 (PS2 54); options 0x10/0x20/0x30 = interpolation, same bits;
  animation = the same six words (channel, ref, begin, transition 0.85 typical, speed, flags 0x15/0x3f/0x2a); attach (4), show/hide (6),
  message (7) = same 8/4/8 bytes. Sound: PC `ref, f32 duration, u16 volume, u16 flags` -> PS2 `ref, u32 (volume | flags << 16)`
  (flags 0x08 / 0x18 / 0x38 appear on both; duration dropped). Resource ref = `(db << 16) | id`, db relative to the `.DB` beside the SSD.
* Music (PC 8, 16 bytes `musicId, dls, fadeIn, fadeOut`) = PS2 type 8 (28 bytes): `ref (u16 id, u16 db), 0, f32, f32, 0, 0, 1`.
* **Messages**: the numbers are engine ids shared by both platforms: 4097 On, 4098 Off, 4257 Play Sequence, 4258, 4259 Blow Up, 4162, 0
  appear in both datasets (PS2 additionally 4096, 4133, 4139, 4263 x74, 4264, 4308, 1259). OpenDrakan names 4103 Triggered, 4353 Lock,
  4354 Unlock (PC only: 4093, 4260, 4261, 4353; not seen on PS2, passed through, counted in `report.unverifiedMessages`). The sequence
  message goes to the **actor's own object** (OpenDrakan `receiveMessage(actor, msg)`), not along its links. Level object fields
  (Trigger Message, Message To Send) store *ordinals* (0 Off, 1 On, 2 Lock, 3 Unlock, 4 Play Sequence, 5 Blow Up, 6 Triggered) on both
  platforms; only the sequence action uses 0x1000 + n numbers. PC also has small numbers (25 x37, 52, 15, 9, 13, 253, 667-669, 2469) used as
  numeric payloads for faders; PS2 has 0 only (and 1259), so these are passed through (`smallMessages: 'drop'` removes them).
* Camera: PS2 camera actors (74/103) carry a type 9 `1.0f, FOV` (40, 50, 55, 60, 80, 105) after the first type 5; PC has no FOV, so
  `cameraFov` (default 40) is inserted for the actor named `Camera`.

## Guessed

* Quaternion/position are copied verbatim (positions match object positions in the same units on PC; the rotation convention across
  platforms could not be checked: no PS2/PC pair of the same prop).
* Type 5 (run/stop AI): PC goes 0 at start, 1 at end; every PS2 one is 0, so values are zeroed (`keepAiResume` keeps PC's).
* Music stop (PC id 0) -> `a=0, b=1`; `fadeIn`/`fadeOut` = c/d; e, f, g (0, 0, 1) copied from disc patterns.
* Attach `a` = actor id, `b` = channel (PS2 values in LEVEL5NE are actor ids, fits).
* Actors whose object has no PS2 counterpart are dropped (and `Rynn`/`Arokh`/`Camera` fall back to `playerObjectId` / `dragonObjectId` /
  `cameraObjectId`); type 10 hold is never written.

## API

```ts
convertPcSequences(pc: SrscReader, { maps: {objectIdMap, animMap?, soundMap?, musicMap?}, usedIds: Set<number>, pcDbIndex?, only?, folderId?,
  folderName?, ps2Db?, playerObjectId?, dragonObjectId?, cameraObjectId?, cameraFov?, keepAiResume?, smallMessages?, actorMode? })
  -> { entries: {type,id,sub,data}[], idMap: Map<(pcDb<<16)|pcId, (ps2Db<<16)|newId>, report }
```
Maps are `{get(pcRef)}` (a `Map` fits): PC ref words in, final PS2 ref words out (already in the target SSD's `.DB` frame). Other exports:
`parsePcSequence`, `convertSequence`, `convertAction`, `allocateId`/`usedIdsOf`, `remapSequenceRefs`, `ssdEntryPath`, `ssdPackFiles`, `KNOWN_PS2_MESSAGES`.

## Shipping

* A sequence ref in an object parameter is `(db << 16) | id`, db 0 = the SSD with the **stem of the class's owner ODB**. LEVEL1NA's
  Stomp Player class lives in `Level 1 - Common` = `LEVEL1ON`, so sequences go to **`LEVEL1ON.SSD`** (shared by LEVEL1NA/1BS/1ED/1IR/1OR),
  not to a per-level file. Ship as `new/LEVEL1ON.SSD/0311_<id>_<folder>.bin` plus `new/LEVEL1ON.SSD/0312_<folder>_0.bin` (`ssdPackFiles`).
  The level's LVL dependency table and `.DB` need **no change** (the stem is already a dependency); sound/anim refs inside must be valid in
  that stem's `.DB` list (LEVEL1ON.DB: 2 FX, 3 System, 4 Resources, 5 NPCs, 6 Magic, 7 World Common, 8 PS2 Music, 9 Mountain Common, 10 Sound).
* Ids: `usedIdsOf(templateSsd)` seeds `usedIds`; holes below the current maximum are taken first (new ids above a file's maximum did not work
  for ODB classes; unverified for SSD). Share one `usedIds` across levels of a pack. The 0x0310 header (two u32, apparently id counters) is not touched.
* Object parameters: write the Stomp Player's `Sequence List` (type 0x100b, array) with `remapSequenceRefs(pcValues, idMap)` -> words
  `(0 << 16) | newId`, via `setObjectParams`; a single sequence field (0x0b) likewise. convert.ts must stop zeroing FIELD_SEQUENCE and call this
  (not done here: file off limits). `Initial State` and `Message To Play Next` (ordinal 4) copy by field name.

## Open

* Not tested in game (the SSD `new/` read path was not confirmed for SSD even for the disc's own entries, see level-format.md section 14).
* Needs objectIdMap and animation/sound/music maps from the other converters; unmapped ones drop the actor/action.
* Meaning of PS2 actor `mode`, type 4/6/8 tails, and whether PC messages 25/52/... do anything on PS2 are unknown.
