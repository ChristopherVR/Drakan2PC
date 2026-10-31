# Level data: object placement (LVL 0x0000 / 0x0020, ODB 0x0102) and the `lvl-objects` JSON

Tool: `analysis/scripts/lvl_objects.py` (tests `scripts/tests/test_lvl_objects.py`). Output: `build/lvl-objects/<LEVEL>.json`
(gitignored, never commit). Evidence tags: **[RAM]** checked against a live world dump (Surdana, `drakan::world`),
**[disc]** all 13 PAL level LVLs decode with the bodies consuming the payload exactly, **[D]** OpenDrakan doc only,
**[inf]** inference, **[?]** open. No game bytes or disassembly are reproduced here.

Credit: the object skeleton (id/class/db table, rotation, scale, links, parameter pairs, `field_def` list, the dependency
table, the class field model) is derived from OpenDrakan (Zalasus, GPL-3.0, https://github.com/Zalasus/opendrakan, commit
6c0d499): `doc/lvl_format.txt` and `doc/riot_database_format.txt`. No OpenDrakan code is copied. PS2 differences were found
against the PAL disc (section 2).

## 1. Which file holds what

Surdana is `Level 1A - Surdana` = disc folder `LEVEL1NA` (`LEVEL1NA.LVL`, SRSC index with u32 count). The shared level data
lives in `LEVEL1ON/` (ODB, MOD, TXD, SSD); other levels follow `LEVEL<n><XX>` the same way. The live resource id of an actor
(`resId` in the world dump) is `(db_index << 16) | class_id`, exactly the pair stored in the 0x0020 table. **[RAM]**

| Entry | Content | Tag |
| --- | --- | --- |
| LVL 0x0000 | `string level_name; u32 max_w, max_h; u32 n; n x {u16 db_index, u16 0, string path}`: the dependency table. `db_index` selects an ODB by path (`..\..\Common\NPC\NPCs` -> `NPCS.ODB`; use the disc 8.3 mangler `iop_mangle`). Surdana: 2 System, 3 Resources, 4 FX, 5 NPCs, 6 Magic, 8 level common, 9 Proc Textures, 10 Mountain World Common, 11 World Common, 13 Sound, 14 Music, 18 Interface, 20 Debug | [disc] |
| LVL 0x0020 | The placed objects (section 2). Surdana: 4675 | [RAM][disc] |
| ODB 0x0102 | **Class record** (not an instance): SRSC `id` = `class_id`, `sub` = group id (0x0101 = group name, e.g. "Level Properties"). Body: name, group, model ref, default field data (not decoded) | [D], names [disc] |
| ODB 0x0103 | Shared `field_def` pool (`u32 type, name`); type table in section 4 | [D][disc] |
| LVL 0x0001 / 0x0002 | Layer (terrain tile) definitions and layer groups; PS2 terrain data is inline and packed (not zlib). Not decoded here | `ootf-reuse-study.md` |

A string is `u16 len` (counts NUL padding) followed by `len` bytes.

## 2. LVL 0x0020 object list

```
u16 count
count x { u32 id; u16 class_id; u16 db_index }          // ids ascend; table order = body order
count x body:
  u32 lighting_layer                                    // 0 in nearly all records [inf]
  u32 unk                                               // 0 seen [?]
  f32 x, y, z                                           // world units, y up (section 3)
  u32 flags                                             // 0x01 visible, 0x100 scaled, 0x10 radius word present; others [?]
  u16 initial_event_count                               // 1 or 0 seen; the events are not stored here [?]
  [f32 radius if flags & 0x10]                          // e.g. 6144.0, 8000.0, 100000.0 (trigger/detect range) [inf]
  u16 link_count; u32 link_id[link_count]               // ids of other objects in this list (trigger targets)
  i16 rot_x, rot_y, rot_z                               // degrees, signed
  [f32 sx, sy, sz if flags & 0x100]
  u32 0; u32 0xFFFFFFFF                                 // constants; the first word is probably an owner/parent id [?]
  u32 data_dwords; u32 param_count
  u32 data[data_dwords]                                 // value dwords; strings/arrays live in the trailing part
  u16 field_index[param_count]                          // index of the field in the object's class
  param_count x { u32 type; string name }               // field_def, same format as the ODB pool
```

Walking it consumes the payload exactly in all 13 PAL levels (368 to 6071 objects). **[disc]** The records have no separator:
the 8 bytes before a position are that body's two prefix words, the previous body ends right before them.

PS2 differences from the OpenDrakan layout **[disc]**: link ids are u32; two prefix words (OpenDrakan lists only the
lighting layer); flag 0x10 adds an f32 *between* the event count and the link count; a constant `0xffffffff` follows the
rotation/scale; rotations are signed.

### Parameters

`param_count` fields differ from the class default. Parameter `i` uses `data[i]` and `field_def[i]`. By type (low byte):
strings (`0x0a`) and arrays (second byte 0x10) hold `u16 offset (dwords into data), u16 length (dwords)`, strings are
NUL-padded to a dword; int (1) is signed, float (2) an f32, colour (0x0f) ARGB, the rest (enum 7, class 3, sound 5, layer 0x0c,
sequence 0x0b ...) are ids as stored. The script emits `{field, name, type, type_name, value}` per parameter. **[disc]** the
stream stays in sync and names such as "Trigger Message", "Initial State", "Number (max 340)" come out; the value semantics
per class are not verified. Class **default** values (ODB 0x0102 data) are not decoded, so the JSON lists overrides only.

## 3. Coordinates and rotation

* Positions are plain f32 **world units equal to the runtime actor coordinates** (`actor+0x14`): exact match (1.0 tolerance)
  for 4379 of 4602 live Surdana actors, no 1/2048 scale (2048 units = one layer cell, so cells = value / 2048). **[RAM]**
  Axis order x, y (up), z.
* Stored y is not always the final y: Arokh is stored at 200000.0 (the layer world height) and rests at 202773 at runtime;
  some props settle onto the ground. Only 27 of the 296 unmatched records are near a live actor of the same resource
  (<300 units), the rest are not live in that dump. **[RAM]**
* Rotation: three signed degrees (`rot_deg`). With rx = rz = 0 the runtime yaw (`atan2(m[0][2], m[0][0])`) equals
  `rot_y - 90`. 3890 of the 4379 matched actors agree within 6 degrees; 326 of the remaining 489 have rx = rz = 0 as well,
  so their rotation changed after load (AI turning, physics). **[RAM]** The composition order of the three angles for tilted
  objects is **[?]** (the dump only exposes yaw); an editor should keep all three values.

## 4. Field types (low byte)

1 int, 2 float, 3 class, 4 model, 5 sound, 6 boolean, 7 enum, 8 character channel, 9 animation, 0x0a string, 0x0b sequence,
0x0c layer, 0x0d unknown, 0x0e texture, 0x0f colour. Second byte 0x10 = array; 0x20/0x30/0x40/0x50 only for class. **[D]**
Seen on the PS2 pool: 0x1, 0x2, 0x3, 0x5, 0x7, 0xb, 0xf, 0x2003, 0x3003, 0x4003 **[disc]**.

## 5. Resolving a class

`(class_id, db_index)` -> dependency path -> ODB stem -> ODB 0x0102 with SRSC `id == class_id` gives `name` and `group`
(`sub`). The engine's numeric **runtime class** (`0x100d3`, `0x1001c`, ... the id of the actor handler table) is stored in
that record's body: the u32 at body +80 when the u16 at +14 is non-zero (records with a transform/model), else the u32 at
+16 (offsets from the byte after the name field). 4379/4379 matched Surdana actors agree with the live class, and 601/601
resources. **[RAM]** The rule is empirical; the rest of the body layout is undecoded.

## 6. JSON emitted by `lvl_objects.py`

```
{ "format": 1, "level": "LEVEL1NA", "level_name": "Surdana",
  "dependencies": {"2": "..\\..\\Common\\System\\System", ...}, "count": 4675,
  "objects": [ {
    "index": 0, "id": 2,                       // table order; id is the link-target id
    "class_id": 2656, "db_index": 2, "res_id": "0x20a60",   // res_id = (db_index<<16)|class_id = world dump resId
    "db": "SYSTEM", "name": "Dragon", "group": 2462, "runtime_class": "0x1001c",   // when the ODBs are available
    "pos": [x, y, z], "rot_deg": [rx, ry, rz], "yaw_deg": 150,   // yaw_deg = ry-90 wrapped to [-180,180)
    "scale": [sx, sy, sz] | null, "radius": f32 | null, "flags": 1, "lighting_layer": 0,
    "links": [u32 ...],
    "params": [ {"field": 145, "name": "Initial Mode", "type": 7, "type_name": "enum", "value": 1} ] } ] }
```

Write-back is not implemented. The body grammar above has no unexplained gaps except the meaning of `unk`, the zero word
and some flag bits, so a writer can re-emit bodies byte for byte and rebuild the 8-byte table; move/rotate/scale edits
change only `pos`, `rot_deg`, `scale`. The JSON drops `unk`, `pre_sentinel` and the raw data area; a writer must take those
from the original LVL (the parser keeps them internally).

## 7. Where the engine uses it

`Level_Load (0x1c94c8)` -> `0x18e448` instantiates the whole list through `0x181aa8` -> `0x181c50` (5039 spawns in Surdana:
the 4675 list objects plus runtime children such as equipped items, which share their owner's position). Class comes from
the resource record built from the ODBs, the transform from this list. **[run]** per `actor-spawn.md`; not re-traced here.

## 8. Open

* Class default values (ODB 0x0102 data area) and the Euler composition order.
* Meaning of `unk`, the zero word before the sentinel, flag bits other than 0x01/0x10/0x100, `initial_event_count`.
* Layer terrain (LVL 0x0001) is opaque; needed to snap an editor's y to the ground.

## 9. [OotF] Cross-check against the PC Order of the Flame files

Tag **[OotF]** = read from the user's own OotF PC install (retail data, 23 `.lvl` files, 16312 placed objects, 4445 terrain
layers), session 2026-10-31; details in `docs/journal/2026-10-31/ootf-data-study.md`. No PC bytes are reproduced here.

* **Container [OotF]**: PC `SRSC` header is 12 bytes (`u16 version 0x0100`, `u32 index offset`, `u16 count`); the 14-byte index
  entries are identical to PS2. PC ODB version record is 0x102 (PS2 0x109); library record names `Dragon.rfl` (PS2 `Drakan 2.rfl`).
* **LVL 0x0000 and the 0x0001 definitions [OotF]**: identical to the PS2 layout (the PS2 parsers read PC files unchanged).
  PC `level_name` may start with a localised-string reference `<0xNNNN>` (id into `Dragon.rrc`) followed by fallback text.
* **Terrain data (LVL 0x0001) [OotF]**: PC stores, after the definitions, `u32 1` and one `{u32 compressed_size, zlib}` block per
  layer. The inflated size is exactly `(w+1)*(h+1)*4 + w*h*26` (vertices, then faces; `w`,`h` are cells) for **4445/4445**
  layers, with no trailing bytes in any file. PS2 keeps variable-length packed data inline after each definition, beginning
  with `{u16 sequence 1..n, u16 0x000c, u32 0x40 or 0x80}`; the second u16 is the constant 0x0c (not a face count, as an
  earlier note assumed). PS2 packing is still undecoded.
* **LVL 0x0020 object list [OotF]**: PC bodies are inline (no id/class table): `u32 id, u16 class, u16 db, u32 lighting_layer,
  f32 x y z, u32 flags, u16 event_count, u16 link_count, u16 links[], u16 rot[3], f32 scale[3] (flags & 0x100), u32
  data_dwords, u32 param_count, u32 data[], u16 indices[], {u32 type, string name} x param_count`. All 23 files consume
  exactly, and `field_value` decoding (section 2) works unchanged (0 range errors). PC lacks every PS2 addition: no second
  prefix word, no 0x10 radius flag (flags seen: 0, 1, 0x100, 0x101), u16 link ids (PS2 u32), unsigned rotations 0-359, no
  `0 / 0xffffffff` pair. `initial_event_count` is 1 on every PC object with no event data after it. Ids are not always ascending.
* **ODB class record [OotF]**: PC `0x0102`: name, `u16 group`, model ref (`u16 id`, `u16 db`), `u16 rfl_class_id`, `u16 icon`,
  `u32 field_count`, `u32 dword_count`, `u32 data[]`, `field_count` x `{u32 type, name}` (no 0x0103 pool on PC). PS2 classes
  without a model: same up to the model ref, then 10 zero bytes, `u16 rfl_class_id`, `u16 1`, `u16 icon`, `u32 field_count`,
  `u32 dword_count`, `u32 data[]`, no field definitions (they are in the 0x0103 pool); 33 of 54 sample classes fit this
  exactly. **The low 16 bits of the runtime class id (section 5) equal the PC `rfl_class_id`** (Level Properties 54,
  User Interface Properties 98, Dragon 28 on both).
* **Models (MOD) [OotF]**: same record types 0x0200-0x0207 on both (PC adds 0x0209 LOD links). PS2 vertices are 16 bytes
  (PC 12-byte vect3f) and PS2 polygons carry only `u16` vertex indices (PC: index plus UV floats per vertex).
* **Dependencies [OotF]**: a level's `.db`/`.DB` text file serves its own ODB; the LVL 0x0000 table (different indices) serves
  object class references. Same on both platforms.

## 10. Terrain layers (LVL 0x0001) on PS2

Decoder: `editor/packages/formats/src/terrain.ts` (`parsePs2Terrain`, `parsePcTerrain`, tests `terrain.test.ts`). Validated on
all 13 PAL level LVLs plus `OUT.LVL` (every layer decodes, block sizes match) and on the 23 OotF PC files (4445 layers).
Credit: the definition fields, the height formula, the cell split rule and the UV corner order are from OpenDrakan
(`doc/lvl_format.txt`, `src/odCore/Layer.cpp`, commit 6c0d499); the PS2 packing was found against the disc.

```
u32 layer_count
layer_count x { layer_def; inline_blob }        // layer_def identical to PC/OpenDrakan (section 9)
u32 1
layer_count x { u32 size; zlib block }          // blocks end exactly at the end of the payload      [disc]
block = (w+1)*(h+1) x vertex(6) , w*h x cell(22) // 6 / 22 bytes (PC: 4 / 26)                           [disc]
vertex = u16 flags[?], u16 mask, u16 raw_height  // height_wu = world_height + (raw - 0x8000) * 2      [disc]
cell   = u16 flags, u32 texture, 8 x u16 uv      // row-major, top-left first, like PC
```

* **Height [disc]**: `world_height` (f32 in the definition) + `(raw - 0x8000) * 2`, in world units (the same units as object
  positions; a cell is 2048). Origins are in cells. Check: for Surdana, objects with y pinned to the ground (Flower 01: median
  dy 0, 75th percentile 0) land on the decoded surface; `Grass (Sod)` sits a constant +158 above it (a model pivot offset),
  Birch trees median -69 (placed sunk into the ground). 866 of 4675 objects are within 1 unit of a surface, 1437 within 50.
  Scale 1 (not 2) and the inverted diagonal both fit clearly worse. Layers stack (floor/ceiling/in between); an editor picks the
  surface nearest the object's y.
* **Cell flags [disc]**: bit 0 set = diagonal from top-left (a) to bottom-right (d), else top-right (b) to bottom-left (c)
  (best fit against object heights). Other bits and the varying high byte (values like 0xd0, 0xf8) are **[?]**.
* **Texture ref [D][disc]**: `u16 id, u16 db`; db is an index into the LVL 0x0000 dependency table (Surdana: 9 = `Proc
  Textures`, id = SRSC id of a type 0x40 entry in that TXD). `0xffffffff` hole, `0xfffffffe` invisible [D]. PS2 stores **one**
  ref per cell (PC: one per triangle); the editor applies it to both triangles. Only 33 distinct refs in all of Surdana.
* **UV [D][disc]**: 8 x u16, `0xffff` = 1.0, order `Uc Ud Ub Ua Vc Vd Vb Va` with corners `a b / c d`; v up (GL). 13 distinct
  patterns in Surdana (the 8 flips/rotations of the unit square plus about 20 odd cells **[?]**).
* **Vertex words [inf]**: first u16 (0x200, 0x241, 0x000, ...) **[?]**; second u16 low 4 bits = which of the four surrounding
  cells exist (8/4/2/1 corners, 12/10/5/3 edges, 15 inside).
* **Inline blob [?]**: each definition is followed by a variable-length blob (92 bytes for a flat 3x4 layer, 7 KB for 21x47)
  that starts `{u16 seq, u16 0x18, u32 0x40}` (the 0x0c in section 9 is not what the layers show; 0x18 was seen on every layer
  checked), then header words (w, h, texture-ref count and list, w+1, h+1, count), per-vertex byte arrays and, in most
  layers, per-vertex `xx xx xx 0x7f` signed-byte normals (probably baked lighting data). It is **not needed** for geometry.
  The reader does not walk it: it finds the next definition by structure (name string, type <= 2, width/height equal to the next
  block's size). That resync matched all 483 Surdana layers and ends exactly at the zlib section; a real walk is open.
* **Units**: PC `Vertex` is `{u8 type, u8 1, u16 height}`, face `{u16 flags, u32 left, u32 right, 8 x u16 uv}`.

## 11. Models (MOD) on PS2

Decoder: `editor/packages/formats/src/model.ts` (`parseModel`, tests `model.test.ts`); 3700 of 3704 models in every MOD on the
PAL disc decode (the 4 exceptions: three FX quad records and one RESOURES polygon table, see Open). Credit: record names, the
0x0208 field list and the shading flags are from OpenDrakan (`doc/riot_database_format.txt`, `src/odCore/db/Model.cpp`).
All records of a model share its SRSC id.

* **0x0200 [disc]**: `string name, u32 shading, u32 0, [f32 distance if shading & 0x20000], u32 count, count x {u16 id, u16 db}`.
  The texture list lives here (PC has a separate 0x0206); the float (49000.0, 20000.0, 140000.0 seen) is probably a fade/cull
  distance **[?]**. `db` 0 = the model container's own TXD, else an index into its `.DB`.
* **0x0203 [disc]**: `u16 n, f32 xyz[n], then n x {i8 nx, i8 ny, i8 nz, 0x7f}`: positions are plain `vect3f` (12 bytes) and the
  "16-byte vertex" of earlier notes is a separate 4-byte-per-vertex array after them. The trailer decodes as unit normals
  (|v| about 127) **[inf]**.
* **0x0204 [disc]**: `u16 count`, then polygons `{u16 flags (2 double sided, 4 on quads), u16 vertex_count (3 or 4), u16 texture
  (index into the 0x0200 list), u16 index[vertex_count]}`, `u16 0` pad when the polygons end at 2 mod 4, then one `f32 u, v`
  per polygon corner in the same order (0..1, v up). So the PS2 polygon is index-only and the UVs follow as a block; sizes close
  exactly (`4 + 36 n` for triangles only). Quads are rare (NPC and FX boxes).
* **0x0208 character data [disc partial]**: bounds (64 bytes), `u16 lod_count`, names, nodes (36 bytes) as OpenDrakan, but the
  PS2 joint block differs (an extra u32 vertex count and 6 more bytes follow the nodes), so the skeleton is not decoded. The LOD
  table (`{u16 1, f32 distance, u32 usage, u32 node, u32 firstVertex, u32 vertexCount, u32 firstPoly, u32 polyCount}`, 30 bytes
  each, ranges chain and the polygon counts sum to the total) is found by that signature: unique for 132 of 153 skinned models
  (the rest fall back to "all triangles"). Rynn: LOD 0 = 1628 vertices / 3068 triangles, LOD 1 = 208 / 387 at distance 2300.
  Polygon indices are relative to the LOD's first vertex.
* **0x0207 / 0x0211**: 0x0207 bounds (not used; the editor measures the vertices). 0x0211 is the PS2 display-list copy of the mesh
  (GS/VU1 packets with the same UVs); not decoded and not needed.
* **Unit [inf]**: model space is in terrain cells (bounds of the Birch tree 3.8 x 4.9 x 3.8, a crate 0.3 x 0.2 x 0.2): multiply
  by 2048 for world units. No comparison with a live actor dump was made; the numbers are plausible against each other
  (tree about 12 x Rynn's height).
* **Class -> model [disc]**: ODB 0x0102 body after the name field starts `u16 group, u16 model_id, u16 model_db`; `model_db` 0 is
  the MOD of the same database, else an index into that database's `.DB`. 0 id = no model (triggers, timers). Surdana: 444 of
  538 placed classes have a model.
* **Open**: three FX polygon tables with 50-byte records (quad variant), RESOURCES model 4871; the 0x0208 joint block and the
  channel/bone data; what the per-vertex trailer and the 0x20000 float are exactly; polygon flag 0x0f06 on some FX quads.
