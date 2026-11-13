# Order of the Flame without spells, part 2: Skills, the tab row, the texts

Round 3 (`ootf-no-spells.md`) turned spells off per campaign (`campaign_spells = false`) and left four things: both Skills
pages listed Magic and took points for it, the native tab row kept an empty slot where the Spells icon was, the game's own
help texts still spoke of spells, and several cases were never played (switching games in one session, a drawn bow, Arokh,
Drakan 2 afterwards). This note covers those.

## What was wrong and why

* **Skills**: the native Skills section (vtable `0x54f1f0`) builds one row per skill of the stats block (count
  `*0x4fbad0`, rows in the order of the disc's row table `0x54f258`: Melee, Archery, Magic). Nothing in round 3 touched
  it, and the modern Skills and Character pages list every skill the reader finds.
* **Tab row gap**: the five tab records (`screen+0x19c`, `0x24` bytes each) hold two scene nodes, the icon (`+0x1c`)
  and the ring behind it (`+0x20`). The icons all share the page frame; each icon model carries its own slot offset, so
  hiding the Spells icon leaves its slot empty. Every frame `42b0a8` copies the page frame (`screen+0x16c`) into the
  icon (`node+0x14`) and puts the ring at the icon's box centre; the glow `42faa0` is placed from the ring.
* **Texts**: the Skills help box (shown on the first visit, in both Tab screens; the modern screen shows the same text in
  its own box) says Rynn's points go to "one of three skills" for "weaponry and spells", and the Circle line of the game's
  own Controls screen (Options > Controls) reads "Use Magic/Parry". Both come from `INTERFCE.XDB` (PAL ids 150 and 10),
  read from the disc when a screen asks for them. The Spells help (149) is never shown, since the section is skipped.

## What changed

| Where | How |
| --- | --- |
| Native Skills | The key handler `445898` (Up / Down wrap, Left / Right), the text pass `445b78` and the first-row choice `447150` run with the skill count lowered to 2 (`withTemporaryGuestState`, restored at once): Magic, the last row, is neither drawn, selected nor spent on. The third row's level orb and frames (`446290` draws `row+0x148 / +0x16c / +0x190` with `4316c8`) are skipped. `446290` is long and can be suspended past its hook, so the section is remembered at its entry, not scoped to the call. |
| Modern Skills, Character | `gameui::skillShown(entry, spellsEnabled)`: no Magic row, no Magic line, no Spend action for it; rows keep the native row index for the actions. `drakan_tab_screen.inl` refuses a spend on a row beyond the visible ones. |
| Menu mouse, Skills rows | `captureInventorySkills` captures two rows without spells and steps through two. |
| Native tab row | After `42b0a8` places a tab, a tab right of the hidden Spells tab moves left by `drakan_views::tabSlotShift` (Skills 2/3, Map 1/3 of a slot; Equipment and Journal stay), icon and ring, so the glow follows. The slot is a quarter of the distance between the first and last rings, accepted only near the icon box width (the first frame of a screen has no last ring yet). |
| Menu mouse, tabs | The captured tab boxes come from the icon's box, which does not follow the move; the hit test moves the boxes of tabs 2 and 3 by the same slots (`MenuMouse::categorySlotShift`), so clicks land where the icons are drawn and the old spots are empty. |
| Texts | `dsettings::textWithoutSpells(id, lang)`: our own sentences for ids 150 and 10 in the five language slots (no spells, no Magic, no em-dash), served by an `XdbTextOverride` rule in `drakan_overrides.cpp` while `drakan_game::spellsEnabled()` is false (PAL profile only). The PC Settings controls page already said "Parry" (round 3). |

Not hidden: the empty strip and faint emblem of the third Skills row are painted into the section's page art (they are
not row objects), so an empty band stays where Magic was. Moving the Skills page art was not attempted.

Code: `drakan_campaign_spells.inl`, `drakan_inventory_views_policy.h`, `drakan_tab_screen.inl`, `drakan_overrides.cpp`,
`runtime/gameui` (`Model.h`, `Screen.cpp`), `runtime/input` (`MenuMouse`), `runtime/settings` (`Bindings`). Decompiled
functions used for the analysis stay in `build/review/ootf-spells2/ghidra/` (not tracked). Reference:
`docs/reference/modding.md`, "Campaigns without spells".

## Evidence

Exe: `Build-LocalGame.ps1 -Incremental` under the build lock into `build/review/ootf-spells2/play` (a copy of
`baseline-play`: the owner's `drakan.ini` with modern inventory, Original pause and title menus, game choice `ask`, HD
textures, DLSS; private ELF copy). "Before" is the exe the owner has (`dist/DrakanPC/drakan.exe`, round 3) run from the
same folder as `drakan_before.exe`. Pack: the current `dist/DrakanPC/mods/ootf-levels.zip` (it carries
`campaign_spells = false`). Runs: `play_run.py` (fresh card, scripted keys, mouse and pad through the game's input path),
recipes in `build/review/ootf-spells2/recipes/`, frames in `build/review/ootf-spells2/runs/<run>/`, sheets in
`build/review/ootf-spells2/*.png`. Harness settings beyond the owner's ini are named per row.

| Run | Played / placed | Seen |
| --- | --- | --- |
| `ootf-native-before`, `ootf-native-after` (`DRAKAN_GAME=ootf`, `DRAKAN_INVENTORY_SCREEN=native`, `DRAKAN_DEBUG_SKILL_POINTS=3`) | New Game, Tab, PgDn to Skills, Down x3, Right x2, PgDn / PgUp | Before: tab row with a gap after the chest; Skills help "one of three skills ... spells"; Magic row reachable (Down x2 lands on it). After: four tabs evenly spaced, the skill-point glow on the moved Skills tab; our help text; Down cycles Melee and Archery only; points go to Archery (1 -> 3); no Magic label, value or orb. Sheets `skb.png` (before), `na.png` (after). |
| `ootf-native-mouse`, `ootf-native-mouse2` (same settings, mouse) | Clicks on the drawn icons and on the old Spells / Skills spots | Skills, Map and Journal open from clicks on their drawn icons; clicks on the old Spells spot and on the old Skills spot (now the gap between Skills and Map) open nothing. With the build before the box fix (commit `5cc63e7b`) a click on the old Skills spot still opened Skills; that run's frames were replaced by the later run. Sheets `mouse.png`, `mouse2.png`. |
| `ootf-modern-before`, `ootf-modern-after` (`DRAKAN_DEBUG_SKILL_POINTS=3`) | Tab, E E to Skills, Down x3, Right, Q to Character | Before: Magic row and Magic line on Character, help text with spells. After: Melee and Archery rows only, Down stops on Archery, Right spends on Archery (2 / 8, 2 points left), Character lists Melee and Archery; the help box shows our text. Sheet `modern.png`. |
| `ootf-controls-before`, `ootf-controls-after` (`DRAKAN_GAME=ootf`) | Esc, Options, Controls | Circle line "Use Magic/Parry" before, "Parry" after. Sheet `controls.png`. |
| `switch2` (game choice `ask` from the owner's ini, `DRAKAN_QUIT_EXITS=0` so the Original pause Quit goes back to the title as on the disc; the owner's ini closes the game there) | Drakan 2 New Game, Circle, Options > Controls, pause Quit > Yes, "Choose your game" > Order of the Flame, New Game, Tab, Circle, Options > Controls, Quit > Yes, choose Drakan 2, New Game, Tab, Circle | One process: Drakan 2 mana orb, Spells tab, Controls "Use Magic/Parry", Circle summon state 1; Order of the Flame (log `[game] switched to ootf`, `spells off`) no orb, no Spells tab, Controls "Parry", Circle `[spells] Circle: no cast`, summon state 0; back in Drakan 2 (`switched to d2`, `spells on`) the orb, the Spells tab and Circle summon state 1. Sheets `sw1.png` .. `sw5.png` (first attempts, see below), frames in `runs/switch2/`. |
| `d2-native-after`, `d2-modern-after` (`DRAKAN_GAME=d2`, points 3) | As the OotF runs | Drakan 2 unchanged: stars tab and five even tabs, Spells help, Magic row reachable, the disc's Skills help text, modern Spells page and Magic row, mana orb. Sheet `d2.png`. |
| `mines-bow` (`DRAKAN_GAME=d1`, `DRAKAN_START_LEVEL=OotF 04 - Grimstone Mines`, `DRAKAN_FAST_START`, god mode) | Start on Arokh, Circle mounted; Cross held to dismount; **placed** on the Long Bow and Magic Arrows (walked onto them, both picked up); modern inventory: equip the bow; wheel to draw it; Circle held | Mounted: Circle starts nothing (summon state 0); the HUD shows a dark right orb while mounted (not identified, see below). Bow drawn: the right orb slides in with the arrow count (75) and Circle is the bow sight. Sheet `bow.png`, `mines.png`. |

The switch run took three tries: the quit box defaults to No (the first try went back to the pause menu and, by chance,
showed Drakan 2's Controls screen), and the game list after Quit opens by itself with Drakan 2 focused (Up picks Order of
the Flame). Circle right after closing the modern inventory is ignored for a moment, so the last Drakan 2 Circle waits
about two seconds.


## Tests and builds

* `ctest --preset runtime-tests` in a private worktree (first commit's tree): 190 / 190 passed, 7 skipped as
  data-dependent. New checks: `inventory_views_policy` (tab slot shifts, even gaps, the shop, visible skill rows),
  `gameui_test` (`testSkillsWithoutMagic`: no Magic row, line, target or spend; the disc game keeps them),
  `settings_test` (the replacement texts: every language, no spell or magic word, no em-dash, under 256 bytes, other ids
  untouched), `menu_mouse_inventory_test` (moved tab boxes: the old Skills spot is empty, the drawn tabs hit, spells on
  keeps the boxes).
* After the last change: the same suites (`-R gameui|settings|inventory_views|menu_mouse|mod_test|policy`) 44 / 44 passed;
  that build ended with MSBuild child-node crashes (`MSB4166`, a machine fault seen once) after the test binaries were
  linked.
* Game: six incremental builds under the lock into `build/review/ootf-spells2/play`, all exit 0. Two of them were
  diagnostic builds of code that was not committed (the main-tree file was restored right after the build).


## Not done or not verified

* The empty strip and faint emblem of the third native Skills row stay (page art).
* The dark right orb seen while riding Arokh in the Mines was not identified (Arokh's own gauge or an empty mana orb);
  this round did not touch the HUD, and neither a "before" run on Arokh nor Drakan 2 on Arokh was compared.
* The switch was played with `DRAKAN_QUIT_EXITS=0`; with the owner's `quit_exits = 1` the pause Quit closes the game, so
  in the owner's setup a switch happens at the next start (or through a death's "main menu").
* Mouse clicks on the native tabs were scripted at fixed points (no hover sweep); the shop's two drawn tabs keep their
  ends and were not opened.
* Non-English texts are our own translations, not checked by a native speaker; the USA disc gets none of this (PAL ids).
* No owner play; no save / load round trip in this round (round 3 showed F5 / F8 keep the state).

