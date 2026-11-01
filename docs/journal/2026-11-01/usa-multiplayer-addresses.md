# USA disc and the multiplayer / mod hooks (2026-11-01)

No USA ISO exists on this machine (`Get-ChildItem` over `C:\Games` and `D:\` finds only the PAL ISO, an OotF image and
build products). An extracted `build/usa-game/SCUS_971.28` from an earlier session does exist, which is enough to test
address finding, not to run the game, so nothing was enabled for USA.

## State found

The region plumbing the task asked for already exists (docs/reference/regions.md): `scripts/buildtool/regions.json`,
`configs/regions/<id>.json` (capabilities, audited `addresses`, typed layouts) and `drakan_region::guestAddress()`
(identity on PAL, table lookup elsewhere, 0 = unknown). The profile is chosen when the game is built from the disc's
`SYSTEM.CNF` serial, and a wrong disc is refused at startup, so selection is per build rather than per run; kept as is.
Arena, PvP, ghost and co-op already refuse to install when `guestAddress()` returns 0. Level change, lifecycle and
foliage already refuse on non-identity profiles. Netplay, co-op, ghost, PvP and arena are only installed from the
advanced-input glue, so on USA they never ran and said nothing.

## Done

- `drakan_region::reportMultiplayerAvailability()` (called from `drakan_overrides.cpp` before the lifecycle install)
  prints one line on a non-PAL profile: multiplayer, co-op, arena, level change and lifecycle are disabled, why, and
  where the table lives. No-op on PAL.
- `analysis/scripts/mp_region_find.py`:
  - `inventory` lists the 79 hard-coded PAL guest addresses in the MP/mod sources (57 code, 22 data) with the constant
    names, use sites and function/offset (from `analysis/functions.csv`).
  - `find --pal <PAL ELF> --target <other ELF> [--check-map analysis/regions/<id>]` builds relocation-masked signatures
    from the PAL ELF (function heads, windows around mid-function addresses, windows around instructions that
    reference a data address, decoded and voted) and searches the target `.text`. Only exactly-one hits are reported.
    It reads the player's ELFs at analysis time and writes only addresses/counts.
  - `table --candidates <json>` prints the markdown table.
- Test of the finder on the USA ELF: 68 unique, 2 ambiguous, 9 missing. All 68 agree with `region_map.py`'s
  `function_map.csv` / `data_map.csv` (0 disagree). PAL against PAL returns identity for every unique row.
  Ambiguous: `0x13b7c0` and `0x3d5a30` (short bodies); missing are range ends / scratch addresses
  (`0x100000`, `0x4fd184`, `0x5b7d20`, `0x5b8480`, `0x5b84c0`, `0x523150`, and three words beside the input table
  `0x4e8c4c..54`), which are not symbols and need a decision per feature (e.g. a free-memory scratch area).

## Not done / still needed for USA

The finder gives address candidates only. The MP features also read PAL object layouts (player object `+0x584`,
`+0xa48`, `+0x1040`, ...: 4 bytes lower in places on USA), so none of the candidates was added to
`configs/regions/usa.json`; that needs a run on a USA disc with `arena_two_instances.py`. Steps for someone with a USA
disc: build it, extract the ELF, run `find` and `table`, audit each row, add the verified pairs and typed layouts, enable
the capability, run the arena/co-op harnesses.
