# One release stream: editor builds join Drakan PC

Audit of the release workflows and the change that removes the separate editor release.

## Findings

- `release.yml` (tag `v*`) built the installer and zip kit only. `editor-release.yml` created a new pre-release
  `editor-b<run>` on every push to main that touched `editor/**` (b1 to b4 so far), so editor downloads lived in a
  second, ever-growing list and the Drakan PC release had no editor.
- `editor/packages/formats` is now a submodule of `ChristopherVR/drakan-formats` (public). The editor workflow already
  used `submodules: true`; the version stamp used `github.run_number`, which restarts per workflow and is not tied to
  the commit. The game builder kit uses `git archive` of the superproject (a gitlink becomes an empty directory), so
  `release.yml`, `build-tools.yml` and `pages.yml` do not need the submodule (the repository guard skips gitlinks).
- No code in `editor/` or the runtime checks GitHub releases (no `editor-b`, `releases/latest` or API calls), so no app
  update logic needed changing. README and install guide link only to `releases/latest`, which stays a game release
  because each game release is marked Latest and the editor never creates releases.

## Change

- `editor-release.yml` ("Editor assets") now only builds and uploads. Triggers: push to main touching `editor/**`,
  manual run (optional tag), and `workflow_call` from `release.yml`.
- Target release: the given tag, else the newest published `v*` release by creation date (ignores `site-media-*`,
  `editor-b*`). No `v*` release: the build and tests run, nothing is published (warning).
- Fixed asset names (`DrakanEditor-Setup.exe`, `DrakanEditor-Portable.zip`, `DrakanSaveEditor-Setup.exe`,
  `DrakanSaveEditor-Portable.zip`, `EDITOR-SHA256SUMS.txt`) uploaded with `--clobber`; any other `Drakan(Save)Editor-*`
  asset is deleted so nothing stale stays. A separate checksum file keeps `SHA256SUMS.txt` of the game files untouched.
- Version is `0.1.<commit count>` (monotonic, same for a given commit, `fetch-depth: 0`).
- `release.yml` gained an `editor` job (`needs: installer`) that calls the workflow with the release tag, so a new game
  release carries editor builds of the same commit. A failed editor job leaves the game release published.
- Docs: `BUILDING.md` "Releases" (procedure), README, `editor/README.md`, handover, `github-pages.md`, release notes.

## Legacy

Releases and tags `editor-b1` to `editor-b4` are legacy and no longer created. They can be deleted
(`gh release delete editor-bN --cleanup-tag --yes`) once the editor assets are on the newest `v*` release.
