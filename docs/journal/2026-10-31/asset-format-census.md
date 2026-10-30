# Asset format census (groundwork for a level editor and a reimplemented engine)

Date: 2026-10-31. Scope: every game data format the project already understands, with the level-data
formats (geometry, placement, triggers/scripts, terrain, collision) flagged. Read-only survey of
`analysis/scripts`, `docs/reference`, `docs/journal`, `runtime/{hle,ui,spu2,gs}` and the local sample
trees under `iso/sample/`. Counts come from the SRSC index of the sample files (structure only, no payload
content is recorded here). No code or data from the game is copied into this note.

Legend: **Parsed** = decoded to fields, with a script or runtime reader. **Partial** = some fields or
types decoded. **Opaque** = type ids and sizes only. **Write** = a writer or editor exists in the repo.

## 1. Container: SRSC (applies to almost everything)

`SRSC` magic, u16 version 0x0101, u32 index offset, u32 count. The index is at the end of the file, with
count x 14-byte entries `{u16 type, u16 id, u16 sub, u32 offset, u32 size}`. Lookup key is `(type, id, sub)`.
Parsed in full: `analysis/scripts/iop_srsc.py`, `runtime/hle/include/hle/Srsc.h`. Resource type 0x0402 is
present in every file (per-file header). **No SRSC writer exists** (none in scripts or runtime). The format
is simple enough that a writer is a small job. Note that the disc bundles most data by file, so a writer
must keep ids stable, because the game looks resources up by key.

## 2. Format table

| Format | Ext | What it holds | Understanding | Covered by | Missing for editing |
|---|---|---|---|---|---|
| SRSC container | all below | Index plus payloads | Parsed | `iop_srsc.py`, `hle/Srsc.h`, `iop-and-io.md` 5.4 | Writer; most type ids are unnamed |
| LEVELS.RRC | `.RRC` | Seven IOP IRX modules (Sony libs + RSRCIOP). **Not level data** | Parsed (decrypt + inflate + split) | `iop_rrc.py`, `iop_bundle.py`, `iop-and-io.md` 2 | Nothing for the editor |
| DRAKAN2.RRC | `.RRC` | SRSC with zlib payloads; UI layouts (type 0x600) | Partial | `ui_layout_dump.py`, `settings-menu.md` | Layout writer; zlib payload types |
| Textures | `.TXD` (type 0x40) | 8/16/24/32 bpp, 0x40-byte header, palette for 8 bpp, linear pixels | Parsed (decode, export) | `txd_decode.py`, `hd_txd_census.py`, `ui/Txd.h`, `hd-textures.md` | Resize or new textures; 16 bpp and palette kind 2 not in `TxdEdit` |
| Object database | `.ODB` | Class definitions, named instances (entities, triggers, players) and a zlib schema blob | Partial (see 3) | `ui_layout_dump.py` (reads one record), `fmv.md` 4, `settings-menu.md` | Instance field layout; parser; writer |
| Level data | `.LVL` | Level body and tables (see 4) | Opaque (sizes and ids only) | `iop-and-io.md` 5.5 (vague) | Everything about the record layouts |
| Models | `.MOD` (types 0x0200-0x0207) | Mesh and skeleton data (per doc). Sample: 15 entries in 8 KB | Opaque | `iop-and-io.md` 5.5 | Vertex/index layout, skeleton, parser, writer |
| Animation | `.ADB` (0x0500-0x0505) | Animation and sequence database | Opaque | `iop-and-io.md` 5.5 | Everything |
| Level sound scripts | `.SSD` (0x0310-0x0312) | Per-level sound cue lists (doc, [?]) | Opaque | `sound-debug.md` | Record layout |
| Sound bank | `.SDB` (0x0300-0x0307) | Descriptors (0x302), SPU ADPCM (0x304), stream header (0x305) | Partial (descriptor fields and subtitles decoded) | `sub_sdb_text.py`, `sub_export.py`, `spu2/Adpcm.h`, `adpcm_ref.py`, `subtitles.md` | ADPCM encoder; SDB writer; semantics of 0x300/0x301/0x307 |
| Text | `.XDB` (0x330, 0x400, 0x500) | Localised string sets, 8 language slots | Parsed | `xdb_dump.py`, `ui_xdb_scan.py` | Round-trip not tested here (`encode_strings` exists); SRSC writer |
| Video | `.VDB` (0x340-0x343) | IPU (MPEG-style intra) frames, offsets table | Parsed (read, extract, validate) | `vdb_common.py`, `vdb_list.py`, `vdb_extract.py`, `vdb_ipuparse.py`, `fmv.md` | IPU encoder (not needed unless movies change) |
| Dependency list | `.DB` | Text: version, dependency count, relative paths | Parsed | `iop-and-io.md` 5.5 | Trivial to write, not done |
| Name mangling | n/a | Long name to 8.3 mapping | Parsed, validated 111/111 | `iop_mangle.py`, `iop-and-io.md` 5.3 | Not a file format |
| Creature blobs | `.HSD` | 11712-byte high-entropy blobs | Opaque | `iop-and-io.md` 5.5 | Purpose ([?]) |
| Level list | `LEVELLST.TXT` | Level paths (14 entries) | Parsed | `iop-and-io.md` 5.3 | Trivial |
| Saves | `.rsg`, `.rlt` (memory card) | Save game, level state | Names and paths only | `save-slots.md`, `iop-and-io.md` 5.6 | Binary layout not surveyed |
| Subtitle text | in `.SDB` 0x302 | Dialogue lines with page-break markers | Parsed | `sub_export.py`, `subtitles.md` | Write-back |

## 3. ODB (entity and object definitions): the most important gap for placement and triggers

Per-file census (sample `LEVEL0RO.ODB`, 5080 bytes, 28 entries; `INTERFCE`, `DEBUG`, `SOUND` also checked):

- `0x0100`: one header record (per level).
- `0x0101`: named class/schema records (5 in LEVEL0RO, 13 across the samples). Names are class names such as
  "Movie", "Movie Player", "Timer", "Fader" (`fmv.md` section 4). The class names are the entity vocabulary.
- `0x0102`: named instance records (20 in LEVEL0RO). Each has a length-prefixed name, a `sub` that appears to
  reference a `0x101` class ([?]), and a body of u32 fields. The body is the likely home of placement, links
  and trigger parameters. **Not decoded.**
- `0x0103`: one zlib-compressed blob per ODB (4 of 4 samples decompress; LEVEL0RO inflates to 6000 bytes,
  INTERFCE to 9640). Its contents are the schema or property table, a different thing from the instances
  ([?]). The engine's "User Interface Properties" schema sits in the ELF at 0x505388 (`settings-menu.md`).

Parsed by nothing in the repo except `ui_layout_dump.py`, which reads one `0x102` record of `INTERFCE.ODB`.
The `0x101`/`0x102` record structure is consistent, but the field layout is not written down. The next
step is to dump `0x102` bodies of `LEVEL0RO.ODB` against the object properties the game reads.

## 4. Level data (LVL): geometry, placement, triggers, terrain

Sample `LEVEL0RO.LVL` (142874 bytes, 7 resources):

| Type | Count | Bytes | Note |
|---|---|---|---|
| 0x0000 | 1 | 512 | Begins with a path-like string; likely a header or path table |
| 0x0001 | 1 | 109946 | Largest block. Likely level body (geometry or tables). Not decoded |
| 0x0002 | 1 | 116 | Short text block |
| 0x0003 | 1 | 4 | Single word |
| 0x0020 | 1 | 25608 | Regular table of small u32 pairs; likely spatial or sector data ([?]) |
| 0x0025 | 1 | 6548 | Mixed text and binary |
| 0x0402 | 1 | 4 | Per-file header |

**Contradiction to fix:** `iop-and-io.md` 5.4 says `0x03xx` types are level data (LVL/SSD). In the sample,
LVL uses types `0x00xx`, and only SSD uses `0x031x`. Correct that line when the LVL types are named.

Status by topic:

- **Level geometry**: no decoder. The runtime sees level geometry only as GS/VU draw batches (`graphics.md`
  section 7; landscape routines `0x158148` / `0x161a78`). Terrain in the ray tracing work is "resident faceted
  terrain" built from the runtime, not from files (`journal/2026-10-23/terrain-ao-creases.md`). Candidate
  source: `LVL` type 0x0001, or `MOD` for meshes. Both are unproven.
- **Entity and object placement**: `ODB` 0x102 bodies, probably with positions in the `LVL` tables. Not decoded.
  The runtime finds entities in live EE memory (`drakan_scene_models.h`, class `0x100ac` shop in `env-flags.md`).
- **Triggers and scripting**: no bytecode or trigger table identified. Trigger and script behaviour is in EE
  code (`combat-controls.md`, `input.md`). Nothing on disk is mapped to it yet.
- **Collision**: camera collision and hit joints are runtime (`input.md` line 196; `combat-controls.md`
  collision joints). No file-level collision data identified. The `.HSD` blobs are a possible candidate ([?]).
- **Spawns and sectors**: the doc's "spawn/sector tables" (`iop-and-io.md` 5.5) are unverified. The `0x0020`
  table is the candidate.

## 5. Gaps list (honest)

1. No parser or writer for `LVL`, `MOD`, `ADB`, `SSD`. Their record layouts are not documented beyond type ids.
2. `ODB` instance (`0x102`) layout is not decoded. This blocks placement and trigger editing.
3. No SRSC writer. Round-trip writing has not been tested for any format.
4. Level geometry and terrain have no file-level decoder. The only route so far is runtime capture.
5. The meaning of most resource type ids (`0x0000`-`0x0025` in LVL, `0x0200`-`0x0207` in MOD, `0x0300`-`0x0307`
   in SDB) is unnamed.
6. The `LVL` type-class claim in `iop-and-io.md` is wrong (see section 4).
7. The `ODB` `0x0103` blob is unidentified. Its inflated content is the likely property schema.
8. SDB: no ADPCM encoder, no writer, and loop-point rules are only partly known.
9. Save file layout (`.rsg`, `.rlt`) is not surveyed.
10. Other level sets (`WORLDCON`, `MOUNTAON`, `DESERTON`, `PROCTEES`) are not in the local sample. Only LEVEL0RO is sampled.
11. The local sample is small (64 files). Full-disc counts are from `iop-and-io.md` 5.2 and were not recomputed.

## 6. Suggested next steps

1. Locate the LVL consumer in the ELF. Find the code that reads resource type 0x0001 and 0x0020, and
   name the record layouts from it.
2. Dump `ODB` `0x102` bodies for LEVEL0RO against the property table to fix the instance layout.
3. Write an SRSC writer in Python (`analysis/scripts`) and round-trip every sample file byte for byte.
4. Add a MOD decoder for types 0x0200-0x0204 on `DEBUG.MOD` (small and self-contained).
