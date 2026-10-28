# Tab rework: data model and native entry points for a host-drawn inventory — 2026-10-24

Read-only research for replacing screen 0x15 with a host-drawn UI (see [ootf-map-inventory-ui.md](ootf-map-inventory-ui.md)).
Sources: Ghidra decompile `build/recomp_ui/alldecomp/*.c`; RAM dumps
`build/combined-build06-private/t/build06-final-native32/ram_{mouse,cat}_*.ram` (Equipment / Spells / Skills / Journal
open, dagger + bow fixture) and `build/harness/magic5/ram_k0.ram` (Slot 5 save, Jade's shop open, 376 gold). Probe
scripts (read-only, `python <script> <ram>`): `build/tab_rework_probe/` (`fn.py` prints a decompiled function, `inv.py`
the slots, `strs.py` the string cache, `spells2.py` the spell list). Nothing was built or run.

Markers: **[RAM]** checked in a dump, **[dec]** read in the decompile, **[inf]** inferred, not checked.

Notation: `ctx = *(0x5565d4)` (= `0x4de5e0`), `res = ctx+0x320` (resource db: record table `*(res+8)`, count
`*(res+0x10)`, 0x24-byte records), `S = 0x4fbac8` (player stats block), `key = *(0x4fe86c)`,
`dec(x) = rotr32(x ^ key, 8)` and `enc(v) = rotl32(v, 8) ^ key`. `player` = Rynn's data object
(`screen+0x14c`, or `*(*(ctx+0x4b0)+0xfc)+0x10`). `ctl = player+0x584` (inventory controller, vtable `+0x4a4` =
`0x52e5a0`).

## 1. Inventory model

### Controller (`ctl = player+0x584`)

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x0` | busy (selection change in progress; `345d08` drops keys while != 0) | [dec] `345d08`, MenuMouse |
| `+0xc` | shop mode (`340508`); `+0x18` shopkeeper / stock list (`+0x18` items, `+0x1c` count) | [dec] `33c660`, `44a678` |
| `+0x1c` | shop-list selection | [dec] `341268` |
| `+0x20` | Equipment selection: cell 0..23, `0x1a` / `0x1b` special, `0x1c` = "spell" (details of `+0x4b4`), -1 none | [dec] `33eaf8`, `342f90` |
| `+0x24` | focused pane: 0 Equipment, 1 Spells, 2 shop list | [dec] `33c660`, `33f100` |
| `+0x30 + i*0x10` | slot record i (0..23); `0x1a` -> `+0x1c0`, `0x1b` -> `+0x1d0` | [dec] `340510`, `33eaf8` |
| `+0x1b0..+0x1bf` | preview record (`+0x1b8` = previewed handle) | [dec] `33eaf8`, [RAM] |
| `+0x244 +0x284 +0x2c4 +0x304 +0x344 +0x384 +0x3c4 +0x404 +0x450` | **formatted details text** for the selection (ASCII, 0x40 each): name, "Skill Needed: n", (2nd req), "Damage: n", speed / ammo, range / "%d/%d", shop price lines, durability or "Arrows: 200/200" | [dec] `342f90` / `433850`, [RAM] |
| `+0x444` / `+0x448` / `+0x44c` | quantity (plain), ammo count, low-ammo blend | [dec] `342f90` |
| `+0x498` | icon / glyph id for the line under the stats, -1 none | [dec] `433850` |
| `+0x49c` | requirement met (1 = draw normal colour; `341858`) | [dec] |
| `+0x4ac` | details valid; `+0x4b0` = 1 name too long (`433850` shrinks) | [dec] |
| `+0x4b4` | selected spell index; `+0x4a8` = spells page has entries | [dec] `3422c8`, MenuMouse |

### Slot record (16 bytes)

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x0` | **equipped flag** (1 on the dagger in `item0`, on the bow in `item1`; mutually exclusive in the fixture) | [RAM] |
| `+0x4` | **hot-slot flag** (Cross toggles it, `341ce8`) | [dec] |
| `+0x8` | item: actor handle (> 0x53), or **continuation marker** `dx*10 + dy` (<= 0x53) pointing back to the origin cell (`origin = cell - v/10 - (v%10)*8`); 0 empty | [dec] `340510`, [RAM] (bow 1x2: cell 1 handle, cell 9 = 1) |
| `+0xc` | 3D icon object; exists only while screen 0x15 is open (0 after close) | [RAM] |

Slots `0x1a` / `0x1b` are not drawn on the Equipment page. `0x1b` holds the shop's current stock item while a shop is
open (magic5: a `0x100b2` spell scroll); the record is stale garbage when no shop is open. **[RAM]**

### Item (actor handle `h`)

| Field | Meaning |
| --- | --- |
| `h+0xc` | object type: **10** weapon, **11** usable (potion, scroll); details and skill checks need type 10 or 11 (`342f90`, `341858`) |
| `h+0x10` (u16) | resource index -> `rec = *(res+8) + ix*0x24`: `rec+4` type, **`rec+8` class**, `rec+0xc` property block (= `h+0x54`) |
| `h+0x54` | property block `{+4 data, +8 vtable 0x51bd50}`; `prop(id) = *(data + id*4)` (the getter `192e48(pb, id, -1)` is just that); indexed `prop(id, k) = *(data + (u16(data+id*4) + k)*4)` |
| `*(h+0xfc)+0x10` | instance data: **`+0x14` current, `+0x18` max**, both `dec()`. Bow: arrows (200/200 -> 181/200), melee: durability (144/150), -1/-1 = indestructible, potion 100/100 |

[RAM] for all rows. Note that `h+0x54` changed between "screen open" and "closed" dumps for the same handle: re-read it,
don't cache it.

Properties seen [RAM] / used [dec]: `0` name string id; `0x11` base price **[inf]** (bow 325, club 205, potion 100,
scroll 540, Rynn's dagger 0); `0x13` skill / level needed; `0x16` / `0x17` max quantity or durability (scroll: spell
index + 1 / level); `0x1a` melee damage **[inf]** (dagger 4 = "Damage: 4", club 8); `0xd` drop allowed (`33f2a0`).
The bow's damage (5) comes from its arrow item (`Wooden Arrows`), not a bow property **[inf]**.

### Equipped channels

`player+0xed0` / `+0xeac` are u16 actor-registry refs (`handle = *(*(ctx+0x7c) + (ref-1)*8)`); `321b88` resolves `ed0`
first, then `eac`. In `item1`, `ed0` points to a *different* object (`0xa74e10`, not the inventory dagger), so these
channels are the held weapon actors, not inventory identities. **For the UI, use slot `+0x0` as "equipped"** (it matched
the equip in both fixture snapshots). No armor / shield channel was found. The armor classes (below) presumably also
set `+0x0` **[inf]**.

### Hot slots / Quick Inventory

Hot-slotted items are the records with `+0x4 = 1`. Cross (`0x1c`) in Equipment calls `341ce8`, which toggles the flag. It
refuses with message `0x1af` (`3414a8` true; probably full) or `0x1ae` (requirement not met, `341858`) through
`44cd98(player, msg)`. Keys 1..5 send `0x403b..0x403f` and 6 / R1 send `0x403a` (`23cae0(1)`, the contextual Quick
Inventory). The quick-inventory order and slot count were not traced **[unknown]**.

### Reading everything per frame from the host

All of this is plain memory reads, already done by `MenuMouse` and `scripts/harness/*ram*.py`: 24 records, handle
checks (`> 0x53`, inside RAM, `h+0x10 != 0xffff`), class via `res`, and quantity via `dec`. The data is valid while
the screen is closed too, except `+0xc`. Guards: `ctl+0` == 0 (not mid-change), a player present, and no dragon
(`3d2798(S)` / `ctx+0x4b0` class `0x1001c`).

## 2. Names, descriptions, stats

* **There are no prose descriptions.** The details panel is the name plus stat lines. `433850` (called from the
  Equipment / shop draw `433610`, vtable slot `0x24`) draws the controller buffers above with `434080`. Those buffers
  are filled by the builder **`342f90(ctl, cell)`** (controller vtable `+0x2c`, called from `33eaf8` on every
  selection change, and from `3422c8` with cell `0x1c` for spells).
* `342f90` builds the text per class from item properties plus the labels of the UI strings resource (`2bec48()`,
  properties `0x130..0x144` -> string ids `0x12002d..`: Damage, Durability, Indestructible, Armor Rating, Speed, Range,
  Price, Charge, Skill Needed, Spell Level, Mana Cost, Exp. Level Needed, Arrows, Duration, Health Regained, Sell,
  Repair ...). Speed and range words are strings too (Very Slow .. Very Fast, Short / Medium / Long). In a shop it adds
  prices: `40bb18(shop, item)` for the stock item (`0x1b`), `40bc88(shop, item, 0/1)` for sell or repair **[inf]**.
* **Strings**: string manager `ctx+0x344` (`0x4dbca8`). Lookup `1b9fb0(mgr, id, buf, 1, max)` -> `12b640(mgr, {id, 6, 1, 0})`.
  Strings are resources in a 128-bucket hash: bucket `mgr[(id*0x59 & 0x7f) + 8]`, chain `+0x14`, `entry+0` id,
  `entry+8 & 3 == 3` loaded, `entry+0x1c` text. ASCII, already localised (the PAL disc's language). Id ranges: `0x3xxxx`
  item names, `0x6xxxx` spell names, `0x12xxxx` UI. **[RAM]** "Short Wood Bow", "Wood Club", "Health Elixir",
  "Rynn's Dagger", "Ice Blast Level 1", and the quest text "Find Lady Myschala at the palace in Surdana."
* **Residency**: only strings that something requested are cached: 65 entries in magic5. Quest text appeared only in
  the Journal dump, and properties 10..12 of items are ids that were not cached. So the host can read names **without
  the native screen drawing** only when they are resident. The safe options:
  1. Read the cache directly (pure memory). Fall back to the class name or "…" when absent.
  2. Request missing ids from an EE-thread hook (like `MenuMouse`'s `GuestCall(0x2885c8)`) with `1b9fb0(..., flag 1)`:
     with flag 1 and `mgr[0x8b] == 0` it starts an async load and returns without spinning; it does take
     `mgr[7]` (`4a0208`, `WaitSema`). Flag 0 **spins** until loaded. **[dec]**, untested.
* **Stats per item without the native screen**: either re-implement `342f90`'s per-class formatting from the
  properties (a large function, ~1000 decompiled lines, only partly mapped), or read the native buffers for the
  selected item. Steering the native selection to the hovered item (as the mouse code does) makes the buffers describe
  it within a frame. Comparison needs the equipped item's buffers, cached from when it was last selected, or a second
  steer. Calling `342f90` on another cell and back is possible on the EE thread, but it can early-return while the
  labels are unloaded **[inf]**.

## 3. Categories

There is no explicit category field. Use `h+0xc` (type) plus `rec+8` (class, a resource **type**, not an item identity:
Rynn's Dagger and Wood Club are both `0x1000e`):

| Class | Type | What | Evidence |
| --- | --- | --- | --- |
| `0x1000e` | 10 | melee weapon (melee skill, `3d5a30(S,1)`) | [RAM] dagger, club |
| `0x1004f` | 10 | bow (archery skill, `3d5a30(S,2)`) | [RAM], [dec] `341858` |
| `0x1002d`, `0x10090`, `0x100c7` | 10/11 | needs **Exp. Level** (prop `0x13` vs `dec(S+0x28)`): armor / shield **[inf]** | [dec] `341858`, `342f90` |
| `0x1000a`, `0x10059`, `0x100ab` | 11 | used directly by Square (potions etc.); `0x1000a` = Health Elixir | [dec] `33f630`, [RAM] |
| `0x100b2` | 11 | spell scroll (prop `0x16` = spell index + 1, `0x17` level; magic skill `3d5a30(S,3)`) | [dec], [RAM] |
| `0x10027`, `0x1005f` | ? | own branches in `342f90` (ammo / other) **[unknown]** | [dec] |
| others | | keys / quest items: not identified (no fixture) **[unknown]** | |

## 4. Spells, skills, stats, journal, map

| Data | Location | Evidence |
| --- | --- | --- |
| Spell list | `list = player+0x1024`: table `player+0x1028`, count `player+0x1030` (10), 0x14-byte entries: `+0` known level (0 = not learned), `+4` resource index of that level's resource (class `0x100d6`), `+0x10` state | [RAM] magic5: Fireball L1 known |
| Spell type and name | `38f478(list, i)`: `prop(*(*(list)+0x80)+0x54, 0x1b6, i)` -> resource (class `0x100ae`), its prop 0 = name ("Fireball", "Ice Blast") | [dec], [RAM] |
| Selected spell / casting | `ctl+0x4b4`; `player+0x1040` 1..4 = summon running (selection refused) | [dec] `3422c8` |
| Skills | `*(S+0)` table, count `*(S+8)` = 3, 0x4c-byte records `{id, level, max, name[...]}`: 1 Melee, 2 Archery, 3 Magic (levels 1, max 8), **plain ints**, name ASCII inline | [RAM], [dec] `3d5a30` |
| Unspent skill points | `dec(S+0x2c)` (`0x4fbaf4`) | [dec], MenuMouse |
| HP / max | `dec(S+0xc)`, `dec(S+0x10)` | [RAM] 100/100 |
| Mana / max | `dec(S+0x1c)`, `dec(S+0x20)` | [RAM] |
| **Gold** | `dec(S+0x18)` (`0x4fbae0`, drawn "%u" by `4330b0`) | [RAM] 376 / 106 / 595 in shop saves, 0 in the fixture |
| Experience | `dec(S+0x24)`; added by `3d5020(S, xp)`; next-level fraction `3d5208(S)` | [dec] |
| Character level | `dec(S+0x28)`; thresholds from property `0x1f7`; a level-up adds to `S+0x2c` | [dec] `3d5020` |
| Inventory open flag | `S+0x34` = `DAT_004fbafc` (1 while 0x15 is open) | [RAM], [dec] `33c660` / `42f758` |
| Quests / map markers | `DAT_004fbb70`, count `DAT_004fbb78`, 0x20-byte records: `+0/+4` map x, y (markers), `+0xc` icon, `+0x10` enabled, `+0x14` region, `+0x18` quest resource index -> props `0` status (1 / 2 listed), `1` optional, `2` text string id, `3` caption id | [dec] `4387b0`, `393448`, `3937c8`, `393868`, [RAM] 6 entries |
| Regions | names `DAT_005126b8 + i*0x20` (i < 14; only "Surdana" filled in this save); Journal region = `section+0x58`; map region = map section `+0x70`, background via `DAT_004fbb68` | [RAM], [dec] |

Primary quests are listed with heading UI property `0x12e`, optional ones with `0x12f` ("Primary Quests" / "Optional
Quests").

## 5. Actions: native entry points

Every action below is a key into the controller's key handler (`345d08` -> `33f100`), reached through screen 0x15
(`430d68` msg `0xb` -> `4309d8` -> section slot `0x3c`). Pad -> DIK: Square `0x2b`, Triangle `0x0e`, Cross `0x1c`,
Circle `0xc7`, arrows `0xcb / 0xc8 / 0xcd / 0xd0`, R1 / L1 `0xd1 / 0xd3`.

| Action | Path | Notes / safety |
| --- | --- | --- |
| Select item | arrows -> `344de0` -> `33eaf8(ctl, cell)` (mouse hook substitutes the target) | synchronous, rebuilds details |
| Use / equip | Square, pane 0 -> `33f630` | posts message `0x1045` through `ctx+0x33c` (deferred) or uses directly (`0x1000a/0x10059/0x100ab`); refused on the dragon |
| Drop | Triangle -> `33f2a0` | needs prop `0xd`; refusal plays a sound |
| Hot slot | Cross, pane 0, not shop -> `341ce8` | toggles record `+4`; refusal messages `0x1af` / `0x1ae` |
| Sell | Circle in a shop -> `33f520` -> `40c948(shop, item)`, else `40f2c0(shop,10)` refusal | opens the native Yes / No box (`4319f8(DAT_004fb664, 2)`, `44b328`) |
| Buy | Square, pane 2 -> `341268` -> `33e960(ctl, ctl+0x1c, 1)` | Yes / No box; a refusal is silent |
| Spell select / Summon | `3422c8(ctl, i, 0)` / Square in Spells -> `3422c8(ctl, ctl+0x4b4, 1)` -> `342478` | refused while `player+0x1040` is 1..4 or on the dragon |
| Spend / return skill point | Skills section key handler `445898` (slot `0x3c`); Right / Left | leaving the page with changes opens the "apply points?" box |
| Section switch | R1 / L1 -> `4309d8` | the old section may refuse (`0x4c(0)`) |
| Open / close | `0x4030` -> `345b10(ctl,1)` / `33c660`; Select closes (`42f640` -> `42f758`) | the open path calls the screen factory `2c4458`, which can block: tail-call only (see `save-slots.md`) |

Recommendation: keep the existing approach. The host UI posts a request ("select cell n, press Square"). `MenuMouse`
emits pad taps, and the `33eaf8 / 3422c8 / 33e960` entry hooks substitute the index. The action is pressed only when
the native selection equals the target and `ctl+0 == 0`. Do not call `33f630`, `33f2a0` and the rest from the host:
they post messages, open boxes through the factory, and play sounds. Native refusals stay native (and mostly silent),
so the host UI has to watch for the effect: the record flags, the quantities, gold, or `0x4fb60c` becoming a box
screen. It should not assume success.

## 6. Pausing and hiding the native screen

* While 0x15 is open: `DAT_004fbafc = 1`, gameplay input is disabled (`ctx+0x358`'s vtable `+0x434` slot `0x11c` =
  `InputDev_SetEnabled(0)`, back to 1 on close), and `player+0x90 |= 0x400000`. The camera is moved to frame Rynn
  (saved at `cam+0x510..`), and many player / combat paths return early on `DAT_004fbafc` (`Player_GatherInput`
  zeroes movement, `324038`, `32a488`, `26fe48`, `27f658`, `460670` ...). **[dec]**
* The engine is **not** paused: the engine-level pause (`142688`: `ctx+0x4c4 & 2` or `ctx+0x4fc`) is 0 with the screen
  open, and the frame counters `ctx+0x4c8/+0x4cc` advance (15316 -> 17364 between two open dumps). But NPC positions
  and AI states were identical across four inventory-open dumps spanning about 2000 ticks, while Rynn's preview moved.
  So the world is effectively frozen for actors **[RAM]**, by a mechanism not identified **[unknown]**: gates on
  `DAT_004fbafc` in actor code, or the idle enemies were simply still. A live check with a moving enemy or projectile
  is needed.
* **Hiding while staying "open"**: keep 0x15 active (its handler `430d68`, section updates and key handling all keep
  running, so the steering works), and replace the per-frame draw **`42fd18`** (msg 10) with: set `DAT_004fb604 = 0`,
  `DAT_004fb608 = 1.0` (no world crop, as `42f640` does on close), and skip the page, the section draws, the tabs and
  the hints. Then draw the host UI over the world (optionally dimmed, since the world camera shows the preview
  framing). Consequences to handle:
  * the `437170 / 4481e8 / 445b78 / 44a678 / 438be8` mouse captures no longer run;
  * the details buffers do still update, because they are built on selection, not draw;
  * the native 3D icons and the Rynn preview are not drawn (`+0xc` icon objects, and the preview, which uses
    `player+0x2e8`);
  * the first-visit help boxes and the Yes / No / apply boxes are separate screens over 0x15 (`0x4fb60c` changes):
    draw them natively or show a host equivalent and answer them with taps.

  Untested **[inf]**: whether anything in `42fd18` (the UI camera install `430e90`, or the renderer slot `0x8c`) is
  needed by the section update or by the item-icon objects.

## Biggest unknowns

1. How actors freeze under 0x15 (live check).
2. Names and stats of items not yet resident in the string cache. Is an async `1b9fb0` request from an EE hook safe?
   Otherwise the full `342f90` re-implementation, or steering the selection per item.
3. Class ids for armor, shields, keys and quest items, and the armor equip slot (needs a later save).
4. Quick Inventory order and capacity (`23cae0`, `341fd8`, `3414a8`).
5. Hiding `42fd18` without side effects; the first-visit help-box flags.
