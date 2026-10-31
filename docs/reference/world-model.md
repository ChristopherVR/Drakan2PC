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
| NPCs / creatures (types 6 and 7) | instance data `q = *(*(h+0xfc)+0x10)`: **plain signed ints** (no key), current `q+0x24c`, max `q+0x250`; validity: `*(q+0x23c) == h` (back pointer). See section 7 | [RAM] (40 to 36 to 29 to 26 over three sword hits on one NPC; 137 creatures per Surdana dump) |

The snapshot reports HP only where it is known (`HpKind::PlayerHp`, `HpKind::Durability`, `HpKind::Creature`).

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

* NPC / creature AI state (HP is known, section 7); the meaning of `+0x04`, `+0x9c` and the five secondary lists.
* Whether positions of attached objects are parent-relative; the yaw convention.
* Stability across save / load and level change in game: the registry is rebuilt on level load (counts differ per level);
  the reader degrades to `NoContext` / `NoRegistry` or fewer entities while the pointers are being rebuilt. Verified
  only offline on dumps of two levels; a live level-change run is still to do.
* Spawn / despawn is another agent's area; this layer only reads.

## 6. Pose model: the controlled actor's skeleton channels (S2 groundwork)

`world/Pose.h` (`readPlayerPose`, `PoseBuffer`, `toPlayerState` / `fromPlayerState`) records the local player so a ghost
can replay it. Every offset below was re-checked against RAM dumps (`world_pose_ram_test`: 7 dumps, 85 checks; Rynn idle,
strafe, three attack frames, bow drawn, and Arokh controlled; a wider scan of about 2800 dumps confirmed the channel layout
for every dump with a live player).

Naming: `H` = the player handle (`ctx+0x4b0`), `data = *(*(H+0xfc)+0x10)` = the class instance data (what
`drakan_dual_wield.cpp` calls `player`), `sk = *(H+0x50)`. `*(data+0x80) == H` in every dump, so the dual-wield code's
"model" is the handle; the spawn journal's "skeleton at actor+0x50" is right.

| What | Where | Confidence |
| --- | --- | --- |
| Skeleton | `sk = *(H+0x50)`; flags `sk+8` (`0x980078` idle, `0x980058` attacking) | [RAM] |
| Channel array | `*(sk+0xe8)`, count `u32(sk+0xf0)`, stride `0x80`; own node id `+4` (Rynn: 0, 9, 10, 61, 55, 5, 4, 37, 2, 6, 11), attach `+8` (-1 or 0), **clip id `+0x50`** (u32 resource id), **time `+0x58`** (float, seconds into the clip) | [RAM] |
| Channel count | Rynn 10 to 19 (11 normally, 13 with the bow, 19 with held items), Arokh 7. Cap reads at 32 | [RAM] |
| node to channel | u16 table `*(sk+0xdc)`; Rynn: node 0 -> channel 0, nodes 2 / 16 / 34 -> channel 8, nodes 54 / 60 (legs) -> channel 0. Arokh: nodes 0 / 2 / 16 / 34 / 60 -> 0, node 54 -> 1 | [RAM] |
| Locomotion channels | the channel of node 0 (channel 0, plus 3 and 4 = nodes 61 / 55, and 11 / 12 when present) play one clip: the walk / run / idle clip, which keeps playing during a moving attack | [RAM] |
| Upper body channels | the channel of node 2 (channel 8, plus 1, 2, 5, 6, 7, 9, 10): spine, arms, weapon attachment. Equal to the lower clip while idle or walking; **a different clip during an attack, block or aim** (e.g. lower `0x2066d`, upper `0x206a5`) | [RAM] |
| Clip ids seen | `0x2047b` / `0x2081b` (dual wield pair), `0x20577` idle, `0x2066d` / `0x206a5` melee stance pair, `0x20691` attack, `0x20894` / `0x20895` / `0x20897` / `0x208ae` bow, `0x40663` Arokh | [RAM] |
| Clip time | 0 to about 3, wraps when the clip loops (the interpolator treats a backwards step as a wrap) | [RAM] values, [inf] unit is seconds |
| Attack flag | `data+0x94 & 0x80` (`0x100` during blocking / aim holds, `0x20000` with the bow out) | [RAM] |
| Weapon holders | `data+0xeb4` (right, melee) and `data+0xe90` (left, bow), `+0x1c` = u16 registry ref of the held item. Resource ids seen: sword / club `0x33cbc`, usable `0x33752`, bow class `0x1004f` (res `0x31bc9`), ammo class `0x1005f` (res `0x3379d`). 0, `0xff00`, `0xff01` mean none | [RAM] |
| Mounted | the controlled handle's class is `0x1001c` (Arokh); Rynn is then not posed. The class decides; `data+0x4d8` is not a reliable flag (`1`, `0x530001`, `0x5b` seen) | [RAM] |
| Position / rotation | `H+0x14`, `H+0x20` (section 1); for Rynn a pure Y rotation. The facing sign of `yaw = atan2(m[0][2], m[0][0])` is still unverified in game | [RAM] / [?] |

Library: `PlayerPose` {handle, class, mounted, attacking, time, tick, position, rotation, yaw, velocity, `lower` and
`upper` (`ChannelPose` = clip + time), `lowerMask` (bit per channel playing the lower clip, all bits when both are equal),
`weaponRight` / `weaponLeft` resource ids}. Velocity is the finite difference between two reads (`PoseReadState`).
`PoseBuffer` keeps time-stamped samples: lerp of position and velocity, nlerp of the rotation (shortest arc), clip time lerp
inside a clip (advance on a wrap), a clip change keeps the old clip until the next sample is reached and then snaps,
teleports (step over `jumpDistance`) are not blended, a read past the newest sample extrapolates by velocity up to 250 ms,
`sample(now)` renders `delaySec` (100 ms) behind. Net bridge: duck-typed templates in `Pose.h`, so `world` does not depend
on `net` (checked against the real `net::PlayerState` by `net_pose_bridge_test` in runtime/net); the wire carries the lower
clip as u16, the weapon as the low byte of the resource id, yaw only.

Tests: `world_pose_test` (synthetic memory, interpolation, buffer, bridge), `world_pose_ram_test` (local dumps, exit 77
when absent; `--json out.json <dumps...>` writes poses for playback), `net_pose_bridge_test`.

## 7. Creature HP, panel verification and the USA disc (2026-10-31)

Notes: [journal](../journal/2026-10-31/world-panel-verification.md).

**Creature HP [RAM].** The type column of the registry splits the creatures differently than section 1 guessed: the
objects that carry HP are **types 6 and 7** (in Surdana: 127 of type 7, resource ids `0x5112b`/`0x5112c` 10 to 40 HP,
`0x513bf` 40, `0x5140b` 150 ..., and 10 of type 6, class `0x1001e`, 300 HP), not type 4 (type 4 is the camera and
world / effect actors). HP is **not obfuscated**: with `q = *(*(h+0xfc)+0x10)` (the creature's instance data, the same
object the combat code calls the NPC data), `int32 q+0x24c` = current, `int32 q+0x250` = max, `*(q+0x23c) == h`. The
combat harness (`combat_ram_report.py`) already used these offsets plus a vtable at `q+0x268` (PAL address range,
dropped here to stay region neutral). Checked on `build/harness/pal-combat-melee1`: one NPC went 40 (target), 36, 29
and 26 (end) over the three sword hits; `world_ram_test` reads 137 creatures in that dump. Dead creatures: current
can go to 0 or below (accepted down to -100000); a destroyed handle leaves the registry. The panel shows them as
`6/7 creature` with `cur/max` and the HP filter includes them.

**Panel [RAM, live].** Opened in game on Surdana (PAL, 3840x2160 overlay): the window is now sized from the font
(`em * 60` wide, at most 60% / 80% of the output); with fixed 760x520 pixels it was 20% of a 4K frame and clipped. Filters
(class, type, has HP 4602 to 108 shown), the player highlight, the distance column, the details line and Dump JSON
(status line shows the written path) work. The type label `actor` for type 4 was misleading and is gone.

**Save / load [RAM, live].** F5 then F8 (quicksave, quickload) with `DRAKAN_WORLD=1` and `=2`: no crash; the reader
logs `[world] sample status ...` whenever the status or level name changes. With `=1` the registry read `Ok`, 4609
entities, 'Surdana' after the load; with `=2` it went `NoRegistry` (title) to `Ok` 'Intro' to the level.

**USA disc.** The panel stays off for USA. Region profile `configs/regions/usa.json` already maps `0x5565d4` (ctx
pointer), `0x4fe86c` (HP key) and `0x4fbac8` (stats block) and the PAL to USA data map lists the handle vtable
(`0x51b8b0` to `0x51ac30`, 2 refs, 0 conflicts), so the four globals have **candidate** equivalents, but nothing else
is verified: no USA ELF, ISO or RAM dump exists in this tree (only the `analysis/regions/usa/*.csv` maps), and the USA
object layouts differ (offsets 4 lower inside matched code, [regions.md](regions.md)), so every offset in sections 1 to 2
(ctx `+0x7c`, handle `+0xfc`, resource record, stats `+0xc`, creature `+0x24c`) is unconfirmed there. To enable: run a
USA build with `DRAKAN_WORLD=1` after adding the four addresses and compare the panel against a dump (level name at
`ctx+0x298`, player class `0x10009`), then fix the offsets that differ in typed per-region fields.

## 8. Lifecycle events (drakan_lifecycle)

`ps2xRuntime/src/lib/drakan_lifecycle.{h,cpp}` turns the engine's load / save / mount transitions into listener events for
host features that own guest actors (ghost players, editor hot reload, multiplayer). PAL only. Evidence, ordering and the
policy for destroying actors: [ghost-spike-s3](../journal/2026-10-31/ghost-spike-s3.md). Check it in game with
`python scripts/harness/lifecycle_run.py`.

| Event | Guest function (PAL) | Verified in game |
| --- | --- | --- |
| `beforeLevelLoad` / `afterLevelLoad` (level stem, `fromSave`) | `Level_Load` `0x1c94c8(path, flags)`; flags 0 = fresh, 1 / 3 = inside a save restore | New Game (intro, Surdana), restores |
| `beforeSaveWrite` / `afterSaveWrite` | `0x169c28`, the serialiser behind menu save, quick save and the level-change save | menu save, F5 |
| `beforeSaveRestore` / `afterSaveRestore` | `0x16adb8`, the loader behind F8, Load Game, Continue; calls `Level_Load` itself | F8 |
| `mountChanged` (Rynn <-> Arokh) | class of the resource record of `ctx+0x4b0`, polled at `PadMgr_Update` and after loads | Rynn -> Arokh (intro to 4A); Arokh -> Rynn, cross-level restore and a real exit still unverified (a level load cannot be injected into a running level, see journal section 5) |

Listeners run on the EE thread at a call boundary. The save write and restore yield to other guest threads, so the "after"
events come from a `$ra` trampoline and `transitionActive()` tells host spawners to wait. Destroying an actor: `0x1413c0`
only flags it and queues it in the kill list (`ctx+0x440`, count `+0x448`); the list is flushed at the end of every engine
frame (`0x142e50`) and by the first statement of the save writer, **not** by `Level_Load` or the restore. A ghost must
therefore be destroyed with a synchronous flush before a level load or restore, and plain `0x1413c0` is enough before a
save write (`drakan_lifecycle::destroyActor`).

## 9. Player progression and inventory in the save file (2026-10-31)

Notes: [save-editor journal](../journal/2026-10-31/save-editor.md), third pass. The save is a snapshot of the RAM
structures above; the same obfuscation applies (`enc(v) = rotl32(v, 8) ^ 0x739acd45`, key `*(0x4fe86c)`, constant in
every run and save).

| RAM | In the save | Confidence |
| --- | --- | --- |
| Stats block `S = 0x4fbac8` (HP `+0xc`, max `+0x10`, gold `+0x18`, mana `+0x1c` / `+0x20`, XP `+0x24`, level `+0x28`, skill points `+0x2c`) | raw copy in `0x401` id 893, found by the u32 pair `6, 3` at `S+4` / `S+8`; loaded back into RAM unchanged | [game] edited values appear in RAM and on the Tab screen |
| Inventory controller `player+0x584`, cells `ctl+0x30` (16 bytes: equipped, hot, handle, icon), multi-cell markers `dx*10+dy` | raw image in Rynn's `0x102` record at a layout-dependent offset (2-byte phase) | [game] flags and counts round-trip |
| Item actor instance data `+0x14` / `+0x18` (current / max durability, arrows, quantity, `enc()`) | item `0x102` record, right after its twice-stored self pointer (`+8` / `+12`) | [game] arrows 77, durability 33 loaded |
| Item resource id (`0x31bc9` bow, `0x3217b` club, `0x33752` elixir, `0x33cbc` dagger) | record u32 `+0` minus `0x20000` | [RAM] vs save |
| Quest / map marker rows `DAT_004fbb70` (0x20 bytes) | `0x401` id 893 `+0x84`, count at `+0x80` | layout matches; edits not tried |

Item handles in the save are the saving session's heap pointers and are remapped on load. Creating a new item by
cloning a record did not survive load; equipping by cell flag leaves the held-weapon actors (`+0xeb4` / `+0xe90`) alone.

## 10. Level change: the exit trigger and the main loop (2026-10-31)

Notes: [journal](../journal/2026-10-31/level-change.md). Read from the Ghidra decompile and checked in game (PAL).

**The exit trigger** is class `0x10037` ("Level Transition", also named "Portal Transition ..." and "Transition from 1(A) to 1 (D)"
in the level data); 5 to 11 per level (Surdana: 11). Its message handler is `0x365138(handle, message)`, its instance data
`inst = *(*(h+0xfc)+0x10)` (`inst+0x28` = the handle, `inst+0` = u16 flags: bit 2 armed, bit 3 / bit 4 see below). Properties come
through the property block `*(h+0x54)` (vtable `*(block+8)`, accessor at vtable `+0x1c`, `this = block + s16(vtable+0x18)`, args
`(index, -1)` return a pointer to a u32):

| Property | Meaning | Confidence |
| --- | --- | --- |
| 0 | "Transition To...": destination, a level-local string id (`0x8000n`, n 5 to 17 in Surdana). Text through the string manager `*(ctx+0x344)`, accessor `vtable+0x94(this, id, buf, 0, 0x100)`; it blocks on guest semaphores (not callable nested) | [RAM] ids, [dec] use |
| 1 | portal flag: arrival object is named `portal` (else the arrival is looked up by the old level's name) | [dec] |
| 2 | read in the same function as 1 (`*p == 1`), meaning unknown | [dec] |
| 3 | text / prompt id shown before leaving, `-1`/0 none | [dec] |
| 5 | bit 3 of the instance flags | [dec] |
| 6 | **direct** transition: `!= 0` skips the confirm / save screen and the trigger sets the pending state itself | [dec]; 0 on every Surdana exit [RAM] |

**Chain.** Event `0x1001` (4097, the player touches the trigger) reaches `0x361440(inst, msg)` (msg `+4` -> u16 event id).
With property 6 set it writes `DAT_004fb6fc = 0` and the pending handle `DAT_004fbba4 = handle`; otherwise it sets handle `+4 |= 8`
and queues the handle in the engine's list (`ctx+0x520` vslot `0xdc`). The queue later delivers message 4 to `0x365138` ->
`0x361288` -> `0x361e18` (dequeue, clear flags, `0x361f60`: if `DAT_004fdec8` (the level-change save slot name, empty at first)
is empty it opens the level-change **save screen** (screen 5, mode 1) and returns 0; `Yes` (`0x238b` to `0x365370`) re-enters
`0x361e18`), which sets `DAT_004fbba4 = handle` and opens the loading screen (id `0x1a`, `0x2c4458`). Everything up to here is
UI-thread work with modal screens.

**The transition itself runs on the main thread inside the per-frame game-state function `0x2cbf88`** (a state-table entry; no
static caller): `if (DAT_004fbba4 != 0)` it calls `0x362d38` when property 6 is 0, else `0x364c90`, and both do the whole job
inline, blocking the main thread: read property 0 (destination text), write the **level-change save** `<slot>/<level>.rlt` (`0x169c28`),
free the level lists, `Level_Load 0x1c94c8` (flags 0), then place the player: `0x362a60` finds the **arrival marker** (a class
`0x10037` object of the *new* level whose own destination text equals the old level's name, or `portal` for property 1), the player
and camera keep their offset from the old trigger (`Maintain Player Position`), the new level's `.rsg` is saved and `0x2cb7e8(2)`
re-enters gameplay. `0x364c90` (direct) skips the arrival and just loads `DAT_004fd53c(path)` (the table slot that holds `0x13b7c0`,
which is `0x136628(); Level_Load(path, 0)`, no caller).

**Why calling `0x13b7c0` hangs:** it is only the middle of that sequence. The world keeps running on the main thread while the load
tears it down (null vtable calls); the real flow owns the main thread for the whole call.

**Host side** (`drakan_levelchange.{h,cpp}`): scans the registry for class `0x10037` and "fires" an exit at the frame boundary
(`PadMgr_Update` hook) by writing what the trigger writes: `DAT_004fb6fc = 0`, `DAT_004fbba4 = handle`. The transition function
`0x362d38` is wrapped (`transitionEntry`): as its very first act it puts the player (and the camera, at its recorded offset from
the player) onto the trigger. This matters: the transition keeps the player's offset from the trigger (`0x3620f0`) and re-creates
the player at the destination's arrival marker plus that offset (`0x362310`), and **the position at `player+0x14` is rebuilt from
the physics copies every frame**, so a write at the frame boundary is gone when the main loop gets to the transition (the player was
somewhere random, the arrival got a garbage offset: Rynn left the level, died, or the main thread spun in the Catacombs). Written
inside the transition, the arrival is exact for every destination tried. No guest call is made (a posted `0x361e18` / `0x361440`
froze or did nothing: the queue and the screens need the UI thread). Listener `setExitListener` reports exits the player walks into
too (read at the `.rlt` save, where the pending handle is still set); `followExit(pos)` fires the exit at the same world position in
another copy of the level (used by multiplayer, [netcode](netcode.md)). F1 > World lists the exits with "Go" buttons; env
`DRAKAN_LEVELCHANGE=<step>[,<step>...][@frames]` (step = exit index in nearest-first order, or a destination name) is the harness
aid (`scripts/harness/level_change_run.py`, `--sweep <level>` for a table of every exit).

**Property 2** (`*p == 1`): relative placement. 1 for the exits to Kragmor, Catacombs, Shadowmire and the Lair: the player's offset
from the old trigger is applied at the arrival marker; 0 for the City and the Isles: the player keeps its absolute coordinates (the
levels share one world; the City exit and its arrival marker are at the same position).

**Destination names.** Property 0 is a string id; its text is a level name ("Level 1B - Kragmor") or a file path
(`Levels\Level 1A - Surdana\Level 1A - Surdana.lvl`, Kragmor's exit back), both reduced to the name. The accessor (`0x1b9fb0`) must
not be called nested: on a cache miss it starts a load and spins (`while ((flags & 3) == 2)`), freezing the guest. The host reads
the string manager's cache instead (`*(ctx+0x344)`, `this = sm + s16(vt+0x90)`, 128 buckets at `this+0x20`, bucket `(id*0x59)&0x7f`,
chain at entry `+0x14`, `entry+0` id, `entry+8 & 3 == 3` text pointer at `entry+0x1c`). The marker search of a level load reads that
level's exit texts up to the match, so most are cached; an uncached one shows `?` until the exit is used once (the level it led to
is remembered per (level, id)).

**Verified in game** (journal sections 3 and 5, harness `level_change_run.py`): Surdana -> City of the Damned, Inquisitor's Lair, Kragmor,
Catacombs, Shadowmire (Rynn on the arrival marker, 70 to 180 fps), round trips and chains; the sweep table is in the journal (section 7).
The walk-in path (touch event, queue, save screen, loading screen) was not scripted: the main-loop half is the same code as the fired path.
