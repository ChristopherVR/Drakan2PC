# Issue 1 parallel repair integration

The user requested separate GPT-6.1 Sol agents for the language, audio and shadow problem areas in
[GitHub issue 1](https://github.com/ChristopherVR/Drakan2PC/issues/1). Work started from HEAD `0892bc1` with the
existing local gameplay/editor changes preserved. No commits or GitHub messages were made.

## Starting checks and source review

* Repository guard: 2231 approved tracked files, zero prohibited files.
* Python suite: 234 tests passed before the new fixes.
* Language source review confirmed explicit English guest paths bypassed the selected implicit HLE root. The agent
  traced the actual guest selection to console OSD language, implemented boot configuration and kept native labels
  and automatic narration on the same effective language. Root review requested the latter synchronization so an
  unavailable/invalid language fallback cannot leave mixed labels/subtitles.
* Audio source and capture review identified a synthesized non-silent parking block. Regression covers both cores,
  cold/warm/repeated initialization, six pitches and preservation of uploaded audible samples. Root independently
  reviewed the patch and tests; the ADPCM decoder and device output are unchanged.
* Shadow review distinguishes duplicate silhouettes from the camera-following round patch. The native projector
  contains render-target setup and silhouette generation; final receiver blending needs separate attribution before
  changing default submission policy. The initial projector-disable diagnostic is opt-in only.
* Clang-format dry-run passed the SPU2 source/test, new language policy/header test and changed System.cpp.

## Shared build and test coordination

The combined incremental build uses `Build-LocalGame.ps1`, existing `build/final-game`, the player's PAL ISO and
normal `dist/DrakanPC` deployment. Its output is private at `build/issue-review/combined-build.log`. A temporary
exclusive `build/final-game.build.lock` guards this invocation. Generated sources include the changed syscall header,
so the build recompiles the generated translation units as well as affected runtime code.

The language agent added a regression through the real OSD syscalls in the existing kernel test file. Its broad
ps2x-tests build began recompiling unrelated VU/AOT units; the agent stopped only that owned process tree to avoid
contending with the game build. The focused language policy test passes; the full kernel regression is pending.

The agents coordinate deployed launches sequentially and use isolated cards, native options and log directories.
Any unrelated game processes remain under their owner's control. Per-area findings and detailed evidence live in
`issue-1-language.md`, `audio-parking-tone.md` and `issue-1-shadow-isolation.md` beside this entry.

## First deployment

The standard incremental build and deployment completed successfully. Deployed executable SHA-256:
`19EBEDA6A3E876836E38D2455B90CE59F0C0DFAE096CEDF24E4EAA56194CE31E`.
It includes language/audio corrections and the opt-in native projector diagnostic. Shadow metadata and shader
isolation probes prepared after linking are not in this first executable. No default shadow replacement fix is
claimed by this build. A fourth Sol agent separately investigates the camera-following circle while the other
shadow agent handles duplicated silhouettes.

## Audio deployed validation

The audio agent repeated the baseline recipe against the first deployment with internal PCM and Realtek system
loopback recording. Internal PCM from 13.1 to 13.6 seconds is exactly zero, replacing the former 11027 Hz parking
tone. The corresponding loopback interval contains no matching narrow line; it retains a decaying broadband/device
tail. No before-fix system recording exists, so this is internal before/after plus post-fix system confirmation.
The tested interval predates unrelated game launches observed later. The run exited cleanly.

Asset-free parking regression passes all 36 cases after failing those cases before the patch. The existing
data-dependent SPU2 suite passes 88 checks, and the audio output test passes. Reporter AE-7 hardware and subjective
long-duration listening are not claimed validated by this local Realtek test.

## Language deployed validation

German and English native title runs both exited through the script with code zero. Visible native menu labels and
actual guest archive/interface/music paths match the requested language. Root also inspected the German screenshot.
The German intro opens `GERMAN/LEVELS/LEVEL0RO/LEVEL0RO.SDB` and selects `intro_german.srt`; the optional external
subtitle file was absent. The internal intro recording is 34.255 seconds. No listening/transcription of a specific
German spoken sentence is claimed. The modern PC title screen remains hardcoded English independently of the fixed
guest language selection.

An initial relative harness path created a temporary isolated test directory inside the play folder. The agent left
it for the next normal deployment refresh after automatic review rejected its recursive removal. Successful proof
runs use absolute paths under ignored `build/issue-review/language/`; player cards/settings were not modified.

## Shadow feedback and second deployment

The optional replacement policy records actual backend-admitted object/generation pairs rather than assuming every
exported model casts rays. It uses two acknowledgements: caller composition and asynchronous worker admission/
submission. Review identified that caller composition can precede DXR worker admission; publication now supports
either ordering without a CPU wait, and an older result cannot overwrite newer failure/reset state. A merely pending
new frame does not starve an older successful worker submission. Native remains the default while attribution is
pending; `DRAKAN_NATIVE_CHARACTER_SHADOWS=auto` enables the experimental policy.

Focused backend/feedback tests pass, covering actual admissions, zero opacity, singular transforms, camera shells,
startup, address reuse, disabled/failed/reset paths and asynchronous ordering. Diagnostics isolate AO, direct shadows
and receiver confidence on both backends. Ray scene dumps now have same-frame model metadata sidecars.

The second build and a verification invocation through the standard script completed. Verified deployed SHA-256:
`0C08F057E4FD453FC54BA7D34E4FA6E99C32B735253910AB812799A6C8DC3BB4`.
The shared build script gained its own exclusive writer lock during this session. The root's older outer lock wrapper
encountered the persistent closed lock-file path; a subsequent externally held lock was correctly rejected by the
script. Verification used the script's own lock directly, with no competing builder found. The temporary misplaced
language-test directory was removed by the normal deployment refresh and verified absent, resolving that cleanup
without a manual recursive delete.

## Rejected replacement and reporter-configuration harness

Sunny courtyard comparisons prove the full native projector owns ordinary Rynn's visible silhouette: forced native
off removes it. However the experimental automatic policy also removes the only clear humanoid silhouette while
modern world shading remains. Admission/submission evidence alone therefore does not establish a perceptually valid
replacement. The experimental feedback code, integration hooks and test target were removed surgically; its probes
are archived under ignored `build/issue-review/experimental-auto-policy/`. Native remains enabled by default, and
only the explicit native-projector isolation switch is retained. No duplicate-shadow fix is claimed.

The resulting standard game rebuild/deployment succeeded with SHA-256
`166AB8BEECBE39E2BD1B36B9F7FA1E379A13B8158F6B672760F3443DA0ADAB54`.

The user has no reporter save/location and directed updating the harness using the reporter's configuration.
`scripts/harness/shadow_review.py` and its accompanying profile implement a repeatable matrix with fresh isolated
cards/settings and camera sweeps. The observed baseline retains water/reflections on with shadows off, scale 4,
16:9, uncapped cadence, no vsync, native filtering/weave, borderless presentation and foliage strength 0.90.
Other cases explicitly isolate native, ray, AO, direct and receiver contributions on DX11/WebGPU. Manifests distinguish
known configuration, unknowns and test overrides; execution/evidence status cannot imply a visual defect is fixed.

Root independently checked the dry-run 14-case plan and its German/scale/water/reflections/shadows settings.
Initial live runs confirmed reporter renderer settings on both backends. They exposed that global dump epoch 200
can precede the target world's preparation; the next diagnostic change makes dump epoch selectable and adds an
applied-mask receipt for WebGPU rather than inferring it from a requested environment variable.

## Final harness and validation

The final standard build/deployment succeeded with SHA-256
`1BC35F076F10A9C529E4B862BAE1A76D7000FC51E478295A6F896718402D0531`.
It retains the audio/language fixes, native projector isolation, AO/direct/receiver diagnostics, selectable scene-dump
epoch and same-frame model provenance. The rejected automatic replacement is absent.

Four selected final reporter-profile runs completed with clean scripted exits and complete **backend-supported**
execution/configuration evidence: German DX11 observed baseline and receiver mode, German WebGPU direct mode, and
English DX11 observed baseline. All executable hashes match before/after each run and across the set. The runs use
zero mod packs/overrides and fresh cards/settings; the known renderer, language, foliage and cadence receipts match.
DX11 has target epoch-1000 scene/model/depth evidence with matching model-level identity and camera X/Z; the receiver
mask is observed. WebGPU's direct mask is observed, while its absent equivalent scene/depth and reflection-enable
receipts remain explicit unverified fields. These are not visual defect closure claims.

Root independently aggregated the three summary files: four clean exits, four supported-evidence successes, stable
binary, and all `visual_review.fixed` fields still null. Root inspected the German native title, receiver-confidence
image and WebGPU direct image. No reported circle or separated duplicate attribution is established by these samples.

Final checks: full Python suite 245 passed; the nine harness regressions were rerun successfully after final isolation
changes; Ruff lint/format and relevant whitespace checks passed; repository guard reports zero prohibited tracked files.
The two focused GS GPU tests pass. Prior audio and language validation is recorded above and in their per-area notes.
The full OSD kernel regression remains unrun because of its unrelated broad VU/AOT rebuild dependency. No GitHub
messages, commits or releases were made, and no unrelated game processes were terminated.

Usage and the full opt-in 14-case matrix are documented in `scripts/README.md` under "Disc settings and camera checks".
Local results: `build/harness/issue1-reporter-isolated-{d3d11,webgpu,english}/summary.json`.

## Modern menus follow-up before committing

The user subsequently requested localized modern menu titles/labels and a commit of all pending changes.
Modern main, pause, embedded legacy Audio/Controller, and save/load menus now use a shared five-language catalog
selected from the validated effective boot language. Explicit lookups and placeholder templates preserve custom
skin text, campaign names and guest/save metadata. Longer translated headings/buttons are measured and fitted.
The selected catalog and live validation are recorded in `modern-menu-localization.md`.

German and English modern-title runs and a German pause/save run exited cleanly. Root inspected the German main,
pause and save screenshots. The full runtime suite passed 103 executed tests with 6 data-dependent skips and no
failures; Python passed 247 tests. This resolves the earlier modern-title English limitation for the covered menus.
The reported shadow symptoms remain unresolved; broad PC settings/editor/multiplayer-page translation and other
documented project gaps are not claimed complete by this follow-up.
