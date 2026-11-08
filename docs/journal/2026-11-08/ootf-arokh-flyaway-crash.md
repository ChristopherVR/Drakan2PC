# Order of the Flame: "after Arokh flies away the game crashes"

User report: on a normal New Game of Order of the Flame (`game = ask`, `title_menu = native`, play folder with
`ootf-levels.zip` converted 11:35 and `drakan-character-motion.zip`) the game crashes after Arokh flies away.

## Which scene

Arokh appears in no Ruined Village opening scene (1.1, 1.2a, 1.3 have Rynn, Delon, the villagers, the Wartoks and the
knights; dumped from the PC `Ruined Village.ssd` with `parsePcSequence`). He is in the Intro level: "Intro 3"
(`Intro Level.ssd` record 452, 13.3 s) flies `Aroch` (2852) with Rynn attached over the landscape, then its
`Gui Trigger` ends the level; the converter's intro exit takes the player to Ruined Village after 44 s
(`INTRO_SECONDS`). So "after Arokh flies away" is the Intro to Ruined Village hand-over and the start of the village
opening. (The other Arokh scene, "arokh awakening", is in the temple at the end of Ruined Village.)

## Evidence from the user's session

None survived. `dist/DrakanPC/drakan.log` had been overwritten by later runs (an agent run at 13:41 without its own log
folder; the play folder has since been refilled by rebuilds) and no `drakan.1.log` exists. The two 0-byte
`crash_33932.dmp` (13:27:04) and `crash_36044.dmp` (13:28:24) are not evidence of this crash: `drakan_crash.cpp` writes
dumps next to the exe whatever `DRAKAN_LOG_DIR` says, so every harness run from `dist/DrakanPC` lands there, and
`build/shots.log` shows a `setting_shots.py` run (Surdana, Drakan 2) whose driver timed out waiting for the game at
13:27:34, with a second attempt (`shots2.log`) at 13:27:49. Windows logged no Application Error / WER report for
`drakan.exe` today. `iso/mc0_ootf` (the play folder's Order of the Flame card) holds no save and was created at 13:37:44.

## Reproduction attempts (none crashed)

All with a private copy of the two user packs (`DRAKAN_MODS`), audio on, the play folder's `drakan.ini` (HD textures,
DLSS, ray traced water / reflections / shadows, uncapped world, 21:9) and an empty private card; runs under
`build/ootf-run/flyaway/`:

| Run | Start | Input | Result |
| --- | --- | --- | --- |
| `ootf-flyaway-1` | `DRAKAN_GAME=d1` | none for 430 s, then W | Intro (owl, logo, Intro 3) 44 s, village at +44 s, villager scene, HUD and control, clean exit |
| `ootf-flyaway-ask4` | `game = ask`, native title, Up + Enter on "Choose your game" (`SelectGame(0)`, `switched to ootf`) | none | same, clean exit |
| `ootf-flyaway-fs` | same as `ask4`, borderless full screen 3840x2160 (the user's display settings) | none | same, clean exit |
| `rep-1..3` | d1, 1920x1080, three at once | none | hand-over at +45 s in all three, clean exit |
| `v-enter` | d1 | Enter every 2 s through the Intro | hand-over, clean exit |
| `v2-escA`, `v2-escB`, `x-p1`, `x-p2` | d1 | Esc during / at the end of Intro 3 | pause menu over the scene, no crash |
| `x-mash`, `x-mash-native` | d1 (modern / native title) | Space, E, Enter every 1.5 s through the Intro and the village opening | see below |

The current executable (13:55, and the 13:37 one for the first runs) differs from the one the user ran around 13:20 only
in menu, settings preview and bow aiming code, nothing on the level change path.

## What the mashing runs found: a death in a converted level ends at the title

With buttons pressed through the opening the village scenes end early and Rynn stands in the open; a Wartok kills her.
The game then reloads the level-start save the engine wrote at the arrival (`/OotF 01 - Ruined Village.rsg`, card root),
and the save guard refused it:

```
[saves] OotF 01 - Ruined Village.rsg: made before pack builds were recorded (pack 'ootf-levels' build 321463ed808626e8)
[saves] load of '/OotF 01 - Ruined Village.rsg' refused before the restore (OotF 01 - Ruined Village.rsg)
[in] t=173.14 frame 6965: gui screen -1 -> 8 (gameplay=0)
```

and the player lands on the title with "This save was made with an earlier version of the mod pack Order of the Flame
levels" (`x-mash/s160.png`), although the save was written by the same session a minute earlier. Cause
(`drakan_save_guard.cpp`): the pack record `<save>.rsg.mods` was written only for saves inside a slot folder
(`<saveId>Slot<n>/`), not for the level-start save at the card root, and `checkSave` refuses a record-less save of a
level that a build-declaring pack owns. Since the save guard (November 7) every death in every Order of the Flame level
went back to the title instead of restarting the level. For a player this looks like the game throwing them out right
after the opening, which may be what was reported; it is the only failure found on this path.

## Fix

`drakan_save_guard.cpp` `written()`: every `.rsg` the game writes on the card gets its record (slot saves as before,
and the level-start save at the root); the orphan clean-up works per folder as before. Drakan 2 is unaffected: without
content packs no record is written, and a disc level is never refused for lacking one.

## Verification

Rebuilt with `Build-LocalGame.ps1 -Incremental` (exe 14:35), same packs, d1, the mashing recipe followed by Enter taps
(the death prompt "Press Enter to load the last saved game"):

| Run | Title | Result |
| --- | --- | --- |
| `death-modern` | modern | `[saves] OotF 01 - Ruined Village.rsg: pack record drakan-character-motion 3450512399c1fc82, ootf-levels 321463ed808626e8` at the arrival; after the death `beforeLevelLoad ... fromSave=1`, Rynn back at the level start with full health, clean exit |
| `death2-native` | native (the user's) | same record; death, Enter, `fromSave=1` reload at +100 s, then S 3 s and A 2 s move her away from the hut (`walkS.png`, `walkA.png`), clean exit |

`death-native` (mashing stopped before the death, nobody pressed Enter) shows the death prompt and then the native
title after its own timeout, the original behaviour. Not covered by a unit test: the change is the write observer's
file filter in the runtime glue; `save_record_test` already covers the record format and policy.

## Open

* The reported crash itself was not reproduced (13 runs on the reported path, with and without input). If it happens
  again, `dist/DrakanPC/drakan.log` right after the crash (before another launch overwrites it) and the `[crash]` lines
  in it are what is needed.
* Crash dumps of every run from `dist/DrakanPC` land in the play folder whatever `DRAKAN_LOG_DIR` says, and the minidump
  can come out empty (0 bytes); writing it into `DRAKAN_LOG_DIR` when set would keep harness dumps out of the player's
  folder.
* Harness note: with `game = ask` and the native title the classic "Choose your game" list focuses Drakan 2, so the
  autopress Cross starts a Drakan 2 New Game (`ootf-flyaway-ask`); a recipe picks Order of the Flame with Up, Enter once
  the list is bound (about 17 s after launch; earlier taps are lost, `ootf-flyaway-ask2`, `-ask3`).
