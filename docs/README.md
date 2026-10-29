# Documentation index

Convention: **reference** docs describe a feature or subsystem and are kept current as the code changes. **status** docs are living trackers, updated in place. **journal** entries are append-only dated records of investigations, measurements and handoffs; they are not rewritten to match later results, so read them as of their date.

## Player guides and website

The [GitHub Pages site](https://christophervr.github.io/Drakan2PC/) presents the guides with navigation and search.
The canonical Markdown remains in this repository:

- [PC enhancements compared with PS2](guides/enhancements.md)
- [Install on Windows](guides/install.md), [install on Linux](guides/linux.md) and [play in your browser](guides/browser.md)
- [Controls & combat](guides/controls.md) and [graphics & PC settings](guides/settings.md)
- [HD textures, video & subtitles](guides/extras.md)
- [Saves & updates](guides/saves.md) and [troubleshooting](guides/troubleshooting.md)
- [Developer overview](guides/development.md) and [technical reference index](guides/reference.md)
- [Maintain the GitHub Pages site](reference/github-pages.md)

## Reference (25)

- [GitHub Pages documentation site](reference/github-pages.md)

- [Combat input investigation — 17 October 2026](reference/combat-controls.md)
- [Optional DLSS integration: actual status](reference/dlss.md)
- [Environment flags of the gs renderer](reference/env-flags.md)
- [Disc regions: PAL SCES-500.06 and USA SCUS-971.28](reference/regions.md)
- [Drakan FMV / cutscene pipeline](reference/fmv.md)
- [Ghidra setup and function list](reference/ghidra.md)
- [Drakan PS2 (SCES_500.06) - graphics pipeline analysis and PC replacement design](reference/graphics.md)
- [GS / GIF capture format (`DRKGSCAP` v1)](reference/gs-capture.md)
- [HD texture pipeline: review on real data, gaps, fixes, workflow](reference/hd-textures.md)
- [EE hardware-register access summary (Drakan SCES_500.06)](reference/hwregs-summary.md)
- [Input: pad -> game data flow and mouse + keyboard hook plan](reference/input.md)
- [Drakan: The Ancients' Gates (PAL, SCES-500.06) - IOP modules, SIF RPC and file I/O](reference/iop-and-io.md)
- [Mouse on the game's own menus, and the PC Settings overlay over a running game](reference/mouse-menus.md)
- [Performance work (branch `drakan-perf` of tools/PS2Recomp, worktree `tools/PS2Recomp-perf`)](reference/perf.md)
- [Portability: Linux and browser readiness](reference/portability.md)
- [Save pages (16 slots), title "Quit", text-bound menu hit areas](reference/save-slots.md)
- [Sony / middleware libraries present in SCES_500.06](reference/sdk-libs.md)
- [Game menus and the "PC Settings" entry](reference/settings-menu.md)
- [Sound: the "missing attack / footstep / ambient sounds" bug, how it was found, and how to debug the sound path](reference/sound-debug.md)
- [Stability audit: crash signatures, instruction-level differential tester, race stress (branch `drakan-stab`)](reference/stability.md)
- [Subtitles: what the game has, how it works, what to add](reference/subtitles.md)
- [Controller prompts -> keyboard/mouse prompts](reference/ui-prompts.md)
- [VU1 correctness investigation (clipping wedges, grid/warp overflow)](reference/vu1-correctness.md)
- [Widescreen (16:9, 21:9) for the Windows port](reference/widescreen.md)

## Status (2)

- [Recompiled Drakan: status](status/recomp-status.md)
- [Recompiler evaluation: PS2Recomp vs. own translator](status/recompiler-eval.md)

## Journal (84 entries, by date)

### 2026-10-17

- [Combat encounter evidence (2026-10-17)](journal/2026-10-17/combat-encounters.md)
- [Quick Inventory stall: reproduced and fixed](journal/2026-10-17/combat-quick-inventory.md)
- [Main-thread death after the intro skip / during the Level 0 load: root cause and fix](journal/2026-10-17/crash-listener.md)
- [Main-thread death ~45 s into the level ("memcpy size clamp 0xfffffffc", jump to 0xa21624a1): root cause and fix](journal/2026-10-17/crash-memcpy.md)
- [Graphics, pacing and PC controls follow-up — 17 October 2026](journal/2026-10-17/followup.md)
- [Geometry fixes (branch `drakan-geo` of tools/PS2Recomp, worktree `tools/PS2Recomp-geo`)](journal/2026-10-17/geometry-fixes.md)
- [Opt-in backend CPU call timing (GS, `GSR_PROFILE`)](journal/2026-10-17/gs-cpu-profile.md)
- [GS alpha follow-up](journal/2026-10-17/graphics-followup.md)
- [Inventory, graphics and build workflow — 17 October 2026](journal/2026-10-17/integration-graphics.md)
- [Neural rendering and Quick Inventory integration — 17 October 2026](journal/2026-10-17/integration-nr.md)
- [Combined Super Resolution, neural rendering and combat — 17 October 2026](journal/2026-10-17/integration-sr-nr.md)
- [Combined build continuation — 17 October 2026](journal/2026-10-17/integration.md)
- [Movement/combat follow-up handoff (input)](journal/2026-10-17/input-followup.md)
- [Inventory alpha-only mask regression](journal/2026-10-17/inventory-alpha-mask.md)
- [Inventory categories, Equipment and movement validation](journal/2026-10-17/inventory-category-validation.md)
- [Inventory orb: proven shifted render-target row alias](journal/2026-10-17/inventory-rt-row-alias.md)
- [SR/NR pacing measurement, 17 October 2026](journal/2026-10-17/pacing-benchmark.md) Data: [pacing-benchmark.json](journal/2026-10-17/pacing-benchmark.json).
- [Pacing follow-up: CPU candidate and measurement contract](journal/2026-10-17/pacing-followup.md)
- [Where the time goes: Level 0 intro cutscene and 3D gameplay (measurements for the perf agent)](journal/2026-10-17/perf-notes-intro.md)
- [Independent world/culling audit](journal/2026-10-17/world-followup.md)

### 2026-10-18

- [Combat and audio verification — 18 October 2026](journal/2026-10-18/combat-audio.md)
- [Direct mouse combat follow-up, 18 October 2026](journal/2026-10-18/combat-mouse.md)
- [Native timed-parry investigation — 18 October 2026](journal/2026-10-18/combat-parry.md)
- [Controls freeze and native menu typography — 18 October 2026](journal/2026-10-18/controls-font.md)
- [SR depth transitions — 18 October 2026](journal/2026-10-18/depth-transitions.md)
- [DLSS presentation and experimental DXR water review](journal/2026-10-18/dlss-water-review.md)
- [Final performance scenarios — 2026-10-18](journal/2026-10-18/final-perf-profile.md)
- [Depth and combat follow-up — 18 October 2026](journal/2026-10-18/followup-depth.md)
- [Native inventory, input and renderer follow-up](journal/2026-10-18/followup-render-input.md)
- [Combat, texture coherence and upload performance — 18 October 2026](journal/2026-10-18/followup.md)
- [Free mouse combat and field of view](journal/2026-10-18/free-combat-fov.md)
- [Higher world frame rate — experimental](journal/2026-10-18/higher-fps.md)
- [Orb edge lines: captured atlas and point-sampling evidence](journal/2026-10-18/hud-orb-edge.md)
- [Equipped weapon and tutorial HUD mapping](journal/2026-10-18/hud-weapon-tutorial.md)
- [Inventory texture and text fixes — 18 October 2026](journal/2026-10-18/inventory-texture-fixes.md)
- [Inventory stale-gap source correction](journal/2026-10-18/inventory-viewport-handoff.md)
- [Frozen-source Map mouse probe](journal/2026-10-18/map-mouse-live-probe.md)
- [Native inventory refinement — 18 October 2026](journal/2026-10-18/native-inventory.md)
- [Resident model reflections follow-up — 18 October 2026](journal/2026-10-18/reflection-scene-review.md)
- [Uncapped fixed4 performance matrix — 2026-10-18](journal/2026-10-18/runtime-perf-matrix.md)
- [Native GS sprite sampling convention — source-only follow-up](journal/2026-10-18/sprite-sampling-convention.md)
- [Subtitle rendering and visibility fixes](journal/2026-10-18/subtitle-fixes.md)
- [Decoded texture cache coherence — 18 October 2026](journal/2026-10-18/texture-coherence.md)
- [Texture sampling and pixel-validation handoff](journal/2026-10-18/texture-sampling-handoff.md)
- [Uncapped world rendering — experimental](journal/2026-10-18/uncapped-fps.md)
- [Native water coverage and resident reflection geometry](journal/2026-10-18/water-global-review.md)

### 2026-10-19

- [CPU render pipeline performance](journal/2026-10-19/cpu-render-queue.md)
- [VU batch scheduling and observer refinement, 2026-10-19](journal/2026-10-19/cpu-vector-batch-scheduling.md)
- [Vector FMAC compiler analysis — 2026-10-19](journal/2026-10-19/cpu-vector-fmac.md)
- [Live multicore VU geometry, 2026-10-19](journal/2026-10-19/cpu-vector-live-batch.md)
- [Vector operand CPU work, 19 October 2026](journal/2026-10-19/cpu-vector-operands.md)
- [Multicore vector geometry investigation — 2026-10-19](journal/2026-10-19/cpu-vector-parallel.md)
- [Explicit vector write-back choice — 2026-10-19](journal/2026-10-19/cpu-vector-shadow.md)
- [CPU performance follow-up, 19 October 2026](journal/2026-10-19/cpu144-followup.md)
- [Menu artwork, loading layout and save previews — 19 October 2026](journal/2026-10-19/menu-presentation.md)
- [Shared menu presentation — 19 October 2026](journal/2026-10-19/menu-suite.md)
- [Native vector kernels, 19 October 2026](journal/2026-10-19/native-vector-kernels.md)
- [Native VU loop candidates](journal/2026-10-19/native-vu-loops.md)
- [Camera backdrop and river reflections](journal/2026-10-19/water-backdrop.md)
- [Water edges, terrain shadows and movement validation](journal/2026-10-19/water-edges.md)
- [Reflection filtering and neural motion texture work](journal/2026-10-19/water-polish.md)

### 2026-10-20

- [Vector batch pipeline investigation — 2026-10-20](journal/2026-10-20/cpu-vector-batch-pipeline.md)
- [Reflection coverage and remaining renderer limits — 2026-10-20](journal/2026-10-20/render-limits.md)
- [Shallow shoreline reflections and directional sky retention](journal/2026-10-20/water-sky.md)

### 2026-10-21

- [CPU upload block decoder and audited gameplay, 2026-10-21](journal/2026-10-21/cpu-block-color-decode.md)
- [Broader CPU bottleneck check — 2026-10-21](journal/2026-10-21/cpu-frame-scope.md)
- [GPU readback stalls and gameplay validation — 2026-10-21](journal/2026-10-21/cpu-gpu-readback.md)
- [Partial upload coherence and Surdana validation - 2026-10-21](journal/2026-10-21/cpu-partial-upload.md)
- [RGB24 uploads and VU flag retirement — 2026-10-21](journal/2026-10-21/cpu-rgb-upload.md)
- [Sub-block upload ownership and redundant texture work — 2026-10-21](journal/2026-10-21/cpu-subblock-source-reuse.md)
- [Surdana consistency and texture resource reuse — 2026-10-21](journal/2026-10-21/cpu-surdana-consistency.md)
- [Texture and geometry gameplay investigation — 21 October 2026](journal/2026-10-21/cpu-texture-submission.md)
- [Renewed GPU upload and geometry tests — 21 October 2026](journal/2026-10-21/cpu-upload-coalescing.md)
- [Worker dependency plans — 2026-10-21](journal/2026-10-21/cpu-vector-dependency-plans.md)
- [Guarded geometry reuse investigation — 2026-10-21](journal/2026-10-21/cpu-vector-guarded-reuse.md)
- [Paired geometry transforms — 2026-10-21](journal/2026-10-21/cpu-vector-transform.md)
- [Ray material residency experiment, 21 October 2026](journal/2026-10-21/dlss-material-residency.md)
- [Moving-model guide allocation overhead](journal/2026-10-21/dlss-overhead.md)
- [Packed ray textures — 21 October 2026](journal/2026-10-21/dlss-packed-textures.md)
- [Rejected deferred ray completion wait experiment — 21 October 2026](journal/2026-10-21/dlss-ray-wait.md)
- [World-triangle packing cache — 21 October 2026](journal/2026-10-21/dlss-world-cache.md)
- [Hillside lighting gap: fixture correction](journal/2026-10-21/lighting-fixture.md)
- [Water-edge shadow continuity — 2026-10-21](journal/2026-10-21/shore-shadow.md)

### 2026-10-23

- [HD movie pack (AI-upscaled logo / intro / outro)](journal/2026-10-23/hd-movie-pack.md)

### 2026-10-25

- [Linux and browser ISO-only readiness audit](journal/2026-10-25/linux-browser-readiness.md)
- [Linux runtime foundation](journal/2026-10-25/linux-port-foundation.md)

### 2026-10-26

- [Linux and browser player implementation](journal/2026-10-26/linux-web-player.md)
- [Portable GPU rasterization and validation](journal/2026-10-26/portable-gpu-renderer.md)
