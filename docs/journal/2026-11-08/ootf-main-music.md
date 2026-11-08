# Order of the Flame main music (2026-11-08)

User report: "The ootf main music is also not playing."

## What the PC game plays where

Read from the user's install (`build/ootf/cab_disc`, counts and ids only):

* `Music.rrc` = 11 DirectMusic segments (type 0x401, ids 1..11, RIFF `DMSG`) and 11 DLS Level 1 banks (ids 256..266, segment
  k uses bank 255 + k, matched by the band's `DMRF` GUID = the bank's `dlid`). Segment names: 1 Drakan1Test, 2 SubduedV10,
  3 NUORDER1, 4 **gui**, 5 ord-sub, 6 Dark Union, 7 Order Corrupted1, 8 nitetime1, 9 Industrial4, 10 dsub3, 11 ord-sub (same
  bytes as 5). Every segment is a plain sequence: tempo (`tetr`), time signature, one band (`DMBT`) and a sequence track
  (`seqt`: notes with durations, pitch bends, CC 7 / 10 / 11 / 64 / RPN and pitch bend curves). No style, chord or motif
  tracks. The banks hold mono MS-ADPCM (mostly) and 16-bit PCM waves with `art1` articulations (EG1, LFO, velocity to
  attack). The 4-byte records 4097..4107 are per segment values (not used).
* `Common/Interface/Interface.odb`, class "User Interface Properties": `Background Music ID` = **4** (the title menu),
  `Credits Music ID` = 7.
* The cut scenes play music by id (PC sequence action 8): **the Intro's scene 452 starts music 4 at 0.2 s**, Ruin Book
  plays 11, Ruined Village scene 381 plays 6 (390 stops it), the Volcano ending plays 7.
* Level Properties (engine 54, never placed; the first class of that engine in dependency order) `Initial Music ID`:
  Ruined Village 8, Wartok 1, Grotto 1, Grimstone Mines 9, Rift Crystal 7, Succubus Lair 10, Rift World 6, the Intro, Book,
  Islands and Volcano 0 (arenas: 0, 6 or 7).
* Music Trigger objects (37) carry `Music 1 ID` / `Music 2 ID` and no links: they are region triggers (two sides, like the PS2
  "Music Ambient Trans"), not detector targets.

So the "main music" is segment 4 ("gui"): on the PC it plays on the title menu and again from the start of the Intro.

## Root cause

Nothing of the PC music reached the pack. The converter had no DirectMusic renderer: it mapped PC music ids to a palette
of Drakan 2 tracks (`MUSIC_PALETTES`) for the Music Trigger objects only. The `PS2MUSIC.ODB rebuilt with 5 entry patch(es)`
line in the user's log is class records, not tracks; no `new/PS2MUSIC.SDB/` entry existed in `ootf-levels.zip`. In detail:

* the title menu always played the disc's track (PS2MUSIC.SDB 196 "SeaHigh GUI", INTERFCE.ODB class 1 field 58
  `Background Music File` = 0x100c4, db 1 of INTERFCE.DB = PS2 Music), whichever game was chosen;
* the Intro's music action had no `musicMap` in the sequence converter, so it was dropped ("music 0x6: no compatible
  resource mapping" for Ruined Village);
* no level start music: `Initial Music ID` was a `hook` in `LEVEL_PROPERTY_RULES` with no implementation;
* the trigger objects have no detector links after conversion, so they never fire.

## Fix

* `editor/packages/formats/src/ootfdmusic.ts`: RIFF / DLS / DMSG parsers, an MS-ADPCM decoder and an offline DLS1 synth
  (EG1 attack / decay / sustain / release with velocity to attack, LFO vibrato and CC1, velocity (v/127)^2, CC7 / CC11
  (v/127)^2, pitch wheel with RPN 0 range, sustain pedal, linear interpolation at 44.1 kHz, a light Freeverb-style room as
  DirectMusic's synth reverberates by default, windowed-sinc decimation to 22050 Hz mono). `renderOotfTrack` turns a
  segment into a PS2 track: the intro (play start to loop start) once, then the loop body repeated until at least 120 s,
  the release tails past the end folded onto the start (the PS2 streamer restarts a looping track at sample 0, so the intro
  comes back every pass; DirectMusic loops only the body). Loudness is normalised (RMS 0.13, peak 0.89; descriptor volume
  -100 mB). `buildOotfMusicPack` writes `new/PS2MUSIC.SDB/0302|0304_<id>_44.bin` (group 44 = the disc's GUI group, flags
  0xa, class 0x99), ids from the free ids of the disc's PS2MUSIC.SDB (PAL: 1..85 are free), identical segments share one
  track (11 -> 5). Render time about 15 s for all 11; about 29 MB of ADPCM.
* `ootfpipeline.ts`: stage `music tracks` (reads `Music.rrc` and the interface class), `musicMap` for the sequence converter,
  music triggers use the rendered ids (palette only as fallback),
  and `addLevelStartMusic` places a Triggered Music Loop with Start State 1 at Rynn for `Initial Music ID`.
* Title menu: manifest key `campaign_title_music = <id>` (editor-core `ModMeta.campaignTitleMusic`, written by
  `ootf-convert.ts --campaign 1` and the editor's converter). Runtime `hle::CampaignMusicRedirect` +
  `ResourceService::setIdRedirect`: while the campaign is the current game, PS2MUSIC.SDB 0x302 / 0x304 / 0x305 requests for
  196 are served from the pack's id. The decision is latched at the descriptor request so one play never mixes the two
  tracks (they differ in length), with one exception for `game = ask` (below).

A sequence's music ref names PS2 Music by the **level's** dependency index, not the SSD stem's `.DB` (sounds use the `.DB`).
Disc evidence: the only music actions on the disc are LEVEL0RO.SSD `0x70059`, LEVEL2RE `0x120067`, LEVEL3ON `0x110073`,
LEVEL8OS `0x1300ac`; PS2 Music is dependency 7 of LEVEL0RO.LVL, 18 of LEVEL2RE.LVL (LEVEL1NA: 14), while LEVEL0RO.DB does
not list PS2 Music at all. A first conversion with LEVEL1ON.DB's 8 made the Intro start set 8 (`[play] id=80004`) and no
music; with 14 the game streams the track.

## Verification

Runs with audio on, `DRAKAN_SND_DBG=1`, `DRAKAN_HLE_LOG`, `DRAKAN_AUDIO_DUMP` (48 kHz mix), private packs under
`build/ootf-run/music*/` (nothing in `dist/`). The dump is matched against the converter's own render of the track
(normalised cross-correlation over 2 s windows, other game sounds mixed in):

* **Title, `game = d1`** (Intro pack): `[game] campaign 'ootf': title music PS2MUSIC.SDB 4 (disc 196)`,
  `[hle] music: title track 196 -> 4 (campaign)` at the main menu, then `0302/196` and `0304/196 +0` served from id 4. The dump
  matches the start of the rendered "gui" theme at ncc 0.91 (0.2 s window) until New Game.
* **Intro cut scene**: scene 452's action plays `id=e0004` (set 14 = PS2 Music, id 4), `0302/4` and `0304/4` are requested,
  the instance fades in over 0.6 s (`[setgain] id=e0004` 0.04 -> 0.69). From dump second 22.3 to the end of the 100 s run every
  2 s window matches the theme at ncc 0.85 to 0.96 at offset 22.34 s or at a whole loop pass (38.4 s) from it: the track
  streams past its first chunk and loops as rendered.
* **Ruined Village start** (`Initial Music ID` 8): `0302/8`, `0304/8 +0`, `+163840` requested after the level load, the dump
  follows "nitetime1" from second 19 at a constant offset (ncc 0.5 to 0.8). Scene 381's music 6 is requested too (not
  audible in that window).
* **`game = ask`**: the title starts its music at the main menu (`gui screen 30 -> 8`), *before* the classic game list is
  answered (`SelectGame` 1.4 s later), and loops by re-reading `0304/196 +0` without a new descriptor. A strict latch would
  keep the Drakan 2 track for the whole title visit, so a play latched to the disc track switches to the campaign's at its
  next chunk (at most 6.5 s); the guest keeps the disc descriptor's length for that play (61 s, then the OotF track restarts
  from its beginning) and the next descriptor read plays the full track. `ResourceService` serves the original entry when the
  target is too short for a request.
* Tests: `npm test` (editor, 1156 + 8 new in `ootfdmusic.test.ts`: MS-ADPCM, DLS / segment parsing, note pitch and timing,
  intro / loop passes and tail folding, PS2MUSIC entries, id allocation and sharing, level start object, trigger ids), ctest
  `override_test` (`CampaignMusicRedirect`: disc game, campaign, latch, switch during a play, other files / types untouched).

## Open

* The Music Trigger regions are still not converted to PS2 transition planes (Surdana uses FX "AmbientTransition" objects
  with Front / Back exit messages to its Triggered Music Loops); in-level music changes therefore do not happen yet, only the
  level start music and the cut scene music.
* The PS2 loop restarts at the intro; the PC loops only the body.
* The synth has no EG2 / filter (DLS1 has none), no pan (mono output) and an approximate reverb; nobody has compared the
  render with the PC game by ear.
