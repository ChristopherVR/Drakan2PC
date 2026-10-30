# Level editor feasibility (2026-10-31)

Goal: assess a Riot-Engine-style level editor for The Ancients' Gates (PAL SCES-500.06). Static research only; nothing was
built or run. Confidence tags: **[V]** verified in repo docs/code, **[I]** inferred, **[U]** unverified (web or guess).
No game data, disassembly or generated code is reproduced here.

## 1. What the repo already knows

Sources: `docs/reference/iop-and-io.md` (sections 2, 3, 5), `analysis/scripts/iop_srsc.py`, `iop_mangle.py`, `iop_rrc.py`,
`runtime/hle/` (FileService, ResourceService, ResourceOverride, Srsc, IsoFs).

- **Container [V]:** every data file (`.LVL`, `.MOD`, `.ODB`, `.TXD`, `.ADB`, `.SDB`, `.XDB`, `.SSD`, `DRAKAN2.RRC`) is an SRSC
  resource container: 16-byte header, data blocks, trailing 14-byte index of `(type, id, sub, offset, size)`. Resources are
  addressed by (type, id, sub). Per-file names live in type 0x402/0x400 payloads.
- **Per-level files [V]:** `ENGLISH\LEVELS\LEVEL<n><xx>\LEVEL<n><xx>.{DB,LVL,MOD,ODB,SDB,SSD,TXD,XDB,ADB}` plus
  `LEVEL<n>TS.{DB,TXD}`. 14 levels in `LEVELLST.TXT` (`\Levels\<Name>\<Name>.lvl`, mangled to 8.3). Some levels have only `.LVL` +
  tileset TXD; their `.DB` points at shared databases. `.LVL` totals ~13 MB; the name ("Shiverbane") and "spawn/sector tables" were
  seen, nothing deeper is decoded.
- **`.DB` [V]:** plain-text dependency list (`version 1 / dependencies N / idx path`) pulling sibling DBs (World Common, Mountain,
  System, FX, NPC, Magic, Sounds).
- **`.ODB` [V/I]:** object/entity class definitions and behaviour parameters; refers to a `Drakan 2.rfl`. `.MOD` models/skeletons,
  `.TXD` textures (decoded: `txd_decode.py`, `drakan_scene_assets.h`), `.ADB` animation, `.XDB` text (decoded: `hle::xdb`).
- **What "rrc" means here [V]:** `analysis/scripts/iop_rrc.py` handles **`LEVELS.RRC`**, which is NOT level data. It is an encrypted
  (37-byte key + file-size seed) + zlib bundle of 7 IOP modules loaded at boot. `DRAKAN2.RRC` is a (partly zlib) SRSC container.
  So the name `LEVELS.RRC` is misleading; there is no `.rrc` level database to edit.
- **Load path [V]:** the EE never reads level files itself. It issues SIF RPC to the HLE'd RSRCIOP: `0x1201` File Command
  (open/read/lseek by long path; names resolved via 8.3 mangling) and `0x1202` Resource Request (batches of 28-byte entries: file
  handle + (type,id,sub) + offset/len + EE or SPU2 destination). EE open helper at `0x101310`; other Open callers `0x12a850`,
  `0x1f228c`, `0x4a3868`, `0x4a3c88`; resource submit `0x1f0a10-0x1f0e48`. EE path strings include `\Common\...`, `LevelList.txt`,
  `.lvl` (@0x44f680); DB dependencies use `..\..\World Common\...`. `.rsg`/`.rlt` are memory-card save/level-state files.
- **Existing PC hooks [V]:** `runtime/hle` implements both services natively over `IsoFs`. `ResourceOverride.h` provides
  `IResourceOverride` with `DirectoryOverride` (`overrides/<FILE>/<type>_<id>_<sub>.bin`, whole-entry replacement of any size, used
  for HD textures) and `XdbTextOverride`. Folder: `DRAKAN_OVERRIDES`, else `overrides` next to the exe. This is a ready-made mod
  overlay at SRSC-entry granularity, and it applies to `.LVL` entries too.
- **Entity state in guest memory [V, partial]:** journals (e.g. 2026-10-17 combat-encounters) locate live actor/NPC records (pool
  slots, handles, owner-handle check at data+0x23c, float world positions, factory vtables). That is runtime state, not file format,
  but it is a Rosetta stone for matching LVL records to placed objects.

Not found anywhere in the repo: any decoded `.LVL` entry layout, object-instance record, terrain/sector/trigger format, or a map of
the level-loader functions.

## 2. Capturing a per-level file-access trace

Needs an existing `dist/DrakanPC` build and the user's ISO; no rebuild.

1. Set `DRAKAN_HLE_LOG=<N>` (default 400 lines, `0` = silent; use e.g. 200000). `FileService` logs each open
   (`file open '<path>' -> <key> handle <h> size <n> srsc=...`), redirects and not-found; `ResourceService` logs each request
   (`resource: <FILE> <type>/<id>/<sub> +<off> len <n> -> type<d> dest <addr>`) and overrides.
2. Run with stderr redirected into `build/trace/<level>.log`, enter each level (use `scripts/harness/` recipes and saves). The open
   lines give the exact file set per level; the request lines give which `.LVL` (type,id,sub) entries are fetched, their sizes and
   EE destination addresses (the key for matching a resource to the guest function that parses it).
3. This also settles the open item in iop-and-io 5.1 (are paths sent as `\Common\...` or `cdrom0:\...`).
4. To find the consumer of a resource: `DRAKAN_TRACE_FN=<decimal addr>` / `DRAKAN_TRACE_N` on the submit/completion functions, or a
   watch on the logged destination address (no such watch flag exists yet; it would be a small addition to `runtime/hle`).
Keep traces and dumps under `build/`, never committed.

## 3. External research (web; labelled)

- Drakan: Order of the Flame (1999, Surreal Software) runs on the Riot Engine. A level editor and model editor shipped with a
  patch (ModDB "Drakan Editing Tools v1.2"; also in the Arokh's Lair Community Patch). [U: from search snippets; editor contents not
  inspected. "RiotEdit" did not appear in any result, so I cannot confirm that name.] The Community Patch readme credits zalasus
  for reverse-engineering the Drakan database format.
- **OpenDrakan** (github.com/Zalasus/opendrakan): open reimplementation of the Riot Engine for OotF. It loads `*.lvl` files, ships
  `srsced` (SRSC inspect/extract) and documents the container in `doc/riot_database_format.txt` plus a wiki. [I read only the
  README, not the format doc. Check its licence before reusing any code; use docs for hypotheses only.]
- Consequence [I]: the SRSC container found here matches the "SRSC" Drakan data container OpenDrakan works with, and the extension
  set (`.lvl .db .odb .mod .txd`) and `.rfl` reference match the Riot lineage. So OotF documentation is the best prior for inner
  layouts (instances, layers, sectors). [U: the PS2 sequel uses its own type numbers (0x01xx, 0x02xx, 0x03xx), VU1-oriented models,
  and may differ in every inner layout. Verify against real data, never assume.]

## 4. Proposed architecture

Principles: works on the player's own ISO (reuse `IsoFs`/`IsoSource*`); the repo holds code, format descriptions and synthetic test
fixtures only; caches and outputs go to `build/` or the user's overlay folder.

**Phase 0, format tooling.** `analysis/scripts/lvl_dump.py` on top of `iop_srsc.py` (type histogram across the 14 levels, string
tables, per-level diffs). Once stable, port to a C++ library `runtime/level` (`drakan::level`; ISO-dependent tests skip without data),
mirroring `runtime/hle/Srsc.h`; no platform APIs in public headers.

**Phase 1, read-only viewer (first deliverable).** A standalone tool (e.g. `runtime/level_viewer`, imgui from `third_party/`):
load ISO, parse LVL, resolve `.DB` dependencies, load `.MOD`/`.TXD` via the existing decoders, draw geometry with a fly camera and
entity markers (position, ODB class name, inspector). Standalone rather than inside the game because the in-game renderer is fed by
GS/VU1 packets, not scene data; parsing files directly avoids the recompiled engine and covers every level without playing. A
later in-game live inspector (existing settings overlay) could read guest actor pools. Validate by overlaying parsed entity
positions on live actor dumps in a known level.

**Phase 2, entity editing.** Edit instance records only (move, rotate, add, delete, retype from existing ODB classes, parameters).
Fixed-layout records and no geometry rebuild make this the smallest blast radius.

**Phase 3, geometry/terrain.** Only after a decoded terrain/sector format. Likely needs regeneration of PS2-derived data (VU1
packets, vertex lighting, collision, visibility), possibly infeasible without the original toolchain. Realistic stopgap: place
existing MOD geometry as props.

**Getting edits into the game.**
- **A (recommended): data-file round trip via overlay.** The editor writes changed SRSC entries into
  `overrides/<LEVELxx.LVL>/<type>_<id>_<sub>.bin` (existing `DirectoryOverride`; size may change; offset/len applied afterwards), or
  a whole-file replacement through a small new "mod dir shadows ISO path" step in `FileService` resolution (next to the existing
  redirect). Pros: shipped mechanism, survives save/load, shareable as small patch files containing only the author's changes, deterministic.
  Cons: needs lossless parse/write and understanding of any internal offsets or counts. Add a hash manifest so a pack is checked
  against the user's ISO/profile (PAL vs USA, see disc-profiles.md). "Play" = launch `drakan.exe` with `DRAKAN_OVERRIDES=<project>`.
- **B: live injection into guest RAM.** Fast iteration and good for discovering formats, but fragile (pointers, allocator, saves),
  not persistent, version-specific. Use as a dev inspector or preview, not as the authoring path.

**Legal.** Editor requires a user-supplied ISO; extracted data stays in gitignored `build/`/`local/` or the user folder; mods ship as
deltas of changed entries (same stance as the HD texture workflow); `scripts/repository_guard.py` already enforces no assets.
Document formats in prose and field tables, not disassembly.

## 5. Reverse-engineering prerequisites, in order

| # | Prerequisite | Blocking / status |
|---|---|---|
| 1 | Per-level file and resource-request traces (section 2) | Nothing blocking; needs a built dist and the ISO. Unblocks the rest. |
| 2 | Catalogue `.LVL` and per-level `.ODB`/`.DB` resource types and sizes across 14 levels (`lvl_dump.py`) | Needs extraction; `iop_srsc.py` ready. Meaning of the 0x03xx types unknown. |
| 3 | Read OpenDrakan `riot_database_format.txt` and wiki; test their layouts on PS2 types | Not yet read; PS2 deviation is unverified. |
| 4 | Find the guest level loader and LVL consumers in Ghidra (xrefs of the `.lvl` string, `LevelList.txt`, resource submit/completion path) | No level-loader function map exists; depends on step 1 addresses. |
| 5 | Decode world structure: sectors/cells, terrain, object-instance records (position, class id, params) | Blocked by 2-4. Validate with live actor dumps. |
| 6 | Map class ids to ODB definitions and parameter schemas (NPCs, items, triggers) | ODB layouts undecoded; class table in the ELF not located. |
| 7 | Triggers, links, scripting, `.SSD` sound scripts (types 0x310-0x312 partly seen) | Undecoded; behaviour lives in recompiled game code. |
| 8 | Derived data: collision, visibility, lighting, VU1 packets, minimap | Needed for Phase 3 only; regenerability unknown. |
| 9 | Lossless round trip (parse, write, byte-identical entry) per decoded type | Gate before enabling any write path. |
| 10 | HLE robustness with grown entries (`MAX_LEN` 0x40800 per request, 64 file slots, guest size assumptions) | Check `ResourceService` limits before shipping larger LVL entries. |

## 6. Recommendation

Do steps 1-3 and the standalone read-only viewer first; entity editing through the overlay after a lossless round trip of instance
records; treat geometry editing as a separate research project.
