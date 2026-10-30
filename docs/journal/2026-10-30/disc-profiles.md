# USA support review and reusable runtime profiles

## Review findings

The initial USA work correctly verifies the owned ELF, recompiles the USA code and preserves separate deployment
and save identities. The baseline was already observed reaching Surdana with keyboard/mouse input. It was not at
PC feature parity: one `kIsPal` switch disabled all higher-level hooks. Enabling that switch for USA would read
the wrong globals and object fields, including the known class-specific -4/-2 layout differences.

Identity was repeated in the builder registry, CMake branches and a C++ enum/table. The C++ address implementation
treated every non-PAL build as USA, so a third profile required source changes. The config translator also embedded
USA hand mappings and USA CSV defaults, and silently dropped newly unmapped exports. These are now data-driven,
with configuration validation and a test detecting stale USA recompiler config output.

## Implemented

- `regions.json` names a public runtime profile under `configs/regions/`. CMake discovers IDs and boot names from
  that same registry. The profile generator writes a target-specific header under the build directory and avoids
  rewriting unchanged output. Both profile and registry changes trigger CMake reconfiguration.
- Runtime profiles contain explicit capabilities, verified address pairs, typed movie/audio layouts and a rational
  frame rate. No enum or preprocessor selection names individual discs. Missing translations return zero; required
  mappings are rejected at configure time. Only the SHA-pinned reference ELF can use identity translation and
  unported hooks.
- Override installation now gates each feature separately. The common SDK input path is capability-selected and
  uses the shared input-module instance. Advanced input still requires its own layout audit.
- Movie pacing and completion protection use translated entry points, typed frame-counter/FPS-index offsets,
  per-profile rational pacing and screen-table bounds.
- Stereo-option synchronization uses translated sound/options objects and typed offsets on both discs. The fix
  that keeps the sound manager in sync with the Stereo Sound checkbox now applies to USA.
- Recompiler hand mappings and the four permitted missing SDK exports moved to the map directory's
  `config_overrides.json`. Unknown missing mappings fail before overwriting an output config.

## Evidence from the owned ELFs/discs

No game bytes or disassembly are stored here. Inspection used the existing ignored extracted ELFs and ISOs.

| Item | PAL | USA | Evidence |
| --- | --- | --- | --- |
| Movie decode entry | 0x1767a0 | 0x176740 | exact normalized function match; every non-stack player field used by the hook remains unchanged |
| FPS index / frame counter | +0xa8 / +0x2c | same | paired loads/stores in that body |
| Completion thunk | 0x440610 | 0x43ff38 | all 40 bytes identical; neighbour match in the function map |
| Screen manager | 0x4fb600 | 0x4fc480 | data map, 61 references, zero conflicts |
| Screen entries / count | +0x10 / 33 | +0x10 / 32 | paired screen helper 0x2c3f38 to 0x2c3e70 differs at upper bound 0x20 to 0x1f; factory bounds agree |
| Sound manager / stereo field | 0x4db4f0 / +0x440 | 0x4dc368 / +0x440 | unconflicted data map; SetGain 0x1e5470 to 0x1e53b8 loads the corresponding absolute flag |
| Options object / stereo field | 0x4fb6d0 / +0x74 | 0x4fc550 / +0x74 | unconflicted data map; exact Options_ApplyAll 0x3f9e20 to 0x3f9bb0 retains the +0x74 load |
| Logo frames | 264 | 316 | VDB headers |
| Intro frames | 3059 | 3671 | VDB headers |
| Outro frames | 2098 | 2515 | VDB headers |

The frame-count ratio is consistent with NTSC material at 30000/1001 fps; the profile uses that cadence (PAL
remains 25 fps). This is not a measured whole-movie audio sync result. The test clip has 195 frames on both discs;
it is not one of the normal logo/intro/outro paths.

## Validation

Profile validation and C++ tests cover both builds, their movie rates, all mapped addresses, unknown-address
rejection, a listener in the last valid screen slot, and a matching recycled pointer immediately past the table.
The latter catches the USA/PAL slot-count difference. Malformed profiles and unexpected missing SDK exports are
rejected; a synthetic third disc generates without code changes. Native build/run results are recorded below.

- All 153 Python tests passed. Both C++ disc-profile tests passed under the `runtime-tests` preset. Ruff,
  clang-format, `git diff --check`, the tracked-file repository audit and a separate audit of the new files passed.
- Built through `Build-LocalGame.ps1 -WorkDir build/usa-game -Incremental -Jobs 2 -CompilerProcesses 2 -NoWarnings`.
  Changing the profile compiler inputs caused a broad first rebuild, including VU slices and generated game batches.
  A final incremental pass rebuilt the affected runtime sources, reused the generated sources and deployed through
  the same script. Deployed executable SHA-256: `dd02f89028b57f8c44c8e04dd8237ef039bf1aa82ac38c3df73cf7f4c4be6ab5`.
- Runs used `dist/DrakanPC-usa`, D3D11, the existing HD pack and isolated memory cards under `build/harness`.
  `usa-profile-title`: 45-second runtime limit, clean exit 0; inspected capture shows the USA title screen.
  `usa-profile-gameplay`: 100-second runtime limit, clean exit 0; captures show Surdana, first paused and later
  unpaused with Rynn and the HUD. Both runs log `disc profile usa: features=0xf000` and 29.970 fps movie pacing.
- The completion guard is installed, and startup/New Game skipping completed without a crash. The destroyed-listener
  rejection case is covered by the synthetic C++ test; no destroyed-listener diagnostic was observed in these runs.
  Stereo synchronization is enabled, but no corrective stereo write was logged. Audio output was disabled while
  SPU2 continued silently, so neither stereo listening quality nor whole-movie audio/video sync was established.
- PAL's generated profile, full capability mask, translation, movie timing and audio layout passed the C++ tests.
  The PAL native player was not rebuilt or play-tested in this pass. Direct keyboard/mouse/gamepad play, saving,
  later levels and other renderers were not re-tested.

## Remaining work

This is a foundation plus movie/audio feature increments, not full USA parity. The next audits should cover
settings/input/pause together, then menus/saves, camera/combat, widescreen/scene hooks, inventory/map and subtitles.
Each migrated subsystem needs verified addresses plus class-specific layout fields and live validation on both
discs. Linux/browser USA gameplay and whole-movie audio sync remain unverified.
