# Save game editor feasibility (native `.rsg`) — 2026-10-31

Read-only investigation. No repo files were changed except this note. Markers: **[V]** read in a file or
verified with a script on a local save, **[L]** seen live in earlier journals, **[U]** inferred, needs a check.
Local saves inspected with throwaway scripts in the scratchpad: `build/final-game/.../Level 1A - Surdana.rsg`,
`build/harness/magic5/card/...` (paired with `build/harness/magic5/ram_k0.ram`, a 32 MB RAM dump of the same
session), and a 400-file sample of `build/**/Slot*/*.rsg`. Save contents are not copied here.

## 1. On-disk layout

A save folder is `mc0/BESCES-50006SlotN/` (region `save_id` from `scripts/buildtool/regions.json`; the per-region save profile is `configs/regions/<region>.json`, key `save_fixture`).
It holds `icon.sys` (964 bytes, PS2 `PS2D` header, Shift-JIS title "Gates 1..4"), `FolderIcon.ico`, and
`<Level name>.rsg`. The `.rsg` is an **SRSC container** [V], the same container family as the game's data files
(`analysis/scripts/iop_srsc.py`, `docs/reference/iop-and-io.md` 5.4):

| Offset | Field |
| --- | --- |
| `+0` | `'SRSC'` |
| `+4` | u16 version `0x0101` |
| `+6` | u32 index offset |
| `+10` | u32 entry count |
| `+14` | u16 pad |

- Index at `index_offset`, `count` x 14-byte entries `{u16 type, u16 id, u16 sub, u32 offset, u32 size}`, running to
  EOF with no trailer [V]. In the Surdana sample: index `0x259d8`, 840 entries, file size 165,832, all ranges in bounds.
- Payloads start at offset 16 and sit back to back (not 16-byte aligned in the originals; the fixture repack pads to 16). **Every payload is a zlib stream** (`78 xx` header), and all
  840 inflate cleanly [V]. Compression is per payload; the index is not compressed.
- **Checksums:** none found at container or index level [V]. No CRC or length trailer exists; the only integrity
  is the zlib Adler-32 per payload. `combat_save_fixture.py` rewrites the index and recompresses edited payloads
  with no other bookkeeping, and its outputs loaded in game per `docs/journal/2026-10-17/combat-encounters.md` [L].
  Whether the game's validator (`0x23f170`) checks anything beyond SRSC parse and zlib inflate is **[U]**.

### Entry types seen in the Surdana save (sample of one)

| Type | Count | Meaning |
| --- | --- | --- |
| `0x102` | 710 | Object / resource records. Each starts with u32 `+0` (unknown, 0xa50 for Rynn), u32 `+4` (unknown), u32 `+8` = **class id**, floats `+12..+23` = world position. Sizes up to ~6.7 KB. |
| `0x104` | 118 | Small records (max 68 bytes raw). Purpose unknown **[U]**. |
| `0x400` | 1 | Level path: u16 length (50) + `Levels/Level 1A - Surdana/Level 1A - Surdana.lvl` [V]. |
| `0x401` | 3 | Id 891: level name "Surdana". **Id 893: the skill table** (below). |
| `0x402` | 1 | 8 bytes, `07 00 00 00 01 00 00 00`. Unknown **[U]**. |
| `0x23` | 1 | 32,784 bytes: 16-byte header (128, 128, 16 bpp, ...) + CT16 128x128 thumbnail [V header, U meaning]. |
| `0x21`, `0x24`, `0x51`, `0x22`, `0x302`, `0x311` | 1 each | Large or small blocks, purpose unknown. `0x22` contains a `cdrom0:/English/...` path. |

### Skill table (type `0x401`, id 893) [V]

Count u32 `3` at payload `+0x3d0`, then records from `+0x3d4` with stride `0x4c`: `{u32 id, u32 level, u32 max, char name[64]}`
(Melee 1/1/8, Archery 2, Magic 3). This matches the RAM layout in `docs/journal/2026-10-24/tab-rework-data-model.md`
row "Skills" (plain ints, inline ASCII), so **skills are plain and directly editable**.

### Encoding in RAM vs on disk

RAM stores some stats XOR-rotated: `dec(x) = rotr32(x ^ key, 8)`, key at `0x4fe86c` [V, tab-rework doc]. The
on-disk values are **plain** u32s: the decoded RAM value 84 appears as a literal `0x54` in the save record below.
Do not apply the RAM transform to file bytes.

## 2. Serializer and loader in the game

Known from the existing notes (addresses SCES-500.06):

- Write: the native Yes of the save box runs `0x443ac0` (save class, writes the selected slot) [V, modern-save-load-title].
  The level-change save is mode 1 through `0x361f60` (exit trigger) [L, save-slots.md].
- File name: `"%s/%s.rsg"` built by the level transition `0x362d38` (`NEW LEVEL %s`) [V].
- Load: `0x43b4d0` validates through `0x23f170`, then the load starts (screens 4 -> 14 -> gameplay) [L].
- Folder names: pointer table `0x4fe3b8` -> `Slot1..4` cells at `0x5325c8` [V].
- **The inner serializer (what writes the 0x102 records and the 0x401 skill table) is not identified.** Not in
  `analysis/functions.csv` or `analysis/regions/usa/function_map.csv`. `build/recomp_ui/alldecomp/` holds only 23
  files, so a full Ghidra decompile of the ELF is needed to find it. Start from the callees of `0x443ac0` and the
  readers of `0x4fbac8` (the player stats block) and the resource database at `ctx+0x320`.

## 3. Editable fields (candidates)

Values from the Surdana save and the magic5 RAM dump of the same session. The class and offset constants are in
`configs/regions/pal.json` (`save_fixture.player_class` = 386404457 = `0x17081069`, Rynn).

| Field | Where in save | Evidence | Confidence |
| --- | --- | --- | --- |
| Position (X,Y,Z) | Rynn record `0x102`, floats at `+12` | `combat_save_fixture.py` moves it, verified in game [L] | High |
| Skills (level, max) | `0x401` id 893, records at `+0x3d4` stride `0x4c` | names inline, values match RAM skills [V] | High |
| HP (current) | Rynn record `+0x708`, u32 | RAM HP dec = 84 and the record holds 84 [V, one save] | Medium |
| Max HP | not found; RAM says 100 | 100 occurs elsewhere; no clean match [U] | Low |
| Gold | record class `0x19001255`, id 63, `+0x448` | RAM gold dec = 376, unique hit in that save [V, one save] | Medium |
| XP / level / skill points | `0x102` id 77 / 247 candidates | multiple hits, unresolved [U] | Low |
| Inventory items | `0x102` item records or `0x104` small records | Not located in the save; RAM keeps item handles in the controller's slot records (tab-rework doc) [U] | Unknown |
| Quest flags / map markers | not located (RAM table `DAT_004fbb70`) | [U] | Unknown |
| Play time | no field known | `playSeconds = -1` in `gameui` | None |

**Warning on the survey.** The 400-file sample is mostly duplicates of the same starting state (a few distinct
gold values, 376 in 175 files). Gold `0x448` also reads a different raw value in 79 files, which suggests a second
record layout for that class. Treat the two medium rows as hypotheses until a controlled edit proves them.

## 4. Plan for a minimal editor

**Language and home.** Python 3.11+, stdlib only (`struct`, `zlib`, `json`), matching `analysis/scripts/iop_srsc.py`
and `scripts/harness/combat_save_fixture.py` and the repo's ruff config.

- `scripts/savetool/rsg.py`: parse, validate and repack (read index, inflate payloads, edit by (type, id) and
  offset, recompress, rebuild index). Keep the parser shared with `combat_save_fixture.py` instead of a second copy.
- `scripts/savetool/fields.json` or a Python table: field name, record selector, offset, type, bounds (HP <= max,
  skill level <= max, gold >= 0 and fits u32).
- `scripts/savetool/cli.py`: `show`, `set`, `repack` commands, each writing a receipt like `fixture.json`. A GUI can
  come later (tkinter is stdlib). Tests go in `scripts/tests/` so `scripts/run_tests.py` picks them up.

**Never edit the source card.** Follow the fixture rule: copy the save folder to `build/save-edit/<name>/mc0`.

**Validation loop (no hand copies into `dist/`):**

1. Round trip: parse and repack an unmodified `.rsg`. Every decompressed payload must match byte for byte and the
   index must line up. Add this as a unit test on a synthetic container, not a real save.
2. Edit a field in the copy, then re-read the copy with the tool and check the diff touches only the intended bytes.
3. Launch `dist/DrakanPC/drakan.exe` with `DRAKAN_MEMCARD_DIR=<copy>/mc0` (documented in `docs/reference/env-flags.md`
   and `BUILDING.md`). The play folder stays unmodified. Load Game -> slot 1 and check the change in the pause
   and skill screens (Tab screen, Skills page), plus the RAM check through the harness (`scripts/harness/`).
4. For the HP and gold rows, change the value to something unusual (for example gold 9999, HP 37) so a stale
   value cannot pass as a match.
5. Save the receipt next to the edited copy. Keep any test artifacts in `build/`.

**Side effect to document.** The HD preview is keyed by the `.rsg` content hash (`<save>.rsg.<fnv64>.preview.png`).
Any edit changes the hash, so the HD companion stops matching and the native 128 x 128 thumbnail is shown.
Rename or re-render the companion if that matters.

## 5. Key unknowns

1. The inner serializer and its function address (needs a full ELF decompile).
2. The validator `0x23f170`: how strict it is about payload contents.
3. Whether HP and gold are authoritative in the save or are re-derived on load (the 0x102 record vs `0x4fbac8`).
4. Layouts for inventory, quest flags, map markers and the `0x104` records.
5. The USA `save_id` folder name and whether the USA save layout matches PAL (`configs/regions/usa.json` has its own `save_fixture`).
6. Whether the game's 16 slot pages (`save_pages`) share one format (they do, per save-slots.md [L]).

## Corrections from building savetool (same day)

`scripts/savetool/savetool.py` follows these, measured over the local harness saves:

- Rynn's record entry id is not stable: 53 in 1050 saves, 56 in 403. Position is matched by class (0x17081069) only.
- HP at +0x708 holds only in id-56 records; id-53 saves have other data there, so `get/set hp` require `--force`
  for them. HP is still unconfirmed as authoritative (vs re-derived on load).
- The skill count is not at +0x3d0: it sits 4 bytes before the first row, which is located via the "Melee" name.
- Gold (record class 0x19001255, id 63) read 376 in 305 of 403 saves, so the field location holds but the value varies.

## Inventory, equipment and state (second pass, same day)

Method: Node and Python probes over the 60 distinct local harness saves (copies of `build/harness/**/mc0/*/*.rsg`
read in place, nothing written back), diffing record sets and Rynn records between saves from known actions
(dagger vs club, bow, health elixir, fresh vs played). **[V]** = checked on those saves.

Confirmed, now in the field table (TS `save.ts`, `savetool.py`):

| Field | Layout | Confidence |
| --- | --- | --- |
| `arokh.pos` | 0x102 record whose resource id (u32 at +0) is `0xa60` (RAM resource `0x20a60`; Rynn is `0xa50`), class `0x1b001041`, floats at +12. Present in Surdana saves only, far from Rynn [V] | low (symmetry with Rynn, never moved in game) |
| `progress.KEY` | State table at the **end** of type 0x401 id 893: rows `{u32 kind, u32 key, u32 value}` (12 B) run to the payload end. Start found by walking back from the end while `kind` is in {20, 50, 68, 75, 100}, never into the skill rows. Keys are unique in all 60 saves; 94 to 368 rows, fewer after play than at the start (124 in a fresh Surdana save, 94 after play) [V] | layout verified, meaning low |
| item text | Rynn's record keeps the inventory screen's UI text buffers (name, "Skill Needed", "Damage", "Speed", "Range", "Durability: a/b" or "Arrows: a/b", "In left hand"). These are the item last selected on the Equipment page, not the inventory. First bytes of lines are often overwritten (`ange:`, `urability:`, `L=Speed`); the readers repair the known cases [V] | read only |

Negative results (what the save does **not** show):

- **Inventory grid and equipped items.** Saves with different inventories (dagger vs club, bow, elixir) hold the same set
  of 0x102/0x104 records, so items are not separate records. In one lineage the cells of the controller appear in Rynn's
  record around `+0x980` with a 16-byte stride and a 2-byte phase (values `0xa6`, `0xa6`, `0xa7`), which look like
  registry handles, not ids that can be resolved offline. Not decoded; no inventory edit is offered.
- **Rynn's record is not a fixed layout.** Lengths seen: 6734, 6738, 6998, 7082, 7090, 7202, 7230, 7454. The "hp" word
  at +0x708 in entry-id-56 saves moves to +0x75c (7090-byte layout) and +0x764 (7082), so HP sits behind variable
  content. The value 84 appears only in one session lineage; HP is still not confirmed as authoritative. **Max HP and
  XP/level: not found** (no stable word across layouts holds 100 or a plausible level).
- Quest/story flags beyond the state table, the checkpoint, and enemy alive/dead flags: not found. The state table
  shrinking with play suggests it tracks live actors, but its keys only sometimes equal 0x102 entry ids.
- `0x302`, `0x311` (4 to 6308 B), `0x21`, `0x24`, `0x51`: variable blocks, not decoded.

## Validation rules (editor and `savetool validate`)

Duplicate (type, id, sub) keys (except the two 0x401/891 level-name entries every save carries), level path missing or
not `.lvl`, undecodable thumbnail, skill level above max (fix: set level to max), skill max above 99, Rynn record count
and finite position, gold above 10,000,000 (second layout), hp above 100,000, state table empty or with repeated keys.
