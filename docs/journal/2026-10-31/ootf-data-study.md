# Drakan: Order of the Flame (PC) data study (2026-10-31)

Source: the user's own copy of OotF (retail disc image, the official 4.4.5 patch package, the Level Editor package and the
AiO community patch). Everything below comes from **data files**; no executable was run, decompiled or disassembled
(the three `.exe` patch/installer files and `Drakan.exe`/`Dragon.rfl` were not opened beyond their sizes). Throwaway
parsers lived in the session scratchpad (not in the repo); extraction went to `build/ootf/` (gitignored). No OotF
data, images or text dumps are committed; this note carries layouts, counts and short label strings only.

Tags: **[data]** read from the files and cross-checked, **[inf]** inference, **[U]** unknown / not stored in data.

## 0. Headline results

1. **The PC menus are not described by layout data.** `Dragon.rrc` (the engine's resource container) holds localised
   strings, option value tables and a few textures, but **no layout/widget records** (nothing like the PS2 `0x600` records)
   and no coordinates. The PC GUI is built in code (`Dragon.rfl` / `Drakan.exe`, off limits). So per-screen *rects do not
   exist in data*; what we get is the exact control inventory, labels, order of authoring, option value lists, and the
   retail README, which describes every multiplayer screen in prose.
2. **Complete multiplayer option lists are in data** (max players 2-12, kills limit, time limit, 0-4 teams, friendly fire,
   master-server registration, connection speeds, level groups) plus the server browser, scoreboard and message strings.
3. **Modes and rules** are mostly encoded in the level data: a `Level Type` field (0 air, 1 ground, 3 Master of the Dragon),
   dragon launch pads vs Rynn launch pads, a Soul Crystal + red launch pad in Master of the Dragon maps, `Recharge Rate
   Multiplier` 0.25 in those maps, and per-item respawn intervals.
4. **PC `.lvl`/`.odb`/`.mod` are the same family as the PS2 files but not byte-compatible.** Object placement parses exactly
   with the OpenDrakan layout on all 23 PC levels (16312 objects); terrain is a single zlib block per layer whose size
   formula holds for all 4445 layers; the PS2 files differ in specific, listed ways (section 5).
5. **Custom content** is: drop a folder under `Multiplayer/` containing `<name>.lvl/.odb/.db/.txd/.mod`; the engine scans
   that tree, level groups are `.lfl` text files, and clients must hold files identical to the server's (section 4).

## 1. What was in the archives, and how it was read

| Package | Contents used |
| --- | --- |
| `Drakan.zip` | `DRAKAN.mdx` (Alcohol image). Despite the name it is **raw 2048-byte-sector ISO9660 starting at file offset 0x40**, uncompressed (PVD at 0x8040). Also two older patch zips (AiO 2.170, 4.4.5 English). |
| disc `SETUP/DATA1.CAB` | 280 MB InstallShield 5 cabinet with the whole game (337 file entries, 246 after skipping `.exe/.dll/.sdb/.adb/.wav/gif/jpg/ico`). 7-Zip cannot open it, no extractor was installed, so a small pure-Python reader was written (see 1.1). |
| Official 4.4.5 package | `data1.cab` (same cabinet format): `Dragon.rrc`, `Readme_Patch.txt`, updated `.odb`/`.lvl`. |
| Level Editor package | `data1.cab`: `Engine.exe`, `Level Editor.exe`, `Modeler.exe`, `Control.rfl`, `RERES.dll`, `working.pal`, 3ds Max / SoftImage exporter plug-ins. Executables not opened. |
| AiO patch zip | Loose copies of the levels (`.lvl`, `.odb`), `Arokh.ini`, `Dragon.rfl` etc. (see section 4.4) and a readme. |

### 1.1 InstallShield 5 cabinet, in our own words

`ISc(` signature; the common header (20 bytes) holds the descriptor offset (0x200) and size. In the descriptor: the file
table offset at +0x0c, file table size at +0x14, directory count at +0x1c, file count at +0x28. The file table is an array
of `directory_count + file_count` u32 offsets (relative to the table) followed by NUL-terminated directory names and
42-byte file descriptors: name offset u32, directory index u32, flags u16 (0x04 compressed, 0x08 entry unused: 22 such
placeholder entries exist), expanded size u32 at +0x0a, compressed size u32 at +0x0e, absolute data offset u32 at +0x26.
Compressed data is a series of `{u16 length, raw deflate chunk}` blocks; concatenated inflate output equals the expanded
size for every file. Everything is a single volume (no `.hdr`).

### 1.2 Resource containers on PC

Same `SRSC` family and the same 14-byte index entries `{u16 type, u16 id, u16 sub, u32 offset, u32 size}`, but the **PC
header is 12 bytes: `SRSC`, u16 0x0100, u32 index offset, u16 record count** (PS2: 16 bytes, u32 count, version 0x0101).
This matches the OpenDrakan description. **[data]**

## 2. Menu and multiplayer-flow data

### 2.1 Where the strings are

`Dragon.rrc` (retail 559 records, 4.4.5 adds a few): 426 records of type **0x0400** (strings, XOR-obfuscated with a rotating
32-bit key; the OpenDrakan note calls it 0x402 but it is 0x400), 17 of type 0x0401 (plain: integer tables and the credits), 115
textures (type 0x40) and one palette. IDs group by screen; the file order of the records (authoring order) is the same as
the control order within a screen. Level names in `.lvl` headers and item names in `.odb` class defaults refer back here with
a `<0xNNNN>` prefix (for example `<0x9006>Alwarren` = string id 36870); the prefix is followed by an English fallback.

| ID range | Screen / purpose |
| --- | --- |
| 5184-5192 | Main menu hint line (one hint per gem, see 2.2) |
| 5224-5259 | Multiplayer player-setup screen and its hints |
| 5296-5303, 5208-5223 | Host Game screen: fields, option names, option value lists |
| 5312-5334 | Join Game browser, Add Server dialog, Server Info |
| 5280-5286 | Scoreboard / stats columns |
| 12544-12587 | Network messages, errors, round-end messages |
| 12579-12582 | Game-type names |
| 36876-36885 | Multiplayer level display names |
| 16385-16471 | Key-binding names (includes network chat, change teams, map/scoreboard) |

### 2.2 Main menu (single-player and multiplayer entry) [data for inventory, U for positions]

The PC main menu is a **ring with seven gem buttons**: `Interface.odb` has one class, "User Interface Properties" (37
fields), naming the models for an inner ring, an outer ring, and seven crystals (top, left, middle, right, lower left, lower
right, bottom), per-crystal "note offset" floats (a musical pitch per gem), button crystal/ring models for ordinary and wide
buttons, a hover loop sound, click sounds, scrollbar sounds, a music id and the difficulty scale percentages (easy 75,
hard 128). `Interface.mod` holds the matching 14 models. The README calls the entry "the Multiplayer gem". Seven hint
strings exist: Multiplayer, Load, New Game, Save, Options, Credits, Exit to Windows, plus "Press ESC to Resume the Current
Game". The mapping gem position to action is not in data. [U]

### 2.3 Multiplayer setup screen (README 4.1 + ids 5224-5259) [data]

Controls in file order: Player Name field, Host Game button, Join Game button, an "mplayer.com" button, Player Color (two
colour boxes: hue/saturation and brightness, with a live Rynn portrait to the right), Human Skin (rotating portrait that
cycles through armour/clothing styles in the chosen colour), Dragon Skin (slider under a rotating dragon portrait), Connection
Speed (drop-down: Modem 28.8k, Modem 33.6k, Modem 56k, ISDN 112k, LAN/T1 1M). Hint strings per control (host, join,
mplayer.com, hue/sat, brightness, dragon look). The colour chosen is also drawn beside the player's name in the stats box.
Skin counts come from `Skins.odb`: Rynn skin model array has **4** entries, the dragon has **7**; each class also has an
array of three animation references. The dragon skin does not apply in Master of the Dragon.

### 2.4 Host Game screen (README 4.3, ids 5296-5303 and 5208-5223) [data]

Top to bottom: Server Name (default "<player>'s Server"), Password (blank = open), Level Group, Starting Level, a Game
Options box with six rows, and Begin Game. "All Levels" is the first level group; further groups are `.lfl` files in the
`Multiplayer` folder (section 4). Option rows, with the integer value tables that sit next to the labels:

| Row | Values (table in data) |
| --- | --- |
| Maximum Number of Players | 2, 3, ... 12 (11 entries). README says "1 to 20 and No Limit" and recommends 8 or less on the internet; the shipped table is 2-12. |
| Kills Limit | No Limit, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100, 150, 200 |
| Time Limit | No Limit, 5, 10, 15, 20, 30, 45, 60 (minutes) |
| Number of Teams | No Teams, 2, 3, 4 |
| Friendly Fire? | No, Yes |
| Register With Master Servers? | No, Yes |

Validation messages: enter a player name, a server name, pick a starting level; session creation failed; no levels in the
Multiplayer folder; unreadable level list file (then all levels are used).

### 2.5 Join Game screen, Add Server, Server Info (README 4.2, ids 5312-5334) [data]

Table columns: **Server Name, Current Level, Players, Ping**; sorted by ping. Row colours: red = version-incompatible,
white = internet/custom (custom entries get a "+" prefix), blue = LAN, green = selected. Password-protected servers are
marked next to the name. Buttons: Add Server, Remove, Server Info, Refresh, Stop (while scanning), Connect (README: lower
right). Status text: "Contacting Master Server...", "Master Server Contacted", "Contacting Server...", a wait message,
"Available Servers: %d", "Valid Servers: %d". Add Server asks for Server Address and Query Port (default port offered).
Server Info shows IP Address, Current Level, a players list (name, kills, ping) and a Server Rule / Rule Value table (rules
sorted alphabetically, standard rule names Admin_Name, Admin_Email, Admin_Web, URL_Levels, URL_MOD from `ServerRules.txt`),
with Connect and Cancel.

### 2.6 Team select, scoreboard, chat, results [data from README, 4.4.5 notes and strings]

* **Team select**: in a team game (Number of Teams > 0) every joining player sees a team list after the level loads and
  picks one with a single click; the "Change Teams" key reopens it. The scoreboard is shown while choosing so team sizes
  are visible (4.4.2). Default team names Fire, Water, Earth, Air; names and RGB colours are `TeamNameX`/`TeamColorX`
  (X = 1..4) in `Drakan.cfg`, server side. All non-dragon players on one team get the same coloured skin; team colour
  shows left of the over-head name (4.4.5).
* **Scoreboard**: key "Map/Scoreboard" (TAB). Columns **Player, Kills, Deaths, Ping, Time**; server STATUS additionally
  prints Kills/Hour and Time Online. Colour swatch beside each name. Team scores are separate from player scores (4.4.5).
* **Chat**: "Network Chat" key (backslash toggles global and private; team text yellow). Server-side flood protection
  (message: you cannot chat for 15 seconds). SAY/TEAMSAY can be bound.
* **Results**: **no results screen exists in data**. When the kills limit or time limit hits, a banner message is shown and
  the next level in the level group loads a few seconds later ("Game KILL limit reached!..." / "Game TIME limit
  reached!..."; two sound events). The server can log results to `serverscores.txt` (`LOGSCORES`).
* Event sound slots (class "Multiplayer Sounds"): time limit reached, kill limit reached, public chat, private chat,
  a player died, new dragon owner.

### 2.7 Unknown / not stored

Pixel positions, rect sizes, fonts, button art placement, the gem-to-action mapping, the colour picker geometry, the tab
order. Seven small integer tables (ids 5170-5175, 8194, 217-222 entries) look like font/text metrics, not menu geometry. [U]

## 3. Modes and rules as defined in data

| Mode (string / `.lfl`) | Folder | Level Type | Content of the maps [data] |
| --- | --- | --- | --- |
| "Air Only Match" / `Air Only.lfl` | `Multiplayer/Air/` (4 maps) | 0 | Dragon launch pads (4-8), Dragon health power-ups, dragon invulnerability; Nemesis and Grotto of Death also place a Dragon and Rynn pads; Alwarren of Death / Arena of Death have only dragon pads |
| "Ground Match" / `Ground Only.lfl` | `Multiplayer/Ground/` (3 maps) | 1 | 12-18 Rynn launch pads, weapon pads, Rynn health; no dragon at all |
| "Master of the Dragon" / `Master of the Dragon.lfl` | `Multiplayer/QOD/` (3 maps) | 3 | Rynn pads, one Dragon, one **Fire Dragon Soul Crystal**, one **red dragon launch pad**, DragonStrike pickups; `Recharge Rate Multiplier` 0.25 (dragon energy recharges at a quarter rate) |
| "Air and Ground" | n/a | n/a | label exists, no list file ships |

Details:

* **Spawning**: "Rynn Launch Pad" and "DragonLaunch Pad" objects are the spawn points (class "Rynn Launch Pad" /
  "DragonLaunch Pad", the latter points at the player dragon class). Pads per map: Hill of Fire 12, Relic 18, Sanctuary 15,
  Nemesis 4 + 4, Grotto of Death 8 + 8, Alwarren of Death 5 dragon pads. In 4.4.5 launch pads can be restricted per team.
* **Master of the Dragon** (README 4.5): one hostile unclaimed dragon; the first player to find the Soul Crystal bonds to it;
  killing the rider (or the dragon) transfers it to the killer, who is moved onto its back; the dragon then returns to full
  health; if the rider dies on his own the dragon becomes unclaimed again and the crystal reappears. Human and dragon health
  are not linked. The dragon class in these maps is the green/red MP dragon (class with Initial Mode, 3 s launch pad
  respawn, camera and speed tuning), not the single-player Arokh class.
* **Scoring**: kills per player; the round ends at the kills limit (per player, or per team in team games since 4.4.5) or the
  time limit, then the next map in the group loads. With both set to No Limit the round never ends. Scoreboard shows kills
  and deaths.
* **Respawn**: per-item `Respawn Interval (Sec)` on power-ups and weapons, for example basic melee 10 s, frag weapons 30 s,
  Rynn health 30 s (+25), armour 30/45/90 s (banded/plate/dragon), dragon health 10 s, DragonStrike 60 s, Greater
  Invisibility 150 s. Player respawn time is not a field in these classes; 4.4.5 adds Loser/Winner Respawn Time (5 s and
  2 s default) in "Level Properties" for **Arena** levels.
* **Weapon tiers** by class prefix: Start (Ugly Stick, 10 damage), Basic (Katana 15, Cutlass 17, Axe 20, Maul 21), Frag
  (Axe of Fire 25, LifeGiver 25, Thunder Hammer 30, Frost Axe 20, Crypt of Defense 35, SoulTaker 40), Gib (Dreadnaught and
  FragileNaught 80); damage maximum, weapon speed 0.8-1.2 are class fields. Dragon breath classes in the MP database:
  fire ball, lightning, magma, poison, ice.
* **Team colours**: not in data (only default names via README and the 4.4.5 "Level Properties" fields `Team 1..4 Name`,
  `Team Play Control`, `Allow Friendly Fire?`, `Ignore Team Colors?`, `Is Arena Level?`; a map may force these). The four
  `Alpha/Beta/Gamma/Delta Color` fields of "Level Properties" are present in all levels with mostly default values; their
  role is unknown, they are probably not team colours. [U]
* Per-level properties also set gravity (6000), flight ceiling (50000-300000), kill altitude (set in 3 maps), saveable flag.

## 4. Custom content in OotF

### 4.1 Layout on disk [data]

```
Drakan/                       (game root; Dragon.rrc, Dragon.rfl, Drakan.exe, Drakan.cfg)
  Common/<Lib>/<Lib>.{db,odb,mod,txd,sdb,adb}        shared databases (System, FX, Resources, NPC, Interface, Multiplayer, Skins)
  <World>/<Level>/<Level>.{lvl,db,odb,mod,txd,sdb,ssd,adb}    one folder per level
  Multiplayer/<Type>/<Level>/<Level>.{lvl,db,odb,mod,txd}     multiplayer maps (Air, Ground, QOD, or any new folder)
  Multiplayer/*.lfl                                  level groups (text)
  ServerRules.txt                                    optional name=value rules shown in Server Info
```

* A **database** is the set of same-named files in one folder (`.odb` classes, `.mod` models, `.txd` textures, `.sdb`
  sounds, `.adb` animations, `.ssd` sequences) plus a plain-text **`.db`** file listing its dependencies:
  `version 1`, `dependencies N`, then `index path` lines (paths relative to the file, no extension, `..` allowed; index 0 is
  the database itself). A class or resource reference is always `(id, dependency index)`.
* The `.lvl` carries its **own** dependency table in record 0x0000 (different indices from the `.db`, which serves the level's
  `.odb`). Multiplayer maps depend on `System`, `Resources`, `FX`, `Multiplayer Common`, `Skins` plus a scenery database;
  their own `.odb` holds only map-specific classes (a few KB), models/textures sit in the shared databases or the map's
  own `.mod/.txd` (multiplayer `.lvl` files are 9-72 KB).
* **`.odb` class** = a typed instance of an engine class in `Dragon.rfl` (`rfl_class_id`, e.g. 14 melee weapon, 28 dragon,
  54 level properties) with default field values; the per-class field definitions (type + name) travel with the record.
  A level object overrides only some fields. So weapons, power-ups, pads and dragons are **data-only variations of fixed
  engine classes**; new behaviour requires a new `.rfl` (not shipped for custom levels). 4.4.5 added several MP class types
  for designers (goal triggers, purchasable powerups, team-restricted pads/powerups, grouped powerups, arena options).
* **Custom models / skins**: models are `.mod` records referenced from classes; player skins are arrays of model references
  in `Skins.odb` (Rynn 4, dragon 7); weapon/armour models live in `Multiplayer Common.mod`. 3ds Max and SoftImage exporter
  plug-ins and the Modeler ship with the Level Editor.

### 4.2 How maps are found and picked [data: strings + README]

* Maps are discovered by **scanning the `Multiplayer` tree recursively for `.lvl`** (error strings: "no levels exist in the
  Multiplayer folder", "level name not found anywhere in the Multiplayer directory"). The display name comes from the
  localised string id in the level header, or its fallback text.
* **Level groups**: every `.lfl` in `Multiplayer/` is listed in the host screen next to "All Levels". Format: first line
  `LL`, then one relative `.lvl` path per line, CRLF, blank lines skipped (4.4.2 fixed a crash on blank lines). Retail ships
  three (Air Only, Ground Only, Master of the Dragon). Dedicated servers take `+levellist "name"`; the `levellist` console
  command switches live; `level <name>` jumps to a map by (partial) name.
* **Compatibility**: server and client must hold identical level files ("level files do not match the server files" for the
  joiner, and a message naming the player that mismatched). The version is also checked (red rows). Servers publish
  `URL_Levels` / `URL_MOD` rules so players can fetch the files; the README speaks of "MODs" = replacing shared databases.

### 4.3 Config files [data]

* `Drakan.cfg`: `bind <key> "command"` and `set VAR "value"` lines, rewritten by the game at every run. Notable MP variables:
  `MasterServerList`, `ServerQueryPort` (27046), `ServerGamePort` (27045), `TeamNameX`, `TeamColorX`, client port base 27910
  with `NumAlternatePortSelections`, max update rate (default 50/s).
* Command line: `+dedicated +host +connect +levellist +hostname +password +maxplayers +fraglimit/+scorelimit +timelimit
  +queryport +nomaster +master +name +retry`.
* **`Arokh.ini` is not content config.** It belongs to the AiO community patch's proxy `dinput.dll`: window/borderless
  hooks, server-browser URL, LOD and FOV multipliers, fog override, texel shift, and a `[445SP1]` list of level files for
  which special-attack animation tweaks are active (it ships `NoWhere.lvl`, a community map). Nothing about maps/models.
* The AiO patch shows the community's in-place modding style: it overwrites level `.lvl/.odb` and `NPCs.odb`, keeping a
  `.bak` of the original.

### 4.4 Level files in the AiO patch

Copies of the original levels with fixed visibility data (the layer visibility lists). The multiplayer folders in the
patch hold only the changed files, matching the "overwrite in place" style.

## 5. Format cross-check: PS2 Drakan tooling vs OotF files

Run: `lvl_objects.py` helpers (header and field-value decoding were imported unchanged) plus a scratch copy of the object
walker with the PC layout, over all 23 PC `.lvl` files (3 world sets, 10 multiplayer maps).

| Item | Result |
| --- | --- |
| LVL 0x0000 header | **Identical.** `lvl_objects.parse_level_header` parses all PC headers to the last byte. PC adds a `<0xNNNN>` localised-name prefix on `level_name`; `max_width/height` are 512/512 (Alwarren) and 128/64/32 for multiplayer maps (cells). |
| LVL 0x0001 layer definitions | **Identical** (same field order, 40 layers parse to the `1` marker exactly). |
| LVL 0x0001 layer data | **Different.** PC: after the definitions, `u32 1`, then per layer `u32 compressed_size` + one **zlib block** whose inflated size is exactly `(w+1)*(h+1)*4 + w*h*26`: vertices (4 B: type, 1, biased u16 height) then faces (26 B). **4445 of 4445 layers** match, no trailing bytes in any file. PS2: data inline after each definition, variable length (3844-5692 bytes for 25x25 layers, so compressed or packed), starting with `{u16 sequence number 1..n, u16 0x000c, u32 0x40 or 0x80}`. **Correction to `ootf-reuse-study.md`:** the second u16 is the constant 0x0c on every layer (not the face count). Not decoded. |
| LVL 0x0002 / 0x0003 | 0x0002 layer groups present on both. 0x0003 is one u32 per level on both (PC values 0x7fee0 (8 levels), 0x30d40 etc., looks like a height or depth limit). |
| LVL 0x0025 vs 0x0051 | PS2 has 0x0025 (6.5 KB in LEVEL0RO, mixed text/binary); PC has none. PC has 0x0051 (u32 zero in every level); PS2 sample has none. |
| LVL 0x0020 object list | **Same logical record, different packing.** PC: `u16 count`, then bodies **inline** (`u32 id, u16 class, u16 db, u32 lighting_layer, f32 xyz, u32 flags, u16 event_count, u16 link_count, u16 links[], u16 rot[3], f32 scale[3] if flags&0x100, u32 data_dwords, u32 param_count, u32 data[], u16 indices[], field_defs`). All 23 files consume exactly. PS2: id/class/db table first, 8-byte prefix (second word extra), `u32` link ids, a 0x10 flag with an f32 radius, signed rotations, and the `0 / 0xffffffff` pair before the data area. PC has **none** of those PS2 additions. |
| Object stats (PC) | flags only 0x0, 0x1, 0x100, 0x101 (so 0x10 radius and 0x01/0x11 combos are PS2 additions); `initial_event_count` is 1 for all 16312 objects and no event data follows; rotations unsigned 0-359; object ids are **not always ascending** (Alwarren, Grimstone Mines); lighting layer 0 for 15462 of 16312; up to 141 links; field types seen in params: 1,2,3,5,7,9,0x0a,0x0b,0x0f, arrays 0x1003/0x1005/0x1008/0x1009/0x100b/0x100c/0x100e and class types 0x2003/0x4003/0x5003. |
| Param value decoding | `lvl_objects.field_value` works unchanged: every string and array parameter resolves in range (0 errors); strings are NUL padded dword blocks addressed by `(offset, length)`. |
| ODB container | Records 0x0100 (library path, `..\..\Dragon.rfl` on PC vs `..\..\Drakan 2.rfl` on PS2), 0x0101 groups, 0x0102 classes, 0x0402 version (0x102 on PC, 0x109 on PS2). PC has **no 0x0103 pool**: PC 0x0102 records parse exactly (name, group, model ref, `u16 rfl_class_id`, `u16 icon`, `u32 field_count`, `u32 dword_count`, `u32 data[]`, then `field_count` x `{u32 type, name}`), 1/1 in every database checked. |
| ODB class record on PS2 | For the 33 of 54 sample classes with no model: the same up to the model ref, then **10 extra bytes (all zero)**, `u16 rfl_class_id`, `u16 library index (1)`, `u16 icon`, `u32 field_count`, `u32 dword_count`, `u32 data[]`, **no field_defs** (they live in the 0x0103 pool). The remaining 21 classes carry non-zero data in those 10 bytes (the existing rule says these are the records with a transform/model), and the rfl word is at +80 (the existing rule in `level-format.md` section 5). |
| Runtime class id | **Confirmed:** the low 16 bits of the PS2 runtime class id equal the PC `rfl_class_id` for shared classes (User Interface Properties 0x62 = 98, Level Properties 0x36 = 54, Level Transition 0x37 = 55, Dragon 0x1c = 28). So PC `rfl_class_id` is the key to the engine class and the OotF 4.4.5 class list in the OpenDrakan doc applies by number where the class exists on both. |
| Class defaults | PC 'Level Properties' has 16-20 fields (30 after 4.4.5); the PS2 class of the same engine id has 134 default dwords, so the PS2 engine grew many properties. Field-name text for defaults is therefore only available from PC for shared classes. |
| MOD models | Same record types 0x0200-0x0207 (PC adds 0x0209 LOD links and has a texture list 0x0206 per model; the PS2 sample has neither). PS2 0x0203 vertices are **16 bytes** each after a `u16` count (PC: 12-byte `vect3f`); PS2 0x0204 polygons store `u16 flags, u16 vertex_count, u16 texture` then only `u16` vertex indices (PC: 10 bytes per vertex, index + UV floats), so the PS2 UVs sit elsewhere (probably in the 16-byte vertex). PS2 0x0200 is longer (18 more bytes than PC for the same name). PS2 0x0207 is 96 bytes vs 88. |
| ODB level `.db` | Same plain-text format on both. |

## 6. Recommendations for our mod / custom-map system

Our current packs (`docs/reference/modding.md`: `mod.toml`, `files/`, `overrides/`, `new/`, `levels = [...]`) already cover
the "overwrite in place" model. To mirror OotF's useful parts:

1. **Per-map folder with a manifest**, found by scanning a `multiplayer/` (or `mods/*/maps/`) tree recursively; display
   name from the pack manifest, not from a magic folder name. Group maps by a `kind` (air, ground, master) instead of
   relying on the folder name.
2. **Level groups as plain-text lists** (`.lfl`-style: header line, one path per line, blank lines ignored) that the host
   screen lists next to "All Levels"; keep the format so existing community lists work.
3. **Content hash on connect**: host sends a per-map digest (for example CRC32 of the pack file set); a mismatch gives the
   OotF-style error naming the player. Optionally publish `URL_Levels`/`URL_MOD`-style key/value rules in the browser's
   Server Info.
4. **Data-only classes**: custom weapons, power-ups and pads are class defaults with overridden fields on engine classes;
   our editor can mirror this by storing only changed fields per object (already the format's model) and by adding new
   class records through `new/<file>/0102_...` entries. No new code classes.
5. **Skins** as arrays of model references per player class (Rynn 4, dragon 7 in the stock set), so the player-setup screen
   can enumerate them from the data.
6. Keep `Arokh.ini`-style host settings out of the pack format: they were engine tweaks, not content.

## 7. Open questions

* Positions of the PC menu widgets (code only). For our port, take the control inventory and ordering from sections 2.3-2.6
  and use our own layout (the PS2 `0x600` layout records in `DRAKAN2.RRC` are the framework we already use).
* Gem-to-action mapping on the PC main menu; role of the `Alpha/Beta/Gamma/Delta Color` level fields; meaning of Level Type 2
  (used by four single-player levels); the Level Properties `Level Type` enum labels (names are in code).
* PS2 terrain packing, PS2 class-record "model" block, PS2 vertex 4 extra bytes.
