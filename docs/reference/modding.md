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
* **`ModManager::extraLevels()`** (valid after `installFiles`) returns `std::vector<ExtraLevel>{name, modId, path,
  listIndex}` for menus (std-only API). `ModManager::status()` entries gained `files`, `added`, `levels` counters.
* Order of work in `installFiles`: whole files, then entry patches on the resulting files (so a pack can ship a file and a
  later pack patch it), then `LEVELLST.TXT`.

* **`maps/*.ini`** (directly in `maps/`): multiplayer map and game mode definitions, see netcode.md "Multiplayer maps".
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
definitions. `text.ini` still only patches strings that exist; adding a string always goes through `new/`.
The editor's Strings and Quests panels write exactly these files (editor-core `story.ts`, `buildStoryPackFiles`).

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
