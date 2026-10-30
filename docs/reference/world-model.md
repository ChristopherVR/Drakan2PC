# World model: the actor registry and the entity snapshot

How the guest keeps its world objects, as read by `runtime/world` (`drakan::world`) and shown by the F1 "World" panel.
Addresses are PAL (SCES-500.06); `ctx = *(0x5565d4)` (`0x4de5e0`). Object offsets are region independent, the absolute
globals go through `world::Globals` and `drakan_region::guestAddress()` ([regions.md](regions.md)). No disassembly is
reproduced here, only offsets and meaning.

Confidence tags: **[RAM]** verified in RAM dumps (Surdana, 4 level-1 fixtures plus about 25 more dumps, and one Ravenshold
dump with the dragon), **[dec]** read in the decompile only, **[inf]** inference, **[?]** unknown. Dumps are local
(`build/`, never committed); `world_ram_test` re-checks the invariants below against whatever dumps are present.

## 1. Where the actors are

There is one master registry. Everything the guest treats as a game object (player, camera, NPCs, props, items, level
geometry pieces, lights, triggers ...) has a **handle** object registered in a slot table owned by `ctx`.

| `ctx` offset | Meaning | Confidence |
| --- | --- | --- |
| `+0x7c` | pointer to the slot table; 8-byte entries `{handle*, refcount}` | [RAM] |
| `+0x80` | slots allocated (0x1240 = 4672 in Surdana) | [RAM]; the role "capacity" is [inf] |
| `+0x84` | highest slot index in use (0x1219 in Surdana). A ref is valid when `ref <= value + 1` | [RAM] value, [dec] the bounds rule |
| `+0x98` | disc path of the loaded `.lvl` (NUL-terminated, about 64 bytes) | [RAM] |
| `+0x298` | level display name ("Surdana", "Ravenshold") | [RAM] |
| `+0x320` | resource db (0x24-byte records, see `tab-rework-data-model.md`) | [RAM] |
| `+0x4b0` / `+0x4b4` | player handle / camera handle (pointers to the handle objects) | [RAM] |

A **ref** (the 16-bit values stored elsewhere in guest objects, e.g. `player+0xed0`) is `slot index + 1`; the handle is
`table[(ref - 1) * 8]`. Entries are reference counted: the second word is the count (1 to 10 seen), released by a small
function that also clears the caller's ref when the slot is empty or out of range [dec].

**Liveness test** (what the reader uses): the slot's pointer is a RAM pointer, the object's first word equals the common
handle vtable (`0x51b8b0`) and its own ref (`+0x12`) equals `slot + 1`. All 4627 non-null slots of the main fixture pass.
Slots in other dumps occasionally hold a pointer that fails the test (1 stale slot in about a third of the dumps, 6 to 20
at the title screen): free-list links or half-destroyed entries; the reader counts them (`Snapshot::staleSlots`) and
skips them. **[RAM]**

The registry is large: about 4600 live handles in Surdana, 4780 in Ravenshold. Most are level geometry (type 8, class
`0x100d3`, 2339 handles). The count is stable across the dumps taken during play (4583 to 4630) and differs by level.

### Handle object (about 0x120 bytes)

| Offset | Meaning | Confidence |
| --- | --- | --- |
| `+0x00` | vtable, `0x51b8b0` for every registered handle | [RAM] |
| `+0x04` | packed word (high byte 0x10 to 0x29, low 16 bits look like a ref or tag; not a valid ref for most) | [?] |
| `+0x08` | model / mesh resource id; equals record `+0x10` for 95% of handles (the player differs: 2) | [RAM] |
| `+0x0c` | object type; always equals the resource record's `+4` | [RAM] |
| `+0x10` | u16 resource record index (`0xffff` = none) | [RAM] |
| `+0x12` | u16 own ref = slot index + 1 (true for all 4627) | [RAM] |
| `+0x14` | position x, y, z (floats, world units around 1.5e5 to 7.4e5) | [RAM] plausible for all types; whether attached objects store world or parent-relative coordinates is [?] |
| `+0x20` | 3x3 float rotation matrix; the player's is a rotation about Y (row 1 = 0,1,0) | [RAM] |
| `+0x54` | property block; equals record `+0xc` for about 70%, re-read it | [RAM] |
| `+0x9c` | state word (0 for 97%; player `0x10004`) | [?] |
| `+0xfc` | inner object; `*(inner + 0x10)` = instance data | [RAM] |

The reader reports `yaw = atan2(m[0][2], m[0][0])` in radians. The sign / zero direction convention against the game's
facing was **not** verified in game **[?]**.

### Resource record (0x24 bytes, `*(res+8) + index*0x24`, count at `res+0x10`, index limit inclusive)

`+0` resource id (Rynn `0x20a50`, Arokh `0x20a60`, camera `0x20002`), `+4` type, `+8` **class id**, `+0xc` property
block, `+0x10` model id. **[RAM]**

### Types and classes seen (Surdana dump, counts of handles)

| Type | Count | Seen classes | What (descriptive; mostly **[inf]**) |
| --- | --- | --- | --- |
| 3 | 2 | `0x10009` (Rynn, the player), `0x1001c` (Arokh, second instance far away) | player characters [RAM] |
| 4 | 871 | camera `0x1001b` (one, `ctx+0x4b4`), `0x10070`, `0x1003d`, `0x1003e`, `0x10076`, `0x100b4` | the camera and a large family of world actors (NPCs, creatures, effects); not split further |
| 5 | 8 | `0x1000c` (2 shown), others not listed | **[?]** |
| 6 | 10 | `0x1001e` | **[?]** |
| 7 | 138 | `0x1009d`, `0x10031`, `0x10029`, `0x10025` | **[?]** (props / triggers) |
| 8 | 3155 | `0x100d3` 2339, `0x100d8` 406, `0x10011`, `0x10014`, `0x10023` | static level pieces / props **[inf]** |
| 10 | 45 | `0x1000e` melee weapon, `0x1004f` bow | weapons [RAM], also in the inventory ([tab-rework-data-model.md](../journal/2026-10-24/tab-rework-data-model.md)) |
| 11 | 73 | `0x1006d`, `0x100b2` scroll, `0x1000a` elixir | usable items [RAM] |
| 12, 13, 15 | 1, 323, 1 | `0x10006` (182), `0x1003f`, `0x10041`, `0x1002b` | **[?]**, class `0x10006` is the biggest type-13 group |

The items the player carries are handles too (same registry, positions are meaningless for carried items). Names are not
read: item and spell names come from the string manager (`tab-rework-data-model.md` section 2) and are only resident on
demand, so the snapshot carries ids, not text.

### Secondary lists

`ctx` holds more `{u32* items, u32 capacity, u32 count}` triples whose items are handle pointers. Five of them are read
as a membership mask (`EntitySnapshot::listMask`, bit i = list i): offsets `+0x44c`, `+0x458`, `+0x464`, `+0x470`,
`+0x488`. They are subsets of the registry (checked: 100% of their entries are live handles) but their semantics are
**unknown**. Observed in Surdana: `+0x44c` 17 entries (types 3, 4, 8, 13: the player, the camera-side NPCs, class
`0x10014`), `+0x458` 91 (items, classes `0x1000e`, `0x1002b`, `0x1009d`), `+0x464` 27, `+0x470` 161 (91 of class
`0x10006`), `+0x488` 45. The player is in `+0x44c`, `+0x464` and `+0x488`. They overlap partly and change with position
(magic5 dump: 21 / 34 / 14 / 62). Working guess **[inf]**: per-subsystem working sets (nearby / awake / collidable /
renderable). Do not rely on them yet. `+0x440`, `+0x494`, `+0x4a0`, `+0x4ac` hold other structures (not handle lists).

## 2. Hit points

| Entity | Where | Confidence |
| --- | --- | --- |
| Player | stats block `S = 0x4fbac8`: `dec(S+0xc)` / `dec(S+0x10)` (mana `+0x1c`/`+0x20`), key `*(0x4fe86c)`, `dec(x) = rotr32(x ^ key, 8)` | [RAM] (100/100, 84/100 in other fixtures) |
| Weapons, usables (types 10, 11) | `*(*(h+0xfc)+0x10)`: `dec(+0x14)` current, `dec(+0x18)` max; -1/-1 = indestructible; this is durability / quantity, not HP | [RAM] for all 118 |
| NPCs / creatures (type 4 ...) | **not found.** A scan of the first 0x300 bytes of the instance data and 0x100 of the inner object for `dec()` pairs (cur <= max) found no consistent offset across NPCs | [?] open |

The snapshot reports HP only where it is known (`HpKind::PlayerHp`, `HpKind::Durability`); a creature HP search needs a
fight fixture (two dumps before / after a hit, diff the instance data).

## 3. The library

`runtime/world` (std only; guest memory through `gameui::IGuestMemory`, `FlatGuestMemory` reads dumps offline).

| Piece | Role |
| --- | --- |
| `world/Addresses.h` | the offsets above; `Globals` = the four absolute addresses |
| `world/Snapshot.h` | `EntitySnapshot` (handle, ref, class, type, resource index/id, model id, flags, refcount, listMask, position, rotation, yaw, HP), `Snapshot` (level name/path, player, camera, entities, registry stats) |
| `world/Reader.h` | `readSnapshot(mem, Globals, ReaderState, out)`: pure bounds-checked reads, no guest calls; `isLiveHandle`, `readEntity` |
| `world/Exchange.h` | `SnapshotExchange` double buffer (same contract as `gameui::ModelExchange`) |
| `world/Json.h` | `toJson(Snapshot)` for offline diffing |

Tests: `world_test` (synthetic memory images built in the test: enumeration, stale slots, HP decoding, region globals,
exchange, JSON) and `world_ram_test` (local dumps, exits 77 when none).

## 4. Guest adapter and panel (ps2xRuntime)

`drakan_world.cpp`: opt in with `DRAKAN_WORLD=1`. While the F1 overlay is open the EE thread fills a snapshot after each
`PadMgr_Update` (about 4600 handles, plain reads, no guest calls, nothing allocated in steady state) and publishes it; the
overlay's render thread draws the **World** window over the open menu (`SettingsOverlay::setMenuExtra`): header (level,
counts, player position and HP), filters (class substring in hex, type, has HP, in lists, distance limit), table (handle,
type, class, resource index, model id, position, distance to the player, HP; the player row is green; nearest first by
default) and a details line for the selected row. **Dump JSON** writes
`<exe dir>/dev/world/world_<level>_<sequence>.json` (the exe dir is `dist/DrakanPC`, git-ignored). It is read only: the
adapter never writes guest memory. Regions: the four globals need entries in the disc profile's address table; a profile
without them logs "disabled" and the panel stays off (the USA profile has not been checked).

## 5. Not yet known / next

* NPC / creature HP and AI state; the meaning of `+0x04`, `+0x9c` and the five secondary lists.
* Whether positions of attached objects are parent-relative; the yaw convention.
* Stability across save / load and level change in game: the registry is rebuilt on level load (counts differ per level);
  the reader degrades to `NoContext` / `NoRegistry` or fewer entities while the pointers are being rebuilt. Verified
  only offline on dumps of two levels; a live level-change run is still to do.
* Spawn / despawn is another agent's area; this layer only reads.
