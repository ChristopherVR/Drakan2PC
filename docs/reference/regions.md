# Disc regions: PAL SCES-500.06 and USA SCUS-971.28

The port was written against the PAL release. Since 2026-10-29 the builders also accept the USA release and build
it as an **experimental** version: the game recompiled from the USA executable with the IOP/sound/disc HLE and
keyboard / mouse / gamepad input, but without most PC features. Those read PAL addresses and PAL object layouts and
stay PAL-only until each is ported ([Feature matrix](#feature-matrix), [What is left](#what-is-left)). This page is
the source of truth for the player-facing summaries ([install guide](../guides/install.md#usa-disc-support-experimental),
`README.md`, the installer text). Investigation notes: [journal 2026-10-29](../journal/2026-10-29/usa-region.md).

| | PAL | USA |
| --- | --- | --- |
| Boot ELF (`SYSTEM.CNF BOOT2`) | `SCES_500.06`, 4,566,488 bytes, `VER=1.04`, `VMODE=PAL` | `SCUS_971.28`, 4,563,544 bytes, `VER=0.10`, `VMODE=NTSC` |
| ELF SHA-256 | `7d790b35…3b8e` | `252e82d1…040b` |
| Build stamp in the ELF | `May  3 2002 13:24:48` | `Dec 17 2001 17:42:38` (five months older) |
| Sony libraries | SDK 2.3.0 (`libmc 2.3.2`) | SDK 2.2.0 (`libcdvd 2.2.2`, `libmc 2.2.1`) |
| Memory card id | `BESCES-50006` (`…Slot1`, `…Settings`) | `BASCUS-97128` |
| IOP replacement image | `IOP\IOPRP23.IMG` | `IOP\IOP.IMG` (+ `IOP\VSSVER.SSC`) |
| Languages | `ENGLISH FRENCH GERMAN ITALIAN SPANISH` | `ENGLISH` only |
| Builder status | validated | experimental: plays by keyboard and mouse in Level 1A ([Feature matrix](#feature-matrix)) |

## Disc layout

Both discs hold the same tree under the language root (`COMMON`, `LEVELS`, `MUSIC`, `WORLDCON`, `DRAKAN2.RRC`,
`LEVELS.RRC`, `*.HSD`). On the USA disc every one of those files lies inside one 1.17 GB extent that the ISO9660
directory also lists as `DRAKAN2.BIN` (LBA 572); the per-file directory records point into it, so a path lookup
(`hle::IsoFs`, `RSRCIOP`'s `sceCdSearchFile`) finds the same names. Most sizes are equal; the NTSC movie/speech
containers differ (`LEVEL0RO.VDB` 213,672,854 → 217,233,918 bytes, `LEVEL0RO.SDB` 3,558,486 → 4,801,254,
`SYSTEM.SDB`, `.XDB`, `.TXD` by a few bytes; `LEVELS.RRC` 79,685 → 78,664; `OUT_SPTS.DEF` is PAL-only). The IOP
modules differ in size (`RSRCIOP.IRX` 36,652 → 31,100, `MCMAN.IRX`, `LIBSD.IRX`, `SDRDRV.IRX`), consistent with
the older SDK. `LIBSD.IRX` is still found at `IOP\MODULES\` (reverb presets).

## Executable differences

Same section list; `.text` grows by 0xe70 bytes (0x3d73f4 → 0x3d8264), `.data` shrinks by 0x1aa8, `.bss` grows by
0x1380. The VU1 microcode (`.vutext` and the six DVP overlays) is byte-identical, so the private
`ps2_vu1_image.inc` and the four VU1 AOT slices are shared; only the file offsets differ (`regions.json`
`vu_chunks`, +3696 bytes).

`analysis/scripts/region_map.py` matches the 12,173 PAL functions (`analysis/functions.csv`) against the USA image
by relocation-masked instruction patterns (jump targets, `lui` halves and non-`$sp` 16-bit immediates masked):

| Match kind | Functions | Meaning |
| --- | --- | --- |
| exact | 6,815 | whole normalised body found once |
| neighbour | 5,241 | short or repeated body, placed by the neighbouring functions' delta |
| callsite | 47 | the callee named by a `jal` at the same place in matched callers (body differs) |
| fuzzy | 41 | opcode-similar function inside the gap its neighbours leave (≥ 0.75 ratio) |
| unmatched | 29 | PAL-only code (jaw/speech animation, `FlankingAI`, "Load Settings", SDK 2.3 additions) |

3,860 matched functions are bit-identical, 7,078 differ only in absolute addresses, the rest also in immediates.
The USA build has its own extra code (`FlyingAI`, older `libcdvd` paths). All 171 jump tables in matched
functions sit at the mapped `jr` with the same target count.

### Structure layouts

The data-reference pairing also shows that **object layouts differ**, not just addresses. Inside matched bodies
2,407 loads/stores use an offset 4 lower in the USA build (179 distinct offsets between 0x94 and 0x1118, e.g. the
player object's `+0x1024` spell list is `+0x1020`, `+0xab0` → `+0xaac`), and 95 byte accesses 2 lower
(0x9d–0xf5). PAL added fields (the jaw/subtitle members in the string table are PAL-only). Every PC feature reads
such offsets (`runtime/gameui/include/gameui/Addresses.h`, `runtime/input/include/input/Config.h`, the
`drakan_*` hooks), so translating addresses alone is not enough to enable them.

## How a region is selected

| Step | Where | What |
| --- | --- | --- |
| Profiles | `scripts/buildtool/regions.json` | boot name, serial, memory-card id, ELF size/SHA-256, recompiler config, function/jump-table CSVs, VU offsets, status |
| Disc check | `scripts/buildtool/prepare_iso.py` | reads `SYSTEM.CNF BOOT2`, picks the profile, verifies the ELF hash, writes `region` / `boot` / `serial` into `local-input.json`; the default template `configs/drakan.toml` is swapped for the region's (`configs/drakan-usa.toml`). `--detect` prints `pal` / `usa`. `--refresh-config` refuses an ISO of the other region. |
| Recompiler input | `configs/drakan-usa.toml`, `analysis/regions/usa/{functions,jumptables}.csv` | generated (below); PAL inputs unchanged |
| Runtime prep | `scripts/prepare_local_runtime.py` | accepts either verified ELF; VU image identical |
| Build | `Build-LocalGame.ps1` | reads `local-input.json`, passes `-DDRAKAN_GAME_REGION=<region>`, uses `<WorkDir>/<boot>`; a non-PAL build deploys to `dist/DrakanPC-<region>` so it never replaces the PAL play folder |
| Player build | `Build-Drakan.ps1` | always deploys to `dist/DrakanPC` (installer shortcuts); when the ISO is the other region than the existing `build/game`, that folder is rebuilt from scratch |
| CMake | `ps2recomp/ps2xRuntime/CMakeLists.txt` | `DRAKAN_GAME_REGION=pal\|usa`; `usa` adds `DRAKAN_GAME_REGION_USA=1` to `ps2_runtime` (PUBLIC). PAL adds nothing |
| Runtime | `ps2recomp/ps2xRuntime/src/lib/drakan_region.{h,cpp}` | `drakan_region::build()` profile (boot name, serial, save id, NTSC flag); `guestAddress(palAddr)` = identity on PAL, table lookup elsewhere (0 = no verified equivalent) |
| Overrides | `drakan_overrides.cpp` | registers the game override under the build's boot name; `kPcFeatures = kIsPal` gates every PAL-only feature |

PAL builds stay byte-identical in behaviour: the PAL config/metadata/stamp inputs are the same files, PAL compiles
without a new definition, and `guestAddress()` folds to the constant.

### Wrong disc or build

| Situation | What happens |
| --- | --- |
| `Build-LocalGame.ps1 -Incremental` on a WorkDir of the other region | `prepare_iso.py --refresh-config` stops: "holds a … build but the ISO is …; build the other region into a fresh WorkDir" |
| `Build-Drakan.ps1` / installer with the other disc than last time | `build/game` is deleted and rebuilt from scratch for the new disc; still deployed to `dist/DrakanPC` |
| `drakan.exe` started with the other release's disc image (`drakan.ini [general] iso`, `DRAKAN_ISO`) | logs and shows "Drakan: wrong disc for this build" and exits (checked: USA build + PAL ISO) |
| Saves | the game writes its own id: PAL `mc0/BESCES-50006…`, USA `mc0/BASCUS-97128…`; neither release lists the other's saves |
| Play folders | `Build-LocalGame.ps1`: PAL `dist/DrakanPC`, USA `dist/DrakanPC-usa`; the installer always uses `dist/DrakanPC` |

### USA baseline (what a USA build installs)

`ps2_stubs::setIpuInitTables` (libipu tables 0x5163e0 / 0x516430), `LoadIopModules` (0x1016b0) and
`sceSifLoadModuleBuffer` (0x4c45e8) skipped (IOP modules are HLE'd), newlib `rand` (0x4cee98, `_impure_ptr`
0x51826c), `sceSdRemote` (0x4ba570; RPC buffer 0x56e6c0, end-function slot 0x516360), the RSRCIOP file/resource
services, SPU2 output, `DRAKAN_TRACE_FN`, and input: `installRegionInput` (`drakan_overrides.cpp`, Windows) replaces
the libpad 2.2 entry points (`scePadInit` 0x4b0080, `PortOpen` 0x4b02e0, `Read` 0x4b0630, `GetState` 0x4b06b0,
`SetMainMode` 0x4b0bc8, `SetActDirect` 0x4b0c80) with the input module's pad sample, as the PAL glue does. The only
game word it reads is the GUI screen pointer (0x4fc48c) to pick the menu or game binding layer; mouse look uses the
module's emulated right stick (`LookMode::Stick`, forced on USA because direct look needs the PAL camera hooks).
These are SDK functions and one pointer, so no object offsets are involved. `DRAKAN_AUTOPRESS` / `_UNTIL` /
`DRAKAN_AUTOSTART` work as with the stock pad stub. Not installed: everything else in `applyDrakanOverrides` (movie
pacing assumes 25 fps PAL movies; the stereo-flag sync and `DRAKAN_TEST_SUB` write PAL addresses and are skipped).

### Runtime address table

`drakan_region.cpp` lists only addresses the USA baseline uses; `scripts/tests/test_regions.py` checks each row
against `analysis/regions/usa/function_map.csv` / `data_map.csv`. Two rows come from reading both `sceSdRemote`
bodies (a "callsite" match, so its immediates are not paired automatically): the reply buffer
`lui 0x57; addiu -0x2100` → `-0x1940` and the end-function slot `lw 0x6f80(0x51…)` → `0x6360`.

### Hand-mapped SDK entries (`region_config.py MANUAL`)

| PAL | USA | Evidence |
| --- | --- | --- |
| `sceDmaPause` 0x4b07b8 | 0x4aff10 | first of the two 28-byte functions after `sceDmaWatch` (libdma 2.2 has no D_ENABLEW write) |
| `sceSdTransToIOP` 0x4b9140 | 0x4ba4f0 | between `sceSdRemoteInit` and the `sceSdRemote` thunk; same argument packing and calls |
| `__sjpopnthrow` 0x4c4db0 | 0x4c5c20 | same offset inside the identical 0x1d8-byte gap after 0x4c5aa8 (24/24 words) |
| `__cp_exception_info` 0x4c7a98 | 0x4c8908 | +0x18 inside the matched 0x4c88f0 body |

Dropped (absent from SDK 2.2): `sceSifAllocSysMemory`, `sceSifUnloadModule`, `sceSifSearchModuleByName`,
`sceSifSearchModuleByAddress`.

## Regenerating the USA metadata

Needs both owned ELFs (extract with `prepare_iso.py --output-directory build/<dir>`), Ghidra as in
[ghidra.md](ghidra.md) with a separate project:

```sh
G=tools/ghidra/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat
$G "$(cygpath -w tools/ghidra_proj_usa)" DrakanUSA -import <SCUS_971.28> -processor r5900:LE:32:default -overwrite -max-cpu 8
$G "$(cygpath -w tools/ghidra_proj_usa)" DrakanUSA -process SCUS_971.28 -noanalysis -scriptPath "$(cygpath -w analysis/ghidra_scripts)" \
   -postScript FixFunctions.java -postScript ExportFunctions.java "$(cygpath -w build/usa-ghidra)"
python -I analysis/scripts/region_map.py --pal <SCES_500.06> --region <SCUS_971.28> \
   --pal-functions analysis/functions.csv --region-functions build/usa-ghidra/functions.csv --out analysis/regions/usa
cp build/usa-ghidra/jumptables.csv analysis/regions/usa/jumptables.csv
python -I analysis/scripts/region_config.py --map analysis/regions/usa --pal-functions analysis/functions.csv \
   --template configs/drakan.toml --boot SCUS_971.28 --out configs/drakan-usa.toml
```

Ghidra found 12,128 USA functions (99.42 % of `.text`); `region_map.py` splits Ghidra functions so that every
matched PAL entry is also a USA entry (`functions.csv`). Regenerate `configs/drakan-usa.toml` whenever
`configs/drakan.toml` changes.

## NTSC

The runtime's vblank clock is 60 Hz for both regions (the PAL port already runs at 60), and the GS output follows
the game's own `SMODE`/`DISPLAY` registers, so no mode switch is needed. The PAL-only movie pacing hook (25 fps
PAL movies) is not installed on USA; the USA movies play at the game's own pace.

## Feature matrix

USA column as of 2026-10-29 (Windows, D3D11). "Ran" = seen working in a run; see [Validation](#validation).

| Feature | PAL | USA |
| --- | --- | --- |
| Build from the ISO, boot, memory-card screen, title, New Game, Level 1A | yes | ran |
| Keyboard in menus (arrows), Esc pause, WASD movement | yes | ran |
| Mouse look | direct (camera hooks) | ran, as emulated right stick |
| Gamepad (XInput through the input module) | yes | not tested |
| Bindings from `drakan_input.ini` | yes | defaults ran; custom bindings not tested |
| In-game key binding screen, F1 PC settings overlay, Options > PC Settings | yes | no |
| Widescreen / FOV | yes | no (see What is left) |
| Frame-rate options, PC frame pacing, movie pacing | yes | no |
| Free mouse combat, modern bow aim, dual wield, quick slots | yes | no |
| Modern Tab inventory, M map, mouse on native menus | yes | no |
| Quicksave / quickload, 16 save pages, title Quit | yes | no |
| Native saving / loading | yes | not tested |
| Subtitles | yes | no |
| Memory-card warning auto-skip | yes | no (press confirm) |
| Sound | yes | HLE active; not listened to |
| HD textures, DLSS, scene enhancements | yes | HD pack staged, not checked; scene hooks no |
| Linux / browser builds | yes | not built (`runtime/web/pre.js` names `/SCES_500.06`) |
| Wrong-disc guard | yes | ran (USA build + PAL ISO) |

## Validation

2026-10-29 ([journal](../journal/2026-10-29/usa-region.md)): the USA build compiles with no recompiler warnings, boots
from the USA ISO, shows the memory-card warning and the title screen, and with `DRAKAN_AUTOPRESS` loads New Game into
Level 1A Surdana. With `installRegionInput`, injected keys moved the title cursor (New Game → Credits with two Down
presses), Esc closed the pause menu, W walked Rynn forward and relative mouse motion turned the camera. The PAL build
was rebuilt incrementally after each step and a run from `dist/DrakanPC` still installed the full PC feature set and
reached the title.

## What is left

Rough effort, per area, to bring a PC feature to USA (address translation through `guestAddress()` plus an audit of
every struct offset the feature reads against the -4/-2 shifts above):

| Area | Guest addresses used | Effort |
| --- | --- | --- |
| Boot/IOP/sound/IPU baseline | 9 | done |
| Keyboard / mouse (stick) / gamepad pad path | 7 | done |
| Movie pacing, movie-done crash guard, Options "PC Settings" | ~6 + screen/movie offsets | 0.5 day |
| Menus (title Quit, 16 save pages, quick save; `BESCES-50006` folder names → `drakan_region::build().saveId`) | ~25 | 1–2 days |
| Input glue (camera, bow aim, combat, quick slots; `runtime/input/Config.h`) | ~50 + many player/camera offsets | 2–3 days |
| Widescreen / FOV, frame rate, scene camera and DLSS scene hooks, GS `sourcePc` scene ids | ~60 | 2–3 days |

Widescreen was examined on 2026-10-29 and left for later (beyond the half-day box): its six hooks (0x1480e0,
0x132d68, 0x132f60, 0x139010, 0x147690, 0x199ff0) and return-address checks (0x139344, 0x2c4444) all map to
"exact" USA bodies, and `SetViewport` / `CropViewport` have no differing object offsets, but it also reads
`ctx+0x4b4` (the camera handle), which is `ctx+0x4b0` in 15 USA loads while `+0x4b4` stays in 231 others (another
class), plus camera `+0x14c` and screen `+0x18/+0x34`, and it is coupled to the scene camera, the settings overlay and
the renderer's PAL `sourcePc` scene ids (`gs_gsr_bridge.cpp`, `runtime/gs`). The frame-rate module has 11 code
addresses in functions without a USA match. Player-object offsets shift by 4 from about 0xa3c (e.g. `+0x1028` →
`+0x1024`, `+0x1030` → `+0x102c`, `+0x1040` → `+0x103c`), which affects the Tab/inventory and spell code.
| Inventory / Tab screen / typography / map mouse (`gameui/Addresses.h`) | ~80, player object offsets shifted | 3–4 days |
| Dual wield, object motion, parry/combat audits | ~40 | 1–2 days |
| Subtitles (SetSubtitle, speech buffer; English USA speech timings differ) | ~5 + new subtitle timings | 1 day |
| Browser/Linux USA builds (`runtime/web/pre.js` names `/SCES_500.06`) | — | 0.5 day |

`analysis/regions/usa/function_map.csv` maps 99.8 % of the PAL functions, so code addresses are mostly mechanical;
the struct-offset audit is the real work. Data addresses come from `data_map.csv` (3,198 PAL addresses with an
unconflicted USA pair) or, for addresses no matched body references, from reading the code.
