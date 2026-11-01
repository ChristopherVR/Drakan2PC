# Editor Play: straight into the level (2026-11-02)

User report: Play in the editor launched the game, opened a save game, showed the intro, and sometimes Rynn was dead.

## Causes (from the code)

* **A save was loaded.** Every editor launch with "Skip title..." used `DRAKAN_AUTOPRESS` Cross/Start taps on the
  player's own memory card. With saves on it, the title's first row is Continue (`runtime/gameui/src/TitleScreen.cpp`),
  so the first Cross loaded the latest save; the harnesses never saw this because `play_run.py` always runs on an empty
  card (`DRAKAN_MEMCARD_DIR`, `docs/journal/2026-10-22/no-save-harness-and-palette-hd.md`).
* **Rynn dead.** After that, `DRAKAN_PLAYTEST_POS` (a point of the edited level) was applied in whatever level the
  save held, so she was put at a spot with no floor in that level. Same root cause.
* **The intro.** `DRAKAN_START_LEVEL` replaced `Level 1A - Surdana`, which New Game opens only after `Level 0 - Intro`
  (movie + opening cut scene). The multiplayer maps already load in the intro's slot instead
  (`DRAKAN_START_LEVEL_FROM=Level 0 - Intro`, `docs/reference/netcode.md` "Straight into the map", proven on Surdana,
  Inquisitor's Lair and City of the Damned with creatures in them).
* With a campaign pack installed, the taps could also pick the wrong game card ("Choose your game").

## Change

* `editor/packages/app/src/launch.ts`: auto start sets `DRAKAN_PLAYTEST_CARD=1` and `DRAKAN_GAME=d2`; a disc level
  (`direct`) also sets `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`. Level copies and converted OotF levels keep the
  Surdana slot (the intro slot does not open a new level's files, `docs/journal/2026-10-31/ootf-ingame.md`). New play
  option "Player cannot die while testing" (`DRAKAN_PLAYTEST_GOD`).
* Desktop shell (`commands.rs` `game_launch`): `DRAKAN_PLAYTEST_CARD=1` is consumed by the shell, which wipes and
  creates `%TEMP%/drakan-editor-playtest/card<n>/mc0` (n = instance) and passes it as `DRAKAN_MEMCARD_DIR`. The web view
  never chooses the path. `DRAKAN_START_LEVEL_FROM`, `DRAKAN_GAME`, `DRAKAN_PLAYTEST_GOD` joined the allow-list.
* The toolbar Play (and the desktop menu's Play) runs "Play from camera" when a level is open, like Play in Unreal;
  with no floor under the view centre it starts at the Rynn player start instead of refusing. Mod panel "Play now"
  and the project's Play also go direct for disc levels.

## Not verified

Not run in the desktop app with the disc yet (no game run in this session). Expected: about 5 s from launch to the
level for a disc level instead of ~25 s. Risk to check: an edited disc level loaded through the intro slot (the arenas
used unedited levels plus a pack).
