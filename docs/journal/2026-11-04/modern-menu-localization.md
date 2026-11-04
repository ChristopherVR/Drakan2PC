# Localize the modern menu titles and labels

Follow-up to the German guest-language repair: the initial deployed run proved localized guest resources and the
original German title, while the modern PC title remained English. The user explicitly requested modern menu titles
and labels to be localized too.

Added the platform-free `gameui/MenuText.h` catalog with 143 authored English keys and German, French, Italian and
Spanish translations. The validated effective startup language configures the catalog alongside OSD initialization.
Unknown locale names fall back to English. This follows the existing restart requirement and profile/absent-tree
English fallback, rather than reading restart-pending settings independently.

Explicit lookups cover the modern title/main menu (including game choice and Solo/Co-op card), pause menu, their
legacy embedded Audio/Controller pages, save/load titles, slot/card/empty states, footer hints, progress cards and
quit/default/overwrite/load/save confirmation dialogs. Dynamic slot/page/confirmation sentences use placeholders;
argument text is inserted after choosing the translation. Guest locations, save metadata, campaign names/descriptions
and explicit skin labels remain verbatim, including names that happen to equal English labels such as `Back`.

Translation happens before the existing layout, wrapping and hit-target measurement. The focused tests exposed a
French overwrite title truncated at the original title size; modal titles and buttons now fit the measured translated
text to their existing bounds, and the reading-card label uses the same fitting path. Existing host fonts already bake
U+0020..U+017F, covering the authored umlauts and French/Spanish/Italian accents. No new font assets or platform APIs.

Validation:

- Configured the existing `runtime-tests` preset and built the focused gameui targets.
- `gameui_locale_test`, `gameui_screens_test`, `gameui_saveload_test` and `gameui_skin_test`: **4/4 pass**.
- New actual draw-output tests cover all five locales at 1024x768, 1920x1080 and 3440x1440; main/pause titles, quit and
  restore-default cards, overwrite/load cards, slot/page formatting, absent/unformatted/reading card states, invalid
  locale fallback, UTF-8 text, verbatim guest/skin values and safe placeholder insertion.
- Existing English behavior and skin override tests remain green. Changed-file diff whitespace checks pass.
- Shared production rebuild and deployed German/English screenshots are owned by the root agent and remain pending
  at source handoff. No independent game build/deploy or commit was performed by this agent.

Scope: broad F1/modern unified PC settings schema, editor and multiplayer-page translation is not included. The title's
Multiplayer/Solo/Co-op entry labels use the shared catalog, but network page content remains as supplied. Dynamic
host-generated error messages, campaign metadata and timestamp content also remain as supplied. Translation wording
has not received a native-speaker review.

## Shared build and deployed verification

Root rebuilt and deployed through the standard incremental `Build-LocalGame.ps1` path using `build/final-game`.
Executable SHA-256: `2AEA124CA47B30F4B850846ACEE1C07DFAF80CF0AE2A98DA33C196F9E275641A`.
Isolated German and English modern-title runs both exited cleanly. Root visually inspected the German title:
Spiel laden, Neues Spiel, Mehrspieler, Optionen, Mitwirkende, Verlassen and the footer/description are localized.
The German gameplay run also exited cleanly; inspected pause and save screens show translated headings, actions,
status labels, slot/page text, empty slots and navigation hints without visible clipping. Player cards/settings
were not used; these runs have fresh cards, private INIs and empty mod/override folders.

Evidence stays under ignored `build/harness/modern-menu-title-{german,english}/` and
`build/harness/modern-menu-pause-save-german/`. The full runtime suite passed: 103 executed tests, 6 data-dependent
skips, zero failures (109 registered, including the full synthetic renderer checks and the new locale test).
The latest character-roster target was rebuilt and passed again after concurrent test assertions were added.

The final merged incremental rebuild also passed after the latest parallel runtime edits and formatting, deploying
SHA-256 `7ECB43CECF083BF2D9C96DF8AA7BC212513D9D47416B107EAA6C60242D7645C5`. Menu source did not change after the
screenshots. The final editor workspace suite passed 855 tests and its typecheck/production build passed; Python
passed 247 tests. Source audits found no prohibited assets in the staged snapshots, including the formats changes.
