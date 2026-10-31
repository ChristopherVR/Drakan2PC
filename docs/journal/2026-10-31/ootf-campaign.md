# Order of the Flame campaign layer: level flow, text, music (2026-10-31)

Code (all in `editor/packages/formats/src`, exported from `index.ts`; tests `test/ootfcampaign.test.ts`, synthetic, 16 tests):
`ootfcampaign.ts` (flow, titles, exit rewrite, shared databases), `ootftext.ts` (Dragon.rrc, XDB ids), `ootfmusic.ts` (PS2 tracks).
Tags: **[data]** measured on the user's install (`build/ootf/cab_disc`), **[inf]** inference, **[U]** untested in game.

## 1. Level flow

13 single-player levels (the 10 multiplayer maps and their `.lfl` lists are separate). Header name string ids (`<0x9000>` ... `<0x900b>`,
Dragon.rrc ids) give the authoring order **[data]**; the intro has none. `Interface.odb` class "User Interface Properties" names the
flow: `Intro Level Filename = MOUNTAIN WORLD\Intro Level\Intro.LVL`, `First Level Filename = ...\RUINED VILLAGE\RUINED VILLAGE.LVL` **[data]**.

| # | Level (world) | Exits (PC "Level Transition" class -> destination) |
| --- | --- | --- |
| 0 | Intro (Mountain) | none in the level; Interface.odb hands over to Ruined Village |
| 1 | Ruined Village | 1-2 Wartok Canyons; "1-Intro" (override) Ruined Village Book |
| 2 | Ruined Village Book (cut-scene level) | "1.5 - 1" (override) back to Ruined Village |
| 3 | Wartok Canyons | 1-2 Ruined Village; 2-3 Grimstone Mines |
| 4 | Grimstone Mines | 3-4 Grotto; 3-2 Wartok Canyons |
| 5 | Grotto | 4-5 Islands (Islands World); 4-3 Grimstone Mines |
| 6 | Islands (hub) | 4 to 5 Grotto; 5 to 6 Alwarren; 5 to 7 Rift Crystal |
| 7 | Alwarren | 6-5 Islands (dead end) |
| 8 | Rift Crystal | 7-8 Islands Night |
| 9 | Islands Night | 8 to 9 Succubus Lair |
| 10 | Succubus Lair | 5-8 Islands Night; 8-9 Volcanic |
| 11 | Volcanic (Volcanic World) | "from 9-10" (override) Rift World |
| 12 | Rift World | none: the ending (no outgoing exit) |

**The PC exit object carries no destination**: the destination is the class default of field `Transition To...` (path text such as
`islands world\islands\islands.lvl`; one exit class per destination) and an object may override it (3 do). 19 exits, 0 unresolvable
(case-insensitive match against the level paths) **[data]**; one class with the field but no text ("Exit" in Grimstone Mines) is ignored.
`Succubus Lair` is reachable from Islands Night; Rift Crystal and Alwarren return only to Islands/Night by one-way exits.

### How PS2 resolves a transition (world-model.md section 10, level-format.md 15.4)

* Exit field 0 = XDB string id of the **class's own database** (class default; `LEVEL1ON.XDB` for classes of `LEVEL1ON.ODB`, which is where
  LEVEL1NA's exit classes live: dependency 8). The text is `Levels\<name>\<name>.lvl` (no leading backslash); 12 disc objects override the field
  with a plain dword = id (length 0) **[data]**. The game then opens `\Levels\<name>\<name>.lvl`, i.e. the `LEVELLST.TXT` title.
* In the new level it looks for the exit whose own destination text equals the old level's name (marker; `Transition To 'portal'` is the
  alternative). None: Rynn keeps the old position and falls.

### Design (implemented)

* **Titles**: `OotF NN - <Name>` (NN = campaign index, 2 digits) so the 8.3 stems are unique (`OOTF00RO` ... `OOTF12LD`, real data: 13 distinct) and
  the splat files `OOTFNNTS.*` are unique too. Pass the title as the converted header name (`ConvertOptions.name`) and to `buildOotfPack`.
* **Registration**: `campaignRegistration(plan).levels` is the mod manifest `levels = [...]` (play order = LEVELLST order). The menu "campaign
  entry" is the first title, `OotF 00 - Intro`; there is no separate campaign concept in the runtime (a `campaign = { name, start }` manifest key and a
  menu entry that starts it would be the follow-up; not built).
* **Exit rewrite**: `applyCampaignExits(list, level, plan, ctx)` sets every converted exit to a plain string id of its destination title
  (`setExitDestinationId`, the disc's override form; other parameters kept) and **adds** exits (a) whose PC class was dropped, (b) the intro hand-over
  (`reason: intro-next`, at the intro's Rynn start), (c) arrival markers (`reason: arrival`) in every destination that has no exit back (Ruined Village
  needs one to the intro, Islands Night to Rift Crystal, Rift World to Volcanic, ...) at that level's Rynn start. Real data: all 13 levels, 7 added
  exits, 0 unreachable. **[U]** A marker at the Rynn start may bounce the player at once (the original markers are the exits and rely on the trigger's
  armed flag); use `CampaignOptions.arrivalOffset` to move them if so.
* **Strings**: `campaignExitStrings(plan, 'LEVEL1ON.XDB')` returns the new `0400` entries (ids `0x7000+i`, sub 1; ids on the disc end at 52) shipped as
  `new/LEVEL1ON.XDB/0400_<id>_1.bin`. English only (other slots fall back to English). The XDB file must be the XDB of the exit class's ODB stem.
* **Shared world databases**: `plan.shared` lists every dependency of the PC levels. Real data: engine-common `System`, `Resources`, `FX`, `NPCs` (13/13/13/11
  levels; mapped onto the PS2 databases of the same role, never converted as units), **world-shared** `Mountain World Common` (x4), `Mountain World Common TD` (x5),
  `Tropical Common` (x5), `Tropical World TD` (x5), and per-level databases (Volcano, Rift World, ...). The pipeline already keeps one asset
  container (`Common\OotFCom\OotFCom`) and one id map per pack, so a world-common asset is converted once **as long as all levels go through ONE
  `convertOotfLevels` call**; do not convert levels one pack at a time. No separate per-world container was added (container layout stays W1's).

## 2. Text

* `Dragon.rrc` type 0x400 (426 records): XOR with a fixed **32-byte key repeated from byte 0** (not id dependent), recovered statistically from the
  strings and verified (0 undecodable bytes; `Alwarren`, `Server Name:`, 162 item names ...). `RRC_STRING_KEY`, `decodeRrcString`,
  `parseRrcStrings(rrcReader)`. 425 distinct ids.
* What text exists in the levels **[data]**: item/object `DisplayName` (162 class defaults, 129 as `<0x850a>The Ugly Stick` refs into Dragon.rrc, 27 object
  overrides), `Message String` of Detector classes (36 class defaults, 11 overrides: the Rimril briefing, "The succubus tell you ...", hint pop-ups) and level
  names. **OotF has no conversation/dialogue classes, no subtitles and no Book text class** (voices are in the levels' `.sdb` without text; the "Ruined Village
  Book" is a cut-scene level with narration audio). So there is nothing to feed `story/conversation.ts`; sound-descriptor subtitles (flag 0x2000) would have to be
  authored.
* PS2: a string field is an XDB id (dword = id, length 0) into the XDB of the class's database; text is cp1252 with `\r\n` breaks. `TextPlan` allocates ids
  per XDB file (base `0x6000`, `reserve()` the target XDB's ids, equal texts share one id, `<0xNNNN>` resolved through the rrc map, literal `\n` -> `\r\n`,
  non-cp1252 characters -> ASCII with a warning); `entries()` are the `new/<FILE>.XDB/0400_<id>_<sub>.bin` changes (same shape as `StoryEntryChange`).
  `rewriteObjectTexts(list, plan, xdbFileOf)` turns literal text params of converted objects into ids; `rewriteClassTextDefaults(fields, data, plan, xdb)`
  does the same for a cloned class's defaults.
* **Gap**: `ootfclass.ts mergeDefaults` copies string defaults as literal words (wrong for PS2). It must call `rewriteClassTextDefaults(tpl.fields, out, plan,
  '<ODB stem>.XDB')` after the merge (owner: ootfclass / pipeline). Not wired because that file is another session's.

## 3. Music

* PC music = "Music Trigger" objects (37 objects, ids on 1..11) with two slots (`Music 1 ID` bed, `Music 2 ID` overlay) and fade times; ids are the
  DirectMusic segments in `Music.rrc` (ids 1..11, RIFF DMSG). **Correction to ootf-sound.md: the same file holds the DLS wave banks** (ids 256..266, RIFF DLS
  with `colh/ins/rgn/wsmp`, 0.4 to 1.2 MB each), so an offline render is feasible; the 4-byte records 4097.. are message stubs.
* PS2: `Triggered Music Loop` (PS2MUSIC.ODB class 20, runtime 0x10006): `Sounds` array of ids of PS2MUSIC.SDB, `Plays overlay music`, fades. Surdana places such
  objects with plain ids and links from detectors.
* **Result (shippable now)**: `MUSIC_PALETTES` maps each level and slot to PS2MUSIC tracks (20 candidates with sizes; world-appropriate groups: Mountain =
  Surdana / Goblin Forest / Northland, Islands = Sea Land and Floating Islands, Volcanic = Desert + dragons, Rift World = Jasad Duthane) and `trackFor(level, slot, pcId)`
  picks by `(id-1) mod n`. Real data: the 37 triggers use 13 distinct tracks. `applyMusicTriggers(list, findMusicTriggers(pc), pcPath, { musicClass })` converts the
  trigger object **in place** (slot 1 keeps id and detector links) and adds slot 2 as a new object that every linking detector also links to; both-0 triggers
  ("stop music") are skipped. Not heard in game **[U]** (the music class, message On = 1 / Off = 0 as defaults).
* Phase 2 (not done): render DMSG + DLS to 22050 Hz mono WAV (needs a DLS synth: parse `ins/rgn/wsmp/wvpl`, a segment sequencer for the style/pattern chunks), then
  `convertPcSounds` -> a mod SDB and the same objects pointing at the new ids. Effort: large; the palette is the stand-in.

## 4. Call order for the pipeline (convert.ts / ootfpipeline.ts owner)

```
load all single-player PcLevels (loadPcLevel)
plan   = planCampaign(pcLevels, { prefix: 'OotF' })               // order, titles, exits, string ids
textP  = new TextPlan({ rrc: await parseRrcStrings(Dragon.rrc) }); textP.reserve(xdb, existing ids)
convertOotfLevels(src, paths, ctx, { name: pc => campaignTitle(plan, pc.path) })   // ONE call for all levels (shared assets)
  classOverrides: map musicTriggerClassNames(pc) -> the "Triggered Music Loop" class (keeps ids/links)
  mergeDefaults: after the merge call rewriteClassTextDefaults(fields, data, textP, '<ODB>.XDB')
per level, on parts.objects after convertPcLevel:
  applyCampaignExits(list, planLevel, plan, { exitFieldOf, exitClass })     // exitClass: a "Level Transition from 1 to 2" class of the template, field 0
  applyMusicTriggers(list, findMusicTriggers(pc), pc.path, { musicClass })  // musicClass: PS2MUSIC.ODB class 20 with the template's dep index (14)
  rewriteObjectTexts(list, textP, o => xdbOf(o))                            // literal DisplayName / Message String params -> ids
buildOotfPack(meta, template, inputs{ title: planLevel.title }, ...)        // title == plan title, so registration == campaign order
pack files += campaignExitStrings(plan, 'LEVEL1ON.XDB') + textP.entries()    // as new/<FILE>.XDB/0400_<id>_<sub>.bin (buildStoryPackFiles)
```

Open: in-game test of the whole chain (exits, markers, music objects) was not run; the converted levels are still mostly empty terrain (ootf-ingame.md).
