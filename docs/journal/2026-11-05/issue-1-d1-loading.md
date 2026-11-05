# Issue 1: converted Order of the Flame levels do not load

The portable editor import dialog used `convertPcLevel` directly, while the CLI used the complete `convertOotfLevels` pipeline. The dialog omitted required template splat DB/TXD files, did not preserve the pipeline's singleton preparation and generated resources, and exported no campaign metadata. Runtime discovery of the zip alone therefore did not imply a playable campaign or a title game choice.

A further mismatch existed between the prefixed pack level title and the unprefixed converted header/dependency names. The shared pipeline now owns level names, campaign ordering and exit conversion. Its campaign plan sorts the Intro first when present; the manifest starts in its first non-multiplayer level. Multiplayer-only imports remain arena packs.

## Changes

- The dialog invokes the same conversion pipeline as the CLI and includes generated files and per-level splats.
- `ootfDiscContext` reads dependency containers and host assets through the existing language-relative disc API. It uses the existing PS2 dependency resolver, skips the synthetic splat dependency, and fetches the template splat DB/TXD separately. Missing required splats stop export with an actionable error. Optional absent resources remain absent; failed pipeline stages stop export instead of presenting a misleading successful download.
- Campaign imports register `campaign = "ootf"`, their own start and title, enabling the existing title choice. Installation text explains enabling the mod, restarting and choosing New Game.

## Validation and limits

`npm run typecheck -w @drakan/editor-app` passed. The focused Vitest run passed 33 tests across the new disc-context tests and existing pipeline, campaign and arena suites. New synthetic tests cover relative dependency resolution, host resource discovery, optional missing procedural textures, and both required missing splat cases.

No German Order of the Flame install was available for a real conversion/playthrough. These corrections address source-confirmed packaging and entry-point defects; they do not establish complete compatibility with every localized PC class/string table. Output pack files retain the existing English virtual-disc contract used by `buildOotfPack` and the pipeline; the selected PC source language does not determine that output root. Native in-game verification and portable-editor release packaging remain parent-task checks.

## Local installation validation follow-up

The local owned Order of the Flame installation is `build/ootf/cab_disc` (it contains
`Drakan.exe` and 23 discovered campaign/multiplayer levels). A scratch Node probe
under `build/ootf-run/issue1-local` read the PS2 ISO directly using its ISO9660
index and invoked the actual `ootfDiscContext` used by the editor, rather than the
CLI's broader pre-extracted context. All 13 template dependency ODBs resolved,
including resources, NPCs, world, mountain and level-common host containers.

With those exact editor dependencies, the complete current pipeline converted
`Mountain World/Intro Level/Intro.lvl` from the local installation: all 15 reported
stages succeeded. The resulting private pack contains 65 files (17,712,561 bytes),
including its converted level's `.DB` and `.TXD` splats. Its manifest declares
`campaign = "ootf"` and both its level and `campaign_start` are `OotF - intro level`.
No original assets or generated pack content were added to tracked files.

The private native test pack and mods.ini are prepared under
`build/ootf-run/issue1-local/editor-mods`; a native run is pending the shared GPU
slot. A separate complete 23-level CLI conversion was also started for campaign
ordering and multiplayer-pack coverage; its log is
`build/ootf-run/issue1-local/conversion.log`. This validates English local source
data only; the earlier German-source limitation still applies.

### Native failure reproduced and runtime correction

The complete CLI conversion finished with zero failed stages: 23 levels, ten arena
definitions and 10,559 files in a 141,886,179-byte pack. Its first four manifest
levels are Intro, Ruined Village, Ruined Village Book and Wartok Canyons.

The exact editor-path Intro pack exposed another concrete failure in the native
runtime with German selected: whole files remained in `ENGLISH`, while generated
level registration resolved the level under `GERMAN`. Dependency entry patches
already targeted the selected language. The log rejected `OotF - intro level`
(`no file GERMAN\\LEVELS\\OOTFINEL\\OOTFINEL.LVL`) and New Game fell back to the D2
intro and Surdana. The bounded baseline run ended without a scripted gameplay
quit; it is a failed import-load test. Executable SHA256 was
`28968FAB5E7B3A7935572D0D2CD489FA5DB2847DFA837413C2E65069CB00904E`.
An external game run overlapped; no performance conclusions are drawn.

`ModManager::installFiles` now mounts English-namespace pack files in the active
language as well, retaining their explicit English paths for converted DB
references. This changes only pack overlays; original ISO path resolution stays
unchanged. A later pack takes precedence; an explicitly localized file wins over
the English fallback in the same pack. A mixed-case ZIP regression fixture checks
German level registration, actual reads, both precedence rules, explicit English
reads and preservation of an unrelated French disc file. The existing
`build/runtime-tests` mod_test target passes all 151 checks. Native retest awaits
the canonical incremental build/deploy.

### Deployed registration retest

On deployed executable SHA256
`9C38CB9042B43A916A7E4E49CFFAC0534F3FF6DF20EDE62E883C303C647F4C78`,
`editor-fixed-absolute` registers the exact editor-generated Intro pack and loads
`cdrom0:/German/Levels/OotF - intro level/OotF - intro level.lvl` successfully.
The world reports 101 entities, input reports player under control, screenshots
are written and the script quits cleanly. This proves localized registration and
loading. Both screenshots show a black world with HUD, so this is **not** a visual
playability pass. Intro is a cinematic level; conversion specifies a 44-second
automatic handoff when its next level is also imported. The initial single-Intro
pack has no next level. A two-level exact editor-context Intro/Ruined Village
pack is being used to test the default campaign handoff and terrain/control.

An earlier `editor-fixed` harness run had relative output paths interpreted from
the play folder and never executed its intended recipe. Root corrected
`play_run.py` to resolve output and executable paths. Only the subsequent
`editor-fixed-absolute` run supplies screenshot/script evidence above.

### Two-level campaign menu attempt

The exact editor-context Intro/Ruined Village conversion succeeded in all 26
reported stages, including both exits and campaign strings, producing 751 files
(36,719,477 bytes). Manifest order/start are `OotF 00 - Intro` then
`OotF 01 - Ruined Village`.

The first `game=ask` native attempt (`editor-campaign-live`) did **not** test
campaign selection: `sync screen 30` matched the startup Sony splash, so its menu
keys and screenshots were too early. The screenshots show that splash, and the
log confirms D2 was chosen. After capturing diagnostics, the test's sole owned
process was closed normally (exit 0, without the intended scripted quit). No game
process remained and the live slot was transferred. The private prepared recipe
now waits for actual title screen 8 before card screenshots and selection.
A corrected menu/handoff/render run remains required; no campaign-menu or
Ruined Village visual success is claimed from this attempt.

### Source-gated game choice verification

PAL profile identifies native title screen 8. `TitleUi::titleDraw` emits
`overlay: title session ... opened` only after host title activation; the first
frame offers the game card when `drakan_game::offerChoice()` is true. An inbox
driver captured that actual German card with both Drakan and Order of the Flame
visible. Its pad commands did not navigate the host card: `injectPadTap` feeds
the EE pad, while a scripted host screen consumes `tabScriptInput` events.
Acknowledged pad injection is therefore not selection proof. That run was closed
normally and its evidence retained.

The corrected native run `editor-campaign-script` waits for screen 8 and drives
the supported script-key path. Its actual screenshots show the game card and the
Order of the Flame title. Logs confirm `SelectGame(1)`, `[game] switched to ootf`,
`SelectGame -> confirmed`, and `New Game of 'ootf' opens 'OotF 00 - Intro'`.
The German converted Intro loads successfully. This verifies the campaign menu
without forcing `DRAKAN_GAME=d1`. Intro remains black with HUD at the initial
screenshot; handoff and terrain evidence are recorded separately below.

### Verified campaign handoff, world rendering and movement

`editor-campaign-script` completed in 93 seconds with a clean scripted quit and
exit 0 on executable SHA256
`9C38CB9042B43A916A7E4E49CFFAC0534F3FF6DF20EDE62E883C303C647F4C78`.
No missing-target or crash marker occurred. The Intro's converted automatic exit
fired and Ruined Village loaded successfully through the German virtual path at
68.573 seconds (Intro loaded at 25.312 seconds). World sampling reports 106
entities in Intro and 2,204 in Ruined Village.

`after_intro.png` visibly shows Rynn, terrain and textured trees;
`village_walk.png` shows the changed forest view after three seconds of W input.
The full 32 MiB `ram_before_walk.ram` / `ram_after_walk.ram` captures were decoded
with the existing profile-aware `settings_check.camera_state` helper. Rynn moved
from `(268101.219, 20505.357, 204748.016)` to
`(267333.094, 20502.736, 205496.359)`, a displacement of 1,072.400 guest units.
This proves actual native movement after the campaign handoff, not only a menu or
under-control flag. The private `movement-report.json` preserves the measurement.
All captures and generated pack contents remain ignored under `build/`.

The loading report is addressed for this owned English local source installation
with German selected in the target runtime: real editor-context conversion, game
choice, New Game, localized level loading, timed Intro-to-Ruined-Village handoff,
world rendering and movement all exercised. The Intro cinematic itself still
renders black with HUD; its scene presentation remains a compatibility limit,
even though the campaign now progresses automatically to the visible playable
level. This is not a full-campaign playthrough or a German PC source-data test.
After clean shutdown no `drakan.exe` remained; the slot returned to root for the
next canonical deployment.
