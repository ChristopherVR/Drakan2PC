# Why 3 or more appended ODB class records broke level loading (2026-11-01)

Question: appending 3+ `0x0102` class records to an ODB (a level's, SYSTEM, RESOURES, FX) stopped the level from loading, and
the converted OotF Intro Level (45 synthesized classes in 4 ODBs) never started. Tags: **[run]** measured in the game,
**[dis]** read from the ELF with `analysis/scripts/gfx_dis.py`.

## Root cause: the ODB header record 0x0100 carries the class and group counts

Every ODB has one `0x0100` record (id 0, sub 0, 32 bytes) that starts

```
u16 class_count    // number of 0x0102 records
u16 group_count    // number of 0x0101 records
u32 ...            // 0x0c53110b (constant)
u32 1, u16 n, "..\..\Drakan 2.rfl"
```

**[run]** All 21 PAL ODBs: `class_count`/`group_count` equal the number of `0x0102`/`0x0101` index entries (SYSTEM 190/26,
RESOURES 1344/384, FX 945/197, LEVEL1ON 582/133 ...). The guest sizes its per-ODB class tables from these counts and fills them
from the index at level load. Records beyond the declared count overflow the tables:

| Added `0x0102` records (SYSTEM.ODB, count left alone) | Result |
| --- | --- |
| 1, 2 | loads (the overflow stays inside the allocator's slack) |
| 3, 8, 40 | Level 0 never finishes: wild `JALR` (`missing-target`, target is data) right after the next ODB opens, or a hang; a corrupted heap |
| 3 / 8 non-class entries (type `0x0150`) | loads, so it is the class records, not the index size |
| ids above the ODB's maximum (4363..4365), correct `0x0103` lists, sorted or appended | still fails: not an id-range or list-pairing effect |
| 3 and 40 records **with the `0x0100` class count raised by the same number** | **loads Level 0 and Surdana** |

The earlier belief "ids above the maximum did not spawn / many records break" was this overflow. Nothing in the 0x0102 reader
itself limits the count (the guest looks classes up lazily: sorted index + binary search on the id, 16-byte index entries).
The container index is sorted by `id` only (stable) by the guest at open (`0x4a2250`), so append order does not matter.

## A second finding: the dist binary ignored `0x0103` overrides

The first runs used `dist/DrakanPC/drakan.exe`, built before commit 048b136 ("Apply mod entry patches to ODB files by
rebuilding them"). That build applied `new/<ODB>/0102_*` (rebuilt container) but dropped `overrides/<ODB>/0103_*`, so the new
classes had no field lists. Any build from 048b136 on applies them (`rebuilt with N entry patch(es)` counts the override).
Check the log line: N must be the number of new records plus one for the `0103` override.

## Fix

`runtime/hle/src/ModPack.cpp` `patchSrsc`: after the patches, when the number of `0x0102` or `0x0101` entries changed, the
`0x0100` record's two counts are set to the new numbers (only if they equal the original counts, i.e. the pack did not write
its own header). Log: `mods: ODB header counts updated: 190 -> 193 classes, 26 -> 26 groups`. Covered by `mod_test`
("ODB header record 0x0100"). Packs need no change; the converter keeps appending records and a longer `0x0103`.

## Verified [run]

Throwaway packs of N cloned records added to SYSTEM.ODB (build/ootf-classes/, not committed), start-up through Level 0 and
Level 1A:

| Build | N added | Counts | Result |
| --- | --- | --- | --- |
| before the fix | 1, 2 | not updated | loads |
| before the fix | 3, 8, 40 | not updated | fails (heap corruption) |
| pack raising `0x0100` by hand | 3, 40 | raised | loads |
| with the fix | 3 (SYSTEM), 4+22+15+4 (Intro pack) | updated by the runtime | loads |

So 40 appended classes in one ODB is verified; no engine limit was found (the guest's lookups do not cap the count, and the
count field is a u16, so the structural ceiling is 65535 per ODB).

## Intro Level (OotF) status

With the fix and the current converter (`ootf-convert.ts` full pipeline: 49 synthesized classes in FX / LEVEL1ON / RESOURES /
SYSTEM, 42 models, 98 textures, 3.7 MB of terrain textures) **the Intro Level starts**: Level 0, then `OotF - intro level`
(`afterLevelLoad ... ok=1`), HUD and the iris fade-in run, the scripted input quits cleanly. Bisect with
`--only` stage lists (classes only; models+classes; textures+classes; textures+models+classes; everything but terrain
textures) all load once the counts are right. An older converter output from the first hour of this session (36 models,
45 classes) still hung in `malloc_consolidate` on the same build; it was not investigated further because the current
output does not. The earlier "Intro fails to start" was the class-count overflow (RESOURES +22, FX +15, SYSTEM +4, LEVEL1ON
+4 classes).

## Not settled

* Whether the guest pairs `0x0103` field lists with records by file order or by id order. Appended lists (ascending id,
  `buildClassEntries`) loaded in every test, but no test used a field of an appended class, and classes whose id lies below
  the ODB's maximum would be misaligned if pairing is by sorted id. The first real use (a mapped-then-edited synthesized
  class, e.g. a changed Timer default in game) will tell.
* `dist/DrakanPC` was not refilled by the last incremental build (a running game held `avcodec-61.dll`); the runs used
  `build/final-game/bin/drakan.exe` with `DRAKAN_MODS`.

## Field-list pairing caveat, closed by construction (later 2026-11-01)

`ootfclass.ts freeIds` now hands out ids **above** the ODB maximum (max+1 ...; `OOTF_IDS_BELOW_MAX=1` restores the old
below-max allocation for comparison). Appended records then sort last, so file order, index order and id order agree and the
appended `0x0103` lists pair with their records whichever way the guest pairs them. Not tested directly (no in-game probe of a
list field of a below-max clone was built); both allocations load and behave the same in the runs below (Alwarren fails
identically with either), so there is no evidence of misalignment damage, and the new default cannot be affected by it.
