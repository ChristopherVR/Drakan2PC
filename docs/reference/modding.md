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

Reserved for later format versions (ignored today, never an error): `files/<PATH>` whole-file replacements and
`new/` entries that add resources to a file.

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
* The 64 slot limit of `FileService` concerns simultaneously open game files, not entries, and is not affected until
  whole-file replacements exist.

## Writing a pack by hand

1. Extract the entry with `analysis/scripts/iop_srsc.py` (or the editor) from your own disc.
2. Edit it and save as `mods/<pack>/overrides/<FILE>/<type hex>_<id>_<sub>.bin`.
3. Add `mods/<pack>/mod.toml` with the manifest above.
4. Start the game; `mods/mods.ini` gets `<pack> = off`; change it to `on` and restart.
5. The log shows `mods: loaded '<id>' ...` plus any skip or conflict messages.

Authoring tools (editor export, `scripts/` helpers) should write exactly this layout.
