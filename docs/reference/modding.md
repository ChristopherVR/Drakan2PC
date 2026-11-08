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
  The language trees are not identical: `LEVEL1ON.ODB` (Level 1 - Common) has 582 class records in English, 578 in German
  (4 English dialogue classes missing, id 2356 added), 585 in Italian (2352, 2355, 2356 added) and 583 in Spanish (2352
  added); every other ODB matches. A new class id that a language tree already uses loses to that tree's own record, so
  the OotF converter starts new ids 16 above the English maximum (`LANGUAGE_ID_MARGIN`).
  The entry is then an ordinary member of the container for 0x1202 requests. An existing `(type,id,sub)` is replaced.
* **Custom levels**: list the level names in the manifest, `levels = ["Level 9X - My Map"]` (top-level key, also
  `"levels": [...]` in mod.json). A name must be 1 to 80 printable ASCII characters without `\ / : * ? " < > | ;`
  (`validLevelName`; a refused name is skipped with a log line only, and the editor's `validateModMeta` rejects it
  before export). The name is the `LEVELLST.TXT` title: the game opens
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

* **`maps/*.ini`** (directly in `maps/`): multiplayer map and game mode definitions, see netcode.md "Multiplayer maps" and
  "Custom game modes" (`[mode <id>]`: teams and their names / colours, lives and last one standing, score, time and point
  limits, points per kill / death / team kill, respawn delay, spawn protection, pickup respawn, extra starting items, allowed
  weapons, capture / hold objectives). The editor's Multiplayer Map > Modes tab writes them; the game lists a pack's modes on the
  Host page's Game Mode row for every map that can play them. The map list
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
build = "a46d89ce1cbcd218"      # optional save compatibility id, see "Saves and pack builds" (the editor writes it)
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
* Two converted Order of the Flame packs cannot be on together: both replace the same widened `RESOURES.MOD` / `WORLDCON.MOD` files and hand out new class ids from the same ranges. The editor's Play on a converted level therefore installs its own temporary pack `editor-play-ootf.zip`, switches every `ootf*` pack off in `mods.ini` for the session, and puts `mods.ini` back and deletes its zip when the game closes (desktop shell `playpack.rs`; a leftover is cleaned when the editor next starts).
* Packs found on disk but not listed are appended as `<name> = new`: off, and not decided yet. They are never enabled by
  themselves; see "New packs" below. `off` is a decision (the player kept the pack off), `new` is not.
* A listed pack that is missing from the folder (moved, renamed, deleted) is reported and skipped. What happens to its
  line depends on the value:
  * `on`: kept (it is a decision, and the pack may come back). The title's main page names it ("A mod pack enabled in
    mods.ini is missing: <name>."), the Mods page has a **Missing** row and the pack's status reads
    `Skipped: not found in the mods folder`; the log has `[mods] pack '<name>' is enabled in mods.ini but missing from
    the mods folder`. Turning it off on the Mods page (or copying it back) ends the line.
  * `off`: kept and not named anywhere but its Mods page entry: the player does not want it loaded anyway.
  * `new`: the player never decided on it, so there is nothing to remember. `ModManager::load` removes the line from
    mods.ini (temp file + rename, comments and other lines unchanged) and logs
    `mods: new pack '<name>' is no longer in the mods folder: removed from mods.ini`. If the pack is copied back later
    it is found again as `new`.

### New packs (title notice)

A pack copied into `mods/` by hand (an editor zip, a download) used to sit there as `off` with no hint in game, so a
campaign never reached the "Choose your game" card ([issue #2](https://github.com/ChristopherVR/Drakan2PC/issues/2)).
Now:

* `ModManager::load` appends a pack it has not seen as `<name> = new` and reports it as `ModStatus::isNew` (also on
  later starts while mods.ini still says `new`). The log says `mods: new pack '<name>' found: off until enabled`.
* The title screen opens a **New mod packs found** card first (before the game card): the pack names, that new packs
  stay off until enabled and apply after a restart, and a line when one of them is a campaign. Buttons: **Enable and
  restart** (turns them `on`, writes mods.ini and restarts the game), **Open Mods menu** (the PC Settings Mods page;
  without the modern pages the F1 overlay's Mods page) and **Keep them off** (writes `off`: not asked again). Esc /
  Back (**Later**) closes the card for this run and leaves `new`, so it comes back on the next start.
* The card is shown at most once per run. The new packs card needs the modern title menu; with an explicit
  `[general] title_menu = native` it is not shown (the Mods page lists the packs).
* Harness runs: not shown while `DRAKAN_INPUT_SCRIPT` drives the input, so recipes keep their title flow.
  `DRAKAN_MODS_NOTICE=1` shows it anyway, `DRAKAN_MODS_NOTICE=0` never shows it.
* **Game choice explained.** When an enabled campaign pack exists but the card is not offered, the title's main page
  shows one line why, the Mods page has a **Game choice** row with the same reason and the log has `[game] ...`:
  `[general] game` (or `DRAKAN_GAME`) is not `ask`; the pack was enabled in this session and loads after a restart;
  the pack is enabled but was skipped (wrong disc, dependency, broken pack); or it loaded but its campaign keys were
  refused (invalid id, duplicate id, start level not one of its levels). The Mods page line adds the loader's reason.
* **Unreadable packs.** A zip or folder in `mods/` whose archive or manifest cannot be read (`ModStatus::readError`,
  kept whether the pack is enabled or not) has no manifest, so the game cannot tell whether it is a campaign. It is
  not offered on the new packs card (enabling it would load nothing); instead, while it is enabled or still `new`,
  the title's main page names the file ("A pack in the mods folder could not be read: broken.zip."), the Mods page
  has an **Unreadable** row and the pack's status line gives the reason, and the log has
  `[mods] pack '<file>' cannot be read (<reason>)`. Turning it off on the Mods page (or fixing / removing the file)
  ends the line. A pack mods.ini enables but the folder lacks gets its own line (see "mods.ini" above).
* **Note plate.** The title's note plate sits in the top left corner of the 16:9 canvas, clear of the logo, the
  campaign plaque and the art's copyright lines; the FPS counter moves to a bottom corner while it shows. Up to three
  notes (game choice, unreadable, missing) share its six lines: each gets an equal share (at most three), lines left
  over go to the notes that need more, in that order, and a note cut short ends in "...". The title's hint row
  (Select / Later / Cancel) moves up beside the art's first copyright line when it is long enough to reach the second
  one (two hints in French, Spanish, Italian and English); see `design::title::kCopyrightLine2Right`.
* The policy is `runtime/settings` `settings/ModDiscovery.h` (`showModNotice`, `modNoticeContent`, `gameChoiceNote`,
  `gameChoiceNoteText`, `unreadablePacks`, `unreadablePacksText`, `missingPacks`, `missingPacksText`) plus
  `ModsModel::undecided / enableUndecided / keepUndecidedOff`; tests in `mods_model_test` (policy, mods.ini values,
  page marks), `mod_test` (`new` in mods.ini, stale `new` lines dropped), `gameui_saveload_test`
  (`testTitleModsNotice`) and `gameui_locale_test` (the card's strings in every menu language). The title strings are
  in the `gameui::menuText` catalogue (German, French, Italian, Spanish); the Mods page stays English like the rest of
  the PC settings.

### The Mods page (in game)

PC Settings (F1) and the modern Options screen have a **Mods** page that lists every pack found at start-up: name,
version, author, target disc, a status line (`Active`, `Disabled`, `New: not enabled yet`, `Skipped: <reason>` for a
wrong disc, a missing dependency, a newer format or a broken pack, `Enabled, loads after restart`), a **Campaign** line
for campaign packs (enable and restart / restart / on the game card / not offered), the overlaps it has with other packs
(`Overrides 'B' on 3 entries (...)`, `Overridden by ...`) and the description in the row help. Per pack: **Enabled**
(toggle) and **Load position** (1..N, Left / Right moves it up or down); plus **Open mods folder** and, once the list
differs from what the game loaded, **Restart game now**. A pack found since the last decision carries `[New]` in its
title and the top section counts them (**New**); a pack whose flag differs from what the game loaded carries
`[Restart pending]`, and the page description then starts with "Restart pending". Every change rewrites `mods/mods.ini` at once (comments and the
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
| the field lists a new class needs | `SYSTEM.ODB` | `0103/<id>/<sub>` (zlib; one list per class record, in record order) | `overrides/` of the whole entry with the new class's list appended; on a language tree whose container has a different number of own class records the runtime keeps that tree's lists and appends only the pack's last lists, one per class the pack adds (`rebaseFieldLists`) |
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

### Puzzles: doors, levers, plates, timers

Measured in game (2026-11-07, `docs/journal/2026-11-07/door-proof.md`, test aid `DRAKAN_PLAYTEST_WATCH`). The editor's message
names map to the engine as Opens = On (`0x1001`), Closes = Off (`0x1002`), Locks (`0x1101`), Unlocks (`0x1102`).

* A **lever, plate -> counter, counter with two plates, or timer** that sends **Opens** to a door opens it (the door swings
  about its origin, about 100 degrees), and Rynn is blocked by the closed door and walks through the open one.
* A **locked door ignores Opens.** For a lever / plate / timer puzzle leave Locked **off** (and Auto-Open off if it must obey
  only the puzzle). The editor warns about Opens sent to a Locked door.
* **Unlocks only unlocks.** The door then opens by Auto-Open when the player walks up (Locked on + Auto-Open on + a key lock or
  an Unlock lever is the key puzzle), or by an Opens message.
* **A lever is One_Time by default** (Switch Type 0: `One_Time, Toggle, Crank, Reset`): the second press sends nothing. The
  editor's "Lever opens door" template sets Toggle, and then a second press sends Off and the door closes again (measured).
* **Plates:** "Send Message When" Both with On at the bottom and Off at the top (the "held plate" option, one plate straight to
  the door, no counter) is the release-closes form; counters count On.
* **Pressing E on an unlocked door does nothing**; doors open by message or Auto-Open only.
* **Ambush:** a creature with `Wait For Trigger` on stays dormant until it gets a message; a trigger volume sending On wakes it
  and it walks at Rynn (the "Ambush" template makes dormant copies plus the volume). A volume centred on the floor did not fire
  for Rynn walking through; one centred 1000 units higher did, so the templates raise their volumes by 1000.
* **Kill all:** creatures send "I'm Dead" (8) to their counter on death; the counter sends On to an unlocked door (proved by
  Blow Up from a volume killing two creatures: counter `0x1003` twice, door opens).
* Key lock, driven in game (2026-11-07, `puzzle_door_check.py` scenarios `keylock` / `keylock_nokey`, Shadowmire start, open
  ground): the lock (`0x353550`) reacts to message `0x10e4`, sent when Rynn uses the key item from her hand (`0x3195b0`). With a key
  item (class `Hermit Key.`, same `Lock Code` as the lock) 300 units in front of her, tapping E picks it up (state word +0x1c 1 -> 2),
  a further E at the lock (700 units away: she walks to it) uses it, the lock sends Unlock and the Auto-Open door swings (yaw 1.7);
  without a key item in the level the same E taps do nothing and the door stays shut. The key item must carry the lock's `Lock Code`
  (the template places only the lock: add a key of a class that already exists in the level and set its Lock Code). Validation warns
  when a lock has no key with its Lock Code in the level.
* Held plate (On pressed, Off released) and real sword kills for the kill-all gate (AUTOAIM) are proven (`plate_hold`,
  `killall_melee` in `puzzle_door_check.py`).
* Bow kills for the kill-all gate and a plate released by Rynn leaving it are proven too (2026-11-08, `killall_bow` with its
  no-bow control `killall_bow_nobow`, and `plate_walk_off`): with a Bow item picked up and equipped, arrows from 2300 units kill the
  two creatures and the door opens; without the bow it stays shut. See `docs/journal/2026-11-08/editor-leftovers.md`.
* Repeat all of it: `python scripts/harness/puzzle_door_check.py --iso <iso>` (PASS / FAIL per scenario; lever open and toggle,
  blocked and walk-through, plate, two-plate counter, timer, locked / unlock rules, ambush, kill all).

### Shops (vendors)

| What | File | Entry | Note |
| --- | --- | --- | --- |
| a shop's stock list and price modifiers | `<LEVEL>.LVL` | the object list (`0020`), object parameters of the `0x100ac` object | `Objects For Infinite Sale`, `Objects For Sale`, `Item Sale Price Mod`, `Item Buy Price Mod` (the editor's Vendors tab, `docs/reference/level-format.md` section 16) |
| the price of an item (every shop that sells it) | `RESOURES.ODB` / `MAGIC.ODB` | `0102/<item class id>/<group>`, field `Cost to Buy (in Gold)` | in-place patch of one data dword, so the container is rebuilt with the same size |
| a new shop | `<LEVEL>.LVL` | the object list (`0020`): three added objects | a `0x100ac` shop of one of the level's shop classes, a copied `0x1009d` shopkeeper, links shop -> keeper (first) and keeper -> shop, and a trigger box sending On to the keeper (editor: right-click the ground, "Add shop here"; level-format.md section 16) |

**Verified in game** (2026-11-01, `scripts/harness/vendor_shop.py`): with both entries the Surdana blacksmith offers an added Iron Mace
and the Rusty Short Sword costs 7 gold instead of 165 in the shop screen; buying it takes exactly 7 gold. Journal:
`docs/journal/2026-11-01/vendor-shops.md`.
A new shop made with "Add shop here" in Surdana (a copy of the Alchemist and its keeper next to the start) opens when Rynn presses Use
next to the keeper and sells its 4 potions; buying the Health Elixir takes 100 gold (2026-11-02, `docs/journal/2026-11-02/add-shop-here.md`).

### What a pack can add (custom weapons and models)

Evidence and open questions: [mods-custom-content](../journal/2026-10-31/mods-custom-content.md). In short: a new
weapon class (`new/RESOURES.ODB/0102_<free id>_<group>.bin`, a copy of an existing class record), a new model id (`new/
RESOURES.MOD` copies of the five model entries `0200 0203 0204 0211 0207`, with the `0200` header's texture ids at
+32/+36/+40 pointed at new `new/RESOURES.TXD/0040_<id>_0.bin` textures) and instances placed through an `.LVL` object-list
override all resolve in the engine. Use ids inside the existing id range of the target file (an id above the file's
current maximum did not spawn). Authoring the class record fields (damage, grip, animation) is not possible yet: the
0x0102 body is only decoded for the model reference.

## Saves and pack builds

A save restores the level's objects by class id and field layout. A save written with one build of a content pack and
loaded with another (class ids, logic defaults or level data changed between converter versions) does not fail cleanly:
the Volcanic sanctum quicksave of an older OotF pack stalled the restore on the current one (`[memset] bad size` from
`0x405714`, then the allocator spun; journal `2026-11-07/save-pack-builds.md`). So every save remembers which packs it
was made with, and the game refuses a save it cannot vouch for.

* **Pack build.** Every loaded pack that ships content (`files/`, `new/`, `overrides/`; packs with only maps, pictures or
  menu skins do not count) has a build id, `ModStatus::build`: the manifest's top-level `build` (1 to 64 of letters,
  digits, `.`, `_`, `-`; anything else makes the manifest invalid) or, without one, the **content hash** the runtime
  computes itself. The hash is FNV-1a 64 over one line per content file, sorted by lower-case path,
  `<lower-case path>\0<size>\0<crc32 as 8 hex digits>\n`, as 16 hex digits (`hle::mods::packContentHash`, `SaveRecord.h`;
  the zip directory's CRC is used, folder packs are read once). The editor computes the same value
  (`packContentHash` / `packBuildId` in `editor/packages/formats` `packbuild.ts`) and `zipModFiles` writes it into the
  manifest of every pack it zips (the OotF converter, level, story, campaign, arena and menu packs) unless the manifest
  already names a `build`. The manifest, pictures, `maps/` and `ui/` do not count, so a new cover or description keeps
  old saves. Both sides share a test vector (`save_record_test`, `packbuild.test.ts`: `8d3872dfe99031fa`). The log
  says `mods: '<id>' build <id> (from the manifest | content hash)` and `[saves] content pack '<id>' build ...`.
* **Pin it only on purpose.** A hand-made pack may set `build` itself and keep it across updates that leave every save
  valid (new textures or text only). Change it (or leave it out) whenever an update changes level, class or object data.
* **The record.** When the game writes a save (`<saveId>Slot<n>/<level>.rsg`, manual, quick and level-change saves
  alike, and the level-start save `<card>/<level>.rsg` that a death reloads) while content packs are active, it writes
  `<level>.rsg.mods` beside it (memory card write observer, `drakan_save_guard.cpp`; `[saves] <file>: pack record <id>
  <build>, ...` in the log). Before 2026-11-08 the level-start save had none, so a death in a converted level was
  refused and ended at the title ([report](../journal/2026-11-08/ootf-arokh-flyaway-crash.md)):

  ```ini
  ; Mod packs that were active when this save was written. ...
  format = 1

  [pack ootf-levels]
  name = Order of the Flame levels
  version = 0
  build = a46d89ce1cbcd218
  ```

  A save written with no content pack active removes an old record; a record whose `.rsg` is gone is removed too. The
  `.rsg` itself is never changed (the save editor and the HD preview companions keep working).
* **The load policy** (`hle::mods::checkSave`):

  | Save | Loads when | Otherwise (the player reads) |
  | --- | --- | --- |
  | has a record | every recorded pack is active with the same build (packs added since do not matter) | another build: "This save was made with a different version of the mod pack X. It cannot be loaded with the version installed now."; pack not active: "This save needs the mod pack X. Enable it on the Mods page and restart the game to load it." |
  | record unreadable (no `format = 1`, a pack without `build`) | never | "The list of mod packs stored with this save is damaged. The save cannot be loaded." |
  | no record (stock saves, saves made without content packs, saves of earlier game versions) | unless its level (the `.rsg` name) is registered by an active pack whose manifest declares a `build` | "This save was made with an earlier version of the mod pack X. It cannot be loaded with the version installed now." |

  The last row is how saves made before records existed are caught: such a save of a converted level was written by an
  older game with an unknown build of the pack, and a pack that declares a build was made by the current editor. A pack
  without a declared build cannot tell, so those saves keep loading as before. Stock saves are never refused for a pack
  that only adds levels; a save made with an entry override of a disc level records that pack like any other.
* **Refuse, do not repair.** The decision is made before the native load starts, and a refused save is never loaded:
  there is no safe fallback inside the restore (the bad sizes come from the save's own records, the level is already
  torn down when they are read), and guessing a migration could write a corrupt state into the next save. The save
  stays on the card untouched, so going back to the pack build it was made with loads it again.
* **Where it shows.** The modern Load screen draws such a slot as unavailable (dimmed, `Unavailable`, with its location
  and picture) and its detail panel gives the reason; it cannot be confirmed, and saving over it is allowed (the new save gets a new record).
  Continue stops on the Load screen at that slot. A quick load (F8, in game or on the title) does not start: the reason
  shows for 8 seconds in a band at the top of the screen (`HudSlot::Notice`). The native slot screens
  (`save_menu = native`) mark such a slot on the Load screen the way the native grid marks a slot without room for a
  save: no picture, and in the picture area, in the grid's grey, `Unavailable` and a short reason ("Made with an
  earlier version of a mod pack."; `gameui::savePackSlotNote`, wrapped by `nativeSlotNoteLines`); caption and date
  stay. Confirming it (Enter, Cross, a click) opens the screen's own message box with the game's "Load Failed!" text
  (property `0x105`) and an OK button, and the reason with the pack's name shows in the notice band; OK returns to the
  slot list, the running level or the title is untouched. Stock and same-build saves load as before, and the Save
  screen does not mark the slot (saving over it is allowed). Anything else that loads a save is caught at the native
  save check `0x23f170`, which then answers "not a valid save": the game shows its own "could not be loaded" message
  and opens the title, as for a damaged save. The texts are in the menu catalogue in all five languages
  (`gameui::savePackProblemText`, `savePackSlotNote`).
* Tests: `save_record_test` (hash vector, manifest key, record format, policy, ModManager build ids of zip, folder and
  declared packs), `gameui_saveload_test` (`testPackRefusal`; `testNativePackMarks`: which native rows are marked, the
  card text in five languages, `utf8ToLatin1`), editor `packbuild.test.ts` (formats) and
  `pack-build.test.ts` (editor-core: the stamped manifest).

## Writing a pack by hand

1. Extract the entry with `analysis/scripts/iop_srsc.py` (or the editor) from your own disc.
2. Edit it and save as `mods/<pack>/overrides/<FILE>/<type hex>_<id>_<sub>.bin`.
3. Add `mods/<pack>/mod.toml` with the manifest above.
4. Start the game; `mods/mods.ini` gets `<pack> = new` and the title offers **Enable and restart** (or set it to `on`
   in the file and restart).
5. The log shows `mods: loaded '<id>' ...` plus any skip or conflict messages.

Authoring tools (editor export, `scripts/` helpers) should write exactly this layout.

## Campaign packs (Order of the Flame and custom campaigns)

The editor uses `files/ENGLISH/...` as the portable namespace for converted assets. The runtime also mounts these
pack files in the selected disc language, so a converted campaign registers and loads with German or another menu
language selected. Explicit English paths remain available for generated dependency references. Later packs win;
within one pack, an explicitly localized file wins over its English fallback. Original ISO language paths keep their
normal meaning.

A level's dependency paths are relative to its own folder. A disc level names its own database by its bare name (the
intro's `Level 0 - Intro`), so a level converted on that template looks for `Levels\<level>\Level 0 - Intro.*`. The
runtime serves the sibling folder's database files there (`IsoFs::addAlias`, every extension but `.LVL`, after the
pack's entry patches, in the running language tree) and logs `names the database ... without a folder`; a bare name with
no such folder logs `... which is not on the disc; the level will not load`. The start-level redirect
(`DRAKAN_START_LEVEL`) rewrites only the redirected level's folder and the file names inside it that start with its
name, so such a database keeps its name. The editor's importer no longer offers the intro as a template and puts
Surdana first (`ootfTemplateLevels`). GitHub issue 1: `docs/journal/2026-11-07/issue-1-ootf-intro-template.md`.

A pack whose `levels` are a whole campaign says so in the manifest (top-level keys, above the tables). Two kinds exist:
the converted Drakan: Order of the Flame (`campaign = "ootf"`; the editor's OotF export and
`editor/scripts/ootf-convert.ts --campaign 1` write it) and **custom campaigns** made in the editor (Project Settings >
Campaign, "Make this a campaign"; any other id).

```toml
levels = ["Surdana Copy", "Kragmor Copy"]            # play order (copies or new levels of this pack)
level_copies = ["Surdana Copy = Level 1A - Surdana", "Kragmor Copy = Level 1B - Kragmor"]
campaign = "test-valley"                             # campaign id; "ootf" = Drakan: Order of the Flame
campaign_name = "The Test Valley"                    # optional: the title on the game card (default "Drakan: Order of
                                                     #   the Flame" for ootf, else the id)
campaign_description = "Two valleys, one road."      # optional: one line under the cover on the game card
campaign_start = "Surdana Copy"                      # optional: the level New Game opens (default the first of `levels`)
campaign_saves = "testvalley"                        # optional: memory card folder suffix (default the id)
campaign_title_music = 4                             # optional: PS2MUSIC.SDB sound id the title menu plays while
                                                     #   this campaign is chosen (the pack adds it, see below)
```

Plus, optionally, **`previews/campaign-<id>.png`** in the pack: the cover picture of the game card (PNG, at most 8 MiB, 16:9;
960 x 540 is what the editor writes). It goes through the map picture path (`ModManager::previewFiles()`, copied to
`previews/mods/` next to the exe; `previews/campaign-<id>.png` there wins). Without one the card shows the title on a plain
panel.
The built-in games use the same path: `previews/campaign-d2.png` (or `previews/game-d2.png`) is the cover of The Ancients' Gates and
`previews/campaign-ootf.png` (or `game-ootf.png`) the cover of Order of the Flame, so a pack can replace either picture.
`Build-LocalGame.ps1` writes `previews/game-d2.png` itself (PAL disc): `analysis/scripts/title_cover.py` decodes the title
screen backdrop (six 256 x 256 tiles, ids 72..77 of `INTERFCE.TXD`), crops its top 768 x 432 (logo, heroine and dragon, not the
copyright lines) and scales it to 1280 x 720; the file is a private derivative of the player's disc and is not tracked. There is
no equivalent for Order of the Flame: a Drakan 1 install carries no title picture (the converter's source has only the map,
loading and menu-frame art), so `game-ootf.png` / `campaign-ootf.png` stay hand-supplied and the card shows the plain panel
without one.

A pack can also ship **`previews/pack.png`** (else `previews/campaign-<id>.png`): its picture on the **Mods** page, shown in the
panel beside the list when the pack is selected (PNG, at most 8 MiB, 16:9; 960 x 540 is plenty). It is read for every readable
pack, **enabled or not**, so a pack can be looked at before it is turned on (`ModStatus::cover`; the game writes it to
`previews/mods/pack-<key>.png` next to the exe). Without one the panel says "No preview picture".

Rules (`drakan_overrides.cpp`, logged as `[game] pack ...`): only packs that are **enabled and loaded** count. The id is 1-32
of `a-z 0-9 _ -`, a letter or digit first, not `d1`, `d2`, `drakan` or `ask`; a second pack with the same id is ignored (the
first in load order wins). The start level must be one of the pack's registered levels (`levels`, LVL present), else
`campaign start level ... is not one of its levels` and the pack is not a campaign. `campaign_saves` is 1-32 of letters,
digits, `_ -` (an invalid one falls back to the id). Every valid campaign pack is listed, in load order. Without one the
game behaves as before: no game card, Drakan 2 only. A pack with plain `levels` (single maps, level copies) is never offered
as a game.

**Keep a campaign self-contained.** Packs are installed once at start-up for the whole session, so an entry override of a
disc level (`overrides/LEVEL1NA.LVL/...`, a shared XDB string) changes that level for Drakan 2 too. A campaign's levels
should therefore be copies or new levels, and its exits should use the "Only this exit" destination (a new XDB string plus
the exit's own parameter in the copy's LVL), which adds instead of replacing. The editor's campaign checks warn about both.

What the runtime does with it (`drakan_game.cpp`):

* **Title menu**: when a campaign is installed, the card "Choose your game" opens with the title and again after Back on the
  main page; Esc on the card keeps the current game. With `[general] title_menu = native` (Original) the same choice is
  asked first as a plain list in the original menus' style (heading and game names as text over the native title art, no
  panels, covers or descriptions; Up / Down, Enter, mouse, same sounds; Esc keeps the current game), then the game's own
  title menu carries on (`TitleState::classicGameChoice`, `drakan_menu_screens.inl` titleExecute SelectGame).
  The native title art (it names The Ancients' Gates) is hidden while the list is up: the screen is black, or shows the
  focused game's own background, `previews/background-<id>.png` (a pack ships it like `campaign-<id>.png`, in its
  `previews/` folder; the player can put `background-d2.png` / `background-ootf.png` in `previews/` next to the exe; the
  same lookup as the covers, `mapPreviewId`), dimmed under the text. `TitleState::Game::background`. It lists Drakan, then every campaign (Order of the Flame and the custom
  ones) on the left, and the focused game's cover and description on the right ("Playing" marks the current one; more than
  seven games scroll). The main menu (New Game, Load Game, Continue, Multiplayer, Options ...) then runs for the chosen game;
  the footer names it. Code: `gameui::TitleState::games`, `TitlePage::GameChoice`, `TitleActionKind::SelectGame` (value =
  card index), `drakan_game::select(index)`.
* **Switch Campaigns**: with more than one game installed the main menu has a "Switch Campaigns" button that returns to
  Choose your game without restarting (the same `SelectGame` / `drakan_game::select` path as at start-up, so the pack redirect,
  memory card folder, menu skin, title art and title music follow; Continue / Load Game rescan the other game's saves). Modern:
  a row before Quit (`TitleRow::SwitchCampaigns`, `TitleConfig::showSwitchCampaigns`). Original (the game's own page, which
  the host does not draw): a small button in the top right corner over the native title, pressed with the mouse, G or the
  pad's Back button; it works even when `game` is pinned (`TitleState::forceGameChoice`; `Shared::switchShown` /
  `switchRequest`, `drakan_menu_screens.inl` titleTick). The label and its tooltip are in the `MenuText` tables (five
  languages). Test aid: `DRAKAN_DEBUG_SWITCH_CLICK=<seconds>` presses the Original button once.
* **Title art**: the native art carries the Drakan 2 logo, so while a campaign is chosen the menu draws a text plaque over
  it: "DRAKAN" and the part of `campaign_name` after "Drakan: " (Order of the Flame), or the campaign's title over "A Drakan
  campaign". A pack-supplied title image is not implemented. Drakan 2 keeps the native art.
* **New Game** redirects the disc's opening cut scene level (`Level 0 - Intro`, which New Game opens first, then Level 1A)
  to the campaign start level at the file layer, so the Drakan 2 movie / cut scene never plays and the campaign's own
  first level comes first. The level chain after that is the pack's own exits; exits out of a copy arrive at the source's
  marker (`drakan_levelchange` arrival fallback, journal 2026-11-02/level-copy-direct-play.md).
* **Saves**: each campaign uses its own memory card folder `<card>_<campaign_saves>` (next to `mc0`: `mc0_ootf`,
  `mc0_testvalley`, or `<DRAKAN_MEMCARD_DIR>_<saves>`), created on first use and seeded with a copy of the options
  (`<saveId>Settings`) of the Drakan card, so the games' saves never mix (`[game] memory card folder ...` in the log).
  Continue / Load Game / Save Game / quick save and the save previews all follow the selected game because they use the one
  configured card root.
* **Menus**: a campaign pack's own `ui/*.ini` skin (see "Menus and HUD") applies **only while that campaign is chosen**:
  the game keeps a base skin (`ui.ini`, `DRAKAN_UI_SKIN`, the `ui/*.ini` of packs without `campaign`) and lays the chosen
  campaign's files over it on every switch (`[ui] skin mod <id>: ui/menus.ini (campaign '<id>'): N setting(s)`). The title
  screen is rebuilt on the main page after a switch so the theme colours follow at once; the pause menu and Options read
  the skin when they open.
* **Co-op**: New Game > Co-op hosts/joins on the campaign's start level (`drakan_game::firstLevel()`); the host level list is
  the start level then the campaign's other levels (Order of the Flame: the pack's levels without its arena bases).
* **Multiplayer is shared**: one Multiplayer menu for every game, maps of any game selectable. A multiplayer launch sets its
  own redirect; when it ends the campaign redirect of the chosen game is restored.
* **Settings** `drakan.ini [general] game = ask | d2 | d1 | ootf | <campaign id>` (env `DRAKAN_GAME`): `ask` (default)
  offers the card when a campaign is installed, the others fix the game without a card (headless runs, harness); a game
  that is not installed logs it and starts Drakan 2. A fixed setting next to an installed campaign is explained on the
  title and the Mods page ("New packs", Game choice explained).
  `scripts/harness/ootf_level_run.py --campaign ...` converts the level as a campaign pack and runs with `DRAKAN_GAME=d1`.

### Campaign title music

`campaign_title_music = <id>` names a PS2MUSIC.SDB sound the pack adds with `new/PS2MUSIC.SDB/0302_<id>_<group>.bin` and
`0304_<id>_<group>.bin` (descriptor and ADPCM, the layout `ps2SoundEntries` writes; flags 0xa for a looping track). While
the campaign is the current game, the runtime serves the disc's title track (PS2MUSIC.SDB 196 "SeaHigh GUI", INTERFCE.ODB
class 1 `Background Music File` = 0x100c4) from that id (`hle::CampaignMusicRedirect`, `ResourceService::setIdRedirect`,
log `[game] campaign '<id>': title music PS2MUSIC.SDB <n>` and `[hle] music: title track 196 -> <n>`). The choice is
latched when the guest reads the track's descriptor, so a play that started for one game finishes with that game's track.
`ootf-convert.ts --campaign 1` and the editor's converter write the rendered Order of the Flame theme ("gui", the PC title
music) there; journal `2026-11-08/ootf-main-music.md`.

### Campaign credits

A campaign pack may ship `credits/credits.txt` (up to 64 KiB, plain text in the credits format: `=Heading` lines followed by
names, a blank line between blocks). While that campaign is the current game the engine's end credits show this text
instead of Drakan 2's: the credits are two text entries (`0x401` ids 8807 and 8808 in `DRAKAN2.RRC`) the engine reads by id,
and the pack's text is served for the first while the second is emptied (`hle::CampaignCreditsOverride`, test
`override_test`). The disc game keeps its own list. `ootf-convert.ts --campaign 1` writes a short end card (the game, the
original studio, the engine) with no personal names, since the PC install carries no credits.

The engine reads the credits with plain file reads of the opened `Drakan 2.rrc`, not through the entry layer, so the text is
patched per read: a `FileService` read overlay (`setReadOverlay`, evaluated at read time, so a game chosen on the title after the
file was first read still counts) rewrites the two entries' bytes in place (`CampaignCreditsOverride::patchRead`; the text is padded
with spaces to the entry's size, a longer one is cut at a line end, the second entry becomes one blank line). Verified in game: the
end card scrolls over the Wartok farm and the game returns to the title (`credits: read at ... patched entry 8807` in the log).

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

## Drakan 2 textures for converted OotF objects

Where Drakan 2 has its own texture for the same kind of surface as an Order of the Flame object, the converter copies the disc
texture into the pack instead of converting the PC picture (formats `ootfps2tex.ts`). The copy is byte for byte, so the game
draws it as it draws the disc's own texture and the Drakan 2 HD pack replaces it too (same decoded picture, same hash); the OotF
HD step (`ootf_hd_pack.py`) skips it because the hash is already in `dist/DrakanPC/textures`. The pairs are a curated table of
names and ids (`DEFAULT_PS2_TEXTURE_SUBSTITUTIONS`, each row with a reason), never a guess at conversion time:

| OotF texture | Drakan 2 texture | What |
| --- | --- | --- |
| `Tropical Common#5425` | `LEVELS/LEVEL3ON/LEVEL3ON.TXD#6041` | tropical plant fronds: Drakan 2 ships the same model (same 80 polygons) with this repaint |
| `Mountain World Common#4619`, `Intro Level Common#4619` | `WORLDCON/WORLDCON/WORLDCON.TXD#412` | pine trunk bark (Drakan 2 PineTree01, pine1SNOW) |
| `Mountain World Common#5279`, `Intro Level Common#5279` | `WORLDCON/WORLDCON/WORLDCON.TXD#412` | log bark (Drakan 2 wraps its SnowyLog in it) |
| `Ruined Village#5433`, `Intro Level#116` | `LEVELS/LEVEL2RE/LEVEL2RE.TXD#545` | old tree / oak trunk bark (Drakan 2 Restoration Tree, 256 px) |

Rules: the disc texture must have the OotF texture's aspect ratio (UVs are normalised, so a different size is harmless); a
texture of a skinned model must stay 8 bpp and square or the OotF one is kept; an animated disc texture brings its frames;
several OotF textures naming one disc texture share one copy. Object textures only: no OotF terrain texture has a better
Drakan 2 Proc Textures counterpart.

* CLI: on by default; `--ps2-textures 0` keeps every OotF texture. `--ps2-texture-map <file.json>` edits the table with
  `[{ "ootf": "<container>#<id>", "ps2": "<disc TXD path>#<id>" | null }]` rows (`null` turns a row off, a new `ootf` adds a
  pair, an existing one replaces it). The disc TXDs are read by file name from `--ps2-txd <dir>`, `<ps2>/txd`, `<ps2>` or the
  hosts dir, else from the disc image (`--iso`, `DRAKAN_ISO`, or `iso` in `dist/DrakanPC/drakan.ini`).
* Editor: Import Order of the Flame levels > "Use Drakan 2 textures where the disc has a matching one" (on by default), with a
  tick per pair and "Add your own pair"; the choice is remembered in the browser and the result line lists what was used.
* Pipeline: `OotfPipelineOptions.ps2Textures` (`{ enabled, off, extra }`) with `OotfPs2Context.readPs2Txd`.

See [the matching survey](../journal/2026-11-08/ootf-ps2-texture-substitution.md) for how the pairs were found and what has
no Drakan 2 counterpart.

## Drakan 2 characters and weapons for converted OotF levels

Where Drakan 2 has the same creature or the same kind of weapon, the converter uses the disc's own model (with its textures,
skeleton, clips and HD textures) instead of the converted PC one (formats `ootfps2model.ts`, table
`DEFAULT_PS2_MODEL_SUBSTITUTIONS`, class names only):

| Group | OotF class | Drakan 2 class | Notes |
| --- | --- | --- | --- |
| Enemies | Troll Leather / Chain / Plate (and their "defense side attack" flankers) | LeatherWartock / ChainTock / PlateTock Center / Flank | same creature and skeleton |
| Enemies | Orc Leather (purple), Orc Plate (yellow) | Purple Orc Center, Yellow Orc : Center | same creature and skeleton |
| Enemies | Spider | Spider | same creature, same 37 joints |
| Enemies | Scavenger | Scavenger | off by default: other skeleton, so turned on it plays as the Drakan 2 class (its health and attacks) |
| Weapons | Short Sword, Long Sword, 2-Handed, Scimitar, Speed Scimitar | Steel Short Sword, Steel Longsword, Steel Two Handed Sword, Steel Scimitar, Iron Scimitar | plain blades |
| Weapons | Mace, Battle Axe, Battle Hammer, club, Goblin Club, Troll Axe | Steel Mace, Steel Axe, Steel Hammer, club, Wood Club, Leathertock Axe - Lev [1] | plain blunt weapons and the Wartok axe |
| Weapons | Long Bow, Heavy Bow, Speed Bow (one PC model), Arrows | Wood Bow: Long, Wood Bow - Arrow Short (the plain quiver) | |

* Enemies are played by the Drakan 2 class (they already were: `PC_CLASS_ALIASES`). By default they keep the disc's own clips;
  `enemyAnimations: 'ootf'` (CLI `--enemy-animations ootf`, editor "Order of the Flame animations on the Drakan 2 model") retargets
  the PC clips onto the disc rig as the converter did before. A row turned off converts the PC model with its own skeleton.
* Weapons keep their PC class data (damage, reach, effects, the cloned template); only the model becomes the disc weapon's, a
  model of the host's own `RESOURES.MOD`, named in place (`OotfAssetBuilder.useNativeModel`). One PC model shared by several
  classes takes the first row's model. Magic, elemental and quest weapons (Runeblade, Atimar Blade, Flame Sword ...) are not in
  the table.
* Rynn and Arokh are always the disc's: the player class and the player's dragon are name-mapped onto the Drakan 2 Rynn and
  Arokh. The OotF villagers, Delon, the slaves and the scene-only characters have no Drakan 2 counterpart.
* CLI: `--ps2-models 0` converts the OotF models of the table's enemies and weapons; `--ps2-model-map <file.json>` edits the
  table with `[{ "ootf": "<OotF class>", "ps2": "<Drakan 2 class>" | null, "category"?: "enemies" | "weapons" }]`.
* Editor: Import Order of the Flame levels > "Use Drakan 2 characters and weapons where the disc has the same one", with a tick
  per group and per pair, the enemy animation choice and "Add your own pair"; remembered in the browser.
* Pipeline: `OotfPipelineOptions.ps2Models` (`{ enabled, categoriesOff, off, on, extra, enemyAnimations }`).

See [the model survey](../journal/2026-11-08/ootf-ps2-model-substitution.md).

## Menus and HUD (UI skins)

The modern menus (title, pause, Options pages) and the multiplayer HUD read a small INI file, so a pack can rename and
reorder entries, restyle the theme and change a few layout numbers without code. Engine neutral: the file only names
stable ids (`new_game`, `pause.root`), so another front end (for example Unreal UMG) can read the same data. Code:
`runtime/gameui/include/gameui/UiSkin.h`; editor: the **Menus** dock tab (Level mode), `editor-core/src/uiskin.ts`.

Where it comes from, later keys win: `ui.ini` next to `drakan.exe`, `DRAKAN_UI_SKIN=<file>` (testing), then
`ui/*.ini` of every enabled pack in load order (`ModManager::uiFiles()`); a campaign pack's files only while that campaign
is chosen on the game card ("Campaign packs"). The game logs `[ui] skin <source>: N setting(s)`
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
