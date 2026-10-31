# Level object placement: how it was found (2026-10-31)

Result and layout: `docs/reference/level-format.md`. Tool: `analysis/scripts/lvl_objects.py`. Ground truth: a world dump of
Surdana (`DRAKAN_WORLD=2`: 4602 live entities with class, `resId`, position, yaw).

1. Surdana is not `LEVEL5NE`/`LEVEL0RO`: the disc folder is `LEVEL1NA` (LVL), with shared ODB/MOD/TXD in `LEVEL1ON`.
2. Exact f32 scan of every LVL entry for actor (x, y, z) triples: only type 0x0020 hit (4363 offsets), so it is the object
   stream; 0x0001 is the layer/terrain definition. Positions are the runtime world units as stored (no 1/2048 scaling).
3. The payload starts with a table `{u32 id, u16 class, u16 db}` (count 4675, near the registry high water of 4705). The
   live `resId` equals `(db<<16)|class`. `db` indexes the LVL 0x0000 dependency table (layouts from `ootf-reuse-study.md`).
4. Hex-reading consecutive bodies gave the fixed part (prefix, pos, flags, events, links, signed rotation degrees, scale,
   sentinel). A chain that searched for the next plausible header reached 4671 of 4675 and then failed; the OpenDrakan
   parameter tail (`data_dwords, param_count, data, indices, field_defs`) made the walk deterministic: all 4675 bodies
   consume the payload exactly, and so do the other 12 PAL level LVLs.
5. Differences from OpenDrakan: flag 0x10 adds an f32 between the event count and the link count (this explained unaligned
   floats such as 6144.0); link ids are u32; rotation is signed (0xffa7 = -89); two prefix words per body.
6. Validation (`--world`): 4379 records match a live actor by `resId` and position (1.0). The other 296 are not live in that
   dump or moved (27 within 300 units; Arokh is stored at y 200000 and sits at 202773). Yaw (`ry - 90`) agrees within 6
   degrees for 3890 of the 4379; the class read from the ODB body agrees for all 4379.
7. Runtime class ids live in the ODB 0x0102 body (u32 at +80, or +16 when the u16 at +14 is zero): 601/601 matches. The
   resource-id hi word mapping I first guessed from name coverage (2 System, 3 Resources, 4 FX, 5 NPCs, 6 Magic, 8 level,
   10/11 world commons, 13 Sound, 14 Music) is exactly the LVL 0x0000 dependency table.
8. Corrections: `lvl-odb-dump.md` said placement was unlocated and the float triples at 0x102 body +0x10 were "too small":
   they are class-default transform data, not placements. `actor-spawn.md` "placement comes from the stream": it is LVL
   0x0020. Scratch files lived in `build/lvl-re` (not committed).

## TypeScript reader/writer

`editor/packages/formats/src/lvl.ts` (+ `odb.ts`, re-exported through `lvl.ts`; one export line in `index.ts`). Tests with
synthetic fixtures: `editor/packages/formats/test/lvl.test.ts`.

* Model keeps every byte: prefix words, all 32 flag bits, event count, the zero word before the sentinel and the sentinel,
  the raw data dwords, field_def types and raw (NUL padded) names. Header: raw strings, the u16 after each dependency
  index, and any trailing bytes. `writeObjectList` / `writeLevelHeader` re-serialise byte for byte; `writeLvl` returns the
  archive unchanged when the list is unchanged, otherwise replaces only the 0x0020 entry via `writeSrsc`.
* Edits: `moveObject`, `rotateObject` (integer degrees, i16), `scaleObject` (only when flag 0x100 is set; enabling scale
  or radius on an object is not supported), `deleteObject`, `duplicateObject` (new id = max + 1, appended so ids stay
  ascending; link list copied, so the copy fires the same targets and nothing links to it).
* Not safe yet: `deleteObject` throws if other objects link to the target; pass `'unlink'` to drop the id from their link
  lists or `'ignore'` to leave dangling links (game behaviour unverified; `danglingLinks` lists them). Object ids that
  might sit inside parameter data are not scanned (parameter semantics undecoded). The 65535 object cap (u16 count) is
  enforced. Euler composition order and unknown flag bits are preserved but not interpreted. Editing class defaults
  (ODB data) is out of scope.
* Class names: `resolveClass(header, odbIndex, classId, dbIndex)` = dependency path -> last component -> RSRCIOP mangler
  -> ODB stem -> 0x0102 record (name, group, runtime class), same rules as `lvl_objects.py`.
* Real-data gate (scratch script outside the repo, run on the PAL ISO via `Iso9660`/`SrscReader`): all 15 LVLs under
  ENGLISH (the 14 `LEVEL*` files plus `OUT`; 7 to 6071 objects) give byte-identical 0x0020 and 0x0000 payloads, a
  byte-identical whole LVL through `writeLvl`, and object count, id, class, db index, position, rotation and resolved
  ODB name/db all equal to `lvl_objects.py` output. No duplicate or unordered ids, no dangling links in any level. A move
  edit re-packs and re-reads correctly in every level.
