# GitHub issue review and repair plan

Reviewed 2026-11-04 against working checkout HEAD `0892bc1` plus existing local changes.
The live GitHub list returned one open issue: [#1, Audio, Raytraced Shadows & Language Selection](https://github.com/ChristopherVR/Drakan2PC/issues/1), reported on v0.1.2.
Read the issue, its owner acknowledgement, attached log and both screenshots. Downloaded evidence stays under
ignored `build/issue-review/`. This is a source/evidence review: no new game run, audio recording, code fix or
release validation was performed. Existing uncommitted implementation work was not changed.

## Findings and confidence

### Language selection: configuration paths disagree (confirmed)

The attached log says `language root \GERMAN`, then opens `ENGLISH\DRAKAN2.RRC`, English interface resources and
English music. This is not merely misleading UI text.

* `drakan_overrides.cpp::resolveLanguageRoot` reads the PC setting and passes it to `IsoFs::setLanguageRoot`.
* `IsoFs.cpp::resolveKey` adds that root only when a path has no recognised language prefix. A guest request for
  `cdrom0:\English/...` keeps English even when the selected root is German. Lookup succeeds, so no fallback occurs.
* `Kernel/Syscalls/Helpers/Runtime.h::ensureOsdConfigInitialized` independently initializes the emulated console
  language to English (`1`). `GetOsdConfigParam` returns that state; no connection to the PC language setting was
  found. Tracing guest SelectLanguage will verify that this is the source of the English guest paths.
* Native added menu labels and external narration subtitles read the PC setting independently, allowing mixed languages.

Fix direction: normalize/validate the selected language once and configure the emulated console language before
guest startup through a runtime-facing configuration API. Keep OSD and OSD2 consistent; use the disc profile's supported
languages and retain the documented restart requirement. Trace SelectLanguage before deciding whether a game-specific
initialization hook is also necessary. Avoid globally rewriting every explicit language path in IsoFs: tools and mods
can deliberately address other trees. Define missing-language behavior explicitly and log the effective selection.

Acceptance: German text and a known German spoken line after restart, German guest resource opens from boot, English
regression, all supported PAL language mappings, environment precedence, missing/invalid language handling and USA
English-only behavior. Include mod lookup and save/load smoke checks.

### Double shadows: native projection and modern shading coexist (strong cause candidate)

The screenshots show overlapping character silhouettes. Native character projection remains active; even the local
avatar projector hook in `drakan_ghost.cpp::hookCharacterProjector` delegates to the native projector after choosing a
body. Separately the renderer composites modern ray shading over the already rendered world. No shared policy linking
native projection to successfully active ray shadows was found. This can retain the native silhouette under a second
shadow with a different light direction. Recent local avatar-shadow work changes the native silhouette source; its
focused validation does not establish that this v0.1.2 ray-shadow report is fixed.

Fix direction: first capture identical frames with native projection and modern shadows independently enabled/disabled
to attribute both silhouettes. Then implement one character-shadow policy across both backends. Suppress only the
identified native world-shadow submissions when the modern replacement is usable, with native fallback when ray
initialization fails or the modern pass is unavailable. Preserve menus, inventory glyph shadows and character geometry.
Account for the fact that pass success is known later than guest draw submission.

Acceptance: one intended character silhouette on ground and walls, including NPCs/dragons/selected avatars, live toggles,
startup failure and missing scene/light/camera cases, with equivalent DX11/DXR and WebGPU behavior.

### Camera-following circle: offending caster is not yet identified

The first screenshot shows a large round dark patch on the cliff. A camera-relative effect mistakenly admitted as a
ray occluder is a plausible cause, supported by the previous underwater-shell investigation. Both backends use
`surface::cameraShell` in `ray_scene.cpp`, but its heuristic can reject a candidate based on triangle count, distance,
face clearance or bounds. Its existence is not proof this particular circle is a shell. Native shadow projection and
receiver reconstruction must remain alternative explanations until an isolation capture identifies the pass.

Fix direction: capture caster identity, model/resource, transform, material and camera-relative bounds while rotating
the camera in place. Disable native projection and isolate ray-shadow versus AO contributions. If a visual-only shell
is confirmed, prefer an explicit effect/caster role in shared scene export, with the geometric heuristic as a fallback.
Do not exclude ordinary spherical scenery or enclosed rooms just because of their shape.

Acceptance: no camera-following patch in the reported scene, underwater and sky-effect checks, and preserved room,
cliff and ordinary prop shadows on both backends.

### Audio beeping: root cause remains open

The issue reports the tone in recordings, in menus and gameplay, on both renderers, with no benefit from an FPS cap.
This points toward the audio generation/output chain, but a desktop recording includes Windows/device processing too.
It does not prove the tone is present in the SPU2 PCM stream. The log confirms the preferred 48 kHz float WASAPI
AUTOCONVERTPCM path opened, rather than the custom native-format fallback resampler. Do not attribute this report to
that fallback code or the graphics backend without further evidence. The older SQRT.S missing-sounds bug has different
symptoms and is already documented as fixed. Optional absent SSD/lip-sync resources are not evidence of this tone.

Fix direction: collect `DRAKAN_AUDIO_DUMP` plus `DRAKAN_SND_LOG=3` and sound-manager/resource diagnostics for a short
title idle, menu selection and gameplay walk. Compare the internal 48 kHz dump with system capture, then isolate
music, effects, speech, voice cores, streaming input and reverb in diagnostic runs. Map offending key-ons/SSA/loop
addresses and pitch to resource uploads. Check ADPCM data, loop flags, streamed-buffer refill and routing against the
actual trace. If the internal dump is clean, investigate WASAPI/device processing and any second output stream instead.
Change the responsible path only after locating the first corrupt stage; do not mask the tone with a low-pass filter.

Acceptance: the tone disappears from both captures while intended high-frequency audio, menu effects, music, speech,
positional sounds, streaming, pause/resume and device switching remain correct. Add a deterministic regression from
the identified mechanism; keep original sample data untracked.

## Implementation order and release gate

1. Record deployed/release executable identity and run the short baseline scenarios from `dist/DrakanPC`, with isolated
   settings/cards. Keep results separate from the current dirty checkout and old historical harness passes.
2. Fix language initialization first: it has a concrete integration defect and a bounded regression matrix.
3. Diagnose audio next (highest persistent player impact); make a targeted fix once the corrupt stage is identified.
4. Attribute the two shadow symptoms separately, then implement shared caster/replacement policy and backend checks.
5. Run focused HLE, SPU2, scene-policy and relevant runtime tests; run repository checks for changed files. Rebuild
   through `Build-LocalGame.ps1 -Incremental -WorkDir build/final-game` and test the deployed folder on both renderers.
6. Validate the packaged executable and publish only after the three issue areas have explicit pass evidence. A green
   library suite or successful build alone cannot close the perceptual audio/shadow report. No GitHub comment or issue
   state change was made by this review.
