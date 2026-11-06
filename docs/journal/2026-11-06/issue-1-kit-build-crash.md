# Issue 1: builder kit crash in "Setting up the play folder"

Source: [reporter comment](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6676435683) with
`build-log.txt` from the bundled builder kit (`Sources: bundled`, Python `tools\python\python.exe`).

## Cause

Step 6 runs `analysis/scripts/character_previews.py`, which imports sibling modules (`coop_character_roster`,
`lvl_dump`, ...). The kit ships the CPython embeddable distribution, whose `._pth` file keeps the script's own folder
off `sys.path`. A normal Python adds it, so developer builds never saw the failure. `character_motion.py`, run right
after it, already inserts its folder into `sys.path`; `character_previews.py` did not. The file itself is in the kit.

`python -I` reproduces the kit behaviour. Run against every script the two builders call, only
`character_previews.py` failed.

## Changes

- `character_previews.py` inserts its own folder into `sys.path`, as `character_motion.py` does.
- New `scripts/tests/test_build_script_imports.py` runs every `.py` named in `Build-LocalGame.ps1` and
  `Build-Drakan.ps1` with `-I --help` and fails on an import error. It fails without the fix.
- `Build-Drakan.ps1` retried every failure with less parallel compiling, so this script error cost the reporter a
  second full pass. `Build-LocalGame.ps1` now publishes its current step in `$global:DrakanBuildStep`, and the retry
  only runs when the failure happened while compiling.

## Not verified

No kit was rebuilt and run end to end with the embeddable Python; `-I` stands in for it. The reporter's compiler
parallelism and memory observations are a separate investigation.

## Follow-up

Verified with the real embeddable Python in a staged kit; the next step (`character_motion.py`) still failed in the
kit (no formats submodule, no Node.js). See [issue-1-kit-build-parallelism.md](issue-1-kit-build-parallelism.md).
