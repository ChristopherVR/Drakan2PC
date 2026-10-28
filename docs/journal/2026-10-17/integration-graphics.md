# Inventory, graphics and build workflow — 17 October 2026

This receipt supersedes earlier same-day deployment identities. Candidate exe:
`2AE7FC64D6AD9C87186925913F8D5F2C35D0AD0F5CA1FA89D994754DF34555FB`.
Runtime development revision: `2c863bd`; public runtime source is reproduced by
`patches/runtime/drakan-runtime.patch` against the pinned upstream commit.
The manifest identifies all 150 authored source paths and excludes the private
VU instruction image and game icon. Those private inputs are generated locally.

## Changes

- Present pacing sleeps outside renderer locks, with a bounded worker/window
  handoff. This removes a demonstrated window-thread starvation path.
- Engine world submission is explicitly bracketed before drawing. Fogged sky
  triangles and inventory preview cameras no longer select gameplay HUD maps.
  World projection and CPU frustum use the same wider view. Escape/pause keeps
  the established world aspect.
- Inventory uses one page transform, shared by geometry, text and mouse hit
  testing. The unused left canvas margin is cleared once per displayed target
  and frame. The right live-world preview is preserved.
- Absolute equipment-grid hit testing uses the native uploaded viewport,
  including its X offset. Selection must complete before native equip input.
  Clicking an empty cell preserves selection, preview and equipment.
- Smooth texture upscaling preserves RGB for textures whose stored alpha is
  entirely zero; PS2 material state can still use their color. This repairs a
  decode/upscale error, not the limited detail in the original textures.

## Combined checks

`inventory-final-native-v2` completed normally at input frame 1312, exit 0.
All seven live RAM assertions passed: dagger/bow selection and native equipment
channels, correct previews and no changes on an empty-cell click. Separate bow
snapshots show ammunition **200 → 199 → 198**. Quick Inventory consumes no extra
arrow and ends inactive, with neutral input. Captures preserve the right-side
preview and remove the stale left-side panel. Existing saves are unchanged.
See [combat evidence](combat-encounters.md) for details and limits.

`inventory-final-srnr` also completed normally, with seven mouse assertions
passing. Logs record **787 successful SR evaluations and 787 successful NR
evaluations**: SR Quality 1364×1364 → 2048×2048, followed by NR with 2048×2048
guides. Both features are optional. This is functional evidence, not a measured
image-quality comparison or a guarantee of moving-object coverage.

The requested 3840×2160 window was clamped by the desktop to **1904×993** in the
final native test. Internal GS scale was **4**. These are not native 4K output
captures. The earlier larger-desktop captures have separate evidence folders.

Five renderer CTests passed: HUD page/canvas, motion history, motion raster
shader, NR guides and texture upscaling. Input checks passed **641 + 43**.
Private runtime preparation passed **17** synthetic tests, and source/history
publication guards passed **5** tests, including rejection of a deleted ISO in
reachable Git history. Moving narration text to private local input preserves
the generated intro/outro SRTs byte-for-byte; no transcript is published.

## Performance limits

Host presentation ticks are not unique rendered-world frames. The final native
diagnostic measured roughly **59.2–60.4 host ticks/s**, but **27–60 guest GS
vsyncs/s**, median 57.5. It includes PNG captures and 32 MiB RAM writes. One late
1.60-second PadMgr interval is not explained by those writes alone. Sustained
60 guest updates throughout gameplay is **not established**.

Follow-up `inventory-final-clean-perf` used the exact same executable and input
sequence with no RAM dumps, PNG captures, combat audits or renderer profiles.
It quit normally, exit 0, in 62.3 seconds. The last ten windows measured
**59.5–60.5 guest GS/s**; host ticks after startup were **59.9–60.1/s**. Earlier
guest transients were 45.5–74.0/s. The late slowdown did not recur. Scene logging
remained enabled, so this is not a completely uninstrumented benchmark. Canvas
review found no direct flush/readback/wait; broader dirty rows can affect later
readbacks, but no resulting regression was demonstrated in this clean run.

The instrumented SR+NR run measured late guest rates around **24–27/s** at the
tested internal resolution. Features remain off by default. No Frame Generation
or Ray Reconstruction was added. Long runs, device loss, enemy damage from the
new bow checks and complete Arokh combat remain unverified. Existing duplicate
ShowCursor and /FORCE linker warnings remain.

## Build workflow

Windows and Linux hosted jobs build the **compiler tool**, check its CLI, apply
the clean runtime patch and create tool/corresponding-source packages without a
game image. Initial builds passed; artifact upload encountered the account's
GitHub Actions storage quota. The follow-up
[run 39027830437](https://github.com/ChristopherVR/Drakan2PC/actions/runs/39027830437)
completed successfully for both operating systems, reporting unavailable
uploads separately. See [BUILDING.md](../../../BUILDING.md) for workflow and local
packaging commands. The final source audit covers 370 files with zero prohibited
tracked files. The working tree is on `main`, tracking `origin/main`.

The local ISO workflow verifies the supported PAL ELF, generates 82 C++ batches
and prepares private VU/icon inputs from the public configuration. Clean source
generation and CMake configuration passed. A complete clean generated-game
compile/link/launch using the new wrapper has **not** completed validation. All
150 exported source paths match the active runtime after line-ending
normalization. A fresh standalone Release build of the exported `gsr_nr` bridge
also passed with official public Streamline headers and no compiler warnings or
errors; its authored transport and pinned interoperability declaration are
included. The
deployed merged executable is separately built and tested as recorded above.
Linux game and browser game builds require real platform adapters and are not
provided by the compiler-only jobs.

Local evidence stays under ignored `build/t/`, `build/source_validation/`,
`build/motion_check/` and `build/subtitle_source_publication_check/`. Deployment
identity and rollback location belong in local `dist/DrakanPC/BUILD_INFO.json`.

The exact tested executable was deployed to `dist/DrakanPC/drakan.exe`.
**3,001 protected files** (settings, saves, texture pack, subtitles and vendor
DLLs) retained their hashes. The previous executable and release notes are in
local `build/release_backups/20261017-211356-inventory-graphics/`.
