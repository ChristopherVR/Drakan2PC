# What we can reuse from Drakan: Order of the Flame knowledge (2026-10-31)

Scope: OpenDrakan (Zalasus) docs and source, tested against our PS2 dumps (`build/lvl-dump/LEVEL0RO`, PAL ISO). Throwaway
parsers lived in the session scratchpad, not the repo. No OotF code, PS2 bytes or disassembly is reproduced here.
Tags: **[V]** verified on PS2 data in this session, **[D]** from the OpenDrakan doc only, **[U]** unverified.
Clone: `build/third_party_study/opendrakan` (gitignored, read-only, HEAD 6c0d499, 2023-12-17; nothing built or run).

## 0. Top of note: what helps the placement decoder

The PS2 `.LVL` and `.ODB` are very close cousins of the OotF layouts. Concrete, verified results:

1. **LVL 0x0000 (level header) parses exactly with the OpenDrakan layout [V].** `string level_name` (u16 length incl. padding),
   `u32 max_width`, `u32 max_height` (256 x 256 for Intro), `u32 db_ref_count` (13), then `{u16 index, u16 0, string path}`
   entries; consumes the 512-byte record to the last byte. The db refs are the **dependency table that class references use**:
   index 1 System, 3 FX, 8 World Common, 11 = the level's own DB, etc.
2. **LVL 0x0001 (layers) definitions match [V].** `u32 layer_count` (19), then per layer `id, width, height, type, origin_x,
   origin_z (u32), world_height (f32 = 200000.0), string name, flags, light_direction (f32), light_ascension (f32),
   light_color, ambient_color (4 bytes), light_dropoff_type, visible_layer_count, visible_layers[]`. All 19 layers parse, the
   25x25 terrain tiles sit on a 25-unit pitch (origins 81/106/131/156 by 73/98/123/148) and the visibility lists reference
   layer indices, as documented. **Difference:** on PS2 the per-layer terrain data follows each definition inline and is not
   zlib (OotF: all definitions, then a `1`, then zlib blocks). Sizes: 4x5 layer = 100 bytes of data, 25x25 about 4.8 KB.
   The PS2 data starts with a u16 pair whose second value equals the face count (1, 12 for 4x5), so it is a different, packed
   layout. Decode it separately (OotF 4-byte vertex / 26-byte face is the wrong stride; the 1/2048 height scale is plausible).
3. **LVL 0x0002 layer groups matches [V]** ("Ungrouped", "Intro Layers", then u32 layer-id lists). Editor-only.
4. **LVL 0x0020 is the object list, and the OotF layout is right in outline [V].**
   - `u16 object_count` (368 in LEVEL0RO), then a **table of 368 x 8-byte `{u32 object_id, u16 class_id, u16 class_db_index}`**
     (ids ascend; db index is 1/2/3/8/11 = the 0x0000 dependency indices; 218 objects are class 1625 from db 8).
   - Then variable-length bodies (22662 bytes). Each body starts with 8 bytes (zero in the first records; probably
     lighting layer id plus one field, OotF has only a u32 lighting layer), then **x, y, z as f32 in 1/2048 world units**
     (x 270761 / 2048 = 132.2, z = 128.4, which lands inside the layer tile that covers x 131-156, z 123-148), then `u32 flags`
     (observed 0x01, 0x101, 0x11; **0x100 = scaled** exactly as documented, 257 of 363 located bodies), `u16 initial_event_count`,
     `u16 link_count`, **linked object ids as u32** (OotF doc guessed u16), `u16 x/y/z rotation` in degrees (190 and 90 seen),
     then three f32 scale values when flag 0x100 is set, then a data section. A float-triple scan found 363 of 368 positions in
     level range.
   - Still open: exact data-section tail (OotF: `data_area_size`, `parameter_count`, values, `u16 indices`, `field_def` list).
     Bodies with parameters embed `field_def` strings (type u32 + length-prefixed name, e.g. type 0x2003 = class with 0x20
     modifier, then "Lens flare"), which is why earlier notes saw "Radius of sphere (wu)" labels in 0x0020. The tail after the
     rotation also contains a constant `0xffffffff` in the sampled records and 16 bytes with no params (initial event record
     probably 16 bytes on PS2; my attempt at a fixed-size model did not close, so walk it with the field-def list).
5. **ODB mapping [V]:** 0x0100 = RFL reference ("..\..\Drakan 2.rfl"; OotF `Dragon.rfl`), **0x0101 = group definition** (a single
   string, e.g. "Level Properties", "Stompstuff"; the "class names" in earlier notes are group names), **0x0102 = class
   record, SRSC `sub` = group id**, 0x0402 = version. So 0x0102 is OotF "Class data", not an instance. `class_id` in LVL 0x0020
   is the SRSC `id` of a 0x0102 record (in the DB selected by `class_db_index`), exactly as OpenDrakan loads objects.
6. **ODB 0x0103 = shared `field_def` pool [V].** Inflates (zlib stream starts at offset 0) to a run of `{u32 type, string name}`.
   Type low byte matches the OotF table: 1 int, 2 float, 3 class (high byte 0x20/0x30/0x40 modifiers seen), 5 sound, 7 enum,
   0x0a string, 0x0b sequence, 0x0c layer, 0x0e texture, 0x0f colour, 0x10 flag = array. Names are identical in spirit to OotF
   ("Initial Music File", "Water Level", "Fog Distance (lu)", "Kill Altitude", "Starting Fader"). OotF stores the defs per class;
   PS2 pools them. The 0x0102 body (name, u32 group, ref, ids, then dword data and pairs such as `{0x33, 1}`) is not yet
   mapped onto the pool; try "data area of dwords + (offset,length) for strings/arrays" from OpenDrakan "Class format".
7. **MOD [V partial]:** 0x0200 name + shading type (PS2 appends 16 more bytes), 0x0201 group, **0x0202 = {group_count, last_group_id,
   last_model_id} matches exactly (3, 11, 25)**, 0x0203 vertices (u16 count, but **16 bytes per vertex** instead of 12), 0x0204 polygons
   (**u32 count and fixed 36-byte records**, not OotF's variable polygons), 0x0207 bounds. 0x0211 is PS2-only. Same family,
   different payloads (PS2 pre-lit/VU1 friendly). Skeletons (0x0208) and ADB animation (0x0501-0x0503) are untested.
8. **Corrections to earlier notes:** `lvl-odb-dump.md` "0x0001 may be geometry/names" -> it is the layer table plus terrain data;
   "0x0020 effect labels, not a sector table" -> it is the object list with embedded field defs; census claim that 0x0025 is
   unrelated: OotF has no 0x0025, so it stays PS2-specific (material table of u16-prefixed names, [V] from hex). 0x0003 is one u32
   (value 0x7fee0, unknown).

## 1. OpenDrakan license

- `LICENSE` is the verbatim **GNU GPL version 3 (29 June 2007)** text. `README.md` ("License" section): "OpenDrakan is released under
  GPL 3.0". **There is no SPDX tag anywhere** (grep for SPDX: zero hits), and **source files carry no per-file license header**
  (only NetBeans/Eclipse-style `Created on / Author: zal` banners). So the license is repo-level, "GPL-3.0" with no
  "or later" language; treat it as **GPL-3.0-only** for planning (our repo is GPL-3.0, compatible either way).
- README states everything is original or taken from other open-source projects under their licenses and that no Surreal/WB material
  is distributed. The format doc credits aluigi's `drakan.bms` script and forum members (BuXXe, HeckFluff, UCyborg); the RIFF PAL,
  string XOR key (0x5FDD390D) etc. came from them. Provenance of those ideas is community RE; we cannot audit it further.
- Caveat: the project is a half-finished reimplementation of an engine for a different game; its code is clean-room RE, not OotF code
  (per its own statement). We take it at its word, and never need to look at OotF binaries.

## 2. Reuse verdict (GPL-3.0 compatible: yes)

GPL-3.0 code can be vendored into a GPL-3.0 project with attribution and license preservation. Practical advice, in order:

| # | Item | Source | Use | Notes |
|---|---|---|---|---|
| 1 | SRSC reader/writer model, `.db` dependency table, ref = `{id, db_index}` | `doc/riot_database_format.txt`, `src/odCore/SrscFile.cpp`, `db/DependencyTable.cpp` | Re-implement in `editor/packages/formats` (we already have `Srsc.h`, `iop_srsc.py`; PS2 header uses a u32 count, OotF u16) | Facts only; no code needed |
| 2 | `.lvl` 0x0000/0x0001/0x0002 definition layouts | `doc/lvl_format.txt`, `Level.cpp` `_loadLevelInfo/_loadLayers` | Port the layer-def reader (about 40 lines) to TS/Python | Verified above; the layout is a fact, port with credit |
| 3 | Object record walk (id/class/db index, rotation, scale, links, parameter pairs, field_defs) | `doc/lvl_format.txt`, `ObjectRecord.cpp`, `LevelObject.cpp` | Use as the skeleton for the PS2 0x0020 decoder; read `ObjectRecord.cpp` once the other agent needs the tail | Differences noted in section 0.4 |
| 4 | Class field model: field types 1-0x0f, dword data area, `{offset,length}` for strings/arrays, ARGB colours | doc "Class format", `db/Class.cpp`, `rfl/` | Basis for the editor's property inspector and schema | PS2 pools defs in 0x0103 |
| 5 | Dragon RFL class catalogue (names, fields) | `doc/dragon_rfl_classtypes.txt`, `src/dragonRfl/classes/*` (Timer, Fader, Detector, SoundEffect, HealthPack, Key, PushableObject, VisibilityToggler, StaticLight...) | Seed the editor's class palette and field labels; compare with the 0x0103 names | Names are descriptive facts |
| 6 | STOMP sequence format (0x0310-0x0312, actions xform/anim/sound/attach/message/music) and message numbers (4097 On, 4098 Off, 4257 Play Sequence...) | doc, `db/Sequence.cpp`, `anim/SequencePlayer.cpp` | Trigger/cutscene editing; our SSD 0x0310-0x0312 types match the type ids | Unverified on PS2 but the ids line up [V ids only] |
| 7 | Texture format notes (bpp/alpha modes, 0x0030 palette) | doc | Only if PS2 deviates; our TXD is already decoded | Low value |
| 8 | Net research: GameSpy-style `\status\` query keys, TCP plus UDP, tick/snapshot design | `doc/drakan_network_protocol.md`, `doc/net_protocol.md` | Ideas for `runtime/net` (see section 3) | OotF protocol is not PS2-compatible and we do not need it |
| 9 | `srscEd` extract/inspect CLI | `src/srscEd/Main.cpp` (196 lines) | Reference for a dump CLI; we already have `lvl_dump.py` | Not needed |

Do **not** vendor C++ into `third_party/`: it depends on OSG, glm, zlib and its own engine types, and we would only use the layouts.
Port the few small readers into our own languages, with a comment `Layout derived from OpenDrakan (GPL-3.0), doc/lvl_format.txt`.

**Housekeeping if we port or vendor code:**
- `THIRD_PARTY.md` / `LICENSES.md`: entry "OpenDrakan, Zalasus, GPL-3.0, https://github.com/Zalasus/opendrakan, commit 6c0d499, files used,
  nature of use (layout knowledge, ported readers)". Keep a copy of the license text if any source file is copied (we are GPL-3.0 already,
  so the project license text covers it). If only facts and layouts are used, still credit (the repo can say "format knowledge from").
- Mention the community RE credit chain (aluigi bms, Arokh's Lair forum) in the same entry.
- `scripts/repository_guard.py` stays green: this is code and prose, no game bytes. The study clone stays under gitignored `build/`.
- If anything is ever copied from OpenDrakan's `resources/` (locale cfg, shaders), check those files separately (not reviewed here).

## 3. OotF multiplayer, public facts and recommendations

Sources (web search, thin): MobyGames press text and box copy, Wine bug list, OpenDrakan capture notes. No manual text was found.

- **Verified from OpenDrakan network capture [D]:** discovery uses UDP with GameSpy-style backslash key/value text. LAN browse =
  broadcast `\status\` to the query port; servers answer with a player list, then a detailed status: `gamename drakan`, `gamever
  375/445`, `hostname`, `hostport` (27045), `mapname`, `gametype` (the German build showed "Nur Boden" = ground-only), `numplayers`,
  `maxplayers` (8 in capture), `gamemode openplaying`, `Dedicated Server?`, score limit, time limit, `Network Speed` preset,
  `Num Teams`, friendly fire, `password`. Gameplay: TCP session for sign-on and some packets, most traffic UDP, 1-byte type plus 2-byte
  length header, source/destination ids and a sequence number, chat as a wrapped packet. Client/server shape (server id 1, clients 2+).
- **From public listings [U-ish, marketing text]:** up to 8 players over LAN/internet, 2 over modem, "over 11 unique multiplayer levels".
- **Not found:** deathmatch/team modes in detail, whether dragons were playable or synchronised in multiplayer, pickup/projectile sync,
  in-game browser UX. Mark all of these [U]. The server settings above imply: frag-limit and time-limit rounds, optional teams, friendly
  fire toggle, "ground only" vs presumably dragon-enabled game types, dedicated or listen host, password.

Recommendations for `runtime/net` match modes (design choices, not OotF facts):
1. Keep the authoritative host and thin clients model already in `netcode.md`. Do not chase OotF wire compatibility (the original is dead and
   different). Offer a LAN broadcast browser first, direct-IP second, and a lobby-code relay later.
2. Copy the **settings surface** from the server query: max players (up to 8), frag limit, time limit, teams on/off, friendly fire, ground-only
   vs with-dragons, password, dedicated flag. These map to a single `MatchSettings` struct that also drives the browser listing.
3. Modes to ship in order: free-for-all deathmatch (frags), team deathmatch (teams, friendly fire), then a dragon-enabled variant (needs
   dragon pose sync; our ghosts already sync Rynn pose). Add co-op only after placement/trigger state is understood.
4. Sync order of value: player pose and animation (done for ghosts), damage events (reliable), pickups and projectiles (event-based with
   host authority), dragon state last.
5. Take the OpenDrakan draft ideas worth keeping: server-side table of asset ids to names exchanged at handshake (our class/db ids are
   per-level dependency indices, so an id-to-name table avoids mismatches), snapshot plus change-count confirmation for loss tolerance.

## 4. OotF level editor, public facts and recommendations

Facts found [U beyond one-liners]: Blues News reports an "editing tools" release adding model and texture export to the Level Editor, a 3D
modeling tool and exporters for 3D Studio Max and SoftImage (fetch was blocked, snippet only). A VOGONS user describes spending hours in the
Level Editor fixing **visibility data** on stock levels. The format docs (OpenDrakan, derived from the editor's output) imply the editor's
model: terrain made of rectangular **layers** (grid heightmap, floor/ceiling/between types, per-layer light direction/colour/ambient, a
**per-layer visibility list = the portal/PVS system**), layers organised in **groups**, objects placed with a class from a **class
database** (classes grouped, with icons and typed fields), **per-instance overrides** of class fields only, object **links** and initial
**events/messages**, a **STOMP sequence editor** (actors, actions on a timeline), and database-organised assets (models, textures, sounds,
animations) with dependency lists. Editor names and exact UI are unverified; no tutorial was found.

Recommendations for our editor UX:
1. Mirror the data model, not the OotF UI: Layers panel (grid, type, light, visible-layer matrix with auto-compute), Object list, and
   an Inspector that shows class defaults with overrides highlighted (store only changed fields, like the format does).
2. The 0x0103 field pool gives labels, types, enums and ranges, so the inspector can be schema-driven (colour picker for 0x0f, class
   picker filtered by the 0x20/0x30/0x40 modifier, sequence picker for 0x0b).
3. Priority features: move/rotate/scale (flags 0x100), class retype, links editor, message/trigger fields, then terrain height brush.
   Visibility (layer PVS) must be recomputed or kept intact when layers move; flag it prominently.
4. Pack/export as overlay entries (`DirectoryOverride` per resource, see `level-editor.md`), not whole files.
5. Gate writes behind a byte-identical round-trip of each decoded record (the PS2 layer data and object tail are not decoded yet).

## 5. Next steps

1. Finish the 0x0020 tail (walk with `field_def` list; check the 16-byte block after the rotation against ODB event definitions),
   then validate all 14 levels (every body consumed, positions inside layer tiles).
2. Decode PS2 layer terrain (inline, packed, per-layer).
3. Map 0x0102 class bodies onto the 0x0103 pool.
4. Try the 0x0310-0x0312 SSD records with the OotF sequence layout.
5. Add the OpenDrakan entry to `THIRD_PARTY.md` when the first port lands.
