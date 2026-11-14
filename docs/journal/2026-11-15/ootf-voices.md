# Order of the Flame: Rynn, Arokh and Wartok idle voice choices (15 November)

Owner: "Let Arokh + Rynn use PS2 voices but configurable in the editor. The same with Wartok idle grunt." Reading (with the
coordinator): the shipped behaviour (Drakan 2 voices on Rynn and Arokh, the Drakan 2 Wartok clips' idle grunts) stays the
default; the importer gets choices for the Order of the Flame (PC) voices and for no idle grunts. Scratch, packs, runs and
frames: `build/review/ootf-voices/` (nothing from either game is quoted here beyond class, field and clip names).

## 1. Why this needed a runtime part

* Rynn ("Rynn") and Arokh ("Dragon") are name-mapped: the converted levels use Drakan 2's own records
  (`ootf-class-sounds.md`, decisions table). A pack is installed for the whole session, so writing PC sounds into those
  records would change Drakan 2's Surdana too, and a clone of the player and the dragon would touch every scene that names
  them.
* The Wartok idle grunts are not a class field: the Drakan 2 Wartok model's clips carry a sound list (ADB `0x0505`,
  `u16 count, count x u32 sound id`) that the animation-event path (`0x10d100`, `ra=10d1c0`) plays. The clips with the
  grunts are the model's `idle_base`, `ready`, `walk`, `walk_back` (NPCS.SDB `Grull_NEWidle_2-1/2-2/2-3/3-1/6`, ids
  `0x3ee 0x3ef 0x3f0 0x3f2 0x3f6`) and the plate Wartok's (`Wartok_Idle_1/3/5`, `0x336 0x338 0x33a`). They are disc
  NPCS.ADB entries shared with Drakan 2.
* The Order of the Flame Wartoks have **no idle sound**: the PC Troll and Orc classes have no ambient field for it (only the
  orcs have "Ambient Sounds", which the class-sounds work already handles), and the PC ADBs have no `0x0505` lists at all
  (`NPCs.adb`: 963 clips, types 0x501..0x503 only). So "OotF PC Wartok idle sounds" do not exist; the choice is Drakan 2
  grunts (default) or silent, which is what Order of the Flame does.

So the pack names the swaps and the game applies them only while that campaign is the current game.

## 2. The change

* **Runtime** (`hle::CampaignSoundSwaps`, `campaignSoundSwapHook` in `drakan_overrides.cpp`, ModPack reads
  `sounds/swaps.ini`): lines `<play ids> = <play ids> | none`. While the pack's campaign is the current game
  (`drakan_game::currentModId`, evaluated per call; PvP arenas excluded) PlaySound `0x1b7f30` gets the swapped id (in turn
  through the list) or returns 0 for `none`. The hook wraps the `DRAKAN_SND_DBG` hook, so `[play]` shows the id that
  really plays; `[sound] campaign swap <from> -> <to>` logs the first 400. Reference: `modding.md` "Campaign sound swaps".
  Play ids are `database index << 16 | id` of the template's dependency table (SYSTEM 2, RESOURES 3, NPCS 5 for the
  Surdana template every converted level uses).
* **Converter** (formats `ootfsoundswaps.ts`, sounds stage of `ootfpipeline.ts`): `pcVoices` matches the PC Rynn and Dragon
  sound fields by name to the Drakan 2 records (`matchClassSounds`), adds the PC clips to the pack and writes one line per
  field; loops and body sounds (wing flaps, sheathe, push, slide, landing) stay Drakan 2's. `wartokIdle: 'off'` reads the
  sound lists of the `idle*` / `ready*` / `walk*` clips of the Drakan 2 Wartok records the converted Wartoks use (Wartock,
  Tock, Orc classes shown with Drakan 2 models) and writes `= none` lines. CLI `--pc-voices 1`, `--wartok-idle off`.
* **Editor**: Import Order of the Flame levels has two rows: "Rynn and Arokh: Drakan 2 voices / Order of the Flame voices"
  and "Wartok idle grunts: Drakan 2 grunts / Silent, as in Order of the Flame", each saying Drakan 2 keeps its own.

Swap table of the full pack with both choices (`packs/on4`, convert log): 22 lines, 99 Drakan 2 ids (35 ids are in two fields, mostly the HitRxn lists; their PC targets are merged). Arokh: Separated,
Got Hit, Crash, Done Mounting, Can't Land, Call Response, I'm Close Enough, Can't Fly, Killed Enemy, Health Low, Got Hit By
Rynn, Get Out of My Way, Done Dismounting. Rynn: Separated, Drown, the six HitRxn lists (they share ids, merged into one
line), Can't Reach, Can't Use, Inventory Full, Attack Grunts. Not swappable, because Drakan 2 plays nothing there (field
empty) or has no such field: Arokh's Meet At Cave Entrance and Take Off, Rynn's Dragon Call (no field in the Drakan 2
record), No Arrows, Fall Into Water, Get-the-point, and the "Got Hit (Rynn)" sound object. The default pack (`packs/base1`,
new code, no choices) is entry for entry identical to the pack of the code before (`packs/base0`, formats e5f395c): 12,211
entries, none differs. The choice pack changes only `new/` entries (217 more of them for the added PC clips; the ids after them shift),
`mod.toml` and `sounds/swaps.ini`; no `overrides/` entry differs, so Drakan 2 reads the same disc records.

## 3. Evidence (logs; I cannot listen)

Play folder `build/review/ootf-voices/play`: copy of `baseline-play` (owner's `drakan.ini`, HD textures), private ELF, exe
built from main at this change. Runs with `DRAKAN_SND_DBG=1`, god mode; levels started directly
(`DRAKAN_START_LEVEL`), so Rynn was placed at the start; the swings, the stand and the fights are real input. Same exe and same pack for every run but the default one.

| Run | Pack | Played / placed | Log |
| --- | --- | --- | --- |
| `runs/rvv-base` | default | Ruined Village: equip, 10 swings, placed at the Wartok camp (330000, 233600), 40 s stand, fight a Wartok | No swap table. Wartok grunts `0x503ee/ef/f0/f2/f6` 113 plays (`ra=10d1c0`); Rynn's Drakan 2 attack grunts `0x20191..0x2024f` 76 plays |
| `runs/rvv-on` | both choices | same | `[game] pack 'ootf-levels': 99 campaign sound swap(s)`. Every Wartok grunt request swapped to nothing (`503ee/ef/f0/f2/f6 -> 0`, 117 times), none of those five ids reached a play. Rynn's attack grunts swapped to the PC `attack1`..`attack7` (`0x30269..0x3026f`), 60 plays, each made an instance |
| `runs/iss-on` | both choices | Islands, on Arokh, placed beside the succubi, 2 fights | Arokh's Got Hit `0x202d6..0x202db` swapped to the PC `ArohkHitReact_1..7` (`0x30270..0x30276`), 92 times; Rynn's HitRxn `0x201a2/a9/ac` swapped 6 times. The PC clip's data was loaded: `RESOURES.SDB 0304/624 ... len 9776 -> dest 00142040` (SPU upload) |
| `runs/d2-on` | both choices installed | Drakan 2 Surdana, equip, 10 swings | see section 4 |

Frames: `evidence/rvv_on_wartoks.png`, `evidence/rvv_base_wartoks.png` (the camp stands), `evidence/iss_on_succubi.png`.

## 4. Drakan 2 with the pack installed

Run `runs/d2-on`: the same exe with the choice pack (`packs/on4`) installed, `DRAKAN_GAME=d2`, Surdana direct start,
equip and 10 swings (frame `evidence/d2_on_surdana.png`). The table is read (`[game] pack 'ootf-levels': 99 campaign sound
swap(s)`) and **no swap happened** (0 `campaign swap` lines). Rynn's Drakan 2 attack grunts played (`0x20191`, `0x20199`,
`0x2024f`) and so did the Drakan 2 grunts the swap silences in the campaign (`0x503ee` 35, `0x503ef` 33 plays, the
level's own creatures). The swap follows the game chosen per call, so a game switched on the title card in one session
should follow as well; that switch was not run (the game was fixed by `DRAKAN_GAME`).

## 5. Risks

* A swap is by id: any other use of a listed Drakan 2 id while the campaign is chosen is swapped too. In the converted
  campaign only Rynn, Arokh and the Wartok clips name those ids (the scenes play converted PC clips), but a custom campaign
  that reuses Drakan 2 scenes with Rynn's lines would hear the PC voice there.
* On the animation-event path (`0x10d100`) the slot compares the requested id with the playing instance before restarting
  it; a swapped instance carries the other id, so a clip event that asks again for the same sound restarts it. Not seen as
  a problem (Rynn's swapped voices go through `0x1b8330`, the Wartok grunts are silenced).
* Lists are played in turn, not at random: Drakan 2 picks a random entry of its list, the swap maps it to the next PC clip.
* Co-op characters other than Rynn (`drakan_ghost` voice mapping) see the swapped id, which is not a Rynn id, so they would
  speak with Rynn's PC voice on those lines. Not run.
* Lip sync: the PC clips have no `0x0305` track, so Arokh's jaw does not move on a swapped line.

## 6. Not verified

* Listening: nothing was heard; only logs.
* Arokh's remarks other than Got Hit (Separated, Meet At Cave, Killed Enemy, Call Response, Get Out of My Way, Crash, Can't
  Land ...) and Rynn's Can't Reach, Can't Use, Inventory Full, Separated, Drown: in the table, not triggered in play.
* The editor rows were checked by typecheck and the app tests only, not clicked in a browser.
* Other levels than Ruined Village and Islands; a New Game from the title (direct starts were used).
