# Issue 1: synchronize PC and guest language initialization

Source fix for [issue #1](https://github.com/ChristopherVR/Drakan2PC/issues/1).

The supplied log selected the German HLE root while the guest opened English archives. Inspection of local generated
SelectLanguage and sceScfGetLanguage confirms that the guest consults GetOsdConfigParam before constructing explicit
language paths. The local PAL ELF jump table maps console values 1 English, 2 French, 3 Spanish, 4 German, 5 Italian
into Drakan's language order. Existing OSD defaults always returned English. No guest initialization hook or IsoFs
explicit-path rewrite is required.

Added a host-facing syscall configuration API that initializes both OSD language/version fields before guest startup,
while preserving other OSD configuration. The boot path normalizes the PC setting once, respects DRAKAN_LANG including
explicit empty/invalid values, constrains it to the disc profile and falls back to English for an absent selected
DRAKAN2.RRC. Native added menu labels and automatic narration language consume the effective selection until restart.
The ISO metadata check confirms DRAKAN2.RRC exists under all five PAL language roots. Explicit language paths and mod
lookup rules are unchanged.

Validation so far:

- CMake runtime-tests configuration succeeded.
- Focused language_policy CTest passed (all five PAL mappings, USA English restriction, environment precedence,
  case/slash normalization, empty/invalid values, effective language publication).
- Added kernel regression through actual GetOsdConfigParam/GetOsdConfigParam2 calls for every language, English reset,
  and preservation of unrelated fields. The existing ps2x-tests target began rebuilding unrelated VU/AOT units;
  that build was stopped to prioritize the shared game build. This added kernel regression remains unrun.
  Deployed boot validation is recorded below.

A missing selected archive fallback checks the boot resource archive, not completeness of every speech/file tree.
German text and English boot regression now pass below. Perceptual confirmation of a German spoken line and save/load
smoke checks remain outstanding.

## Deployed evidence

Ran the shared incremental build from `dist/DrakanPC`, executable SHA256
`19EBEDA6A3E876836E38D2455B90CE59F0C0DFAE096CEDF24E4EAA56194CE31E`. Each successful proof run used
absolute isolated log/card/native-options/recipe paths under `build/issue-review/language/`, process-local DRAKAN_LANG,
DX11, and the existing harness. No production source changed during these runs.

- `boot-german-native`: console language 4, resource root GERMAN; actual guest opens GERMAN/DRAKAN2.RRC,
  GERMAN/COMMON/INTERFCE resources and GERMAN music. Inspected `language-title.png`: **Spiel laden, Neues Spiel,
  Optionen, Mitwirkende, Verlassen**. Harness reports script quit, exit 0 (31 seconds).
- `boot-english-native`: console language 1, resource root ENGLISH; actual guest opens English archives again.
  Inspected `language-title.png`: **Load Game, New Game, Options, Credits, Quit**. Script quit, exit 0 (31 seconds).
- `german-intro-proof`: entered New Game from the original title, reached intro screen 29 and captured the book movie.
  The guest opened **GERMAN/LEVELS/LEVEL0RO/LEVEL0RO.SDB** (3,542,870 bytes), and the narration callback selected
  `intro_german.srt`. That optional external subtitle file is absent, so no narration subtitles display. Captured
  34.255 seconds of internal 48 kHz stereo PCM16 in ignored `german-intro.wav`; clean script quit, exit 0 (40 seconds).
  This establishes the German intro resource and callback path. No listening/transcription assessment was performed,
  so it does not claim perceptual verification of a specific German sentence.
- Existing `drakan-character-motion` mod remained enabled: the German run rebuilt **GERMAN/COMMON/NPC/NPCS.ADB**
  with its 6,132 patches. This verifies language-relative mod targeting at boot; gameplay/mod behavior remains untested.

### Separate modern frontend limitation

The modern native PC title menu is hardcoded English in `runtime/gameui/src/TitleScreen.cpp`, independently of the
localized guest menu and selected resources. The `boot-german-proof` screenshot confirms it stays English while
German resources load. Broad modern frontend translation is outside this bounded guest-selection fix. The added
legacy Quit/Back/Next labels and automatic narration selection follow the effective startup language.

### Harness correction

The first `boot-german` attempt passed a relative output root to play_run.py. Its recipe became unavailable after
switching cwd to dist; it still proved German resource opens. Closed only that run's own child process through its
window close request (exit 0), then repeated with absolute paths for every successful proof above. Its isolated card
was generated under `dist/DrakanPC/build/issue-review/language/boot-german`; automatic command review rejected recursive
cleanup with `blocked by policy`, so it was left for the normal build's dist cleanup. Player cards/options were untouched.
