# Issue 2: make newly discovered mod packs obvious

Follow-up from issue 1 ([cliff, intro and city](../2026-11-05/issue-1-cliff-intro-city.md)). A Drakan 1 campaign zip
copied into `mods/` by hand was detected, appended to `mods.ini` as `off` and never loaded, with no hint in game. Our
own first final-binary retest hit the same trap. Packs still start disabled; the change makes that state visible and
gives a one-step way out.

## What changed

* **mods.ini value `new`.** `ModManager::load` appends a pack it has not seen as `<name> = new` instead of `off`.
  `new` loads nothing (older builds read it as off too) and marks `ModStatus::isNew`. It stays until the player decides:
  enabling, keeping off, toggling or moving the pack on the Mods page writes `on` / `off`. This is what lets a notice
  that was skipped (Esc, or a harness run) come back on the next start without nagging after a decision.
* **Title notice.** A "New mod packs found" card opens before the game card, lists the packs, says that they stay off
  until enabled and take effect after a restart, and adds a line when one is a campaign. Enable and restart writes
  mods.ini and relaunches the game (the same `relaunchSelf` path as the Mods page's Restart game now); Open Mods menu
  opens the title's own PC Settings pages on `mods` (or the F1 overlay's Mods page); Keep them off writes `off`.
  Shown once per run, never under `DRAKAN_INPUT_SCRIPT` unless `DRAKAN_MODS_NOTICE=1`. Because the native title cannot
  draw it, the modern title is used for a run that wants the notice, as `offerChoice()` already did for the game card.
* **Mods page.** `[New]` and `[Restart pending]` in the pack titles, a New count, "New: not enabled yet" as the status,
  a Campaign line on campaign packs, "Restart pending" at the start of the page description.
* **Why the game choice is missing.** `drakan_overrides` now records why a loaded campaign pack was refused (invalid id,
  duplicate id, start level) and hands that with the `[general] game` setting to the settings host. `gameChoiceNote`
  picks one reason (pinned setting, enabled this session, skipped by the loader, refused campaign); the title shows it as
  one line on the main page, the Mods page as a Game choice row with the loader's reason, the log as `[game] ...`.
* **Editor.** The campaign wizard's zip guidance now says to drop the zip in `mods/` and start the game, where the
  title offers Enable and restart.

Policy code: `runtime/settings/include/settings/ModDiscovery.h`. Title card: `gameui::TitlePage::ModsNotice`, rows
`ModsEnable / ModsOpen / ModsKeepOff` at 240+ so the game rows (`GameDrakan + i`, up to 100 games) keep their values.
German, French, Italian and Spanish strings are in `MenuText.cpp`; the Mods page stays English like the rest of the PC
settings pages, which have no translation layer.

## Verification

* Unit tests: `mods_model_test` (undecided list, Enable / Keep off, `new` / `on` / `off` written to mods.ini, the
  notice modes, the page marks, every game choice reason and its text), `mod_test` (`new` appended and kept across
  starts, `off` is a decision), `gameui_saveload_test` `testTitleModsNotice` (card first, then the game card; the three
  answers; Esc; Open Mods menu with and without the modern pages; the main page note), `gameui_locale_test` (the card
  in all five menu languages, German strings checked literally). The full runtime ctest run passes.
* In game, from `dist/DrakanPC` with an isolated mods folder (a copy of the private OotF campaign zip) and memory card,
  German menus, `DRAKAN_MODS_NOTICE=1` because the probe drives input with a script (outputs under
  `build/harness/issue2-mods-notice`, private):
  * fresh zip, no mods.ini: the log says `new pack 'ootf-levels' found`, mods.ini gets `ootf-levels = new`, the title
    opens "Neues Mod-Paket gefunden" with the campaign line; Keep them off writes `off` and the main menu follows.
  * `= new` from an earlier start: the card comes back; Esc closes it and mods.ini keeps `new`.
  * Enable and restart: mods.ini `on`, the game relaunches, the new process loads the pack (284 entries, 23 levels,
    campaign `ootf`) and opens "Spiel auswählen" with Drakan and Drakan: Order of the Flame.
  * Open Mods menu: the PC Settings Mods page with the New row and the `[New]` mark on the pack.
  * Pack on, `DRAKAN_GAME=d2`: the main page shows the plated line "Spiel auswählen wird nicht gezeigt: DRAKAN_GAME ist
    auf d2 gesetzt ..." and the log the English one.
* Not checked in game: the F1 overlay fallback for Open Mods menu (only used without the modern PC Settings pages), the
  Rejected and Skipped reasons (unit tests only), French / Italian / Spanish wording by a native speaker.
