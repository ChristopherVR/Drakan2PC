# Mod packs

Status: format version 1, implemented in `runtime/hle` (`hle/ModPack.h`, `src/ModPack.cpp`) and wired into the game
runtime at startup. Tests: `runtime/hle/tests/mod_test.cpp`. Design background:
[level editor proposal](../proposals/level-editor-and-multiplayer.md) (edits ship as SRSC entry overrides, the ISO is never modified).

A mod pack is a **folder or a `.zip`** that replaces or patches entries of the game's SRSC resource files at load
time. Nothing on the disc image is touched; the packs live beside the game in `mods/` and apply to the player's own disc.

## Pack layout

```
my_mod/                    (or my_mod.zip; a single top-level folder inside the zip is accepted)
  mod.toml                 (or mod.json)
  overrides/
    INTERFCE.TXD/0040_97_0.bin       whole-entry replacement
    LEVEL1A.LVL/0100_12_x.bin
    INTERFCE.XDB/text.ini            localised text patches (type 0x400 entries)
```

`overrides/<FILE>/<type>_<id>_<sub>.bin` is the same layout as the dev override folder (`DRAKAN_OVERRIDES`,
[ui-prompts.md](ui-prompts.md#2-file-formats)): `FILE` is the resource file name without directories (case-insensitive,
for example `INTERFCE.TXD`, `LEVEL1A.LVL`), `type` is hex, `id` and `sub` are decimal, `sub` may be `x` (any sub id; an
exact sub id wins over `x` within one pack). The file content replaces the entry payload verbatim.

`overrides/<FILE>/text.ini` patches the localised strings of `.XDB` text entries, one `<id>[.<lang>] = text` per line
(`lang` = `en de fr it es` or `*`, default all; `;` and `#` start comments; escapes `\\ \n \r \t`; write the game's
glyph escape `\033` as `\\033`). Same syntax as the dev override folder.

## Whole files, new entries, custom levels (format 1)

```
my_mod/
  mod.toml                          levels = ["Level 9X - My Map"]     (top-level key, see below)
  files/ENGLISH/LEVELS/LEVEL9AP/LEVEL9AP.LVL    whole file, served as if it were on the disc
  files/ENGLISH/LEVELS/LEVEL9AP/LEVEL9TS.TXD
  overrides/LEVEL1NA.LVL/0020_0_0.bin           entry override of a .LVL (now applied, see below)
  new/RESOURES.ODB/0102_16630_11521.bin         a new SRSC entry added to an existing container
```

* **`files/<disc path>`**: the path after `files/` is mangled like every game path (`Level 9X - My Map` and `LEVEL9AP` are
  the same folder), so long names, `ENGLISH/...` and `/` or `\` separators all work. A path without a language folder lands
  in the running language tree. An existing disc file is **replaced**, an unknown path is **added**; a later pack
  replacing an earlier pack's file is reported as a conflict (`mods: conflict: file ...`) and wins. The bytes live in
  memory (`IsoFs::addMemoryFile`), so the guest sees a normal file: open size, read, seek and its SRSC index parse like on
  disc. Languages are separate trees: pack files for `FRENCH` are ignored in an English run.
* **Checks**: files with an SRSC extension (`.LVL .MOD .TXD .ODB .ADB .SDB .XDB .VDB .SSD .RRC`) must have a valid SRSC
  header and index, else `mods: ...: is not a valid SRSC container ..., file rejected`. Replacing an existing container:
  an entry that is larger than the 0x40800 whole-entry request limit where the original entry was not is rejected
  (`... past the 264192 byte whole-entry limit of the resource loader (original N bytes); file rejected`, the disc file
  stays). New files with such entries log a warning only (level data of that size is streamed in chunks). Pack files over
  256 MiB (`ModLimits::maxWholeFile`) or empty are rejected at load. The guest's 64 file slots count simultaneously open
  files, not files on the disc.
* **`overrides/<FILE>/...` for `.LVL` files** (the editor's export) are now applied. The guest reads `LEVELxx.LVL` with plain
  0x1201 file reads, so the file service rebuilds the container when a pack overrides one of its entries
  (`ModManager::installFiles`, `patchSrsc`): entries that fit are patched in place with the index size updated, larger ones
  are appended and the index is rewritten at the end. Overrides of containers read through 0x1202 (`TXD`, `XDB`, ...) are
  still applied per request.
* **`new/<FILE>/<type hex>_<id>_<sub>.bin`** adds an entry (sub must be exact, no `x`). `FILE` is a file name without
  directory (`RESOURES.ODB`); it is added to the file of that name in the running language tree, which is rebuilt like above.
  The entry is then an ordinary member of the container for 0x1202 requests. An existing `(type,id,sub)` is replaced.
* **Custom levels**: list the level names in the manifest, `levels = ["Level 9X - My Map"]` (top-level key, also
  `"levels": [...]` in mod.json). The name is the `LEVELLST.TXT` title: the game opens
  `\Levels\<Name>\<Name>.lvl`, so ship the files with `files/ENGLISH/LEVELS/<mangled name>/<mangled name>.LVL`
  (the mangler keeps 6 characters and the last 2 of an over-long name, `Level 9X - My Map` -> `LEVEL9AP`). The level also
  loads `<Name>_splats.{db,odb,txd,adb,mod,sdb}` -> `LEVEL<first 6>TS`-style names, i.e. `LEVEL9TS.DB` and
  `LEVEL9TS.TXD` for any name that mangles to `LEVEL9..` (the clone test copied Level 1A's `LEVEL1TS.*` under that name;
  missing optional files log `not found` and the game continues). The LVL's dependency table (type 0x0000) decides which
  ODBs the level pulls in, so a clone of an existing level works with no extra files. A level is only registered when
  its LVL exists on the merged disc. `LEVELLST.TXT` is rebuilt (original lines plus one CRLF line per level, no duplicates)
  and served to the game; a list the game reads once at start therefore contains the new level.
* **Level copies** (what the editor's "copy a level" exports): the manifest also carries
  `level_copies = ["City Copy = Level 1D - City of the Damned"]` (`Title = disc level`, top-level key), reported as
  `ExtraLevel::copyOf`. The runtime only uses it to name the copy's exits like the source's (`drakan_levelchange`
  `registerLevelCopy`). The copy's folder must hold the splat files under **both** names: `<copy>TS.*` (6 characters of the
  mangled copy title + `TS`, what the New Game redirect of a play test asks for) and `<source>TS.*` (`LEVEL1TS.*`: the LVL's
  dependency table still says `<source level>_splats`, which is what the game opens when the copy is entered through an
  exit). A missing `.DB`/`.TXD` there crashed the load (journal 2026-11-01/level-copy-campaign.md).
* **Pointing an exit at a copy**: an exit's destination is a level XDB string, `Levels\<name>\<name>.lvl`; override it with
  `overrides/LEVEL1ON.XDB/0400_<id>_1.bin` (text in all language slots) and the game's own transition loads the copy by that
  path, no runtime table involved. The string is shared by every exit that uses it; the editor's exit picker therefore
  offers "Only this exit" (default): it adds a **new string** to the level XDB for that one exit and points the exit
  object's own `Transition To...` parameter at it (string id in the offset half of the dword, length 0, exactly like the
  class default), so the other exits of the class keep the shared string.
* **`ModManager::extraLevels()`** (valid after `installFiles`) returns `std::vector<ExtraLevel>{name, modId, path,
  listIndex}` for menus (std-only API). `ModManager::status()` entries gained `files`, `added`, `levels` counters.
* Order of work in `installFiles`: whole files, then entry patches on the resulting files (so a pack can ship a file and a
  later pack patch it), then `LEVELLST.TXT`.

* **`maps/*.ini`** (directly in `maps/`): multiplayer map and game mode definitions, see netcode.md "Multiplayer maps". The map list
  is shared by both games: the Multiplayer menu lists the disc's (Drakan 2) maps and every pack's maps together, each with a game tag
  (`game =` key; maps of a pack with `campaign = "ootf"` default to Drakan 1). The editor's Order of the Flame import writes one
  `maps/<level>.ini` per `Multiplayer/*` level (`ootfArenaFromPc`: spawns from the Rynn launch pads, snapped to the terrain).
  The map also carries what an Order of the Flame arena needs to be playable (a Drakan 1 New Game starts with an empty
  inventory): `loadout =` (club and axe, granted at the start) and one `pickup =` (respawning weapon) per PC Weapon Pad.
  **Arena next to a campaign pack (fixed 2026-11-01).** The "dark Relic" (110 actors, Rynn at the default start 32768 200498 32768, a black
  screen) was not a size or count limit of any container. A pack with `campaign = "ootf"` makes `drakan_game::applyBase` redirect the opening
  cut scene file (`Level 0 - Intro`) to the campaign's first level, and that ran **after** the `DRAKAN_START_LEVEL` test aid had redirected the
  same file to the arena level, replacing it: the harness started "Relic" but the guest loaded the campaign's `OotF 00 - Intro` (the world dump
  says `levelName: OotF 00 - Intro`, the scene has 18 layers / 10 water layers instead of Relic's 32 / 0), while the netplay / arena layers
  still believed the level was Relic. Packs without the campaign key (arena alone, with one or two other levels) were lit, which is why the
  correlation with pack size looked real. `applyBase` now leaves an env-started redirect alone. The title-menu route (Multiplayer > map) sets
  its redirect after start-up and was not affected. Container sizes are not limited by anything found: 9.96 MB `RESOURES.MOD`, 26.9 MB
  `RESOURES.TXD`, 8 MB `RESOURES.ADB` (2000 patches), 1779 classes in `RESOURES.ODB` all load (journal `2026-11-01/ootf-dark-relic-cause.md`).
  **`previews/<safe map name>.png`** (<= 8 MiB): the map's picture for the multiplayer menu; the name is the map's display
  name with every character outside letters, digits, `-` and `_` turned into `_`. At startup the game copies it to
  `previews/mods/` next to the exe (`ModManager::previewFiles()`); `previews/<name>.png` there wins over a pack's.

## Manifest (`mod.toml` or `mod.json`)

```toml
format = 1                      # optional, default 1; a pack with a higher number is skipped by older runtimes
id = "example.big-buttons"      # required: 1-64 of letters, digits . _ -  (unique among enabled packs)
name = "Big Buttons"            # optional (default: id)
version = "1.2.0"               # dotted numbers, used for dependency checks
author = "someone"
description = "Larger menu button textures."

dependencies = ["example.core>=1.0"]   # top-level key (write it above the tables); must be present and enabled, else
                                       # the pack is skipped; "id>=version" is optional

[target]                        # required: at least one of profile / hash
profile = "pal"                 # "pal", "usa", a list ["pal", "usa"], or "*" for any
hash = "0123456789abcdef"       # optional disc profile hash(es), 16 hex digits (see below)

[load]                          # optional hints; mods.ini order is authoritative, a violated hint only logs a warning
after = ["example.core"]
before = []
```

`mod.json` carries the same keys as nested objects (`{"id": "...", "target": {"profile": "pal"}, "load": {"after": []},
"dependencies": ["a>=1"]}`). The TOML reader understands the subset used above (strings, numbers, single or
multi-line arrays of scalars, `[table]` headers, `#` comments).

### Disc profile and hash

Overrides are byte-level patches of one specific release, so every pack must say which disc it was made for. The runtime
checks `target.profile` against its build's profile id (`pal` = SCES-500.06, `usa` = SCUS-971.28, see
[regions.md](regions.md)) and, when given, `target.hash` against the **disc profile hash**: FNV-1a 64 of
`"<serial>|<saveId>"` (PAL `SCES-50006|BESCES-50006`, USA `SCUS-97128|BASCUS-97128`). The hash identifies the release, not
the disc image bytes. The runtime logs its own value at startup (`[mods] disc profile pal hash <hex>`). A pack that does not
match is **skipped with a message**, the others still load. Entry numbering differs between regions, so use `"*"` only for
packs that do not depend on entry contents (for example text only packs keyed by ids that exist in both).

## The `mods/` folder and `mods.ini`

`mods/` sits in the play folder (`DRAKAN_MODS=<dir>` overrides the location).
Each subfolder and each `*.zip` is one pack. `mods/mods.ini` lists packs by folder name or zip name without `.zip`:

```ini
[mods]
example.core = on
example.big-buttons = on
experimental = off
```

* Order top to bottom is the **load order**; later packs win. `on` / `off` enable or disable (`1 true yes` also mean on).
* Packs found on disk but not listed are appended as `<name> = off` so the player opts in by editing the file.
* A listed pack that is missing from the folder is reported and skipped.

### The Mods page (in game)

PC Settings (F1) and the modern Options screen have a **Mods** page that lists every pack found at start-up: name,
version, author, target disc, a status line (`Active`, `Disabled`, `Skipped: <reason>` for a wrong disc, a missing
dependency, a newer format or a broken pack, `Enabled, loads after restart`), the overlaps it has with other packs
(`Overrides 'B' on 3 entries (...)`, `Overridden by ...`) and the description in the row help. Per pack: **Enabled**
(toggle) and **Load position** (1..N, Left / Right moves it up or down); plus **Open mods folder** and, once the list
differs from what the game loaded, **Restart game now**. Every change rewrites `mods/mods.ini` at once (comments and the
`[mods]` header are kept) and **applies after a restart**.

Why not live: the pack overrides are registered on the resource service once, before the first resource request, and
the guest caches what it loaded (textures uploaded to the GS, XDB tables, level data), with no way to drop an override
or invalidate those caches while the game runs, not even at the title screen. A live reload would need an override
table that can be swapped plus a guest-side flush; it was not attempted. Code: `runtime/settings`
(`ModsModel`, `ModsPage`), host glue in `drakan_settings_host.cpp` (`setModManager`).

### Merge rules

1. Every enabled pack is validated in this order: pack opens and has a manifest, format is supported, profile and hash
   match, ids are unique (the earlier pack keeps a duplicate id), dependencies are met (iterated to a fixpoint, so
   dependants of a skipped pack are skipped too). Failures are logged as `mods: skipping '<name>': <reason>`.
2. Entries merge per `(FILE, type, id, sub)`. The later pack wins. Overlaps (same key, or an `x` sub overlapping an exact
   sub) between packs are logged as `mods: conflict: ...` and listed in `ModManager::conflicts()`. Text lines conflict per
   `(FILE, id, lang)`.
3. Registration order in the HLE: mod entries first, then mod texts, then the `DRAKAN_OVERRIDES` dev folder and the
   built-in UI overrides (keyboard prompts, "PC Settings"), so a dev override or the prompt remapping sees and may modify
   what a pack provided.

### Size limits (why oversize entries are rejected)

`RSRCIOP` fails a whole-entry request longer than `0x40800` bytes with status -9
(`ResourceService::MAX_LEN`; [iop-and-io.md](iop-and-io.md) section 3). The guest's own buffer sizes are not known per
resource type, so the runtime does not raise the limit. Policy, enforced when the entry is requested:

* A replacement **larger than 0x40800 where the original entry was not** is rejected and the original entry is kept
  (log: `would grow from N to M bytes, past the 264192 byte whole-entry limit`). Such an entry would turn every request
  into error -9 and stall the game.
* Entries that are already larger than the limit (streamed in chunks with an offset, such as level data) may be replaced
  by larger blobs; the chunked requests apply to the replacement.
* Growth below the limit is allowed and logged (`grows A -> B bytes (guest buffer sizes unverified)`). A request that
  passes an explicit length is clamped to it, so a fixed guest buffer cannot overflow from that path; requests for the
  whole entry rely on the guest sizing its buffer from the original size, which has to be checked per resource type
  before a pack grows it.
* Empty replacement files and files larger than 64 MiB are rejected at load; other files of the pack still load.
* The 64 slot limit of `FileService` concerns simultaneously open game files, not entries or files on the disc; whole-file
  replacements are not affected (see the whole-files section above for their own checks).

### Content files: ODB, XDB, SDB, SSD entries (story editing)

The guest reads `.XDB .SDB .SSD .ADB .MOD .TXD` entries through the 0x1202 resource requests, so `overrides/<FILE>/<type>_<id>_<sub>.bin`
replaces **any single entry** of those files per request. `.ODB` (like `.LVL`) is **not** requested entry by entry (verified in game
2026-10-31), so an ODB override is patched into the rebuilt container at load (`mods: ... SYSTEM.ODB rebuilt with N entry patch(es)`). Both ways replace single entries, not only whole files and `.LVL` entries. The
replacement may be smaller or larger than the original (below the 0x40800 limit, see below); a request with an offset is
served from the replacement. `new/<FILE>/...` adds entries to those files too (the container is rebuilt for the running
language). Both paths are covered by `mod_test` ("content files"). Keys the editor uses:

| What | File | Entry | Ship as |
| --- | --- | --- | --- |
| a string (all five language slots) | `SYSTEM.XDB`, `RESOURES.XDB`, `LEVEL1ON.XDB`, ... | `0400/<string id>/<sub>` (eight `u16 length + text` slots, see level-format.md) | `overrides/` to change, `new/` for a fresh id |
| a quest class (journal text ids, map position, optional flag) | `SYSTEM.ODB` | `0102/<class id>/<group id>` | `overrides/` to change, `new/` for a new class |
| the field lists a new class needs | `SYSTEM.ODB` | `0103/<id>/<sub>` (zlib; one list per class record, in record order) | `overrides/` of the whole entry with the new class's list appended |
| a sound's name and subtitle text | `<LEVEL>.SDB`, `SYSTEM.SDB`, `NPCS.SDB` | `0302/<id>/<sub>` | `overrides/` (the `0304` ADPCM data is a separate entry) |
| a sequence | `<LEVEL>.SSD` | `0311/<id>/<sub>` | `overrides/` or `new/` |

Rules found while testing: a new class id has to lie **inside** the file's existing id range (use a free id below the largest
one; the editor picks one), strings are looked up by id (new ids work when `new/` adds the `0400` entry), and the `0103`
field-list entry must be replaced with the extra list appended when a class is added, otherwise the new class has no field
definitions. The ODB header record `0x0100` holds the class and group counts the guest sizes its tables from; the runtime
raises them when `new/` adds `0x0102` / `0x0101` records (log `mods: ODB header counts updated`), a pack that writes its own
`0100` override keeps it. Without that, 1-2 added classes happened to work and 3 or more corrupted the heap (the level never
loaded); with it 40 appended classes load ([journal](../journal/2026-11-01/ootf-odb-class-count.md)). Builds older than
commit 048b136 also dropped `overrides/<ODB>/0103` (check `rebuilt with N entry patch(es)`: new records + 1).
`text.ini` still only patches strings that exist; adding a string always goes through `new/`.
The editor's Strings and Quests panels write exactly these files (editor-core `story.ts`, `buildStoryPackFiles`).

### Shops (vendors)

| What | File | Entry | Note |
| --- | --- | --- | --- |
| a shop's stock list and price modifiers | `<LEVEL>.LVL` | the object list (`0020`), object parameters of the `0x100ac` object | `Objects For Infinite Sale`, `Objects For Sale`, `Item Sale Price Mod`, `Item Buy Price Mod` (the editor's Vendors tab, `docs/reference/level-format.md` section 16) |
| the price of an item (every shop that sells it) | `RESOURES.ODB` / `MAGIC.ODB` | `0102/<item class id>/<group>`, field `Cost to Buy (in Gold)` | in-place patch of one data dword, so the container is rebuilt with the same size |

**Verified in game** (2026-11-01, `scripts/harness/vendor_shop.py`): with both entries the Surdana blacksmith offers an added Iron Mace
and the Rusty Short Sword costs 7 gold instead of 165 in the shop screen; buying it takes exactly 7 gold. Journal:
`docs/journal/2026-11-01/vendor-shops.md`.

### What a pack can add (custom weapons and models)

Evidence and open questions: [mods-custom-content](../journal/2026-10-31/mods-custom-content.md). In short: a new
weapon class (`new/RESOURES.ODB/0102_<free id>_<group>.bin`, a copy of an existing class record), a new model id (`new/
RESOURES.MOD` copies of the five model entries `0200 0203 0204 0211 0207`, with the `0200` header's texture ids at
+32/+36/+40 pointed at new `new/RESOURES.TXD/0040_<id>_0.bin` textures) and instances placed through an `.LVL` object-list
override all resolve in the engine. Use ids inside the existing id range of the target file (an id above the file's
current maximum did not spawn). Authoring the class record fields (damage, grip, animation) is not possible yet: the
0x0102 body is only decoded for the model reference.

## Writing a pack by hand

1. Extract the entry with `analysis/scripts/iop_srsc.py` (or the editor) from your own disc.
2. Edit it and save as `mods/<pack>/overrides/<FILE>/<type hex>_<id>_<sub>.bin`.
3. Add `mods/<pack>/mod.toml` with the manifest above.
4. Start the game; `mods/mods.ini` gets `<pack> = off`; change it to `on` and restart.
5. The log shows `mods: loaded '<id>' ...` plus any skip or conflict messages.

Authoring tools (editor export, `scripts/` helpers) should write exactly this layout.

## Campaign packs (Drakan: Order of the Flame)

A pack whose `levels` are a whole converted Order of the Flame campaign says so in the manifest (top-level keys, above the
tables; the editor's OotF export and `editor/scripts/ootf-convert.ts --campaign 1` write them):

```toml
levels = ["OotF 00 - Intro", "OotF 01 - Ruined Village", ...]   # play order
campaign = "ootf"                                    # the only campaign kind so far
campaign_name = "Drakan: Order of the Flame"         # optional: the title menu's label (default that text)
campaign_start = "OotF 00 - Intro"                   # optional: the level New Game opens (default the first of `levels`)
```

Only a pack that is **enabled and loaded** with `campaign` set counts (the first one in load order if several; the start
level must be one of the pack's registered levels, else `[game] pack ...: campaign start level ... is not one of its
levels` and no campaign). Without such a pack the game behaves as before: no game choice, Drakan 2 only. A pack with plain
`levels` (single maps, level copies) is unchanged and never offered as a game.

What the runtime does with it (`drakan_game.cpp`):

* **Title menu**: when a campaign is installed, a card "Choose your game" (Drakan / Order of the Flame) opens with the title
  and again after Back on the main page; Esc on the card keeps the current game. The main menu (New Game, Load Game,
  Continue, Multiplayer, Options ...) then runs for the chosen game. The label under the menu names it.
* **Title art**: the native title art carries the Drakan 2 logo, so while Order of the Flame is chosen the menu draws a text
  plaque over it ("DRAKAN" and the part of `campaign_name` after "Drakan: ", i.e. "Order of the Flame"). `campaign_name` is
  the only manifest key involved; a pack-supplied title image (`campaign_title_image`) is not implemented (the text plaque
  is generated, no original art ships). Drakan 2 keeps the native art.
* **Co-op**: New Game > Co-op hosts/joins on the campaign's start level (`drakan_game::firstLevel()`, level list = start
  level then the pack's other levels); the host's level changes follow the pack's own exits.
* **New Game** in Order of the Flame redirects the disc's opening cut scene level (`Level 0 - Intro`, which New Game opens
  first, then Level 1A) to the campaign start level at the file layer, so the Drakan 2 movie / cut scene never plays and the
  campaign's own intro level comes first. (Before this a pack only registered levels, New Game still ran the Drakan 2 flow
  and `DRAKAN_START_LEVEL` replaced only Level 1A after the intro.) The level chain after that is the pack's own exits
  (journal 2026-10-31/ootf-campaign.md).
* **Saves**: Order of the Flame uses its own memory card folder `<card>_ootf` (next to `mc0`; `mc0_ootf`, or
  `<DRAKAN_MEMCARD_DIR>_ootf`), created on first use and seeded with a copy of the options (`<saveId>Settings`) of the
  Drakan card, so the two games' saves never mix. Continue / Load Game / Save Game / quick save and the save previews all
  follow the selected game because they use the one configured card root.
* **Multiplayer and co-op are shared**: one Multiplayer menu for both games, maps of either game selectable (arena maps come
  from the disc's levels and from every pack's `maps/*.ini`). A multiplayer launch sets its own redirect; when it ends the
  campaign redirect of the chosen game is restored.
* **Settings** `drakan.ini [general] game = ask | d2 | d1` (env `DRAKAN_GAME`): `ask` (default) offers the card when a
  campaign is installed, `d2` / `d1` fix the game without a card (headless runs, harness): `d1` without an installed campaign
  logs it and starts Drakan 2.
  `scripts/harness/ootf_level_run.py --campaign ...` converts the level as a campaign pack and runs with `DRAKAN_GAME=d1`.

## HD textures for converted OotF mods

One command after converting (campaign pack, multiplayer arenas, any pack):

```sh
node --experimental-transform-types --no-warnings editor/scripts/ootf-convert.ts ... --out <pack.zip> --hd [--hd-out <dir>]
# or on existing packs, any number at once:
python -I analysis/scripts/ootf_hd_pack.py <pack.zip> [<more.zip> ...] [--out dist/DrakanPC/textures]
```

It runs `hd_pack.py modextract` per zip (every SRSC member: level splat TXDs, `PROCTEES.TXD`, `RESOURES.TXD`, the per-pack model and
creature texture files, hashed like the runtime) into the shared index `build/hdpack-mod`, then one `upscale` (Real-ESRGAN) into
`dist/DrakanPC/textures`, merging `alias.txt`. Incremental: zips unchanged since the last index are skipped (`zips.json`), hashes that
already have a PNG are not redone, a failed Real-ESRGAN chunk is retried tiled and then skipped and reported (exit 1, rerun to retry).
Cost for the 13-level campaign plus the 10 arenas: 3129 unique textures, 1382 not yet in the pack, index 18 s + upscale 162 s on an
RTX 5080; the textures folder grows by about 1.2 GB (4969 files, 1.86 GB with the disc pack). Output is never committed. Default
settings (`texture_upscale = auto`, `texture_dir = textures`) apply it; no pack change. Details: `docs/reference/hd-textures.md`.

## Menus and HUD (UI skins)

The modern menus (title, pause, Options pages) and the multiplayer HUD read a small INI file, so a pack can rename and
reorder entries, restyle the theme and change a few layout numbers without code. Engine neutral: the file only names
stable ids (`new_game`, `pause.root`), so another front end (for example Unreal UMG) can read the same data. Code:
`runtime/gameui/include/gameui/UiSkin.h`; editor: the **Menus** dock tab (Level mode), `editor-core/src/uiskin.ts`.

Where it comes from, later keys win: `ui.ini` next to `drakan.exe`, `DRAKAN_UI_SKIN=<file>` (testing), then
`ui/*.ini` of every enabled pack in load order (`ModManager::uiFiles()`). The game logs `[ui] skin <source>: N setting(s)`
and one line per rejected key. A pack ships only the lines it changes. `configs/ui/default.ini` lists every key with its
compiled-in default (generated and checked by `gameui_skin_test`; `DRAKAN_WRITE_DEFAULT_SKIN=1` rewrites it); loading it
changes nothing.

```ini
[theme]            ; name = #RRGGBB or #RRGGBBAA; accent sets ember and a matching glow (explicit keys still win)
accent = #20C0A0
size_body = 26     ; size_title / size_heading / size_body / size_small / size_numeric, design units (1920x1080)
[hud]
accent = #FF3366   ; respawn countdown, chat names, results highlight, Co-op banner
[layout]           ; title_column_w, title_row_h, title_row_step, title_centre_y,
title_row_h = 64   ; pause_menu_x, pause_menu_w, pause_panel_y, pause_row_h, pause_row_step
[rows]             ; page = ids in order; ids left out are hidden
title.main = new_game, continue, load_game, options, quit
pause.root = resume, options, save, load, quit
[strings]          ; 
 for a line break
title.row.new_game = Begin Anew
title.desc.new_game = A fresh start.
pause.row.resume = Return to Game
```

Pages: `title.main`, `pause.root`, `pause.options`, `pause.audio`, `pause.controller`. Row ids are the snake_case of the
row (`pauseRowId` / `titleRowId`); every colour, string key and default is in `configs/ui/default.ini`. Rules: a
listed id the game has no use for right now (Continue without a save, Multiplayer when disabled) is skipped; a list with
no known id falls back to the default order; entries cannot be added, only reordered, hidden and renamed (the actions are
engine code). Hiding Quit / Back is allowed; the editor warns. The Back hint on the pause menu ("Resume") and the page
titles are not skinnable yet. Screens read the skin when built, so it applies from the next launch.

The Menus tab edits all of this with a 2D mock preview (layout and colours; fonts and art are the game's), stores the
work in the browser, exports `ui/menus.ini` as a zip or into the mods folder, and the project export includes it when it
changes anything.
