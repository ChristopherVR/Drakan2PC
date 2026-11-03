# Recovery follow-through: campaign wizard and OotF tools

Follow-up to `session-recovery.md`. The earlier release workflow succeeded: `v0.1.1` contains all six setup/portable/
builder assets and both checksum files. That release is the earlier `fb75c94` snapshot, not the changes described below.

## Completed implementation

- Installed the campaign wizard in the editor. Start page > Make a campaign and Project Settings > Campaign both
  open it; the Build a campaign guided tour now targets its six steps. Host bindings use normal project creation,
  live project documents, export/install and desktop play APIs. Campaign play requests the title choice on a fresh
  test card instead of single-level auto-start. Browser zip exports count as exported even though they have no native
  save-dialog path.
- Copying a disc level flushes and clones the live document, preserves map layout and both start settings, checks
  autosave success, and reopens an open level under the copy's title. The source autosave is retained as recovery data.
- Exit linking uses resource ids (not potentially duplicate class names). Failed checks clear stale results and block
  shipping; exports recheck before building. Disc changes invalidate caches; project edits invalidate readiness and
  old export success. Keyboard focus is contained while the wizard is open and returns to the previous control.
  Re-linking can recreate a destination whose story string was not saved before a browser reload; it no longer
  silently skips an orphaned autosave reference. Save the project file to preserve its story edits between sessions.
- Eight planner regression tests cover routing, automatic forward/back links, manual/unknown routes, cleared
  destinations, safe names, one-click fixes, copy metadata and unchecked readiness. The workspace's 844 tests passed;
  typecheck/production build passed (existing bundle-size warning remains). An additional texture test verifies that
  suppressing the alpha header flag does not change any pixel bytes.
- Formats diagnostics now share validated environment parsing, default off, with tests and README documentation.
  Malformed/duplicate mappings and invalid model regexes fail clearly; boolean switches accept `0` as disabled.
- Finished the cabinet extractor's bounds checks, MDX-relative sector-padding alignment, exact decompressed-length
  checks and Windows path traversal/drive/stream/reserved-name rejection (including symlink escape checks). Five
  synthetic tests cover extraction, malformed input, decompression length and nonzero ISO-volume offsets. The Python
  suite passed all 220 tests at this point. Retail extraction is not claimed validated by the synthetic fixtures.
- Preserved the seven hard-coded `zz_*.ts` probes under ignored `build/scratch/recovery-probes/`; they are not product
  tools and are not shipped or deleted. The source-controlled directory no longer carries unnamed investigation probes.

## Real-disc interaction check

Private browser session on the local Vite app, using the player's PAL ISO: selected Make a campaign, entered a title,
opened the disc, added Surdana and Kragmor copies, used Link levels in order with the return route enabled, reached
Check with no errors (seven explicit warnings for exits leaving the campaign), and built a pack with both copies,
one exit edit per copy, generated cover and two story-string edits. Screenshots: `build/recovery-wizard-{name,check}.png`.
The first automation download was cancelled by the browser. A later intercepted browser download confirmed a
3,001,579-byte zip with the correct ZIP signature and a successful export banner. Desktop title-card Play still needs
a native click-through; browser pack generation is not proof that the native launch dialog has been clicked.

The exact captured zip was then run through `play_run.py` from `dist/DrakanPC` using an isolated mods folder and card
(`build/harness/recovery-wizard-campaign/`). The game registered The Recovery Valley, New Game opened Surdana Copy,
saves were redirected to `mc0_the-recovery-valley`, and the run completed a clean scripted quit in 20 seconds with
screenshots at 120 and 300 gameplay boundaries. No claim is made here that the player walked the new inter-level exit.
The standard incremental local game build also passed after changing the controller mitigation to opt-in.

## Not resolved / safety decision

The null-controller guard is **not** a complete fix. A real converted-axe regression fires both guards and then hits
another unchecked controller vtable call at `0x140eb0`; the Tick-area run also fails to finish. The diagnostic is now
opt-in, leaving normal gameplay unchanged. Details and the next lifecycle investigation are in
`ootf-null-controller.md`. Do not label these objects playable or turn this guard on by default.

New frame-cadence, jump-harness and multiplayer-HUD changes appeared concurrently in the shared checkout during this
work. They belong to another active session and are not included in these commits. Original no-content-diff Python
status entries were not overwritten to make Git look clean.
