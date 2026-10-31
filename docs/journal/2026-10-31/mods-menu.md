# Mods page in the PC settings (2026-10-31)

Reference text: [settings-menu.md](../../reference/settings-menu.md#mods-page-2026-10-31),
[modding.md](../../reference/modding.md#the-mods-page-in-game).

## What was built

- `runtime/settings/src/ModsModel.cpp` (std only, in `drakan_settings_core`): list of packs in load order, enable / disable,
  move to position, `restartRequired()` (enabled packs in order differ from what the game loaded), state text,
  per-pack conflict summaries, `mods.ini` rewrite (keeps comments and the `[mods]` header, atomic temp file + rename).
- `ModsPage.cpp` (schema lib): the schema page `mods`, built only when `SchemaEnv::mods` is set. No new row kind:
  Enabled = toggle, Load position = choice 1..N, status / author / target disc / about / conflicts = info rows, the actions
  Open mods folder and Restart game now = single-entry choice rows. The restart rows are always present (greyed out
  until the list changed) so the focus does not jump when the first change shifts the rows.
- Both UIs get it from the schema: the F1 overlay (`Page::Mods`, sidebar entry, Pg Up / Pg Dn) and the modern Options
  screen (a tab). `ModernSettings::apply` and `SettingsMenu::drawSchemaRow` route `mods.*` keys to `ModsModel::apply`
  before the `Settings` path; a change is saved at once, independent of the overlay's Apply button.
- `ISettingsHost::mods()` returns the model; `drakan_settings_host.cpp` fills it from `hle::mods::ModManager`
  (`setModManager`, called from `drakan_overrides.cpp` even when `mods/` does not exist, so the page shows "No mods
  installed" and Open mods folder creates it). Open folder: `explorer.exe` on Windows (CreateProcess, no Win32 in public
  headers), `xdg-open` on Linux, disabled in the browser build.
- Harness ops in `DRAKAN_OVERLAY_SHOT_DIR/command.txt`: `mods` (select the page), `mod <row key> <value>`.
- Tests: `runtime/settings/tests/mods_model_test.cpp` (toggle, reorder, restart detection, conflicts, ini write with an
  existing file and CRLF, folder action, schema rows), in the `runtime-tests` ctest run.

## Live reload at the title screen: no

Overrides are added to the resource service once, before the first resource request (`ModManager::install`), and the
service has no way to remove one. Beyond that the guest caches what it already loaded: INTERFCE.TXD textures are uploaded
to the GS when the interface loads, XDB text tables are parsed once, the host UI (title, pause, options) keeps its own
decoded copy of the disc art. Even at the title screen a toggle would leave a mix of old and new data. A safe version
would need a swappable override table and a guest-side flush of the interface resources; not attempted. The page says
"restart required" instead: info row, amber `*` on the changed rows, a status line after each change and the
Restart game now action (`relaunchSelf`, the same path as the other restart prompts).

## Verification (private folder `build/mods-menu-play`, nothing in the repo)

Packs: `modtest_a` (folder; INTERFCE.TXD palettes turned magenta, XDB entry), `modtest_b.zip` (conflicting XDB 118 entry),
`modtest_c` (folder; needs `modtest.core`, not installed), `modtest_usa.zip` (disc profile usa). Scratch drivers
`drive.py` / `drive2.py` and recipes `r*.txt` are in that folder.

- `runs/r1/mods1.png`, `mods2.png`: F1 overlay on the Mods page. Initial list with statuses and "Overridden by 'Test mod B'";
  after turning A off: A reads "Disabled, unloads after restart", the Restart rows appear.
- `runs/r2` (restart): log `[mods] off: Test mod A 1.0 - disabled in mods.ini`, one pack active, C skipped (missing
  dependency), USA pack skipped (wrong disc); `mods.ini` rewritten as `modtest_a = off`, others unchanged. The page now
  shows A "Disabled", no conflict line, no restart notice (`mods3.png`).
- Effect: with A on the title art and credits are magenta (`runs/r4/u1_s.png`, `runs/r5`), with A off they are normal
  (`runs/r1/mods1.png` background, `runs/r3/t4`).
- Reorder: `mod mods.order.modtest_b 1` wrote `modtest_b, modtest_a, ...` to `mods.ini`; after a restart the log lists B
  then A and the conflict flips: `'modtest.a' overrides 'modtest.b'` (`runs/r8`), before it was B overriding A.
- Modern Options (scripted pad, `runs/r5/a3`, `runs/r6/b1.png`, `runs/r7/c2_s.png`): Options > Mods tab; A pressed to Off with
  the pad: `mods.ini` written, status line "Saved. Restart the game to apply the mod list."; the Load position row opens the
  list card (1 of 4 (first) ... 4 of 4 (last)); Left / Right step the position.

## Notes

- The modern pages' "Defaults" (X) on the Mods page does nothing (`ResetPage` for `mods` is ignored: the list is the
  player's choice). The overlay's "Defaults for this page" likewise.
- Info rows on the modern page are one line and clip long text, so descriptions are shortened there; the full
  description, author, disc and conflicts are in the help panel text of the Enabled row.
- Conflicts are the ones from start-up (`ModManager::conflicts()`); after a reorder they are not recomputed until the
  restart. The pack list is also the one from start-up: a pack copied into `mods/` while the game runs shows up after
  the next start.
- The Multiplayer agent's title-menu row landed in the same build during testing (Options moved one row down).
