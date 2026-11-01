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

## In-game runs (same day, dist build, harness = what the editor's Play now sends, empty card)

| Run | Result |
| --- | --- |
| `play_run.py --level 1a` + `START_LEVEL_FROM=Level 0 - Intro` (`build/harness/pie_surdana`) | Surdana up at 18 s after launch, no intro, Rynn alive with HUD; quit clean (25 s total). |
| Same, Stratos, `PLAYTEST_POS` at Rynn's start + `PLAYTEST_GOD=1` | placed after 1 try, stays; clean. |
| Same, Ravenshold | on Arokh in flight, quest text, 21 s total. |
| Stratos with an editor-built pack (`puz-mods-plate`, LVL entry patch) | pack rebuilt into `LEVEL8OS.LVL`, level loads in the intro slot, 3284 entities vs 3281 without the pack. |
| **Exit walk-in with the file redirect only** (`level_change_walk.py --exit 735232,772096 --dist 5000`) | **broken**: `exit taken in 'Level 0 - Intro'`; one run stalled on the level-change save, another reached Kragmor at a wrong arrival, Rynn died, and the title's Continue looped Kragmor -> title. The engine kept calling the level "Level 0 - Intro". Control run on the old route: Kragmor, arrival 341935 172624 230850. |
| Exit walk-in after the fix below (`pie2-walk-*`) | `exit taken in 'Level 1A - Surdana'`, Kragmor loads, arrival 342162 172679 230735 (= the control), no title loop. (The harness's verdict line skips the first two loads, intro + Surdana, so it prints "nothing" for a direct run.) |

A play-test position near an exit trigger (my first Surdana pick, 735232 153300 767096, is the harness's walk start
5000 units short of the Kragmor detector) is a legitimate way to test an exit; a point *on* the trigger's edge keeps
its offset from the trigger centre at arrival ("Maintain Player Position") and can put Rynn off the floor in the next
level. That is the engine's own rule, not the play test.

## Fix: rewrite the path, not the file

`drakan_lifecycle::setLevelPathRewriter` (hook of the level loader `0x1c94c8`, before the engine sees `a0`): the
play test (`drakan_playtest.cpp`, armed by `DRAKAN_START_LEVEL` + `DRAKAN_START_LEVEL_FROM=Level 0 - Intro`) turns New
Game's `.../Level 0 - Intro/Level 0 - Intro.lvl` into `.../<level>/<level>.lvl` (not a save restore, not `Out.lvl`).
The new path is written to the speech controller's idle buffer tail `0x5b7d20` (the spot the `DRAKAN_LIFECYCLE_EXIT`
aid already hands to the level-change entry). The engine then loads the level under its real name. The file redirect
from drakan_overrides stays installed but no longer matches anything on that load.

## Desktop app, clicked (same day)

Debug build (`tauri build --debug --no-bundle`), private WebView2 profile with CDP on 9393, private play folder
`build/scratch/pie-desktop/play` (settings.json `gameDir` pointed there, restored afterwards). Disc reopened, Level 1A
opened (4675 objects; the Details panel shows its title once), real mouse click on the toolbar Play:
* the camera looked down on the whole level, so the editor said "No floor in the middle of the view: starting at the
  player start." and used the Rynn object;
* game log: `[game] playing d2`, `New Game opens 'Level 1A - Surdana' in place of 'Level 0 - Intro'`, the memory card
  warning skipped, `level load '.../Level 0 - Intro.lvl' -> '.../Level 1A - Surdana.lvl'` at 18.7 s, player under
  control, `placed after 1 try`, released, standing at 450786 171630 340019. No Continue, no save, no intro.
* the play folder's `mc0` stayed empty (the game used `%TEMP%/drakan-editor-playtest/card0/mc0`); `mc1` holds only the
  game's settings file.

Not clicked yet: the Project panel's per-level Play and "Change destination", the revert UI, the save editor changes.
Level copies keep the Surdana route (proven earlier, with the intro).
