# Interrupted-session recovery and release review (2026-11-03)

Reconstructed from the checkout, Git history, dated journals and private harness output. The original two chat
transcripts were not available in this review; this is a code/evidence handover, not a claim to reproduce their conversations.

## Repository inventory

- The main repository and `editor/packages/formats` each have only their current registered worktree. No second
  registered worktree was found. Main began at `1222f991`, one commit ahead of `origin/main` (`caabe60b`).
- `1222f991` contains the landscape work: objects follow sculpted ground in the same undo step, blended paint preview,
  terrain writer tests, and the in-game UI proof in `../2026-11-02/landscape-mode.md`.
- The formats submodule is pinned at `7472c8af` (already its local/remote main). Its four dirty source files contain
  investigation switches, not a new default conversion fix. The release keeps the committed submodule revision.
- Two files initially appeared modified but had no content diff: `scripts/harness/coop_characters.py` and
  `scripts/repository_guard.py`. Do not invent a change for them.

## Editor / campaign / multiplayer track

Ready changes reviewed for release:

- Copies resolve their source level's exit metadata in Logic. Campaign exit checks keep the disc index and refresh
  the project's overrides after edits instead of losing the result on every autosave/story notification.
- Campaign settings errors disable project export; export failures are not repeated twice. The Campaign and Checks
  panes share their available space, menu changes refresh the campaign note, and a new project starts with an empty
  name field rather than appending to "My mod".
- Custom mode ids cannot collide with another mode or built-in `dm`/`tdm`. Unsupported modes explain their missing
  markers even before being offered. Arena Play hosts the first offered playable custom mode, forwards its id through
  the desktop allow-list, and bypasses the campaign choice card for both instances.
- The server sends its Results end reason as a second optional PhaseChange tail; clients no longer infer "time limit"
  when the final opponent left a last-standing match. The review also clears this cached reason on reconnect and
  adds explicit tests for both older packet lengths.
- Custom-mode lobby limits use score wording and round partial minutes up. The kill feed sits below the status chip.
  The three-player harness and its opt-in positioning/autostart aids are retained.
- Expanded Help guides describe the shipped tools. The one premature instruction to start the unconnected campaign
  wizard was changed to the existing Project Settings > Campaign route; the Markdown guide is regenerated from source.

### Recovered test evidence (earlier session, not rerun here)

Private output under `build/harness/` and `build/scratch/gamemodes2/`:

- `game_mode3_lrs/summary.json`: all checks pass. Both opponents exhaust their lives; 6.3 seconds separate their
  eliminations, with no respawn or early Results between them; all three clients report the last-standing winner.
- `game_mode3_teams/summary.json`: all checks pass. Enemy and friendly-fire kills produce the configured points;
  clients agree on the winning team, and all three inventories receive map plus mode loadouts.
- `game_mode3_weapons/summary.json`: all checks pass. Forbidden hits are rejected, no kills occur, and the final
  opponent leaving produces last-standing results on the remaining client.
- `run_lobby3.log`: the Host page reaches a listening lobby and all three players join. This log alone does not prove
  that team colours, team names or the "You are out" overlay were visually inspected.

### Still outstanding

1. The untracked `campaignwizard.ts`, `campaignwizard.css` and `wizardplan.ts` are a draft, not a shipped feature.
   Nothing imports/installs the wizard. Wire its host to project I/O, export/install and campaign play; connect the
   start page and tour; add planner and interaction tests; then click through with a real disc. Review its "Use a copy
   instead" path carefully: it currently reads an autosave, so unsaved edits in an open level must not be lost.
2. Visually verify the three-player eliminated-player overlay, named/coloured teams, inventory contents and lobby.
   Saved log assertions do not replace these checks. Recheck editor arena Play end-to-end through the desktop UI.
3. Earlier journal leftovers still apply unless superseded: quest start/finish and puzzle chains in game, natural
   dragon mount/damage, quiet-machine three-player co-op/latejoin/chests, map-vote mouse input and chained rematches.
   See `docs/status/handover.md` and the linked individual journals rather than treating all old bullets as undone.

## Order of the Flame / runtime investigation track

Kept local and excluded from this release snapshot:

- `ps2recomp/ps2xRuntime/src/lib/drakan_overrides.cpp`: a new PAL null-controller guard skips two controller routines
  when their controller argument is zero. The crash is evidenced by `build/ootf-run/c7_axe/run/` (the scene walk reaches
  an object without a controller and indirectly calls address zero), and related Tick investigation exists. No
  post-guard regression proof was located. The comment's `ootf-null-controller.md` journal does not exist. Rebuild via
  the standard incremental build, reproduce the problematic weapons/Tick area, verify rendering and interactions,
  and check ordinary Drakan before enabling this workaround by default. Skipping an invalid controller is mitigation,
  not proof that converted weapons are usable or that their creation/lifecycle bug is solved.
- Formats edits in `src/ootfclass.ts`, `ootfmodel.ts`, `ootfpipeline.ts`, `ootftex.ts`: name/template/model, synthetic
  bounds, class-text and alpha-flag experiment switches. Give retained switches deliberate APIs/tests/documentation
  before committing in the formats repo, push that commit first, then update the parent's gitlink.
- `editor/scripts/zz_{flags,models,near,ps2b,ps2weap,tex,tex2}.ts`: hard-coded scratch probes. Preserve for investigation;
  move to ignored scratch or turn into named, parameterised tools before tracking them.
- `analysis/scripts/ootf_cab.py`: retail MDX/InstallShield extraction draft. It is not release-ready: Ruff reports an
  unsorted import and unused table size; the archive output paths also need traversal checks, the MDX-offset sector
  alignment needs review, and malformed-input/synthetic cabinet tests are missing. No disc data should be committed.

The native mip-filter warning in the old tracker was partly superseded by `caabe60b` (filter fidelity and 32-bpp
conversion changes). Do not treat the older experimental note as the latest implementation status; revalidate full
campaign/arena runs against the current conversion before declaring the historical stalls or creature gaps closed.

## Validation and publication

- Editor workspace: `npm test` passed (86 files / 832 tests); `npm run build` passed, with the existing large-chunk
  warning. These ran with the local draft files present; their presence does not make the wizard reachable.
- Desktop: `cargo test --manifest-path editor/packages/desktop/src-tauri/Cargo.toml` passed (12 tests).
- Python: `python scripts/run_tests.py` passed (215 tests, including documentation/package policy checks).
- Runtime libraries: configured and built with the root `runtime-tests` preset; CTest excluding the full `synthetic`
  sweep passed (97 passed / 6 data-dependent skips / 0 failures). This is not an in-game smoke test of the held
  controller guard. The protocol/custom-rules targets were rebuilt again after the review's compatibility test edits.
- Tracked Python Ruff lint/format checks and the selected release C/C++ files' clang-format 19 check passed; the
  repository guard found no prohibited tracked files. Selected editor files were checked with Prettier.
- Full-tree Ruff initially reports only the two issues in the untracked extractor. Exclude that draft from release;
  validate the tracked Python files separately. Do not discard the extractor just to make the working tree clean.

A main push touching `editor/**` refreshes both desktop apps on the latest existing game release, but does not publish
a new builder. The Release workflow must also run on the reviewed main commit with a new `v*` tag (planned `v0.1.1`):
it publishes the setup/builder zip and calls the editor workflow for both apps from the same commit. Confirm both jobs
finish and all six application/builder assets plus both checksum files exist before calling the release complete.
