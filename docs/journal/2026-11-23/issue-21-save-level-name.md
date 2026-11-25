# Issue #21: overlapping level-name copies during restore

Issue: <https://github.com/ChristopherVR/Drakan2PC/issues/21>.

The Linux PAL report has a missing indirect call target at `0x2bf37c` during save restore. Before that failure,
the texture-budget log names the level `Litor's Lairnquisitor's Lair`, rather than Inquisitor's Lair. The corruption
is significant: a normal level transition succeeds, while loading the saved level fails. A missing-target guard
would conceal the earlier failure rather than repair it.

An uncommitted fix to the guest `strcpy` stub was present at the beginning of this implementation. It replaces
the host `strcpy` with a bounded NUL scan and `memmove`, including the terminator. Retained that fix and documented
why the length must be measured before copying: overlapping writes can change the source. The guest's path
handling can copy within a buffer, whereas host `strcpy` makes overlap undefined. The PAL configuration maps
`0x4cecf8` to this stub, and inspection of the private generated level loader confirms calls to it.

Added `guest_strcpy` to the runtime policy tests. CMake extracts the current production function and reconfigures
when LibC.cpp changes, so the test does not duplicate the implementation. The fixture supplies only register,
pointer-resolution and write-trace doubles. It does not exercise full guest address aliasing or game loading.
It sweeps 32 destination alignments and prefix lengths 0 through 32 for a synthetic level name, checks untouched
bytes outside the write, the destination return register and the complete write receipt. Separate cases cover
non-overlapping copies, empty strings, exact-fit source/destination boundaries, invalid addresses, unterminated
input and insufficient destination space, with host canaries beyond the guest window.

Validation:

- Compiled and ran the test with MSVC x64 in optimized C++20 mode against the corrected production function: pass.
- Recompiled the same test against HEAD's original stub: fails the write-receipt assertion during the overlap
  sweep. Restored the generated fixture to the corrected implementation afterward.
- All six `test_runtime_compile_set.py` policy checks pass. Repository source audit reports no prohibited tracked
  files; the regression source passes clang-format and the patch passes `git diff --check`.
- Full incremental build attempted through `Build-LocalGame.ps1`, using the existing `build/final-game` directory.
  MSBuild fails while querying the Windows SDK registry (`MSB4184`, requested registry access is not allowed).
  The runtime-tests CMake preset hits the same restriction in its dependency subbuild.
- Downloading the issue attachment fails with DNS unavailable in this execution environment. The reporter's
  exact save has therefore not been replayed, and the null callback's disappearance has not been verified.

Existing private `issue21-*baseline*` harness outputs cannot count as successful restore evidence: inspection
shows runs that reach a menu or start a new level without actually restoring a valid manual slot. A clean script
exit alone is insufficient. A follow-up on a build-capable host should load the reporter's unmodified card via
Continue and Load Game, verify the intact level name and gameplay, then save and reload within the session.

No save payload, disc data or generated game code is added to the repository.

## Follow-up: Windows gameplay verification

Published the fix as `100647b9e0c7f08e21d52283da8958d9da9ead2c`. GitHub Desktop completed the push, and the
GitHub connector independently returned that SHA for main. The local Git shell hook could not start, so its commit
policy and source audit were run directly before the local commit. Native Git networking was unavailable.

The full incremental build and normal deployment subsequently succeeded. The SDK and compiler were supplied as
explicit environment paths with MSBuild's `DisableRegistryUse=true`, and the build used `-Jobs 1 -CompilerProcesses 1`
with `MSBUILDDISABLENODEREUSE=1`. The earlier parallel build stalled at Checking File Globs; the same glob check ran
successfully on its own. No build scripts or installed SDK files were changed. The deployed executable's SHA-256 is
`235cbfae261069cfeac64b00f33777d11b61b3b506de014cfac1d4a6af69fec9`.

Successful private replays from `dist/DrakanPC`:

- `build/harness/issue21-fixed-manual-final`: New Game, then Surdana's native Inquisitor exit through the existing
  `DRAKAN_LEVELCHANGE=inquisitor@1200` test aid. The native manual Save Game screen wrote Slot1's Lair `.rsg`, its
  Surdana `.rlt` companion and the normal icons/preview. `afterSaveWrite` reports that actual Slot1 path, and the
  captured screen says the save succeeded. The recipe uses the native pause menu's initial Save selection.
- `build/harness/issue21-fixed-continue-mouse`: a fresh process, a copy of that card, no start-level override.
  The modern title's Continue action selects Slot1. `beforeLevelLoad` and `afterLevelLoad` both identify the full
  `Level 1E - Inquisitor's Lair` name with `fromSave=1`; `afterSaveRestore` completes, the screen returns to
  `gameplay=1`, a subsequent forward-input sequence and screenshots execute, and the script quits cleanly.
- `build/harness/issue21-fixed-load-menu`: another fresh process and copied card, no start-level override.
  The title's Load Game action opens the native load screen; selecting Slot1 completes the same restore and returns
  to gameplay. The script sends forward input, captures the result and quits cleanly. Neither cold restore logs a
  missing branch target or fatal guest unwind.
- `build/harness/issue21-fixed-quicksave`: supplementary in-session F5/F8 smoke test with the file-layer level
  redirect. It returns to the Lair, but its saved filename remains Surdana because of that redirect, so it is not
  the evidence for the manual Lair save case above.

The full Lair name in the cold level-load events is distinct from the native 24-character saved filename/caption
`Level 1E - Inquisitor's L`, which is expected and unchanged. Early discarded attempts included invalid pad-button
tokens, an exit fired before the script's gameplay synchronization, and title actions aimed at the hovered row
instead of Continue. They do not count as successful checks. Final title recipes explicitly position the cursor
over Continue or Load Game before clicking, and the logs confirm the chosen action.

These are Windows PAL checks with newly generated private saves. The reporter's attachment was not replayed and
Linux/Steam Deck remains untested locally; the host-overlap regression covers the portable implementation directly.

## Docker Linux comparison

Replayed `d378ca10f707c4259449691e65acf44456d29aeb` and
`100647b9e0c7f08e21d52283da8958d9da9ead2c` in Ubuntu 24.04.5 Docker, glibc 2.39,
Clang 18.1.3, Xvfb and WebGPU/Vulkan on Mesa llvmpipe. Both builds used the standard
ISO builder and its `build/final-game` work directory, with the pinned formats submodule
`3ab82d367d3db2585c83d3dfe158298565777f23`. Runs launched from `dist/DrakanPC-linux`.

All four final runs used fresh copies of `build/harness/issue21-fixed-manual-final/card/mc0`.
SHA-256 checks matched all twelve fixture files in each run and in the source afterward.
Used `issue21-cold-continue-mouse.txt` and `issue21-cold-load-menu.txt`, adjusting only
cursor Y positions for the Linux menu layout (165 to 182, 202 to 218). The test window
was 640 x 360, internal scale 1, HD textures off and audio off. Logs confirm the intended
Continue and LoadGame actions. Initial menu-coordinate attempts were discarded.

| Build | Continue | Load Game |
| --- | --- | --- |
| `d378ca10` | Restore completed, gameplay and movement, exit 0 | Restore completed, gameplay and movement, exit 0 |
| `100647b9` | Restore completed, gameplay and movement, exit 0 | Restore completed, gameplay and movement, exit 0 |

Every final log retains the full Lair level name, reports `afterSaveRestore` and
`gameplay=1`, and reaches script quit. Position logs show translation after W input,
and captures show Rynn in the cave with the HUD. None logs a missing branch target or
fatal unwind. The original crash therefore did not reproduce on this Docker setup,
including the pre-fix build. This comparison verifies both builds load this manual
save here. Steam Deck hardware and the reporter's attached save remain untested.

Private evidence is under `build/harness/issue21-linux-docker`, with final run folders
`d378ca10-{continue,load-game}` and `100647b9-{continue,load-game}`. Executable SHA-256 values
are `ccab8c863a49005fe40a478cd6d58560865ecd6c806c8c1810b3d4616340c23f` for pre-fix and
`fb4daaf5c8eaecceff77ec636502f713d3fb6f6b55ea8d238904ed2ea23a2abe` for patched.
