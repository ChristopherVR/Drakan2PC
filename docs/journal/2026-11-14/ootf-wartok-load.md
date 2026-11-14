# Order of the Flame: "Wartok Canyons does not load" was a wrong level name (round 7)

Brief: in the 21:30 dist (pack build a7b56aa1af077aa6, formats 5dbdec2, wide chunks) a direct start of "OotF 02 -
Wartok Canyons" never reached gameplay (coordinator run `build/review/ootf-corpses/runs/wc-dist`, foliage run
`build/review/foliage-wind/runs/t1-wartok`), and the suspected cause was a converter change between formats 4ef9cb8 and
5dbdec2. Scratch, play folder, packs and runs: `build/review/wartok-load/` (private).

## 1. Cause

There is no level called "OotF 02 - Wartok Canyons". The pack registers (log lines `level '...' registered at list
index`): 00 Intro, 01 Ruined Village, **02 Ruined Village Book**, **03 Wartok Canyons**, 04 Grimstone Mines, ... 12 Rift
World, then the ten arenas. That numbering is not new: the 15:30 runs that played Wartok Canyons used "OotF 03 - Wartok
Canyons" (`build/review/limits-chunks/played/wide-watch3`). On disc the Wartok files are `OOTF03NS`; `OOTF02OK` is the
Book.

Both failing runs set `DRAKAN_START_LEVEL=OotF 02 - Wartok Canyons` (coordinator: `env.json` of `wc-dist`; foliage:
`start level: 'OotF 02 - Wartok Canyons' replaces 'Level 1A - Surdana'`). The start-level test aid rewrites the path of
the level New Game opens to that name; the file does not exist, so the engine got no level: `[resident-scene]
identity=0 layers=0`, gui screen 14 -> -1 with gameplay 0, `[resident-models] level=0` for 900 s (coordinator), or a
fall back to a menu (foliage run, whose Intro hand-over was also redirected to the missing name). The runtime said
nothing about it.

No converter or runtime change since 4ef9cb8 is involved, so no bisect was needed: with the right name the same dist
exe and pack load Wartok Canyons, walk, F5 and F8 (section 3).

## 2. Fix (runtime, test aid)

`FileService::levelRedirectTargetExists` (runtime/hle) checks that the redirect target is a level file
(`\Levels\<name>\<name>.lvl`, on the disc or added by a pack); if not, it looks in the merged `LEVELLST.TXT` for the one
level with the same title (the part after " - "). `drakan_overrides.cpp` calls it after the packs are installed and,
for a name that is not a level, logs

    [drakan] start level 'OotF 02 - Wartok Canyons' is not a level on the disc or in an enabled pack (did you mean 'OotF 03 - Wartok Canyons'?)

and exits with code 3 at start-up instead of sitting on an empty world (seen in run `wrongname`: exit 3 within
seconds). Players never set `DRAKAN_START_LEVEL`; the editor's Play and the harnesses do. `override_test` covers a
valid target, a missing target with and without a title match, and the redirect off.

## 3. Runs

Play folder: copy of `build/review/baseline-play` (owner's `drakan.ini`, HD textures, DLSS), `elf =` a private ELF copy,
`--keep-display` (owner's ini with windowed mode), `DRAKAN_GAME=d1`, empty Order of the Flame card, autopress on (it
skips the cut scenes), god mode. Each run: direct start, wait for gameplay, 3 s, hold W 5 s, F5, hold W 4 s, F8, wait
14 s; shots `a_loaded`, `b_walked`, `c_after_f8` in each run folder.

Exe and pack:

* "dist": `dist/DrakanPC/drakan.exe` (file time 17:56, not 21:30 as the brief says) and the dist pack a7b56aa1af077aa6.
* "main": exe built from main 882959aa (this fix on top of 178cb890) under the lock, and a full wide pack
  (`DRAKAN_TERRAIN_CHUNKS=wide`) converted from main 178cb890 / formats e5f395c, which includes everything after 5dbdec2
  (pines, crystals, torches, sconces, palms, creature voice clears, class sounds keep); pack build 1b2647e822c023e7.

Wartok Canyons:

| Run | Exe / pack | How Wartok was reached | Result |
| --- | --- | --- | --- |
| `wc03-dist` | dist / dist | direct start "OotF 03 - Wartok Canyons" | gameplay at 5 s on Arokh at the start, W flies him 5,200 units, F5 saves, F8 loads back at the saved spot (`b_walked.png`) |
| `wc03-main` | main / main | direct start | the same, F8 back at the saved spot |
| `wrongname` | main / main | direct start "OotF 02 - Wartok Canyons" | exit 3 at start-up with the suggestion line above |
| `rv-exit3` | main / main | Ruined Village direct start, the opening scene ends, then its exit to Wartok (handle `9f66e0`, dest 27003) fired by the `DRAKAN_LEVELCHANGE=2@3000` test aid | the engine's exit chain (save of Ruined Village, load), Wartok with control on foot at its arrival marker, W walks her 9,000 units, F5, F8 back |
| `rv-crystal2` | main / main | the real route: Rynn placed on the lair ledge (482600, 25200, 201196) facing the Soul Crystal, W held 3 s; the "arokh awakening" scene, the save prompt, Cross on the prompt and the save screen | the scene plays (about 165 s), the "must be saved" prompt, the save screen, `exit taken in 'OotF 01 - Ruined Village'`, Wartok Canyons loads with Rynn on Arokh at the cave arrival (dark cave, as on 7 November); W flies him 3,400 units, F5, F8 back within 90 units (`e_after_f8.png`) |
| `book-dist` | dist / dist | direct start of the Book | the Book hands back to Ruined Village (not to Wartok: the Book is a visit from the village) |

Every level once, main / main (runs `t01` to `t23`, `t12b`; "loads" = `level '...' loaded (playable` and gameplay with
the player moving under W):

| Level | Loads | F5 / F8 |
| --- | --- | --- |
| OotF 00 - Intro | yes, hands over to Ruined Village (autopress skips) | yes (in Ruined Village) |
| OotF 01 - Ruined Village | yes | yes |
| OotF 02 - Ruined Village Book | yes, hands back to Ruined Village | yes (in Ruined Village) |
| OotF 03 - Wartok Canyons | yes (`wc03-main`) | yes |
| OotF 04 - Grimstone Mines | yes | yes |
| OotF 05 - Grotto | yes | yes |
| OotF 06 - Islands | yes | yes |
| OotF 07 - Alwarren | yes | yes |
| OotF 08 - Rift Crystal | yes | yes |
| OotF 09 - Islands Night | yes | F5 yes; the walk set off a cut scene, F8 during it is ignored (as in any scene) |
| OotF 10 - Succubus Lair | yes | yes |
| OotF 11 - Volcanic | yes | F5 yes (`t12b`); W walks Rynn into Arokh's cage scene, F8 during it is ignored. The same on the dist exe and pack (`t12dist`) |
| OotF 12 - Rift World | yes | yes |
| OotF - Alwarren of Death (air) | yes | yes |
| OotF - Arena of Death (air) | yes | yes |
| OotF - Grotto of Death (air) | yes | yes |
| OotF - Nemesis (air) | yes | yes |
| OotF - Hill of Fire (ground) | yes | yes |
| OotF - Relic (ground) | yes | yes |
| OotF - Sanctuary (ground) | yes | yes |
| OotF - Rise to Power (mod) | yes | yes |
| OotF - Volcano of Death (mod) | yes | yes |
| OotF - Island of Death (mod) | yes | yes |

The arenas were started as campaign levels (single player, `DRAKAN_GAME=d1`), not as multiplayer matches.

A side note for harness users: firing the Ruined Village exit with `DRAKAN_LEVELCHANGE=2@600`, while the village's
opening scene still holds Rynn (`rv-exit2`), lands in Wartok with the player held (gameplay 0, the autopress then opens
and closes the pause menu every 3 s). Fired after the opening scene (`rv-exit3`) it is fine. A player cannot leave
during the opening scene, so this is a test-aid timing, not a game defect.

## 4. Not verified

* No bisect of the converter commits between 4ef9cb8 and 5dbdec2 was run: none was needed once the right name loaded.
* The walk from the village start to the lair ledge (Rynn was placed on the ledge; the scene, prompt and save were
  played with input).
* The exit boxes were not walked into on foot in Ruined Village: walking onto the Wartok exit's point (`rv-walk`)
  gives no touch, as expected (that exit fires from the awakening scene, not from a walk).
* Islands Night and Volcanic F8 were pressed inside cut scenes the walk set off; F8 outside a scene was not repeated
  there.
* Only one direct start per level; the arenas only as single-player starts.
