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
| ODB 0x0102 | **Class record** (not an instance): SRSC `id` = `class_id`, `sub` = group id (0x0101 = group name, e.g. "Level Properties"). Body: name, group, model ref, default field data and field definitions (decoded, section 12) | [D], names [disc] |
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
  u16 rot_x, rot_y, rot_z                               // degrees; native loading is unsigned
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
per class are not verified. Class defaults and the full field list are decoded in section 12; this JSON lists overrides only.

## 3. Coordinates and rotation

* Positions are plain f32 **world units equal to the runtime actor coordinates** (`actor+0x14`): exact match (1.0 tolerance)
  for 4379 of 4602 live Surdana actors, no 1/2048 scale (2048 units = one layer cell, so cells = value / 2048). **[RAM]**
  Axis order x, y (up), z.
* Stored y is not always the final y: Arokh is stored at 200000.0 (the layer world height) and rests at 202773 at runtime;
  some props settle onto the ground. Only 27 of the 296 unmatched records are near a live actor of the same resource
  (<300 units), the rest are not live in that dump. **[RAM]**
* Rotation: three 16-bit degree words. Native loader `0x1c9f40` reads each through `0x12cfe8` into an unsigned
  halfword, converts it to radians, and applies Y with a minus-90-degree offset, then X and Z. With rx = rz = 0,
  runtime yaw (`atan2(m[0][2], m[0][0])`) equals `unsigned(rot_y) - 90`, modulo 360. The editor reader retains
  legacy signed bit patterns for roundtrip; that does not make negative and positive degree encodings interchangeable.
  PC 270 must stay 270: writing -90 produces unsigned 65446, adding 16 degrees modulo 360. **[native code/RAM]**
  The earlier Surdana census found 3890 of 4379 matched actors within 6 degrees; discrepancies alone do not establish
  AI turning or physics. Keep all three authored values when copying existing records.

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

* The Euler composition order (class defaults: section 12).
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
  prefix word, no 0x10 radius flag (flags seen: 0, 1, 0x100, 0x101), u16 links (PS2 u32 ids; **a PC link is the 0-based position of the target in the object table, not its id**: all 78 links of Rift World and every link of Alwarren / Ruined Village / Volcano hit an object only when read as positions, see `docs/journal/2026-11-01/ootf-puzzles-ending.md`), unsigned rotations 0-359, no
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

* **Collision triangles [PAL]**: native routine 15a2f0 skips the first triangle on cell flag `0x40` and the second on
  `0x80`, independently of the cell's shared texture. With diagonal bit 0 set these are a-c-d and a-d-b; otherwise
  a-c-b and c-d-b (corners a b / c d). `layerHeightAt` honours these bits only for PS2 layers; PC holes use the two
  separate triangle texture references. Invisible rendering bits `0x30` retain collision.
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
* **Layer type [disc, game]**: 0 floor, 1 ceiling, 2 **water** (both platforms; the engine's draw loop `0x152330` puts type 2
  layers on the water list for `0x161a78`, and the player swims in them). Every water layer of the 14 PAL levels has the empty
  inline blob (no chunks); its cells name Proc Textures entries whose material is a water material (section 12b, texture header).
  The water pass draws **every cell** of a water layer: hole (0xc0) and invisible (0x30) cells, cells with a floor texture and
  vertices with bit 0x100 all drew in game, so the disc keeps a water layer's hole vertices at its surface, under the bank. A
  vertex far above or below the surface near the camera makes the water around it vanish (2048 off already cost part of a small
  pool) [game, journal 2026-11-14/ootf-water-sheets.md].
* **Vertex words [inf]**: first u16 (0x200, 0x241, 0x000, ...): the low byte is the vertex type. PS2 list (ELF strings): 0 Normal,
  1..64 `Vertex Color 1..64` (the Level Properties colours), then the water kinds. Water layers use 0x41, 0x71, 0x5a; that is
  the Drakan 1 vertex type (PC type byte, `Dragon.rfl` list: 0 Normal, 1 Water Still, 2..25 currents, 37..44 Water Phase,
  45..49 Shallow Water 92%..60%, 64..71 Underwater) plus 0x40 (PC 1 = 0x41, PC 49 = 0x71). A water layer with type 0 vertices
  does not make the player swim (converted OotF levels, journal 2026-11-08/ootf-water.md) [game]. Bits 0x100 / 0x200 **[?]**
  (0x100 on vertices with no adjacent cell). Second u16 low 4 bits = which of the four surrounding
  cells exist **and are not holes** (8 down-right, 4 down-left, 2 up-right, 1 up-left; 98.8 % of LEVEL1NA vertices, "exist"
  alone 91 %); its upper byte is 0 on every disc vertex [disc, 2026-11-02].
* **`visible` [disc]**: every layer lists itself (483 of 483 in LEVEL1NA, first in 175); 90 % of links are symmetric. An
  appended layer that did not list itself hung the game when the player came near it [game, 2026-11-02].
  At run time the list is also the search set of the **cell lookup** `0x14b6b8` (x, y, z, world `0x4dc110`, old layer,
  flags): an actor's layer index (actor `+0x7c`) is refiled by `0x1496a0` with the lookup from its old layer at its
  bounding-sphere centre (`+0xe8`), and the view layer `0x4dade4` (whose `visible` list the terrain draw `0x152330`
  walks) is updated every frame by `0x112488` at a point 32 units behind the camera. Only listed layers can be entered;
  when none holds the point the result is -1, and the next refile "from no layer" (`0x1499c8` for actors, the next
  `0x112488` call for the view, whose old layer is the view struct `0x4dad58 +0x8c`; `0x1435c8` sets it to -1 once per
  level load, from `0x218700`) searches all layers. For the player the update `0x321258` handles an actor at -1 itself:
  it puts her at the position saved at the end of the last frame (player `+0xbe4`) minus a step from `0x325d30` and
  files her again (`0x1499c8` from `0x321548`); a large step moves her 1000 units and more in one frame. Inside an
  interior the search from -1 can return the outdoor layer above it, which does not list the interior back: Rynn is
  moved outdoors or the view keeps drawing the outdoor world from below. The engine tick `0x135c40` skips the world
  update while the view layer is -1 and otherwise runs it from the view layer's record (`0x151790`, `0x14cf08`), so a
  view in the outdoor layer also stops the player in the interior. This is the Surdana cellar fault of issue #6: the
  PC camera follow carries the camera round Rynn on an arc, through the corner of a room and past the layer edge. The
  port answers a follow-camera point with no layer from the controlled actor's layer (`drakan_view_layer.h`, view
  only, [journal](../journal/2026-11-12/issue6-cellar-glitch.md)) [game, 2026-11-12]. A level
  should list every layer an actor can walk into, and walls should not let the sphere centre or the camera reach past
  the layer they bound.
* **Inline blob [?]**: each definition is followed by a variable-length blob (92 bytes for a flat 3x4 layer, 7 KB for 21x47)
  that starts `{u16 seq, u16 0x18, u32 0x40}` (the 0x0c in section 9 is not what the layers show; 0x18 was seen on every layer
  checked), then header words (w, h, texture-ref count and list, w+1, h+1, count), per-vertex byte arrays and, in most
  layers, per-vertex `xx xx xx 0x7f` signed-byte normals (probably baked lighting data). It is **not needed** for geometry.
  The parser finds the next definition by structure (`splitPs2Terrain`: name string, type <= 2, width/height equal to the next
  block's size); the blob itself is now walked exactly (`readPs2Blob`, section 10.1).
* **Units**: PC `Vertex` is `{u8 type, u8 1, u16 height}`, face `{u16 flags, u32 left, u32 right, 8 x u16 uv}`.

### 10.1 Inline blob layout

Generator: `editor/packages/formats/src/terrainblob.ts` (`buildPs2TerrainBlob`). Reader / writer: `terrainwrite.ts`
(`readPs2Blob`, `writePs2Blob`): byte-exact on **every** layer of the 14 PAL level LVLs (2026-11-02).

```
u16 seq, u16 level_tag, u32 pool, u32 x_lo, x_hi, z_lo, z_hi, u32 chunk_count            // 28 bytes   [disc]
  or u32 0 (4 bytes, no chunks): layer without draw data                                 //            [disc]
chunk_count x {                                                                          // x outer, z inner
  u32 x0, x1, z0, z1 ; u32 ntex ; ntex x u32 ref ; u32 vw, vh, n(=ntex)
  n x (vw*vh) bytes                                                                      // one array per texture
}
flat layer (definition flags & 0x04):  00 00 00 00  00 7f 00 7f
else: 00 00 00 00, then (w+1)*(h+1) x { i8 nx, ny, nz, 0x7f }  // entry v+1 = vertex v, row-major
```

* **Header [disc]**: `level_tag` is constant per level (0x0c LEVEL0RO, 0x16 LEVEL5NE; meaning [?]). `x_hi = 4w-1`, `z_hi = 4h-1`
  for every solo layer; `pool` = 64/128/256, the smallest that holds the largest `hi+1` of the group. `seq` is a group id
  (layers that coexist share one; twin floor/ceiling layers have equal seq). Inside a group the two ranges are slot
  allocations that **never overlap** (0 of about 14,000 same-group pairs in 14 levels; an earlier "overlap" note came from
  blobs the old resync had cut wrongly); the allocation order is **not derived**. A solo range `0..4w-1` / `0..4h-1` is
  self-consistent **[inf]**. `seq` runs 1..n (LEVEL1NA 50, LEVEL4LD 103; one gap in LEVEL1OR). A layer moved into a
  **new** `seq` (one past the highest) was not drawn at all (collision kept) [game, 2026-11-02].
  Groups hold 1..20 layers (LEVEL1NA seq 1: twelve layers shelf-packed at x 0..11, 12..15, 16..31 / z 0..15, 0..3, ... in a 64
  pool). `planTerrainSlots` (terrainblob.ts) does the same for converted levels; in-game accepted (Volcano: 941 layers).
* **Far textures [disc, game]**: texture n of the level's `_splats` TXD (`LEVEL1TS.TXD` for LEVEL1NA) is the far texture of group
  `seq` n: `pool x pool`, 8 bpp, header flags 0, +40 = 0, +44 = 256.0. Each cell is a 4 x 4 block of its texture's mean colour
  at `x_lo + 4 cx`, `z_lo + 4 cz` (stored rows; no lighting baked), unused texels are a magenta (248, 100, 248) filler. The engine
  draws distant chunks with it. A converted level needs its own (`buildTerrainPoolTxd`, ootfterrain.ts): the template's drew
  its colours and filler on converted terrain ([journal](../journal/2026-11-08/ootf-pink-textures.md)).
* **Limit [game]**: at most **10 textures per chunk**. The chunk record is 0x4c bytes: `+0x00 x0, +0x08 x1, +0x04 z0, +0x0c z1,
  +0x10 ntex, +0x14..0x3b ref[10], +0x3c vw, +0x40 vh, +0x44 n, +0x48 weights ptr`; `FUN_00165840` (stream reader) copies `ntex`
  refs without a bounds check, so an 11th ref overwrites the weight-array descriptor and the heap allocator (`FUN_004acd80`)
  later loops forever. The runtime builder (`FUN_00165f68`) checks `ntex < 10`. The PAL discs never exceed 8. **10 is not
  safe either [game]**: the draw pages built from a 10-texture chunk fill only 7 texture groups (68-byte records, page `+84`),
  the mesh drawer `FUN_00158148` still walks all of them and `FUN_001ed0b8` sends an empty one as VIF `UNPACK NUM=0` (= 256),
  which parses the rest of the packet out of step, changes STMOD and leaves VU1 drawing garbage for the rest of the session
  ("VU1 reserved lower", 2 fps; [journal](../journal/2026-11-07/vu1-reserved-lower.md)). So the limit is 8. There is no
  per-layer limit: chunk rectangles are free-form in the file, so `buildPs2TerrainBlob` splits a chunk whose texture list
  (with the neighbour window, cells x0-1..x1 / z0-1..z1, so a 1x2 chunk sees 12 cells) exceeds 8, down to single cells; a
  single cell that still sees more keeps its own texture and the neighbours' most used ones. Ruined Village (42 textures in a
  layer, 37 in a chunk) loads and draws with all of them (docs/journal/2026-11-01/ootf-texture-limit.md).
  **Wide chunks [game, 2026-11-14]**: the 10-group limit is the mesh drawer's 10-word flag array on its stack (group 0 is
  the base pass, so 9 textures; a 10th makes it read a stale flag and draw an empty group). The exe carries a second copy
  of the twelve functions that use the record (`[[variants]] wide_terrain_chunks` in `configs/drakan.toml`): record
  0xa4 bytes with `ref[32]` at +0x14 and the descriptor at +0x94, drawer flags 64. `[general] terrain_wide_chunks`
  switches them in at start-up; a pack whose `mod.toml` says `terrain_chunk_textures = 32` (converter
  `DRAKAN_TERRAIN_CHUNKS=wide`) always gets them. Then a chunk may list 32 textures and converted layers keep the 12 x
  12 grid ([journal](../journal/2026-11-14/ootf-wide-chunks.md)).
* **Chunks [disc]**: the layer is cut into `ceil(w/12) x ceil(h/12)` chunks of `ceil(n/pieces)` cells (last one shorter);
  vertex counts are cells+1. Matches 291 of 297 layers; the other six (13x24, 25x32, 14x12, ...) split z finer, rule open.
* **No drawn cell = empty form [disc, game]**: in the 14 PAL levels no layer whose cells are all holes / invisible has chunks
  (they all use `u32 0`); chunks that list no texture occur only next to chunks that do. A chunked blob in which every chunk
  lists none makes the engine's layer draw object (vtable `0x51ad90`) end with zero draw pages: its draw pass still links it
  into the terrain draw-data LRU (head `0x4dff60`), but the release (`FUN_0015c820`) unlinks only objects with pages, so
  after a level reload the list runs through freed memory and the eviction walk (`FUN_0015e5a0`) can loop forever.
  `buildPs2TerrainBlob` writes the empty form for such layers ([journal](../journal/2026-11-07/ootf-quickload-lru.md)).
* **Texture list [disc]**: the distinct cell refs of the chunk **and of the cells that touch its vertices from the previous chunks
  (window `x0-1..x1`, `z0-1..z1`; 16 of 19 LEVEL0RO layers)**, holes (`0xffffffff`) and invisible (`0xfffffffe`) left out.
  The **order is a per-texture priority** that is global per level (no conflicting pair in 2 levels) but is not in the layer
  data, so it is not derivable; the base ground texture tends to come last. A few chunks also list a texture no cell uses
  (the unstored right-triangle ref of a PC-style per-triangle cell, or `0x110237`-like specials) **[inf]**.
* **Per-texture arrays [disc]**: byte per chunk vertex `(gx,gz)` = `count | near`, `count` = how many of the four cells around
  the vertex use the texture (cells of neighbouring chunks included), `near` = 0x10 when `gx<x1 && gz<z1` and some cell of
  the texture lies in the 3x3 cells `(gx-1..gx+1, gz-1..gz+1)`. This looks like a per-vertex blend weight for splatting
  the texture over its neighbours **[inf]**. Exact for flat layers and for interior vertices (about 96% of all array
  bytes). Layer-border vertices differ: flat layers count nothing outside, isolated layers count outside cells as matching
  every texture (corner = 4), layers with neighbours depend on the neighbour's data (not a simple function of the layer).
* **Normals [disc]**: all-zero first entry, then per vertex. Central-difference normals from the heights (cell 2048, y up,
  `-dx, 1, -dz` scaled to 127) land within 3 units for about half of the entries and exactly for 15%; about 6% of entries
  are horizontal (`ny = 0`, cliff/seam vertices) and the baked values are not reproducible from the layer alone. Flat
  layers (definition flag 0x04; 278 of 278 checked) store only the up vector.
* **Regeneration score** (generator given the real seq/level_tag/texture order): whole blob byte-exact 6 of 584 layers
  (the flat solo ones); chunk layout correct 575 of 584; array bytes correct 86-96% per level when the texture set matches
  (that is 268 of 584 layers; 30 have every array byte right). Not derivable: slot ranges and pool of grouped layers, texture order, border counts next to
  other layers, baked normals, per-triangle texture extras.
* **Does the engine need it? [inf]** Probably yes for drawing: the arrays and chunk cut are the draw lists (per chunk, per
  texture), not collision; heights, flags and UVs all live in the zlib block. Untested in-game. Safe defaults for a new
  layer: unique `seq`, solo ranges, texture order by the new texture's own priority, `outside` = none for flat layers and
  any otherwise, up-pointing or central-difference normals; set the flat flag (0x04) to skip normals entirely.

### 10.2 Writing edited terrain (2026-11-02)

Writer: `editor/packages/formats/src/terrainwrite.ts` (`rebuildPs2Terrain`); pack wiring in editor-core `terrain-pack.ts`.
In-game evidence: `docs/journal/2026-11-02/terrain-editing-write.md`.

* **Override [game]**: the rebuilt 0x0001 payload as `overrides/<LEVEL>.LVL/0001_0_0.bin`; no other entry, no splat file and
  no runtime change is needed for height, texture (refs the level already uses), resize and new-layer edits. 0x0001 is
  larger than the 0x40800 whole-entry limit on 10 of 14 levels, so it may grow there.
* **Heights [game]**: collision follows the zlib block heights at once (Rynn stands on a +3000 hill, falls without the pack).
  Normals in the blob only shade; recomputing the 3x3 neighbourhood of moved vertices is enough. Objects keep their y.
* **Cell textures [game]**: a new ref in a chunk needs its texture-list entry and weight array; recomputing the bytes around
  the repainted cells blends the patch into its surroundings like disc patches.
* **Resize / new layer [game]**: needs a blob with a slot in an **existing** group (a new `seq` drew nothing); overlapping the
  slot of a group member that no `visible` list links to it drew correctly. A new layer must list itself in `visible`
  (else the game hangs once the player is near it) and the layers around it must list it to draw it from there.
* **Unchanged layers** are copied byte for byte; an unedited rebuild is the disc payload (all 14 levels).

## 11. Models (MOD) on PS2

Decoder: `editor/packages/formats/src/model.ts` (`parseModel`, tests `model.test.ts`); 3700 of 3704 models in every MOD on the
PAL disc decode (the 4 exceptions: three FX quad records and one RESOURES polygon table, see Open). Credit: record names, the
0x0208 field list and the shading flags are from OpenDrakan (`doc/riot_database_format.txt`, `src/odCore/db/Model.cpp`).
All records of a model share its SRSC id.

* **0x0200 [disc]**: `string name, u32 shading, u32 0, [f32 distance if shading & 0x20000], u32 count, count x {u16 id, u16 db}`.
  The texture list lives here (PC has a separate 0x0206); the float (49000.0, 20000.0, 140000.0 seen) is probably a fade/cull
  distance **[?]**. `db` 0 = the model container's own TXD, else an index into its `.DB`. One list serves every LOD: the
  0x0204 texture index and the 0x0211 batch texture of every LOD list index this same list. Low-detail sheets of a
  character are extra entries at its end (FattyChick: indices 3, 4 = 0x1986, 0x1987, used only by LODs 1 and 2; northman4,
  NicholsTheBold, ConcubineNew2/3/Bride, Guard2/3 the same), never a separate per-LOD list. **[game]** The resident model
  keeps one texture library (`model+0x38`, count `+0x40`) and each prepared LOD slot's strip parts index it (BigBrownGuy,
  2026-11-12 RAM dump: library 0x5187e, 0x51884, 0x51885; slot 2 part material 2 = 0x51885).
* **0x0203 [disc]**: `u16 n, f32 xyz[n], then n x {i8 nx, i8 ny, i8 nz, 0x7f}`: positions are plain `vect3f` (12 bytes) and the
  "16-byte vertex" of earlier notes is a separate 4-byte-per-vertex array after them. The trailer decodes as unit normals
  (|v| about 127) **[inf]**.
* **0x0204 [disc]**: `u16 count`, then polygons `{u16 flags (2 double sided, 4 on quads), u16 vertex_count (3 or 4), u16 texture
  (index into the 0x0200 list), u16 index[vertex_count]}`, `u16 0` pad when the polygons end at 2 mod 4, then one `f32 u, v`
  per polygon corner in the same order (0..1, v up). So the PS2 polygon is index-only and the UVs follow as a block; sizes close
  exactly (`4 + 36 n` for triangles only). Quads are rare (NPC and FX boxes).
* **0x0208 character data [disc]**: skeleton, skin weights, LOD table, clip list and bounding spheres; all 154 skinned models of the
  PAL disc decode to the last byte (the 155th record belongs to the RESOURES model 4871 whose 0x0204 does not parse). Parser
  `formats/src/ootfanim.ts` `parseSkeleton(b, 'ps2')` (layout and the PC differences: `docs/journal/2026-10-31/ootf-animation.md`),
  playback semantics `formats/src/skeleton.ts` (`buildRig`, `skinVertices`), proof `editor/scripts/anim-probe.ts`:
  `64 bytes bounds, u16 lod_count, pstr[lod_count-1] LOD names, u16 node_count, nodes {char[32] name, i32 joint (-1 = helper
  node such as chnr_* / eff_*)}, u32 total_vertices, u16 joint_count, joints {f32 t[3], f32 R[3][3], i32 mesh, i32 first_child,
  i32 next_sibling, per LOD: u16 n + n x {u32 vertex, f32 weight}}, LOD table (u16 1 + 28 bytes each, as below), u16 n + u32
  animation ids, channels, spheres`. Facts used for playback **[disc]**:
  * **Matrices are row-vector** (`v' = v * R + t`), stored translation first. A joint's matrix is the **inverse of its
    model-relative bind transform** (mesh space to joint space). Local bind = `bind(j) * inverse(bind(parent))`; the local bind
    translation equals the `base` translation of every unkeyed ADB track of that joint (Rynn: joints 2..5 = 0.03, 0.0345,
    0.042, 0.0137 in both), which pins the convention. Skinning: `v' = sum_k w_k * (v * invBind_j * world_j)`; with every joint at its
    bind transform this reproduces the mesh exactly (154 of 154 models, error < 1e-3).
  * **Weights**: per joint and per LOD, vertex numbers relative to the LOD's first vertex, up to 4 joints per vertex (never
    more), weights per vertex sum to 1 (one vertex of Rynn's 1628 does not). About 2 percent of vertices (4200 over the disc;
    Rynn 34: hair tips, eye parts) have no weight at all and ride the root joint (joint 0, identity).
  * Joint 0 is the model root; the tree is first-child / next-sibling; node 0's joint is 0. Rynn: 125 nodes, 65 joints, 32
    channels, 499 listed clips. Joint names = the node that points at it (`torso`, `Lhand`, `Rweapon`, `sphere1`).
  * The animation id list is the clip set of the model; ids resolve in the `.ADB` of the same stem (7746 of 7747 listed clips
    exist and have one track per joint; the one that does not is a listed id with no ADB entry).
  Open: the LOD table is still found by signature in `model.ts` (`findCharacterLods`), the channel caps and sphere unknown
  field are decoded but not interpreted.
* **0x0207**: bounds and object collision (the editor measures the vertices instead). Sphere, box, then `u16 num_shapes,
  u16 is_obb` and a first-child / next-sibling hierarchy; OBB shapes carry `u16 n + n x u16` polygon indices. The PC lists a
  polygon in every box it crosses and the root lists all of them; a leaf box without polygons is a solid block. On the disc
  inner shapes list none, and most hierarchies (716 of 863 in the host MODs) list no polygons at all. **[game]** Rynn
  is not stopped by a prop's sphere, and she pushes 100 to 150 units past thin listed polygons (a cage's bars); a box
  without polygons stops her outside it. The converter maps the PC lists through its polygon splits and reorders and gives
  cage models one solid box ([cage collision](../journal/2026-11-08/ootf-cage-collision.md)).
* **0x0211 (strip display list) [disc]**: decoded; the engine needs it for skinned models and the Birch tree class (a static prop
  draws from 0x0203/0x0204 alone). Layout, little endian, unaligned:
  `u32 1, u32 V, u32 0`, V vertex groups as `count[0], cum[0], count[1], cum[1] .. count[V-1]` (`cum` = start of the next group,
  padded to a multiple of 4; the 0x0203 vertex count is the padded total, pad vertices are zero), `u32 batchCount, u32 texture of
  batch 0`, then per batch `u32 W, W x {u8 group, u8 slot}` (groups to copy into the vertex window), `u32 triangles` (sum of
  strip length - 2, degenerates included), `u32 cap, cap x u8` (strip stream: 1-based window slot per element, 0 ends a strip),
  `u32 cap, cap x (f32 u, f32 v)` (one UV per stream element, (0,0) on terminators), `u32 texture of the next batch` (0 after the
  last). `cap` = elements incl. terminators rounded up to 64 (disc caps 64..1408). A group is a contiguous run of 0x0203
  vertices; stream value s is window slot s-1; the window held at most 225 slots on the disc, so models with more vertices are
  split into groups with copied vertices. UVs equal the 0x0204 corner UVs up to a whole-number shift (83 % identical). Checked:
  the strips reproduce the 0x0204 triangle set exactly on every model without a tail section (about 1500 models in LEVEL1ON,
  WORLDCON, RESOURES, SYSTEM). Strip orientation is per strip, 62 % opposite to the 0x0204 winding, 38 % equal (the engine does not
  seem to care). Skinned models: every vertex exists once, groups are joint clusters, one batch loads several groups packed from
  slot 0. The leading word is the number of lists, one per LOD of the 0x0208 table: each further list repeats the layout from
  `u32 V, u32 0` on, with group starts relative to that LOD's first vertex (2026-11-10: the 49 multi-LOD models of NPCS.MOD
  decode to the last byte this way; on the two checked, BigBrownGuy and MediumGuy, every list gives the 0x0204 triangle count
  of its LOD; `decodeDisplayList` still reads the first list only and reports the rest as `tail`). Not understood: the tail on large models that is not a further list, 0x0214. Encoder: `editor/packages/formats/src/ootfdisplaylist.ts`; notes
  [ootf-displaylist-decoded](../journal/2026-11-01/ootf-displaylist-decoded.md).
* **Unit [inf]**: model space is in terrain cells (bounds of the Birch tree 3.8 x 4.9 x 3.8, a crate 0.3 x 0.2 x 0.2): multiply
  by 2048 for world units. No comparison with a live actor dump was made; the numbers are plausible against each other
  (tree about 12 x Rynn's height).
* **Class -> model [disc]**: ODB 0x0102 body after the name field starts `u16 group, u16 model_id, u16 model_db`; `model_db` 0 is
  the MOD of the same database, else an index into that database's `.DB`. 0 id = no model (triggers, timers). Surdana: 444 of
  538 placed classes have a model.
* **Open**: three FX polygon tables with 50-byte records (quad variant), RESOURCES model 4871; the channel caps; what the per-vertex trailer and the 0x20000 float are exactly; polygon flag 0x0f06 on some FX quads.

## 12. Class fields: ODB 0x0102 / 0x0103 (names, types, defaults)

Tool: `analysis/scripts/odb_fields.py` (`--iso <disc>` audits every ODB and level; `--odb X.ODB --class Name` dumps one class).
Parsers: `editor/packages/formats/src/{odb,fields}.ts` (`parseOdbClasses`, `setObjectParams`; tests `odbfields.test.ts`).
Credit: field types, the "one dword per field, strings and arrays as offset/length" data area and the ARGB colour order are
from OpenDrakan (`doc/riot_database_format.txt`, "Class format", commit 6c0d499); the PS2 record offsets and the 0x0103 list
layout were found against the PAL disc.

```
0x0103 (zlib, one per ODB; replaces the field_def list that PC keeps inside each class record):
    per 0x0102 record, in record order, no separators:  u32 count; count x { u32 type; string name }
0x0102 body after the name string:
    u16 group; u16 model_id; u16 model_db
    model_id != 0:  74 more bytes, [+80] u16 rfl_class, u16 1, u16 icon;  u32 field_count at +86
    model_id == 0:  10 zero bytes, [+16] u16 rfl_class, u16 1, u16 icon;  u32 field_count at +22
    u32 field_count; u32 dword_count; u32 data[dword_count]       // ends the record exactly
```

* **ODB header record `0x0100`** (id 0, sub 0, 32 bytes): `u16 class_count` (number of `0x0102` records), `u16 group_count` (number
  of `0x0101` records), then constants and the library path (`..\..\Drakan 2.rfl`). Equal to the index counts in all 21 PAL ODBs.
  The guest sizes its class and group tables from them, so records added without raising the counts overflow (3+ corrupt the heap).
  `patchSrsc` keeps them in step. The container index itself is sorted by `id` (stable) by the guest at open.
* `data[i]` is the class default of field i (same encoding as the LVL parameters, section 2): int signed, float f32,
  strings and arrays `u16 offset, u16 length` into `data`. A class's full field list is therefore `list[i] = (type, name)`
  with default `data[i]`; the LVL `field_index` is the index into that list. **[disc]**
* **Evidence:** all 21 ODBs on the PAL disc decode (6520 classes, 130069 field definitions); every 0x0103 blob is consumed
  exactly, `field_count` equals the list length and the record ends exactly after `data` for **6520/6520** classes. Every
  override in every level (38415 objects, 29970 parameters in 13 levels plus the intro) names a class that resolves and has
  the same field index, type word and name as the class definition: **29970/29970**. The editor parsers agree (6520 of
  6520 classes with fields) and the 14 level LVLs re-pack byte-identically.
* **References.** Class (type 3, also with filter bits: 0x2003, 0x4003; arrays 0x1003, 0x3003, 0x5003), model, sound,
  animation, sequence, texture: `value = (db << 16) | id`. `db` 0 = the ODB that owns the class; otherwise an index into
  that ODB's `.DB` dependency list (not the LVL table): 100% of 137 non-zero class references in Surdana resolve through the
  owner's `.DB`, only 36 through the level table. 0x2003 holds a class id, **not** a placed object id (only 5 of 273 non-zero
  values are also object ids). `0xffffffff` = none for channels. Filter bits 0x2000 / 0x4000 are kept as stored; their meaning
  (which classes the editor offers) is **[?]**.
* **Enums.** Type 7 carries no item names in the data (34686 of 130069 fields). Most are yes/no (values 0/1). The editor
  offers the values the shipped class defaults actually use. Type 6 (bool) never occurs. Other seen types: 0x105, 0x10,
  0x1010 (movie ids, sound slots) are plain ids/arrays.
* **Field groups** do not exist in the data; the editor groups fields by a name/type heuristic only.
* **Writing parameters.** A parameter list is rebuilt as one dword per parameter in ascending field index followed by the
  string/array payloads (`setObjectParams`). Verified in game: Initial Health (field 46, int) added to two Purple Orc
  Center objects in Surdana through an exported mod pack: the world dump shows hp/hpMax 500 on exactly those two and 35 on
  the other ten (journal `2026-10-31/level-field-editor.md`).

### 12b. What a model / sound / texture / animation / sequence reference points at

Resolved against the PAL disc (every ODB class default of those types; probe results, 6520 classes). A reference is
`(db << 16) | id`; `db` 0 = the owning ODB's own container stem, otherwise the stem named by that ODB's `.DB` list. The
container file is `<stem>.<ext>` in the same directory, `id` is the SRSC id inside it (all stems are unique across a language
tree, so a stem to file map is enough):

| Field type | Container | Entry (SRSC type) | Name / facts | Evidence |
| --- | --- | --- | --- | --- |
| 4 model | `.MOD` | 0x0200 (and 0x0203/0x0204 geometry) | name in 0x0200 (`Model.name`), texture count | **[disc]** all 178 non-zero class defaults hit a model |
| 5 sound | `.SDB` | 0x0302 descriptor + 0x0304 SPU-ADPCM | name and subtitle text in 0x0302 (docs/reference/subtitles.md 3.1), rate at +8 after the name | **[disc]** 1355 of 1355; one `sub` per id (no id has two subs in SYSTEM, NPCS, LEVEL1ON), 0x0302 and 0x0304 subs match |
| 14 (0x0e) texture | `.TXD` | 0x0040 | no name; header gives size and bpp | **[disc]** 199 of 199 |
| 9 animation | `.ADB` | 0x0501 header, 0x0502 key data (0x0505 on some) | 0x0501 = `pstr name, f32 duration (s), u32 flags, u32 track count, ...`, 0x0502 = per-joint key tracks (see Animation clips below) | **[disc]** 2121 of 2126 hit 0x0501+0x0502; 5 class defaults (`Darkness` flight/attack) name an id with no entry |
| 11 (0x0b) sequence | `.SSD` | 0x0311 (0x0312 = folder names, 0x0310 = file header) | name = `u16 length, string` at the start of the record ("6.04 Guardian Gate", "pillar stomp"); full layout in section 14 | **[disc]** the `Sequence List` of every placed Stomp Player (cut scene player, runtime class 0x10033) is the SRSC id of a 0x0311 record of the level's own SSD: 84 of 84 over the 14 dumped levels |
| 8 character channel, 12 layer | none | | not resource ids (`SYSTEM/0` etc. do not resolve in any container) | |

Notes:

* Array types (`0x1004` models, `0x1005` sounds 4759x, `0x1009` animations 5488x, `0x100b` sequences) hold one such word per element.
* **Break-away floors (engine class 44) [disc, game]**: the class has no model of its own; `Model List` (`0x1004`) names the
  pieces, each modelled in place around the object's origin, so together they draw the intact floor. Eight of the nine disc
  floor classes name their pieces with db 0 (their ODB's own MOD; LEVEL1ON "Bridge Breakway Pieces" 950..963), LEVEL6EN's
  through its `.DB` (db 5); the sampled pieces (LEVEL1ON, MOUNTAON) each have a one-shape OBB 0x0207 and a 0x0211 display
  list. At level load each piece becomes a registry entity of the same class at the floor's
  origin; with `Break Away on Trigger Only` 0 the pieces under the player fall when she stands on them (the others stay).
  An object may name its own pieces in the same field. Converted OotF floors get their PC pieces
  in the template ODB's own MOD the same way (journal 2026-11-07/ootf-breakaway-floors.md).
* **SPU ADPCM (0x0304)** is the stock PS2 VAG block format: 16 bytes = `shift | predictor << 4`, flag (1 end, 4 loop start), 14 bytes
  of 28 signed nibbles (low first); predictor filters (0,0), (60,0), (115,-52), (98,-55), (122,-60); mono, rate from the
  descriptor (22050 speech, 44100 effects). `formats/src/resources.ts` `decodeSpuAdpcm` is a port of
  `analysis/scripts/sub_adpcm_wav.py`; the editor plays it through Web Audio.
* **Texture header (TXD 0x0040) [disc]**: 64 bytes, `u32 w, h, stride, bpp (8/16/24/32), u32 alpha selector at +16, i32
  colour-key palette index at +20 (-1 none)`, fps byte at +36, feature byte at +37 (0x02 = has alpha), 0x80 at +38 for
  bpp > 8. `sub_1bf838` maps the selector (only when +37 has 0x02) to native material flags: 1 -> 0x10000, 2 -> 0x20000,
  4 -> 0x40000, 7 or 8 -> 0x80000; a key index sets 0x8. The draw path (`sub_18c52c`) enables texture alpha only for
  `flags & 0xf0000` or the key bit, so selector 0 means opaque: alpha-0 texels draw in their colour. 0x80000 is alpha
  blending, the others the GREATER 0 test (`runtime/gs/src/ray_scene.cpp`). On the disc: 8 bpp 0 (opaque), 8 (219 of the
  221 with alpha-0 texels, nearly all with soft edges), 1 or 4 (one each); 16 bpp 1; 24 bpp 0; 32 bpp 7. Alpha-0 palette
  entries carry an edge colour, never black. The OotF converter follows this (journal
  2026-11-07/ootf-cutout-textures.md). The PC flags byte sits at +37 (0x40 dynamic, 0x02 alpha: water entries have 0x42).
  **+40 is the texture's material**, a class of the TXD's own ODB (Proc Textures: `PROCTEES.ODB`): 1 "Water", 240 "Surdana Water",
  230 "Stormy Water_Level 3a", 232 "Northman Water", 95 "goblin forest water", 220 "Desert Water", 94 "Lava (Base)" (engine class
  0x100d1: first / second scrolling texture, flow, alpha by distance, wave amplitude, specular), 20 "Tan Dirt", 45 "Blue Snow"
  (splat materials: tiling, weight, fade). The disc's water entries (0x10e, 0x471d, 0x467f, 0x4682, 0x270, 0x3972, lava 0x1d7)
  are 64 x 64 placeholder pictures (a "T", an arrow) that the water pass never shows; it draws the material [disc, game].
* A model's texture refs follow the model container's own `.DB` (db 0 = its own TXD); `scene/decode.ts` `loadModelData` uses
  the class ODB's list instead, which is the same for the shipped data where both name the same stems.
* **Animation clips (ADB) [disc]**: 0x0501 = `pstr name (no NUL), f32 duration (seconds), u32 flags (bit 0 = does not loop), u32 track
  count (one per joint of the model, NOT a frame count), u32 channels, f32 0.2, u32 1, f32, u32 0, f32`; 0x0502 = `u32 tracks,
  tracks x {u32 key offset, u32 key count, u32 flags, f32 xyz base}, u32 key bytes, keys`. Keys are `{f32 t, f32 qx qy qz qw}` (20 bytes) or, on
  tracks flagged `0x80000000`, with `f32 x y z` appended (32 bytes). `t` is in seconds (key-reduced, so irregular spacing),
  `0 .. duration`; there is no frame rate. Rotation is an **absolute parent-relative quaternion** (row matrix = transpose of the
  textbook one; at rest it equals the joint's local bind rotation); translation is the track's `base` unless the track is keyed,
  and the root joint's keyed translation is the **root motion** (Rynn run: about 1900 game units per 0.77 s loop; the editor plays
  in place by default and prints the travel). Interpolation between keys: nlerp along the shortest arc (the engine's own choice was not checked). Decoded: 2512 of 2512 ADB clips in all 19 MOD/ADB pairs; every
  listed clip fits its model's joint count and poses to finite, bounded vertices except 7 of 30984 samples of two huge-travel clips (root
  motion over several model units, not an error). Code: `formats/src/anim.ts` (`parseClip`, `sampleClip`, `rootMotion`, `clipTime`).
* **Class to clips**: Rynn's class lists 219 clips in named fields (`Walk`, `Run`, `Base Idle`, ...) and the model lists 499; the Details
  "Animation" section offers the class fields first and then every clip of the model, idle by default; the animation picker previews
  the highlighted clip on the owning class's model (a clip whose track count differs from the model's joint count is reported instead of
  played; a class without a model borrows the first skinned model of the clip's container with a matching joint count).

## 13. String tables (XDB) and quest classes (System.ODB)

Proven on the PAL disc (all 113 System strings and all 190 System.ODB class records re-encode byte for byte; in game:
an edited journal sentence and a new quest show in the native journal, `docs/journal/2026-10-31/story-editing.md`).

**XDB 0x0400 string entry.** SRSC `id` is the string id, `sub` a group number (named by 0x0330 entries). Body: eight
slots in language order (English, German, French, Italian, Spanish, three unused), each `u16 n` + `n` bytes, with
`n >= chars` and `(2 + n) % 4 == 0`; Windows-1252, NUL padded. The runtime serves a replacement entry verbatim, so an edited
string is the re-encoded entry. An empty non-English slot shows the English text. Characters outside Windows-1252 cannot be
stored.

**Class record 0x0102 / list 0x0103.** Record = `u16 n` + name area (same size rule as the XDB slots: `(2 + n) % 4 == 0`,
no NUL needed, proven on all 190 System records) + body. The body ends in the class data area (a u32 per field, count in the
body header). Quest fields (indices differ per class list, find them by name): `Type` 0 map marker / 1 journal / 2 both,
`SubQuest?`, `Journal Entry` and `Map Text` (string ids into System.XDB, 0 = none), `New Quest Sound`, `Indicator Color`,
`Map X Coordinate`, `Map Y Coordinate` (0..~370 seen; plain ints). 0x0103 is one zlib stream of lists, one per 0x0102
record in index order: `u32 count`, then per field `u32 type, u16 len, name` with `len >= chars`, `len % 4 == 2`, NUL padded
(all 3000+ names on the disc follow this). A class added with `new/` is last in the index, so its list is appended to the
0x0103 entry (an override of that entry); the new class showed up in the journal with that append (a run without the
append was not tried). **[proven in game]** a new quest object (class 4000) in Surdana, started by a `volume-message` template with
message 1 and completed by one with message 18 (both linked to the object), shows in the native journal under Optional Quests with
a ticked box, so 1 starts and 18 completes. A `0102` override alone for an existing quest class (map X/Y) did not move the map dot.

**Where a quest is placed and triggered (levels).** A quest class is placed as an ordinary object whose db index is System.ODB's
and whose class id is the quest class (e.g. LEVEL1NA: 21 quest objects). Other objects list it in their link table:
class 128 timers and `Trigger Detector` (field `Trigger Message`, 1 or 18), and quest givers (LEVEL1ON class 445, fields
`Beginning / Incomplete / Complete / Done Dialog Exchange` id arrays, `Message to send after ... dialog`, `Gold to give when
complete`). *Inferred, not decoded in the engine:* message 1 starts a quest and 18 completes it (18 accompanies the completion
dialogs; the done flag itself is set by quest message 0x116e, so 18 is probably that message's id in the link protocol).

## 14. Cut scenes, conversations and sequences (2026-10-31)

Proven = checked on the whole PAL disc (all English `.SSD` / `.SDB`, the 14 level dumps of `build/lvl-objects`); inferred = read off
the data only. Tools: `editor/packages/formats/src/story/*`, panels `app/src/story/dialogue.ts` and `cinematics.ts`. Journal:
`docs/journal/2026-10-31/story-editing.md`.

### Who plays what

* **Stomp Player** = runtime class `0x10033` ("Stomp Player", 18 in Surdana; in Levels 3, 6 and 8 the players carry the scene's
  name). Class fields (LEVEL1ON ODB class 129): `Sequence List` (array, type `0x100b`), `List Play Order` (enum), `Initial State`
  (enum), `Message To Play Next` (enum, default 4). A placed player overrides `Sequence List` (`[1354]`, `[421, 1022]`) and
  `Initial State`. **[proven]** each list element is the SRSC id of a `0x0311` record in `<level>.SSD` (84 of 84 over the 14
  dumped levels). **[inferred]** `Initial State` 1 starts the scene at level load, 0 waits for a message; message 4 (`Play
  Sequence` in `logic.ts`) starts it.
* **Conversation Handler** = runtime class `0x100b4` (6 classes in LEVEL1ON, 52 placed in Surdana). Fields: five arrays of dialog
  line classes, one per phase: `Beginning Dialog Exchange`, `Incomplete Dialog Eschange`, `Complete Dialogs Eschange`, `Completed
  w/o beginning Dialogs Eschange`, `Done Dialogs Exchange` (type `0x3003`; each has an `Interrupt ...` sibling of type `0x2003`),
  five `Message to send after ... dialog` enums, item arrays (`Items to get from / give to Rynn when beginning / complete`,
  `0x1003`), `Complete if Rynn has Items to get ?`, `Gold to give when complete`, `Initially active?`, `Distance From Rynn` (f32),
  `Is there a quest?`, `Done Dialog Random or Sequential`. **[proven]** placed instances hold the phase arrays as class ids of the
  level ODB: 538 of the 585 class references of placed handlers are dialog lines (runtime class `0x100ba`). **[inferred]** which
  phase plays when (quest state), message 1 = start and 18 = complete.
* **Dialog line** = runtime class `0x100ba`, a class of the level ODB named `Speaker - SOUNDNAME` (`Farmer - C103FA01`) or
  generically (`Greeting`, `Item Sold to Player Dialogs`; 232 in LEVEL1ON). Fields: `Animation` (array `0x1009`), `Delay` (f32),
  `Maintain special head animation during anim?`, `Sound` (array `0x1005`), `Sound Play Mode`, `Debug Text`, `Debug delay (s)`,
  `Camera Position`, `Camera Transition`, camera distances and `Time to Transition Camera`. **[proven]** `Sound[0]` is
  `(db << 16) | id`, db 0 = the level's own SDB: it resolves to a descriptor named like the class suffix (`C103FA01`) for 229 of
  232 lines, and that descriptor's subtitle is the line's text. `Animation` is the ADB id of the talking animation (3009 for NPCs,
  1526 Rynn). **[inferred]** `Sound Play Mode` 1 = several clips in a row (`C103FA03-A`), 0 = one; the camera enums (1 Rynn, 5
  speaker) and what `Delay` does.

Editing: a conversation line is only class data plus sound descriptors, so a mod changes the subtitle (`<LEVEL>.SDB` 0x0302
override), the played sound, the animation and camera fields (patch of the 0x0102 class record in `<LEVEL>.ODB`) and the phase
arrays of a handler class or placed handler (class record, or the `.LVL` object parameters).

### Sound descriptor write-back (SDB 0x0302)

Layout in [subtitles.md](subtitles.md) 3.1. **[proven]** (3398 of 3398 descriptors of the English tree rewrite byte for byte
with `writeSdbText`): at +32 after the name there is either `u16 len + text` (flag `0x2000` set; an empty text is `len 2` plus
two NULs, which is also what `u32 2` of a silent descriptor looks like) and then always 12 tail bytes. The text area is padded so
the descriptor ends 4-aligned; a NUL terminator is optional when the text fills the area exactly. Timed pages: `\<seconds>`
between pages, the number is the delay before the next page.

### Starting a conversation, and the in-game proof (2026-11-01)

**How it starts [proven in game, decoded in the PAL ELF].** Use is pad L1 / key E (command `0x4009`). Press calls `FUN_003168a0(player, 1)`
and only arms a 0.5 s timer (player `+0xb5c`); the release calls `FUN_003168a0(player, 0)`, which does the use **only while that
timer is still >= 0** (game time). A held key (the old harness held E for 20-30 frames at 5 fps) never does anything: tap it
(`key down E` then `key up E` 2 frames later). Past the climb / pick-up tests it calls `FUN_00321180`, which takes the nearest
class `0x1009d` NPC of the player's candidate list (`player +0xe40`) and sends it message **`0x116d`** (`FUN_00321200`).
The NPC (`FUN_00464200`, message `0x116d`) asks its talk gate `FUN_0046b368` / `FUN_00469458` and, if it passes, forwards `0x116d`
to its conversation handler, whose handler method `FUN_002442c8` calls `FUN_002447c0` (begin: loads the phase lines, sends
`0x1065` "conversation started", camera, speech). The gate passes only when all of these hold:

* the NPC is not busy (vslot `+0x174` of its AI sub-object is 0);
* the distance NPC to Rynn is at most `Maximum conversation distance (lu)` x 2048 units (class default 1.0 lu = 2048; field 0x110);
* Rynn faces the NPC: `forward(Rynn) . dir(Rynn -> NPC) > 0.4`; and the NPC faces Rynn: `forward(NPC) . dir(NPC -> Rynn) > 0.2`
  (forward = minus the third matrix column; the matrix is a pure Y rotation `[c 0 s; 0 1 0; -s 0 c]`, forward = `(-s, 0, -c)`);
* the NPC's accept-Use field `Accept Use button?` = Yes (the farmer has it), and the player is not busy (`player +0x94 & 0x104874`).

`Proximity dialog handlers` of an NPC only preload sounds of the phase (`FUN_002443e8`), they do not start speech. A conversation
handler also accepts `0x1001` (On: active) and `0x1002` (Off) and `0x116e` (quest complete: phase state), `0x116f` (end).
The handler **does** need its NPC as the sender: a `Start Conversation` volume linked to a handler (sender = the volume) did
nothing in the earlier attempts; the working path is the NPC, whose own link list names its handler.

**Recipe (harness `scripts/harness/story_dialogue.py`).** Surdana farmer = NPC #5102 `(613788, 152432, 433259)`, forward
`(0.342, 0, 0.940)`, handler #7806. `DRAKAN_PLAYTEST_POS="613993 152400 433823 0.349"` puts Rynn 600 units in front of him facing him
(yaw 0.349 = `atan2(-dx, -dz)` of the direction to the farmer); a 2-frame tap of E at frame 400 starts the conversation. The
lines (`Beginning Dialog Exchange` 876, 875, 883, 878) play one after the other, the subtitle bar showing each sound's SDB text.
`DRAKAN_TRACE_FN=4149a0` logs every `SetSubtitle` text as `[subtitle-set] speaker=.. text=..`.

**Proven** (`build/harness/story-dialogue-*`; real disc, packs built by the editor code): the stock conversation shows
"Please, miss, can you help me?" then Rynn's "Just calm down."; with the Story pack the **edited subtitle** (SDB 0x0302 override of
sound 721), a **swapped sound** (the ODB 0x0102 patch of Rynn's line 875: sound 153 -> 33; the bar shows sound 33's text and
the game requests `0304/33`, not `0304/153`) and a **new dialogue line** (new class in the level ODB + 0x0103 list, inserted first
in the placed handler's Beginning array through the `.LVL` object override, sound 34 with a new subtitle) all show / play in
the running conversation. Sound requests of a line are issued when the line starts, so `LEVEL1ON.SDB 0304/<id>` in the HLE log
(`DRAKAN_HLE_LOG=100000`) is a valid "this sound was played" signal for lines (the level-load preloads only cover 720 / 721).

### Sequence record (SSD 0x0311)

SRSC id = sequence id, `sub` = id of the `0x0312` folder record that lists it (`u16 length + name`, `sub` = parent folder).

```
u16 nameLen, name                     "1.01 Jade, Desert Lord Spy"    [proven]
u32 header word (20, 21, 60, 62, 124 or 126) [proven present on all 202; NOT the duration: 126 on 92, 60 on 56, 21 on 31]
u32 0
u32 slots    (highest actor slot + 1) [proven on all records]
u32 actorCount                        [proven]
actorCount x actor:
    u16 nameLen, name                 "Camera", "Rynn", "Jade", "Running man"
    u32 slot (1-based, ascending), u32 ref, u32 mode (0 on most, 1 and 7 seen), u32 actionCount
    actionCount x action
```

`ref` is the level object id for props and NPCs (for example 7726). **[proven, census 2026-11-01]** `Rynn` and `Camera` actors carry the level's own object ids of the Rynn start and Camera objects, different in every level (Surdana 908 / 910, others 3 / 4, 359 / 360, 1 / 2, 2189 / 2191, 973 / 7896, 3585 / 10); a new sequence must copy the `Camera` ref from the level's other sequences (`ContentSession.cameraRef`).
Names are not always NUL terminated (`Rynn` is `len 4`), so a rewrite keeps the stored length. **[proven]** All 202 sequences /
1264 actors of the 14 English files parse and rewrite byte for byte; every actor's `actionCount` actions consume exactly its
bytes (10041 actions, times ascending inside each actor).

Action = `u16 type, f32 time (seconds)` + payload:

| type | name | size | payload | status |
| --- | --- | --- | --- | --- |
| 0 | transform keyframe | 54 | quaternion xyzw (4 f32), position (3 f32, game units), 8 bytes (zero or `0x10`), scale (3 f32 ~1.0) | **proven** layout (4516 keyframes: unit quaternion in 4485, position inside the level); interpolation inferred |
| 1 | animation | 30 | u32 mode (0, 4, 7), u32 ADB id, u32 0, f32 offset, f32 speed (mostly 1.0, 0.85 for crafting loops), u32 flags (0x10 0x15 0x3f 0x55) | sizes proven; field names inferred |
| 3 | sound | 14 | u32 `(db << 16) or id` of a sound, u32 params (`0x82710` on 809 of the sound actions, `0x382710` 524, `0x182710` 62; low 16 bits 10000) | **proven**: db 0 ids resolve to the level SDB and the subtitle matches the scene (`101JA04-A` ...); db 3 / 4 are SYSTEM / RESOURCES of the `.DB` list; params inferred |
| 4 | unnamed | 14 | u32 a, u32 b | size proven, meaning unknown (b equals the `flags` of a nearby animation) |
| 5 | marker | 10 | u32 0 or 1 | **proven** (census of all 202 sequences, 2026-11-01): value 0 opens an actor (540 times, first action), value 1 closes it (419 times, the last action of 66 of 97 camera actors and 264 other actors) |
| 6 | unnamed | 10 | u32 0 / 1 | size proven |
| 7 | message | 14 | u32 message number, u32 param | size proven; 4097 (256x), 4259, 4263, 4098, 4162, 4257 ... Inferred: `0x1000 + n` is the object message enum of `logic.ts` (4097 = On), 4257 = Play Sequence (OotF list) |
| 8 | unnamed | 34 | u32, u32, 4 x f32, u32 1 | size proven (9 on the disc, camera actors) |
| 9 | camera setup | 14 | f32 1.0, u32 (40, 50, 55) | size proven, first real action of camera actors; the u32 is the field of view in degrees (proven 2026-11-08: the scene tick `0x1a68d8` hands it, interpolated between keys, to the render object's FOV setter, vtable `+0x54` of `*(0x5565c8)+0x124`; the f32 goes to `0x1435a0`) |
| 10 | hold | 6 | nothing | size proven; always followed by a type 0 or 9 at the same time |

The camera is the actor named `Camera` (type 9 and 5, then keyframes, sounds and messages). The same list carries Rynn, NPCs, props,
particle effects (`Jade FireBall`), fades (`DL T Fader`) and light flashes, each with its own transforms, animations and
messages (message targets are not decoded; `param` is not a level object id, inferred).

**Skipping a scene (proven 2026-11-13, PAL code, `docs/journal/2026-11-13/ootf-book-skip-and-reload.md`).** Bit `0x40` of the
header word makes the scene skippable: `0x1a3458` marks every running scene whose sequence has it (and whose instance
has no `0x2000` at `+8`) with `0x40` on the instance (`0xc0` to the end). The scene tick `0x1a4c50` then stops the scene's
sounds, moves its clock to the next hold (type 10) or to its last action, and runs every action up to there in order,
messages included. So a skip ends a scene the way watching it does; only what is timed outside the scene (a level Timer
of the scene's length) is not shortened. Without the bit, Start opens the pause menu while the scene waits (seen for the
OotF church walk-down). The PC sequence flags carry the same bit (Order of the Flame: 43 of 110 sequences).

Not decoded: what types 4, 6 and 8 mean, the interpolation between keyframes, what `mode` and `ref` select for props, how
messages address objects, the units of `length`. A rewrite keeps all of that opaque.

### New story content from the editor (2026-11-01)

Panels `app/src/story/compose.ts` (Story > New dialogue, New cut scene, Quest marker), logic `editor-core/src/story-create.ts`
(`buildCutscene`, `planDialogueLine`, `wireQuestGiver`), tests `editor-core/test/story-create.test.ts`, `app/test/story-content.test.ts`.

* **New cut scene** is written as `new/<LEVEL>.SSD/0311_<id>_<folder>.bin`: a `Camera` actor (marker 0 at t=0, one type 0 key per
  point, sound cues, marker 1 at the end) and one actor per acted-on object (marker 0, animations and messages in time order,
  marker 1). Header word 60, `slots` = highest slot + 1, name length padded so `2 + len` is a multiple of 4 (disc rule). The
  shape follows the census above. **[proven in game, 2026-11-01]** a new `new/<LEVEL>.SSD/0311_<id>_<folder>.bin` sequence is read
  (`resource: ...SSD 0311/1411`), a Trigger Detector + Stomp Player pair from the `cutscene-trigger` template starts it, the camera
  follows the editor's keys and control returns at the end (`scripts/harness/story_cinematic.py`). Key orientation: see
  "Camera key orientation" below.
* **New dialogue line** = new class in `<LEVEL>.ODB` (copy of an existing line, name `Speaker - SOUND`, `Sound[0]` replaced) with the
  0x0103 field list extended, the subtitle on the sound's `0302` descriptor (shared by every user of that sound), and the line id
  appended to a phase array. A class edit only reaches handlers without their own array: placed handler objects (Surdana's farmer
  #7806 holds `[876, 875, 883, 878]`) keep an override in the `.LVL`, so the panel can add the id to a placed object instead.
  **[game]** a pack with such a class (`new/LEVEL1ON.ODB/0102_2_172.bin` plus the list) and the edited `.LVL` loads: the ODB is rebuilt
  (110948 -> 111146 bytes) and the level runs. That the line is spoken was **not seen** (no conversation could be started).
  `freeClassId` falls back to id 2 when the ODB's highest id is below 4000; ids inside the file's range are used because ids above it did not spawn.
* **Quest marker** uses the `quest-object` template (proven path, section 13) with the new class id; a quest giver is wired by
  setting `Is there a quest?`, `Message to send after Beginning dialog` = 31 (Quest Initiate) and `... Complete dialog` = 18 and linking
  the handler to the quest object (**inferred**, not run).

### Camera key orientation and cinematic edits (proven 2026-11-01)

* **Convention.** The key quaternion is a row-vector rotation and the camera looks along its local -Z: forward = `R(q)^-1 * (0,0,-1)`
  (textbook `R`). For `q = qpitch(X) * qyaw(Y)` that is `f = (cos p sin y, -sin p, -cos p cos y)`: yaw 0 looks along -Z, 90 along +X,
  pitch > 0 looks down. Evidence: over 1553 camera keys of all 202 disc sequences this model puts the view a median 22.8 degrees from the
  nearest scene actor (56 % within 30 degrees); the 11 other axis / handedness combinations give 80 to 160 degrees (0 to 14 % within 30).
  Valley sequence 497 at 12.13 s (yaw about -117 degrees) looks toward -X with Zola Dane between the two guards. In game, static
  cameras with pitch +25 / -25 look down / up and yaw steps move the view consistently (`build/harness/cine-single-*`). The earlier
  editor formula (`qyaw * qpitch`, forward +Z) was wrong and is replaced (`yawPitchQuat`, `lookAtAngles`, `quatYawPitch`, `lookDirection`).
* **Interpolation.** Position is interpolated linearly between keys (a camera raised 30000 units at key 0 drifts to key 1 over 6.5 s);
  two keys 0.01 s apart make a cut. Orientation is interpolated too.
* **HUD / camera setup.** The Valley entry scene (player 7577, `Initial State` 1, plays at load) hides the HUD and shows subtitles for its
  sound actions; a new scene without a type 9 camera action keeps the HUD. Type 9's second value (`CutsceneSpec.camSetup`) changes the view
  angle (40 zooms in strongly versus none), so it is a field of view. What hides the HUD is the player being an actor of a running
  scene, not the camera action (2026-11-08): converted Order of the Flame Intro scenes with a camera and a type 9 drew the health and
  mana orbs throughout, and adding the level's player as an actor with one run/stop action hid them
  ([journal](../journal/2026-11-08/ootf-intro-hud-and-owl-camera.md)). A scene that holds the player can also be skipped with
  Start / Cross, as the disc Intro's is.
* **Edits that changed the game picture:** camera key position (y + 30000), camera orientation (all keys yawed 90 degrees), camera
  retime (x2.5), animation id swap; asserted by `story_cinematic.py` (phases stock / edit / new).
* The Rynn start object is not where Rynn stands at runtime; use `DRAKAN_PLAYTEST_POS` to put her somewhere.

### Mod support

Sequence, descriptor and class edits ship as entry overrides (`overrides/<LEVEL>.SSD/0311_<id>_<sub>.bin`,
`overrides/<LEVEL>.SDB/0302_<id>_<sub>.bin`, `overrides/<LEVEL>.ODB/0102_<id>_<sub>.bin`); see [modding.md](modding.md). In game
(2026-10-31, `story-closeout-a.md`): an `0311` override of Surdana's sequence 484 (replaced by an empty sequence) removes the cut
scene, so SSD overrides are read; an SDB subtitle override loads but was not seen on screen (cut scene sound actions show no
subtitle bar in the stock game either; conversations drive it). A camera keyframe edit on that sequence changed nothing visible because sequence 484 is a conversation scene shown through the gameplay camera; cinematic scenes (Valley 497) use the Camera actor and do respond to edits (below).

## 15. Level logic: links, messages, triggers

Code: `formats/src/logic.ts` (vocabulary), `editor-core/src/{logic,logic-templates}.ts` (graph, layout, volumes, validation,
templates), `app/src/editor/{logicpanel,logicview}.ts` (panel and viewport). Statistics: `analysis/scripts/logic_graph.py` over
the 14 PAL LVLs (10453 links, 0 dangling). Tags: **[elf]** read from the engine's property tables (string pool of the PAL
executable, `analysis/strings_vaddr.csv`), **[disc]** statistics over the level data, **[dec]** decompile, **[inf]** inference.
Journal with the evidence: `docs/journal/2026-10-31/level-logic.md`.

### 15.1 The model

Every object has a **link list** (section 2: ids of other objects). An object sends the message stored in one of its *message
fields* to **every** object in its link list when its own condition happens; it does not choose a target per message. So the
wiring of a level is a directed graph (object -> linked objects) and the message of an edge is a property of the *source*:
change the source's field and every edge out of it changes. Targets decide by class what a message does.

Message fields are enum fields (type 7) whose name says "message" (`Trigger Message`, `Message To Send`, `Msg Sent When
Opened`, ...; not the yes/no questions such as `Can be Triggered by Message ?`). Their values index one global list **[elf]**
that sits in the string pool after the generator's "Activate Message" property:

| Values | Names |
| --- | --- |
| 0-8 | Off, On, Lock, Unlock, Play Sequence, Blow Up, Triggered, Move to WayPoint, I'm Dead |
| 9-17 | Gib, Attack Linked AI, Follow, Make Vulnerable, Make Invincible, Stop Speaking, Stop Following, Play Next Dialog, Alert AI |
| 18-26 | Quest Complete, Positive, Negative, Neutral, Enrage, Load, Shoot, Random, Cyclic |
| 27-32 | Simultaneous, Start Conversation, Attack(Stomp), Quest Initiate, Item PickedUp, `<none>` |

The ordinals are checked against the links on the disc **[disc]**: value 4 (Play Sequence) from detectors and timers lands on cut
scene players, 23 (Load) also on cut scene players, 28 and 18 on conversation handlers, 7 on creatures, 3 and 2 on doors, 8 is
the creature default and the counters' decrement default. Values such as 14, 15 and 22 are class defaults for "never used"
slots (elevator Activate/Deactivate, timer Reset), i.e. a message nobody sends. The engine message ids are read as `u16` at `*(msg+4)` by the quest handlers; class handlers are `handler(handle, event, ...)` and
event 20 is a message, with `a3` pointing at the message whose first `u16` is the engine id **[dec + game, 2026-11-01]**.

**Ordinal -> engine id table [dec + game].** The registration code at `0x2cd648` fills a 33 entry table of 8 byte slots at `0x4fbc40`
(u16 id at +0), indexed by the ordinal. Runtime proof (DRAKAN_TRACE_FN on the class handlers, Stratos): ordinal 1 (On) arrives as
`0x1001` at counter, timer and door, ordinal 0 (Off) as `0x1002` at the visibility toggler, ordinal 4 (Play Sequence) as `0x10a1` at the
cut scene player. These three match the table slots; the earlier guess `0x1000 + n` was wrong for 0 and 4. The other slots are read from
the table, not yet seen at run time except 18 (see below):

| Ordinal | Name | Engine id | Ordinal | Name | Engine id |
| --- | --- | --- | --- | --- | --- |
| 0 | Off | 0x1002 (game) | 17 | Alert AI | 0x116c |
| 1 | On | 0x1001 (game) | 18 | Quest Complete | 0x116e |
| 2 | Lock | 0x1101 | 19 | Positive | 0x1170 |
| 3 | Unlock | 0x1102 | 20 | Negative | 0x1171 |
| 4 | Play Sequence | 0x10a1 (game) | 21 | Neutral | 0x1172 |
| 5 | Blow Up | 0x10a3 | 22 | Enrage | 0x1173 |
| 6 | Triggered | 0x1007 | 23 | Load | 0x1174 |
| 7 | Move to WayPoint | 0x1161 | 24 | Shoot | 0x1175 |
| 8 | I'm Dead | 0x1003 | 25 | Random | 0x1181 |
| 9 | Gib | 0x10d9 | 26 | Cyclic | 0x1182 |
| 10 | Attack Linked AI | 0x1164 | 27 | Simultaneous | 0x1183 |
| 11 | Follow | 0x1166 | 28 | Start Conversation | 0x116d |
| 12 | Make Vulnerable | 0x1167 | 29 | Attack(Stomp) | 0x10a7 |
| 13 | Make Invincible | 0x1168 | 30 | Quest Initiate | 0x1176 |
| 14 | Stop Speaking | 0x1169 | 31 | Item PickedUp | 0x1004 |
| 15 | Stop Following | 0x116a | 32 | `<none>` | 0 |
| 16 | Play Next Dialog | 0x116b | | | |

Slot 18 = `0x116e` agrees with the quest handler (`0x393258`, `0x116e` sets the done flag); slot 8 = `0x1003` with the counter's default
Decrement message. The editor's ordinal names are right; only the engine ids differ from the earlier inference. The in-game chain
pressure plate -> counter -> door was observed at handler level (counter and door both received `0x1001`, `scripts/harness/story_puzzles.py`);
the door's visible opening was not captured (Rynn stood inside the huge Necropolis door model in the shots), and lever, timer and key lock
were not play-tested.
* **Ordinal -> engine id table found (2026-11-01, `FUN_002cd648`, written to `0x4fbd48` as `u16[33]`, index = ordinal, 0 ends).**
  Two independent readings disagree on where the table lives and its stride (`0x4fbc40` with 8 byte slots above, `0x4fbd48` as
  packed `u16` here); the ordinal -> id values agree, and the editor only needs the values. Re-check the location before relying on it.
  `0:0x1002 Off`, `1:0x1001 On`, `2:0x1101 Lock`, `3:0x1102 Unlock`, `4:0x10a1 Play Sequence`, `5:0x10a3 Blow Up`, `6:0x1007 Triggered`,
  `7:0x1161 Move to WayPoint`, `8:0x1003 I'm Dead`, `9:0x10d9 Gib`, `10:0x1164`, `11:0x1166`, `12:0x1167`, `13:0x1168`, `14:0x1169`,
  `15:0x116a`, `16:0x116b`, `17:0x116c`, `18:0x116e Quest Complete`, `19:0x1170`, `20..24:0x1171..0x1175`, `25:0x1181`, `26:0x1182`,
  `27:0x1183`, `28:0x116d Start Conversation`, `29:0x10a7`, `30:0x1176 Quest Initiate`, `31:0x1004`. So the earlier "0x1000 + n for
  0..7" inference was wrong for Off / On (swapped: Off = 0x1002, On = 0x1001) and for 2..7; the proven handler facts fit the table
  (conversation handler: 0x1001 active, 0x1002 inactive, 0x116d start, 0x116e quest done). The editor's ordinal list is unchanged and
  consistent with it. The ordinal names of 10..17 and 19..27 remain unverified beyond their table position.
* Door On/Off: decoded and proven in game 2026-11-07 (`docs/journal/2026-11-07/door-proof.md`): handler `0x45ba20`, `0x1001` opens an unlocked door, `0x1002` closes, `0x1101` locks, `0x1102` unlocks only; a locked door ignores On.
* Detector volume: not decompiled; the box (model collision with the LVL scale) remains the editor's model.

Other enum names found in the same tables **[elf]**: Trigger Detector Task (Transfer Link, Enable/Disable Saving, Message
Screen, No-Fly Zone, Play Sequence, Wait For Data, Trigger Only (default), Cave Entrance, Message, Teleport Dragon, Steal
Inventory), Detect Which? (Rynn On Ground, Rynn On Mount, Both (default), Rynn or Mount or NPC, Pushable, Only Linked NPCs, Mount
Only), Detect Method (Outside To Inside, Inside To Outside), Initial State (Enabled, Disabled); Timer Start Mode (Run Instantly, Run
When Triggered By Any Message, Run When Triggered By Trigger On Message); cut scene List Play Order (In Order Once, In Order Loop,
One Scene Per Trigger And Looped, Randomly, All at Once) and Initial State (Triggered, On); Vis Toggler Trigger Mode (Toggle
Visibility, Depends on Message). The full table is `FIELD_ENUMS` in `logic.ts`.

### 15.2 Classes (runtime class = rfl id + 0x10000)

| Class | Name in data | Sends (to its links) | Listens / accepts | Notes |
| --- | --- | --- | --- | --- |
| `0x1003d` | Trigger Detector | `Trigger Message` (default 5 in FX, 1 in SYSTEM) when the player (Detect Which) enters; every exit detector on the disc sends 1 (ON), the only message the exit's touch handler `0x361440` queues a level change on (2026-11-06 `ootf-progression.md`) | Initial State Enabled/Disabled; re-enabled by a message when "Can be Triggered by Message?" | a **box volume**: the model is a unit cube (bounds -0.5..0.5, one model unit = 2048 units), the LVL scale triple (flag 0x100) is the size in cells, rotation applies; 1177 placed, 899 with scale and flag 0x101 **[disc]** |
| `0x1003e` | Timer | `Trigger Message` (default 1) after `Time Until Trigger` s | Start Mode 0 instantly, 1 on any message, 2 on `Trigger On Message`; `Disable/Re-Enable`, `Reset` messages | the most common link source: 1278 placed |
| `0x10007` | Countdown to Zero | `Message To Send` (default 1) at zero | `Decrement` (default 8 = I'm Dead), `Increment`, `Reset`; `Initial Counter Value` | "kill every enemy" gate: creatures link to it, it links to the gate (202 creature -> counter links) **[disc]** |
| `0x100e0` | Message Relay | `Outgoing Message` | `Incoming Message` | translates one message into another |
| `0x1000d` | Lever / Button / Elevator Switch | `Message to send when on` (1) / `when off` (0) | `Activate/Deactivate Switch Message` | targets: doors, mechanisms, elevator handlers, timers |
| `0x10013` | Door / gate | `Msg Sent When Opened / Closed` | Off, On, Lock (2), Unlock (3) **[disc]** | `Locked`, `Auto-Open?` (opens by itself near the player), distances in level units |
| `0x10033` | Stomp Player = **cut scene player** | none | Off stops, On arms (an armed player steps by itself), Blow Up deletes, Load preloads; any other message equal to `Message To Play Next` (default 4) plays a step and arms it except in order 2 | `Sequence List` (SSD sequence ids), `List Play Order` (0 Once: rest back to back; 1 Loop: replays for good; 2 One Scene Per Trigger; 3 Randomly; 4 All at Once; the PC enum lacks 2, journal 2026-11-06 `ootf-stomp-play-order.md`), `Initial State` Triggered or On (On arms it at level load) |
| `0x10016` | Movie Player | `Message To Send When Done`, `... 1 second left` | | `Movie to Play`, Skippable; only the intro level has one |
| `0x100b4` | Conversation Handler | five `Message to send after <state> dialog` | Start Conversation (28), Quest Complete (18), Quest Initiate (30) | quest giver: dialog arrays, items, gold, `Initially active?` |
| `0x10037` | Level Transition / Portal | `Send This Message` | player touch | section 15.4 |
| `0x100e1` | Elevator Handler | `Send Elevator Up/Down Outgoing Message` and the switch light messages | `Go Up/Down Incoming Message`, `Player on Elevator`, Activate / Deactivate | drives lift mechanisms (`0x10023`) |
| `0x1001d` | Pressure plate | `Message To Send At Bottom / Top` | | sinks under the player; only some levels' ODBs have the class |
| `0x100df` / `0x10022` | Combo lock / Lock Plate | `Message to Send on Success/Failure` / `Trigger To Send` | Message List / key item code | the player's Use handler `0x3168a0` opens a Key Lock only for the nearest usable object of the touch list within 1,024 units horizontally, and only with a Key (`0x10021`) in the inventory whose field 22 (Lock Code) matches; other item classes (the 117 egg) are never matched (2026-11-06 `ootf-islands-bell.md`) |
| `0x10079` | Vis Toggler | none | `Show Message`, `Hide Message` | shows/hides all linked props (905 links to scenery) |
| `0x10031` and other creatures (`0x10029`, `0x10028`, `0x1001e`, `0x1009d`, `0x100ad`, ...) | NPCs | `Message To Send` (default 8, I'm Dead) when dead | 7 Move to WayPoint, 5 Blow Up | the field exists twice in some classes (index 2 and 29) |
| `0x10070` | Waypoint | `Message to send` | | links join the waypoints of one patrol: 1317 waypoint -> waypoint links, not wiring |
| `0x10068` / `0x100dc` | Journal entry / quest marker (`Level Number`, `Handler ID`) | | Quest Initiate / Complete | timers link to journal entries (122) |
| `0x100c5` | Chest | | | links to the gold it spews |
| `0x10006` | Looping / Triggered Sound, music | `Message To Send when Sound Ends` | On / Off | targets of detectors (285) and ambient transitions (255) |

### 15.3 Trigger volumes and radii

A detector's volume is its model box (15.2); `LogicClass.volume` marks the class and `volumesOf` returns the box (half extents =
scale x 1024, Euler XYZ rotation of the document). Resizing a face keeps the opposite face fixed (position and scale both
change, `resizeBoxFace`). The LVL "radius word" (flag 0x10, section 2) is not a detector size (only one detector has one,
20480.0); it stays unexplained. Distance fields in level units (cells of 2048 units, **[inf]** from the class defaults and the
"(lu)" in their names): door `Auto-Open Distance (lu)`, conversation `Distance From Rynn`; the editor draws them as circles and
edits the field. In the editor the drawn box coincides with the rendered trigger model.

### 15.4 Level exits

An exit's destination is the class default of field 0 (`Transition To...`, a string field): its dword has length 0 and the
offset half holds an **XDB string id** of the level's own XDB (`LEVEL1ON.XDB` for level 1: 5 = Shadowmire path, 12 = Surdana, 17 =
`portal`). The text is a level name or `Levels\<name>\<name>.lvl`. So there is one exit class per destination ("Level Transition
from 1 to 2"); an LVL object can override the string. Field 1 "Transition To 'portal' object" selects the arrival marker
(`portal`), otherwise the arrival is the exit of the destination level whose destination text is this level's name
([world-model.md](world-model.md) section 10: no marker = the player falls). The editor checks exactly that across all levels
(`exitArrivalProblem`, worker call `exitIndex`).

### 15.5 Authoring templates

`TEMPLATES` in `logic-templates.ts`, one undo step each (`ApplyTemplateCommand`): trigger volume plays cut scene, lever opens door
(open/close or unlock/lock), kill all enemies opens gate (counter), pressure plates puzzle (needs a plate class in the level's
ODBs), trigger volume sends message, level exit. New objects copy the body of an existing instance of the class (so a detector
keeps flag 0x100 and can be sized) and override only the fields the template sets; a class with no instance in the level gets a
generic default body (flagged by Validation, unverified).

Pressure plates: only LEVEL8OS (Stratos) has the class (`Necro Pressure Plate`, rfl 0x1d, two placed plates). Its fields are `Sink
Distance (wu)` (100), `Sink / Rise Speed`, `When Triggered` (Rise, Enable, Both), `Send Message When` (Reached Top, Reached
Bottom, Both, Never; **default 0 = top**, the placed plates use 1), `Message To Send At Top / Bottom`, `Triggered by Rynn Only?`.
The template therefore sets `Send Message When` = 1, `Sink Distance (wu)` = 50 and lets the counter send Play Sequence to a cut
scene, On to a timer, Unlock otherwise. In game (Stratos, `build/scratch/content2/make_pack_plate.mjs`): the template-made plate and
counter instantiate (world dump shows the 0x1001d actor and 0x10007 counter at the chosen positions), render, and the plate sinks
under Rynn; that the counter then starts the cut scene was **not** proven (the level's opening flash plays in the baseline too, so the
screenshots do not discriminate).

### 15.6 Checks

`validateLogic`: trigger/timer/switch/plate without a link (detectors whose Task is not Trigger Only are exempt), a cut scene player
nothing can start (reachability from objects that act on their own: enabled detectors, instant timers, switches, creatures, cut
scenes set to On), a message the target does not react to (cut scenes: 4, 23 and their own `Message To Play Next`; doors: 0-3),
an exit without a readable destination, an exit whose destination level has no arrival marker. On Surdana: 14 triggers without
target, 1 unreachable cut scene (#15177), 1 ignored message (timer #18128 sends Blow Up to a cut scene).

**Verified in game**: a template-made trigger volume at the player start, linked to Surdana's cut scene #7809, plays that cut
scene at the start of the game (baseline: Rynn stands at the start with the quest text; with the pack the orc ambush sequence
runs and Rynn ends about 50000 units away, at the cut scene's location). Journal: `2026-10-31/level-logic.md`.

## 16. Vendors (shops) (2026-11-01)

Proven = read off the whole PAL disc (the 14 level dumps, all ODBs) or checked in the running game; inferred = from the data only. Editor:
`editor-core/src/vendor.ts`, panel `app/src/editor/vendor.ts`; pack builder `editor/scripts/vendor-pack.ts`; in-game proof
`scripts/harness/vendor_shop.py`. Journal: `docs/journal/2026-11-01/vendor-shops.md`.

**The shop object.** A vendor is an object of the runtime class `0x100ac` (RFL id 172). There is one class per shop, in the level ODB
(`Blacksmith` 118, `Alchemist` 119 and `Mage` 126 in LEVEL1ON; `BlackSmith Store` in LEVEL3ON and LEVEL6EN; `Alt The Smith Store` and `The
Outskirts Store` in LEVEL4ON) and 8 placed instances on the disc: Surdana 3 (blacksmith #1648, Mage #7715, Alchemist #13253), Level 3 (LEVEL3ES) 1,
Ravenshold (LEVEL4LD) 1, Northern Tundra (LEVEL4RA) 1 (it sets no stock, so it sells its class's 4 potions) and the Valley of the Fallen
(LEVEL6EN) 2. The shop object is a logic object with no model; it is
linked to the shopkeeper NPC (the blacksmith NPC `0x1009d` #1650 links back to #1648), and the NPC is what the player talks to.

**Shop to shopkeeper** (proven 2026-11-02, `docs/journal/2026-11-02/add-shop-here.md`). There is no shop field on the character and
no character field on the shop: the tie is the LVL link list. On all 8 disc shops the shop's **first link** is its keeper (a
character of RFL 157, runtime `0x1009d`, NPCS database) and the keeper's **only link** is the shop; other shop links are quest
objects and timers. The engine resolves it at load: the shop object's `+0x44` holds the keeper's handle (`DRAKAN_DEBUG_OPEN_SHOP`
log, also for an editor-added pair). Shopkeepers start **switched off** (hidden or idle): Surdana's keepers each get On / Off from two
trigger boxes in their shop's doorway (`15581`/`15582` Alchemist, `15585`/`15586` Blacksmith, `15823`/`15824` Mage). A keeper that
never gets On stays hidden (a copy of the Alchemist keeper, LVL flags 0) or visible but deaf (a copy of the Mage keeper, flags 1), and
Use next to it does nothing. Once on, pressing Use within the keeper's `Player Trigger Radius (lu)` (1 lu = 2048 units) talks to it
and opens the shop screen. A shop class with `Default On Status` 0 (the smiths) also needs the shop switched on. The editor's
"Add shop here" (`buildNewShop`) writes all three parts: shop -> keeper, keeper -> shop, and a 4 x 2 x 4 cell trigger box around the
keeper sending On every time.

**Class fields** (40 fields, same list in every shop class; index in brackets). Stock:
`Objects For Infinite Sale` [1] and `Objects For Sale` [2], both arrays of class references (type `0x1003`). Elements are
`(db << 16) | id` relative to the shop class's `.DB` list (db 4 = RESOURES for LEVEL1ON, so `0x41ebd` = RESOURES class 7869 "Rusty Short
Sword"; the spell unlockers of the Mage are MAGIC classes). **Infinite** items never run out; **limited** items are one unit per element, so
the same class repeated N times is N units (`BlackSmith Store` in LEVEL6EN lists `269107` twice). Class defaults are merged with the placed
object's override per field: the Surdana blacksmith #1648 overrides only the infinite list (15 items), its limited list (Mithril Short Sword)
comes from the class, so the shop shows 16 items. Prices and rules: `Item Sale Price Mod (0-1)` [4] (what the player pays: item price x
this), `Item Buy Price Mod (0-1)` [5] (what the shop pays for an item the player sells; 0.7 for the smiths and the alchemist, 1.0 for the
Mage), `Durability Price Mod` [6] and `Charges Price Mod` [7] (repair and recharge prices), `Repair Quality` [8], `Repair or recharge?` [9],
`Items Purchased` [3] (what the shop buys back: 1 weapons and armour (blacksmith), 2 potions (alchemist), 0 nothing (Mage), 3 and 4 used by
the Ravenshold and later smiths **[inferred]**; in game the blacksmith refuses the Health Elixir and Jade's refuses a bow),
`Display items in Rows?` [10] (the Mage's spells), `Default On Status` [0], `Distance player must move before reopen` [11], the shopkeeper
camera (`Shopkeer View Distance in Front / to Side / Up`, `...Target Vertical Offset`) [12..15], three sounds (bought / sold / repair) [16..18],
fifteen dialog classes (`Greeting`, `Farewell`, `Item Sold To Player`, `Not Enough Money`, `No Room in Inventory`, `Won't Buy`, `Item
Repaired`, spell dialogs, ... [19..36]), `Chance of Verbose Dialog` [37], `Quest Conversations` [38] and `One Free Sale Message` [39].

**Stock slots.** The shop load (`0x40a128`, then `0x40ab00`) merges identical item classes into one list entry: `stock=` in the
`DRAKAN_DEBUG_OPEN_SHOP` log counts distinct classes, a limited unit of an item that is also unlimited is absorbed, repeated limited
units share one entry. There is no per-class entry limit (5, 17 entries run) and no class refuses an item by kind (an Iron Mace sells
from the new Alchemist, limited or unlimited); proved 2026-11-07, `docs/journal/2026-11-07/shop-stock-limit.md`. `stockSlots` in
`editor-core/src/vendor.ts` mirrors it.

**The price of an item** is not on the shop. Every item class (RFL 14 weapons and items, 45 armour, 79 bows, 144 shields, 178 spell
unlockers, ...) has `Cost to Buy (in Gold)` (an int; index 17 in the 49-field weapon class, 17 in the 25-field unlocker class) and `Quantity`
(units a purchase gives, 1 for weapons). 283 RESOURES classes and 30 MAGIC classes have the field. Surdana examples: Rusty Short Sword 165,
Iron Short Sword 210, Ornate Leather Armor 1250, War Bow 850, Fireball Unlocker 270, Health Potion (Red) 100. So a price edit is an
override of the item class record (`0x0102` in `RESOURES.ODB` / `MAGIC.ODB`, in-place patch of the data dword) and changes the item in
every shop that sells it; a per-shop price is the shop's `Item Sale Price Mod`.

**The shop screen.** Talking to the shopkeeper (`33be50`) opens the inventory screen 0x15 in shop mode: tabs Equipment (the sell pane),
Spells and the shop list. Square buys ("Buy this item?" box, text in INTERFCE.XDB: `Buy Item?`, `Sell Item To Store?`, `Sell All To Store?`,
`Price:`), Circle sells. Details: `docs/journal/2026-10-24/shop-mouse.md`, `docs/reference/mouse-menus.md` section 11.

**Editing.** Stock and modifiers are ordinary LVL object parameters (`applyDocumentToLvl`, one `params` change, nothing else touched); an
item price is a class-record override. `readVendor` / `writeStock` group repeated limited elements into one entry with a count and write
infinite entries once, limited entries `count` times, as references the shop's own `.DB` list can name (an item of a database the shop class
cannot reach is refused, not written wrongly). Test: `editor-core/test/vendor.test.ts`.

## 17. The level map: `Map Object`, Map classes and quest markers (2026-11-08)

The map screen of a level is data: the Level Properties object's `Map Object` (class ref, field 0x5a) names a **Map class** of the
level's own ODB (LEVEL1ON: Surdana, Kragmor, Catacombs, City of the Damned). Fields: `Map Texture` (texture id of the level's TXD,
a 256 x 256 picture; the screen shows it at 4:3), `Upper Left / Lower Right Level Corner X / Z (lu)` (world rectangle, 1 lu =
one terrain cell = 2048 world units) and `Upper Left / Lower Right Map Corner X / Z (pixels)` (the part of the texture it covers).
Rynn's red arrow is placed by that linear map; a quest marker is drawn at its quest class's `Map X / Y Coordinate` (texture
pixels) when the class `Type` is 0 or 2. **[game]** a converted level with a new Map class (cloned, texture appended to
LEVEL1ON.TXD, `Map Object` set) shows its own parchment with the arrow at the calibrated spot. Order of the Flame has no such
class: its parchments are five 256 x 256 tiles of `Dragon.rrc` per level that join into 640 x 480, and the PC calibration is not
in the data (the converter estimates it: terrain box over the lit part of the picture). See `ootfmap.ts` and
`docs/journal/2026-11-08/ootf-level-map.md`.
