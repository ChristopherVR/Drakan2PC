# Release notes per component (git-cliff)

There was no `cliff.toml` and no commit convention beyond "imperative subject". Release notes were the static
`scripts/buildtool/kit/RELEASE-NOTES.md`, so a release could not say what changed in the game, the editor or the save
editor.

## Change

- `cliff.toml`: Conventional Commits with a required component scope (`game`, `editor`, `save-editor`, comma-separated
  for several, or `repo` for none). The template prints Game, Editor and Save Editor sections in that order, each with
  New / Fixed / Performance / Reverted groups, or "No changes in this release". Only `v*` tags count as releases
  (`site-media-*` and `editor-b*` are ignored).
- `scripts/commit_policy.py` (tests: `scripts/tests/test_commit_policy.py`): checks type, scopes, capitalized subject,
  no trailing period, 100-character subject, blank line before the body, and no em-dash. Runs from the new
  `.githooks/commit-msg` hook and from `.github/workflows/commit-policy.yml` (every push to main and every PR checks
  all commits after `POLICY_BASE`, PRs also their own range, and the job summary previews the next notes).
- `release.yml` writes `build/release-notes.md` (git-cliff section plus the standing kit notes) and uses it to create
  the release, or to refresh the notes of an existing one.

## Legacy commits

`POLICY_BASE` is `8ae96142`, the last free-form commit. Older subjects are filed by keyword: investigation records
(Record, Document, Audit, Verify, ...) are skipped, "save editor / save item / item catalog ..." go to Save Editor,
"editor / exporter / campaign wizard" to Editor, everything else to Game. Only v0.1.3 (range `v0.1.2..`) contains such
commits; its Game section mixes harness and save editor items, so edit those notes by hand once after publishing.
