# Issue 1 follow-up: German data replacement and stuttering

The follow-up reports that replacing English files inside the PS2 ISO with German files makes the game German,
but causes extreme stuttering. This report alone does not establish whether the stalls come from the edited image,
a game/runtime bug, or a separate rendering/audio performance issue.

## Existing language fix

The original v0.1.2 symptom (German in PC settings/log, explicit ENGLISH guest opens) is already addressed in the
current source. `drakan_overrides.cpp` selects the effective language, checks its `DRAKAN2.RRC`, initializes both
OSD syscall views through `configureOsdLanguage`, publishes the effective menu language, and sets the HLE root
before guest startup. PAL German maps to OSD value 4. Explicit paths remain explicit in `IsoFs`; redirecting every
English path to German would hide intentional cross-language access and is not a necessary second fix.

The previous deployed German/English title and German intro receipts are in
`../2026-11-04/issue-1-language.md`; the later modern-menu localization supersedes that entry's early English-only
modern-title limitation. Those receipts establish correct selection on the tested original image, not a measured
absence of gameplay stutter. No new executable or new performance run was produced for this investigation.

## Why replacing ISO files is an uncontrolled workaround

`runtime/hle/src/IsoFs.cpp` obtains each file's LBA and byte length from ISO9660 directory records. Reads use
`LBA * 2048 + offset` and clamp to that recorded byte length. An in-place substitution that changes file lengths or
moves extents without updating the directory can therefore truncate reads or address unrelated data. This is a
conditional failure mechanism supported by the code, not evidence that the reporter's editor did so. A correctly
rebuilt ISO with consistent extents is not inherently invalid merely because its files were replaced.

Inside resource archives, `FileService.cpp::parseSrsc` loads the index offset/count and individual resource offsets
and sizes from the selected archive itself. `ResourceService.cpp` uses those offsets and reports short reads as
`ST_IO`. Substituting payload bytes while retaining another archive's index can likewise create invalid resource
reads. Whole, internally consistent archive replacements do not automatically have this problem. The language
trees contain localized speech and other differences; copying a subset also stops the image being the original
five-tree configuration against which language selection was verified.

The startup archive-existence check is deliberately not an integrity or completeness check. The boot-ELF-name
region check also does not authenticate replaced resources. Neither should be represented as validating a modified
ISO or diagnosing its performance. There is no evidence here that ISO damage causes repeated retries or that such
retries explain this report, so no retry/performance patch was added.

## Resolution and remaining evidence

Use the original, unmodified PAL image with a build containing the existing language fix; select `german` in
`[general] language`, remove any conflicting `DRAKAN_LANG`, and restart. Expected startup evidence is console
language 4/resource root GERMAN and actual GERMAN archive opens. This removes the need for the replacement workaround.
The current checkout cannot establish whether an already distributed executable includes the fix from its version
label alone; compare its source/build identity and startup receipts.

Extreme stuttering remains unverified. A useful next comparison is the same scene/save and settings on the
unmodified image in German and English, followed by the edited image only if needed, recording frame timing and
resource/short-read logs. The reporter's edited image, replacement procedure and a matched stutter capture were
not available in this investigation. No unsupported code change was made.

Validation: existing Release `language_policy` CTest passed (1/1) on 2026-11-05 using the VS-bundled CTest.
This reuses the existing test executable without rebuilding; it checks language selection policy, including PAL
German, and does not exercise ISO integrity, guest startup or stutter. The initial invocation without `-C Release`
could not select the multi-configuration test; the corrected invocation passed. No full game/shared test build ran.

## Controlled comparison preparation (2026-11-05)

Read-only audit covered 152 available `.log` files under `build/issue-review` and
`build/harness/issue1*`: zero `short read` or `ST_IO` diagnostic lines. The audit
found GERMAN language-root receipts in 53 files and ENGLISH in 64 (these counts
include related captures and duplicate stdout/runtime logs, not independent trials).
None contained `[ft] P` host-present timestamps. This does not prove the absence
of I/O errors or gameplay stalls; it cannot produce a matched p95 comparison.
Machine-readable audit: `build/issue-review/german-stutter-log-audit.json`.

Prepared `scripts/harness/language_perf.py`, reusing the maintained shadow-review
isolated settings/environment and deployed executable helpers. Dry-run is the
default. An exclusive live slot may run:

```powershell
python scripts/harness/language_perf.py --name issue1-language-perf-20261105 --run
```

The bounded sequence is English/German/German/English, each fresh New Game in
Surdana, stationary for 45 gameplay seconds with audio on. It retains the
shadow-review isolation of mods, overrides, HD textures, movies and neural
upscaling, so it is a base-game comparison rather than the unknown reporter
configuration. Each run has fresh cards/settings, effective-language root and
successful archive-open receipts, clean-quit and deployed-executable SHA-256
checks. Final 20-second host-present intervals provide nearest-rank p95, maximum
and counts above 50/100 ms; they are not unique world-frame timing.

No live run or game build occurred at this checkpoint. Concurrent performance
work owns the shared deploy/GPU, so measurements require an exclusive slot and
a stable canonical deploy first. Before execution, verify the deployed INI points
to the user-supplied original PAL ISO. The script cannot authenticate that image
or reproduce the unavailable modified image. No speculative runtime fix was added.

Validation: Ruff check/format and the dry-run manifest generation pass.

## Matched original-disc native comparison completed

`build/harness/issue1-language-perf-20261105` completed English/German/German/English
on unchanged canonical SHA256 `4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278`.
All four runs exited 0 through native scripted quit, prepared all twelve native
options and opened fifteen archives from the requested language. Audio was on;
no other game/build/test workload from the coordinated chats ran during the sequence.

| Run | Final-20-second host-present samples | p95 ms | Maximum ms | Gaps >50 ms |
| --- | ---: | ---: | ---: | ---: |
| English 1 | 2598 | 10.597 | 17.201 | 0 |
| German 1 | 3296 | 7.110 | 16.916 | 0 |
| German 2 | 3256 | 7.462 | 13.801 | 0 |
| English 2 | 3127 | 8.659 | 21.962 | 0 |

No short-read or ST_IO diagnostic appeared. This stationary original-disc case
does not reproduce a German-specific presentation regression. These are host
presentation intervals, not unique world simulation FPS, and do not certify the
reporter's modified image or unknown gameplay scene. The complete-options seed
was added before this run after the arena fixture exposed partial native options
as invalid startup preparation. No speculative German runtime fix was introduced.
The exclusive window was released to the performance chat immediately afterward.
