# OotF campaign walk-through: exits, hand-offs, cut-scene levels, saves (2026-11-01)

Follows [ootf-campaign-e2e.md](ootf-campaign-e2e.md) (host-fired exits) and [level-exit-walk-in.md](level-exit-walk-in.md) (what fires a PS2 exit).
Pack: `ootf-convert.ts --campaign 1` with all 13 single-player levels, dist build, `build/ootf-run/ct*` harness (private).
Harness facts learned on the way:

* Input script times are **absolute milliseconds from the last `sync`** (also in frame mode), not increments. Recipes written as
  increments ran every step at once and cut off the save prompts; use `clock milliseconds` and absolute times.
* `DRAKAN_START_LEVEL` replaces `Level 1A - Surdana`; a level started that way keeps that name, so its exit is taken "in 'Level 1A - Surdana'" and the
  next level finds no marker to arrive on (it falls back to its own Rynn start). Arrival positions are only meaningful for the second level of a run.
* W walks where the *start camera* looks, not where the player is turned; placing Rynn 3000+ units out and walking in only works where the line is open.
  Most runs below put Rynn into the detector box (`DRAKAN_PLAYTEST_POS` on the exit): the touch event, the message box, the save screen and the level
  change are the real game path, the approach is not. One approach was walked with W (Wartok Canyons to Grimstone Mines).
* `PS2X_WATCHDOG` is a diagnostic only: it prints the live guest pc every N seconds. `pc=0x4bc088 ra=0x1d7a24` is the normal frame wait; it does not
  mean a hang (it is printed by every healthy run). A stalled run is one whose `frame-uncapped` lines stop.

## Breaks found in the chain

| # | Break | Root cause | Status |
| --- | --- | --- | --- |
| 1 | The pack listed the levels in directory order, so `campaign_start` was Alwarren ("New Game opens OotF 07 - Alwarren") | `convertOotfLevels` returned the levels in input order; the manifest takes `levels[0]` as the start | fixed (sorted by campaign index) |
| 2 | **No exit could be walked into** | a PC exit is touched by the player directly; on PS2 every exit is the link target of a Trigger Detector box (level-exit-walk-in.md) and the converter added none | fixed: every real exit and the intro hand-over gets a `Trigger Detector` (2 x 2 x 2 cells, linked to the exit) |
| 3 | Each PC exit existed twice in the level (an inert synthesized-class twin with destination 0 and the added real exit) | `applyCampaignExits` could not rewrite the converted object (its class is a synthesized clone, not an exit class) and added a new one | fixed: the converted object keeps its id (plates and detectors link to it) and becomes the exit |
| 4 | The Intro never hands over: black screen forever after the 45 s cinematic | the PC Intro has no player (its "Rynn" is a scenery model); the converter's own Rynn stands at the map corner (32768, 32768); the hand-over exit sat at the PC "Stomp" trigger / Rynn start where nobody goes | fixed: a `Timer` (44 s = Intro 1 + 2 + 3) linked to the hand-over exit |
| 5 | Ruined Village Book never leaves the loading screen (gui screen 14, world 0 fps) | `painting01..05` (scenery classes cloned from weapon-like template classes); bisect below | **open** (not fixed) |
| 6 | Exits of Islands, Succubus Lair and every level with a converted detector did not fire | `addObject` copied another detector's parameters into the exit detector (wrong `Trigger Message`) | fixed (clean detector body) |
| 7 | Islands (flown on Arokh): exits ignored | `Detect Which?` default does not fire for Rynn on a mount | fixed: a second detector with `Detect Which? = 1` per exit |
| 8 | Rift World cannot be finished | the final boss (PC engine class 92 "Dr. Evil", 4 heads counted by `Navros DeathCounter`) has no PS2 class; the 4 objects are dropped, so the counter never reaches zero; Rift World also has no exit (the ending) | **open** |
| 9 | Quest and puzzle logic dropped | engine classes 29 (Pressure Plate, 8 objects in RV / Wartok / Grotto / Islands / Rift Crystal), 123 (combination lock, Rift Crystal), 104 (map / quest markers: Find Rimril, Rescue Arokh, Find Rune Blade, Get Rune Cage Key, Map Triggers) have no PS2 engine class | **open**, see the audit (`ootf-props-audit.md`) |

### Detail: why the exits did not fire even with a detector (break 2 follow-up)

`addObject` copies the body (parameters included) of the first object of the same class already in the list. In every level that has a converted PC
detector of the Trigger Detector class (Islands, Succubus Lair, Rift Crystal, Volcanic ...) the new exit detector inherited that detector's
`Trigger Message` / `Detect Only Once?` / `Task` and sent the wrong message (the exit's touch handler ran, the save prompt never came: log line
`touch handler ... event fd00` without the following `exit handler ... msg 4`). The exit detectors now get a clean body (flags 0x101, no parameters,
so the class defaults: `Trigger Message` 5, `Detect Which?` "Both"). Earlier theories (a terrain-stretched box, the mount) were wrong or only partly
right and were removed.

`Detect Which?` still matters when Rynn rides Arokh: the Islands hub starts mounted (the Isles are flown). With the class default the touch event
arrives and nothing happens; with `Detect Which? = 1` (Rynn on a mount) the Islands to Alwarren exit was taken. Every exit therefore gets two
detectors (class default for Rynn on foot, `Detect Which? = 1` for a mount), both linked to the exit.

### Cut-scene levels (the Intro, Ruined Village Book)

* **Intro**: the PC level has no player; its cinematic is three StompPlayer sequences (Intro 1, 2, 3 = 14.5 + 10.2 + 16.4 s of the Intro SSD, owl and
  landscape, then black). The converted level stood on the black screen forever (player at 32768, 200400, 32768). A Timer (`Time Until Trigger` 44 s,
  `Start Mode` 0 = at once, `Trigger Message` 5) linked to the hand-over exit now ends it. The class default `Start Mode` 1 waits for a message: the first
  attempt never ran.
* The Timer's message reaches the exit's class handler (`0x365138`, message 0x20) but the engine's touch handler (`0x361440`) ignores a message with no
  toucher (log: `touch handler ... event fd90`, no `msg 4`). Runtime (`drakan_levelchange.cpp`, `exitHandlerEntry`): in the Order of the Flame game,
  message 0x20 reaching an exit while the player is more than 8000 units away takes the exit through the host path (the way `DRAKAN_LEVELCHANGE`
  fires, no save prompt). The converter's detectors sit on the exit, so a walker is always closer than that.
* **Ruined Village Book**: entered from Ruined Village (exit "1-Intro" at 279392, 14761, 152908). The level loads and stays on the loading screen
  (gui screen 14, world 0.0 fps, the frame counter keeps flipping; watchdog lines are the normal wait). It does not depend on the campaign, the exit,
  the timer or the faders: a pack with only this level hangs the same way. Bisect with `DRAKAN_DBG_DROPNAME` (pack of one level): all ten `painting*`
  objects dropped: loads; only `painting back` dropped: hangs; all but `painting back` dropped: loads; `painting01..05` dropped: loads; only `painting05`,
  only `painting04`, or `painting01..03` dropped: hangs. No single class is the cause: the level loads once 5 of its 11 `painting*` clone classes are gone
  (14 new classes in the pack -> 9), so the suspect is the number of new class records the pack adds to the template ODB the Book's clones go into
  (the note of 2026-11-01 about the per-ODB record limit), not the models. Not fixed. Until it is, entering the Book (the optional RV exit "1-Intro" at
  279392, 14761, 152908) soft-locks the game on the loading screen; the safe interim is to leave that exit out of the campaign (or drop `painting01..05`
  with `DRAKAN_DBG_DROPNAME` for a test pack) - not done here, the exit is still converted.

## Per-level playthrough table

"Exit" = a real run (`DRAKAN_START_LEVEL` + Rynn put into the exit box with `DRAKAN_PLAYTEST_POS`, then the real prompt chain: touch event, message box, save
screen, loading screen, next level; `build/ootf-run/ct/cw*`). "Walked" = Rynn placed 5000 units out and walked in with W (only Wartok to Mines).

| # | Level | Reaches control | Exits (result) | Cut scenes / triggers / logic | Blockers for the chain |
| --- | --- | --- | --- | --- | --- |
| 0 | Intro | yes (cinematic, no player) | hand-over to Ruined Village after 44 s: **works** (`d1` New Game run, `exit taken in 'Level 0 - Intro'`, Ruined Village loaded, arrival on the marker 220424 19150 176605, `OotF 01 - Ruined Village.rsg` written on the separate OotF card) | StompPlayer sequences play (owl, landscape); then black until the timer | none after the fix |
| 1 | Ruined Village | yes, about 2000 frames after the opening scene | to Wartok Canyons: **works**; to the Book: **works** | opening Stomp scene plays; 3 pressure plates (Book / shelf puzzle), 19 Burning Flames, 7 death triggers, 6 map triggers dropped | pressure plates: the puzzle that they drive cannot run |
| 2 | Ruined Village Book | **no: stays on the loading screen** | exit to Ruined Village not reachable | cut scene level: paintings, narration sequence `The Book` (25.7 s) | Book hang (painting01..05), see below |
| 3 | Wartok Canyons | yes | to Grimstone Mines: **works, walked**; to Ruined Village: **works** | Farmer detectors with sequences; 5 map messages, 1 pressure plate, quickening effect dropped | none found |
| 4 | Grimstone Mines | yes | to Grotto: **works**; to Wartok: **works** | Stomp scene 3.1 detector (its `Sequence To Play` converts to 0, the linked StompPlayer is kept); 2 Crystal Energy dropped | none found |
| 5 | Grotto | yes (one run froze before control with a mis-set placement; not reproduced) | to Islands: **works**; to Grimstone Mines: **works** | cut scene detector "41 cutscene"; 3 map triggers, pressure plate dropped | none found |
| 6 | Islands (hub) | yes, mounted on Arokh | to Grotto: **works** (mount detector); to Alwarren: **works**; to Rift Crystal: the exit object sits at y = 0, 77000 below the terrain: the box fires if Rynn is put there (test) but nobody can fly there | entry detectors with sequences; Bell Hammer / Find Rimril / Elemental Amulet quest markers dropped (the amulet item itself is kept) | the Rift Crystal way is unclear: unreachable as converted unless the dragon can dive to y = 0 |
| 7 | Alwarren | yes | to Islands: **works** (the PC exit is also linked from 3 "Orc Plate" objects) | dart / flame traps kill Rynn at the TuiriInfo detector (a message-popup test died there); Fall01 and Brick17 dropped | none found; dead end by design |
| 8 | Rift Crystal | yes | to Islands Night: the exit object is correct (host-fired, loads Islands Night); putting Rynn onto it freezes the engine, so the walk-in is **not verified** | pressure plate, combination lock (engine 123) dropped | the combination lock gates the route in the PC level |
| 9 | Islands Night | yes | to Succubus Lair: **works** | Rescue Arokh / Find Way markers dropped | none found |
| 10 | Succubus Lair | yes | to Islands Night: **works**; to Volcanic: **the box never fires** (two runs, Rynn put inside it at two heights; no touch event at all; the same level's other exit works; cause not found; and host-fired the transition starts (`exit taken`) but Volcanic never finishes loading within 170 s; the reverse hop Volcanic to Succubus Lair stalls the same way, while Volcanic to Rift World loads: the Succubus Lair <-> Volcanic hop is broken in both directions, cause not found) | detectors with message strings (9 "Detect and Execute"), map triggers dropped | |
| 11 | Volcanic | yes (about 9000 frames) | to Rift World: **works** (walk-in prompt chain, and host-fired: Rift World loads, arrival at its Rynn start); the exit is 42000 units above the terrain layer the converter finds (the real floor may be lower than that, unknown). The arrival-marker exit back to Succubus Lair, host-fired, left the game without a load for 170 s | 8 detectors with sequences, Find Rune Blade / Get Rune Cage Key markers dropped; item-gated detector (`Trigger Only If Carrying Item`) | |
| 12 | Rift World | yes | none (the ending); no hand-off to credits | boss "Dr. Evil" x4 + Navros DeathCounter dropped | the boss and the ending |

## Saving and loading (OotF card)

* Separate card: `[game] playing d1` puts the memory card at `<card>_ootf` (`mc0_ootf`, slot dirs `mc0_ootf_slot1`), settings copied from the Drakan card.
  A d1 run wrote `OotF 01 - Ruined Village.rsg` there after the Intro hand-over and nothing to the Drakan card.
* The hand-over (and any host-fired exit) writes the level-change save into the card root with no slot, so the title's **Load Game stays greyed** after
  the Intro alone (screenshot `cont1/c2_up.png`). A walked exit goes through the save screen and a slot (the Drakan walk-in path); that was not exercised
  on the d1 card in this pass (a d1 run always starts at the Intro; `DRAKAN_START_LEVEL` cannot start another level there).
* Level-change saves are named by the level title (`OotF 04 - Grimstone Mines.rsg`); the `.rlt` of the level being left takes the name the engine knows
  (`Level 1A - Surdana.rlt` in test runs that started through `DRAKAN_START_LEVEL`, the real name in a chain).

## Not covered

Quest items / gates beyond the exits (the Elemental Amulet and Lock, Volcanic item-gated detector, Alwarren plates), text popups (the Alwarren test died to
a trap first), boss fights (Dr. Evil missing), co-op, whether a real W walk reaches each box (only Wartok to Mines was walked), the Rift Crystal and
Volcanic exits by walking.

## Commits

formats: `9c7e8ef`, `056ef11` (detectors, in-place exit twin, level order, intro end, timers), the follow-ups for detector parameters, mount detectors and the
timer start mode; parent pointers `f3bae82`, `fe70110`, `fcc3e97`, `5960c9e`, `974ab9f`; runtime `70116c4` (remote exit message takes the exit).
