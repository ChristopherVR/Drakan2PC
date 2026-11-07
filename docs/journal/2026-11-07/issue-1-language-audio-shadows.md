# Issue 1 and 3: language stutter, audio, shadows, CPU affinity, German OotF

Private runs: `build/iss1/play` (copy of `dist/DrakanPC`, then of the new `build/final-game/bin/drakan.exe`),
`build/harness/iss1-*`. Original PAL disc for every run. `dist/DrakanPC` was not touched.

## Contamination note

Between four and six other `drakan.exe` processes ran during every measured run (`tasklist` before/after each run,
`build/iss1/*.procs`). Frame-time numbers below are therefore only good for "is there a large language effect", not for
absolute FPS.

## (a) Language

Selection: with `DRAKAN_LANG=german` the log shows `language root \GERMAN` and guest opens of
`GERMAN\COMMON\INTERFCE\...`; `ootf-levels` pack files are mounted into both the ENGLISH and GERMAN namespaces
(`mods: ootf-levels: added GERMA...` and `added ENGLI...`), so Drakan 1 pack levels follow the selected language. This
confirms the earlier fix (`issue-1-language.md`, `issue-1-multiplayer-language.md`); no new defect found.

Stutter with the reporter's workaround. New tool `scripts/harness/swap_language_iso.py` makes a consistent private image
whose ENGLISH root record points at the GERMAN extent (no size or sector mismatch, which a hand-edited image could have).
Runs with audio, HD textures and auto upscaling (the default play folder), 30 s Surdana walk, `DRAKAN_FRAME_AUDIT=2`:

| Batch | Runs | Result |
| --- | --- | --- |
| 1 | original-English, swapped-English, original-German, swapped-English, original-English | German-content runs looked worse (p95 present gap 66-71 ms vs 19 ms) |
| 2 | German and English, HD textures and upscaling on and off, order mixed and repeated | p95 24-31 ms in all cells; the worst cell was English with HD off (87 ms); present gaps over 50 ms: 24-52 in German cells, 33-38 in English cells apart from that outlier |

Batch 1 did not replicate in batch 2, where the spread within one configuration (GS worker time spent in >8 ms
commands: 9.6 s to 21.2 s for English) is as large as the spread between languages. No `short read` or `ST_IO` line in
any run. Conclusion: no language-keyed slow path reproduced; the stutter is not caused by anything in the runtime that
looks at the language folder (HD pack lookup is by content hash, resource overrides by file/entry id, IsoFs reads by LBA).
The earlier stationary EN/DE/DE/EN comparison (`issue-1-german-stuttering.md`) agrees. Not reproducible without a clean
machine and the reporter's image; the remaining suspects are the reporter's hand-edited image (extents or sizes
changed, see that note) and machine load. No speculative runtime change was made.

## (b) Audio and ray-traced shadows

Audio: new `scripts/harness/audio_tone_check.py` (narrow sustained peaks, test `scripts/tests/test_audio_tone_check.py`).
It reports the 11854 Hz parked-voice tone in the pre-fix capture `build/issue-review/audio/baseline.wav` (matching the
earlier diagnosis) and finds nothing above 3 kHz in a 104 s capture of the new build (`DRAKAN_AUDIO_DUMP`, title, intro
and a walk in Surdana; the first 42 s are digital silence while the large `ootf-levels` pack mounts). Not reproducible
at HEAD; hardware-specific (AE-7) output is the only thing left to confirm by the reporter.

Shadows: `shadow_review.py` gained `--exe`. Cases `both` and `native-off`, scenes `surdana` and `surdana-cliff`, German,
D3D11, ray shadows and reflections on. Screenshots inspected (`build/harness/iss1-shadow/*`): one character shadow on the
ground, no second silhouette, no round shadow following the camera at the cliff pose; native-off shows no shadow, as
expected. Evidence flag false for one run (surdana `both`) because a receipt was missing; its images show the same.
Not reproducible at HEAD. WebGPU was not re-run.

## (c) CPU affinity at run time

The build-time `-CpuAffinity` already existed. Added the run-time counterpart: `[general] cpu_affinity`
(`DRAKAN_CPU_AFFINITY`): `auto`, `cores:<n>` or a hex mask such as `0xFFF`. Code in `runtime/input` (`CpuAffinity`),
applied once in `PS2Runtime::run`; mask is intersected with the allowed set, a mask with no usable processor is ignored,
bad text gets an actionable log line. Settings: Gameplay > Performance > "CPU cores" (restart), custom ini masks show as
"Custom (0x...)". Tests: `cpu_affinity_test` (parser, resolution, live `cores:1`), settings store and schema tests. Live:
`DRAKAN_CPU_AFFINITY=0xF` logs `running on 4 logical processors (mask 0xF)` in a real run. Docs: `docs/guides/settings.md`,
`env-flags.md`. Fixed on the way: a committed `settings_test.cpp` literal with raw newlines that did not compile.

## (d) German OotF install (issue 3)

No real install exists. Synthetic coverage added in the `formats` submodule: an XOR-coded German Dragon.rrc read through
`parseRrcStrings` (language detection, Windows-1252 0x80..0x9f characters), localized titles becoming runtime-valid ASCII
for every Latin-1 character, French/Italian/Spanish detection, and a pipeline guard: if no selected level can be read the
conversion now stops with the reader's reasons and tells the user to pick the installed game's folder (before it returned
an empty pack). The editor importer shows the detected language as soon as a folder is chosen and explains an empty
folder (`languageNote`); it uses the same `detectOotfLanguage` as the converter.

## Remains

Reporter's modified image and a clean-machine stutter capture; real German OotF install; WebGPU shadow re-run.
