# Subtitles: what the game has, how it works, what to add

Research 2026-10-17 (SCES_500.06 PAL, English/French/German trees of the disc). Everything below was measured on the disc data and the
Ghidra project (`tools/ghidra_proj/Drakan`); the runtime checks were done with a scratch copy of the merged runtime
(`build/research/ws_build`, nothing under `tools/` or `runtime/` was touched). Tools: `analysis/scripts/sub_sdb_text.py`, `sub_export.py`,
`sub_adpcm_wav.py`; proposed code in `patches/proposed/` (see section 7).

## 1. Verdict (short)

| Speech | Text on the disc? | Game displays it? | Our action |
|---|---|---|---|
| NPC / Arokh / Rynn dialogue in the levels (1151 English lines, ~105 min of audio; 1229 French, 1236 German) | **Yes**, stored inside the sound descriptor of every voice sample (SDB type 0x302), localised per language tree | **Yes**, the game has a subtitle renderer and an options toggle **"Show Subtitles"** (Options > Audio), default **on** | Nothing is required to get them (see 4 for the one thing to verify); optional: our own nicer overlay (section 6) |
| Intro narration (123 s) and outro narration (84 s) | **No** (descriptor flag has no text; the string does not exist anywhere on the disc: a full-disc scan for "held strong" finds nothing) | No | **Our own subtitle overlay + transcript files**: transcripts were produced offline with Whisper (`patches/proposed/subtitles/intro_en.srt`, `outro_en.srt`), section 5/6 |
| FMV movies (logo, intro/outro book movies) | none; the VDB files carry video only, the soundtrack is an ordinary SDB sound ("D2 intro audio01" = LEVEL0RO.SDB 0x304 id 99 sub 14, "D2 Outro Audio01" = id 100 sub 14) | No | same overlay, triggered by those two ids |
| Grunts / SFX / "Arokh I need you" style barks (flag set but empty string, ~100 per language) | empty | nothing to show | none |

So the question "does the game have subtitles" has the answer **yes for gameplay speech, no for the narration**. The English cutscene narration was never
subtitled on the PS2 either.

## 2. The "Show Subtitles" option (menu side)

* Text: `INTERFCE.XDB` id **136** "Show Subtitles" (id 137 "Stereo Sound", 134/135 volumes). Audio screen = GUI screen 0xd, layout `0x2350` in `ENGLISH/DRAKAN2.RRC`,
  widget **0x2353** = the check box (widget 0x2354 = Stereo). Property names in the ODB schema (`0x505388` table): index 121 "Sound Options - Subtitles" (-> XDB 136), index 35 **"Subtitle Font"**.
* State: one guest word, **`0x4fb740`** (non zero = on). It is one row of the settings table at `0x54ba18` (`{"Subtitles", &0x4fb740, kind 1}`; the same table
  holds Stereo `0x4fb744`, SoundVolume `0x4fb75c`, MusicVolume `0x4fb760`, ...), so it is saved with "Save Settings" on the memory card.
  Default setter `FUN_00441198` ("Default Settings") writes 1 (and Stereo = 1, volumes 100/80); the Audio screen reads/writes it in `FUN_00441258` (open) and `FUN_00441440` (toggle).
* **Runtime check** (watchdog `PS2X_PEEK=4fb740`): value is **1** during the title menu and in gameplay of the shipped build; so subtitles are enabled without any user action.
* The pause-menu "Options" and the title-menu "Options" both lead to that Audio screen, so no extra UI is needed. A "PC Settings" overlay switch is just a poke of that word
  (`rdram[0x4fb740] = 0/1`, or call nothing: the game reads it every frame).

## 3. How a spoken line becomes a subtitle (code)

```
SDB sound descriptor (0x302)  --text-->  FUN_004053f0 (speech controller)  -->  FUN_004149a0 SetSubtitle(0x5102b0, text, speakerId)
                                                                                      |  word wrap into <= 4 buffers of 0x400 bytes
                                          GUI pass FUN_002cc8e0  --(0x4fb740 != 0)-->  FUN_003afd18 Subtitle_Draw(0x5102b0)
```

### 3.1 Data format (SDB type 0x302, verified on all 45 English/French/German SDBs, `sub_sdb_text.py`)
```
u16 namelen ; char name[namelen]            e.g. "C404NM05-A", "001AR05"  (NPC id + line; level 0 uses 001RY.. = Rynn, 001AR.. = Arokh)
base = 2 + namelen
+0  u32 flags      bit 0x2000 = descriptor carries a subtitle string (values 0x2002, 0x2012)
+4  u32 0x00100001
+8  u32 sample rate (0x5622 = 22050)    +12 s32 detune   +20 f32 15.0 (dialogue) / 20.0 (sfx)   +28 u32 0x55
+32 u16 text length (bytes incl. NUL padding), +34 text (cp1252, NUL padded)    (no text: u32 2 follows instead)
```
The audio of the same resource id/sub is type 0x304 (SPU-ADPCM, 22050 Hz mono, `sub_adpcm_wav.py` decodes it), type 0x305 is the **lip-sync track** of a speech sample (25 entries/s of 4-bit mouth shapes, see `docs/reference/sound-debug.md`).
Key: **resource (id, sub) of the sound == the line**; there is no separate dialogue table. Text per language lives in that language's SDB
(`FRENCH\LEVELS\LEVEL1ON\LEVEL1ON.SDB` ...), so `language =` in drakan.ini switches subtitle language together with the voices (the German/French voices are different recordings).

Counts (lines with non empty text): English 1151 (LEVEL1ON 474, LEVEL4ON 229, LEVEL6EN 134, LEVEL3ON 121, LEVEL8OS 85, LEVEL2RE 72, LEVEL5NE 19, LEVEL0RO 16, NPCS 1),
French 1229, German 1236 (+ SYSTEM.SDB Arokh barks). The English tree has ~80 voiced lines whose string is empty although French/German have text (e.g. LEVEL8OS 85 vs 100 lines).
Longest string 175 characters, average 76. 39 lines contain page breaks.

**Timed pages**: a line may contain `\<seconds>`: `"Have you heard? \.9 They say something's been murdering the Brothers ..."`. Page 1 is shown immediately, the next page `<seconds>` later
(`FUN_004053f0` skips the digits/dot after the backslash (token string at `0x54c6a8` = `\`), stores `now + seconds` in `controller+0x120` and the page index in `+0x124`; `FUN_004051e0` shows the next page when the game clock passes `+0x120`).
This is the only timing information; there is no per-word timing.

### 3.2 The functions
| Address | Role |
|---|---|
| `FUN_004051e0` (0x4051e0) | per-frame tick of the speaking entity's "speech controller" (sound handle at +0x128, speaker id at +0x130, page at +0x124/+0x120). Asks the sound system (`*(engine+0x32c)`, vslot +0x1c) for the handle state; state 3 = loaded/playing -> `FUN_004053f0`. |
| `FUN_004053f0` (0x4053f0) | `soundSys->vslot 0x128(handle, buf 0x5b5d20, 0x2800)` copies the descriptor text into the buffer, splits at `\` and calls `SetSubtitle` for page N |
| `FUN_004149a0` (0x4149a0) | **`SetSubtitle(Subtitle* 0x5102b0, const char* text, int speakerId)`**: clears the 4 line buffers, word-wraps with the "Subtitle Font" (`FUN_00288240/002883b8`, width `*(u32*)0x5102b4` = 0x15e = 350 px, max 2 display lines `+8`), sets state `+0x10 = 0`, speaker `+0x14` |
| `FUN_00414928` (0x414928) | `ClearSubtitle(Subtitle*)`: line count `+0x20 = 0`, empties buffers (called when the sound ends: `FUN_00406440`, `FUN_003b1ed8`, `FUN_003b21d0`, ...) |
| `FUN_003afd18` (0x3afd18) | `Subtitle_Draw`: fade state machine (+0x10: 0 start, 1 shown (+0xc = 2.5 s alpha timer), 2 fading, 3 hidden), a **full screen width black bar** `FUN_002c9eb8(0, y, screenW, h, rgba)` of height `lines * fontH + 2*DAT_0054cda0`, then each line centred with `FUN_00286170` |
| `FUN_002cc8e0` (0x2cc8e0) | GUI render/pop pass: **`if (*(u32*)0x4fb740 != 0 && (no screen || screen id not in {4,5,0xe,0x1a})) FUN_003afd18(0x5102b0)`** = the only place the option is consulted for drawing (screens 4 load, 5 save, 0xe new game, 0x1a are hidden) |

Subtitle object `0x5102b0` = sound manager (`0x510150`) + 0x160: `+0 font, +4 wrap width (350), +8 max lines (2), +0xc timer, +0x10 state, +0x14 speaker, +0x20 line count, +0x24 + k*0x400 line text`.
Screen size used for the bar/centring: `*(u32*)(*(u32*)0x5565c8 + 0xf8/0xfc)` = 512 x 512 (peeked), i.e. the bar is drawn in the 512x512 frame buffer and appears with the 4:3 stretch like all 2D.

## 4. Runtime status in our port

* Option word is 1 at runtime (peek). The draw path is plain EE code + GS sprites, nothing PS2-hardware specific, so it should work; the text fetch (`vslot 0x128`) is a call into the
  already-working sound manager (descriptors are in guest memory after the level SDB descriptors are loaded by the RSRCIOP HLE).
* **Test aid used** (scratch build only, see `patches/proposed/ws_anamorphic_projection.patch`, function `testSubtitleTick`): `DRAKAN_TEST_SUB="text"` calls the game's own
  `SetSubtitle(0x5102b0, text, 0)` every N camera updates. **Result: the game's own subtitles work in the port.** In a run to Level 0 (`build/screenshots/ws_16x9_level0_subtitle.png`) the real line Arokh says after the intro,
  "The Wartoks did their work all too well.  You are the only one left." (`001AR03-B`, LEVEL0RO.SDB id 71), is drawn by `FUN_003afd18` as the dark bottom band with the centred subtitle text,
  with no change to the game or the runtime. (The test aid output `[drakan] test subtitle queued` also appeared; the real line replaced it.)
* Still to verify by playing: other languages (French/German text) and multi-page lines (`\<seconds>`); `DRAKAN_TRACE_FN=4149a0,414928,3afd18 DRAKAN_TRACE_N=400` logs the calls.
* So if the user saw no subtitles, the cause is the intro/outro narration (no text exists) or an old saved settings file with "Show Subtitles" off (Options > Audio).

## 5. Narration (no text on the disc): offline transcription

Workflow (all local; nothing leaves the machine; ~2 minutes of CPU for both movies with `base.en`):
1. Decode the soundtrack: `python -I analysis/scripts/sub_adpcm_wav.py <ENGLISH>/LEVELS/LEVEL0RO/LEVEL0RO.SDB 99 14 intro.wav` (id 100 sub 14 for the outro). 22050 Hz mono, 123.3 s / 84.0 s.
   (The soundtrack contains music + narration; Whisper copes.)
2. `pip install faster-whisper` in a venv, then transcribe the 16 kHz float array with `WhisperModel("base.en", compute_type="int8").transcribe(audio, language="en")`
   (segments with start/end seconds -> SRT). Note: with the current `av` wheel `faster_whisper.decode_audio` crashes (`metadata_errors`), so feed a numpy array (see `patches/proposed/subtitles/README.txt`).
3. Proofread proper names: Whisper wrote "Braggon/Rin/Arok/Sadned"; corrected by hand in `intro_en.srt`. The outro contains names that cannot be verified from the disc text
   and are marked `[NAME CHECK: ...]` (Gisad?, Sadhana?, Malashay?; "Myschala" occurs 19x in the dialogue). Other languages: the French/German movies use different soundtrack recordings
   (`FRENCH\LEVELS\LEVEL0RO\LEVEL0RO.SDB` id 99/100), run the same tool on them (`whisper` multilingual model, `language="fr"/"de"`), then translate/proofread.
4. Result (English intro): 17 cues covering the full 123 s. The transcripts are generated locally and are not committed.

Italian/Spanish trees contain 30 SDB files each too; the same scripts apply, the dialogue strings are expected to be present exactly as for French/German (not exported here).

## 6. Design: PC subtitle overlay (narration now, optional replacement for the game's bar later)

Why an overlay: the game's own bar is drawn into the 512x512 frame (stretched by the 4:3 -> 16:9 present, full width black band, fixed 350 px wrap, 2 lines, English font from the
`Subtitle Font` property) and cannot show anything for the narration. An ImGui overlay draws at final resolution inside gsr's overlay callback (`gsr::Renderer::setOverlayCallback`,
already used by `runtime/settings`), is aspect/DPI independent and needs no guest changes.

Components (all new, none exists yet):
1. **SRT loader**: `overrides/subtitles/<lang>/<name>.srt` next to the exe (user supplied; the repo ships none because transcripts are game text). Cue = `{start_ms, end_ms, text}`.
2. **Trigger** (guest side): the sound request for the soundtrack. Three equivalent hook points, cheapest first:
   * `hle::ResourceService` request log line already contains it: `[hle] resource: ENGLISH\LEVELS\LEVEL0RO\LEVEL0RO.SDB 0304/99/14 +0 len ...`. Add an observer callback in
     `ResourceService` (`onRequest(const ResourceId&, offset)`) and start the track when `type==0x304 && id==99/100 && sub==14 && offset==0`. The first request of the stream = audible start
     minus a few ms of IOP latency; wall clock afterwards. **Needs a runtime/hle change (owners)**.
   * Movie player: `FUN_0016ffc8` family / the `Movie Player` object ("Sound Track" property); hook entry of the movie start (needs a short Ghidra look, not done).
   * SPU2 KON of the voice that plays core-1 voice 23 (see RECOMP_STATUS 4c); less precise.
3. **Clock**: `steady_clock` from the trigger; pause with the settings overlay (`isOpenAtomic`) and with the game's pause (input module `menu` flag); stop on screen change / movie end
   (`FUN_00440610` movie-done thunk is already hooked in `drakan_overrides.cpp`, `hookMovieDone`).
4. **Draw** (ImGui, in the overlay callback): bottom-centre, max width 70 % of the output width (not of the stretched frame), semi-transparent black rounded box, outline text,
   size from `settings_ui_scale`; fade 150 ms. In 16:9/21:9 anamorphic mode the overlay is already at output resolution so it is not stretched.
5. **Optional later**: hide the game's own bar and draw dialogue ourselves: hook `FUN_004149a0` (SetSubtitle: `a1` = text pointer in guest RAM, `a2` = speaker id) to capture the string
   (it also receives the already split pages and the `\<seconds>` markers are consumed before the call), and make `FUN_003afd18` a no-op (replaceFunction) while the overlay is enabled.
   Benefits: font size/outline, no 350 px wrap, no stretch, speaker names via the line name `001AR05` (needs the 0x302 name; `FUN_004149a0` only gets the speaker id).

Settings keys proposed for `drakan.ini`: `[subtitles] game = 1` (writes the word `0x4fb740` at boot, i.e. forces the game option on even if an old save has it off),
`narration = 1` (our SRT overlay), `size = auto`, `language = auto` (= `[general] language`).

Ready-to-apply code: `patches/proposed/subtitle_overlay.cpp` (SRT parser, timeline, ImGui draw function, `startTrack(name)`; compiles standalone with `-DSUBTITLE_TEST`)
and the hook snippet in `patches/proposed/subtitle_hooks.md`.

## 7. Files delivered
| File | Purpose |
|---|---|
| `analysis/scripts/sub_sdb_text.py` | parse 0x302 descriptors, dump `id|sub|name|text` |
| `analysis/scripts/sub_export.py` | export a language tree to TSV (`build/research/subs_ENGLISH.tsv` etc, 1151/1229/1236 lines) |
| `analysis/scripts/sub_adpcm_wav.py` | SPU-ADPCM sound -> WAV |
| `patches/proposed/subtitles/intro_en.srt`, `outro_en.srt` | Whisper transcripts (proofread, names marked) |
| `patches/proposed/subtitle_overlay.cpp`, `subtitle_hooks.md` | overlay code + hook points |
| `patches/proposed/ws_anamorphic_projection.patch` | also contains the `DRAKAN_TEST_SUB` test aid |

## 8. Implemented: narration subtitles (2026-10-17)
* Files shipped in `DrakanPC/subtitles/`: `intro_english.srt`, `outro_english.srt` (`<intro|outro>_<[general] language>.srt`; other languages are simply not shown until a file with that name exists). They are untracked game text: the persistent source is the gitignored `local/subtitles/`, which `Build-LocalGame.ps1` copies into every play folder (a build used to rely on a hand-staged `dist/.../subtitles` surviving the clean).
* Trigger: `hle::ResourceService::setRequestObserver` (new: called for every located resource request on the 0x1202 worker, before the payload is read) -> `drakan_subtitles::onResourceRequest`:
  first request (offset 0) of SDB type 0x304, sub 14, id 99 / 100 in `LEVEL0RO.SDB`. The intro "movie" is the in-engine book scene (screen 29), the trigger happens ~0.2 s before the voice is audible
  (measured on an audio dump: request at audio time 19.77 s, voice onset 19.97-20.0 s), so the cues were shifted by `DRAKAN_SUB_OFFSET_MS` (default 200; **replaced in section 9 by the voice key-on, default 0**).
* (superseded by section 9: cue time 0 is now the key-on of the stream voice) Clock: `spu2::AudioOutput::framesRendered()` (48 kHz frames the SPU2 was actually rendered for), so the text freezes with the audio (F1 menu, pause, stall watchdog) and cannot drift.
  Stopped by the movie-done hook and 1.5 s after the last cue.
* Draw: `SettingsOverlay::setHudLayer` (the overlay now runs an ImGui frame for the HUD layer even when the menu is closed): full-width translucent band at the bottom like the game's own bar, centred
  white text with shadow, 150 ms fades, size = `[subtitles] size` (auto = window height). Respects the game's own "Show Subtitles" (guest word `0x4fb740`) and `[subtitles] narration = 0`.
* `[NAME CHECK]` resolved from the game text: **Gisad = Jasaad** (Jasaad Duthane, Overlord of the Desert Lords: "Stratos is the center of Jasaad's power"), **Sadhana = Surdana** (city of
  Lady Myschala, `001AR13-A`, `102MS01`), **Malashay = Mala-Shae** (the Dragon Mother, `102MS06-C`, `C109MS26-A`). All marks removed from the shipped text.
* Screenshots: `build/screenshots/r3_subtitle_intro_*.png`.

## 9. Retiming of the narration subtitles (2026-10-17, branch `drakan-snd`)

User question: "there seems to be a large space between each sentence. Is this valid?"

**Measured.** Word-level Whisper timestamps (`small.en`, `analysis/scripts/sub_whisper_words.py`) on the decoded tracks, cross-checked against an audio dump of
a real run, show that the narration really does pause between sentences, but only for **0.7 - 1.6 s** (intro, end -> next start: 16.7 -> 17.8, 26.5 -> 27.2,
33.2 -> 34.7, 41.5 -> 42.3, 46.2 -> 47.2, 56.3 -> 57.3, 60.8 -> 62.2, 69.7 -> 70.6, 75.1 -> 76.0, 107.0 -> 107.9, 113.8 -> 115.1 s). The pace is slow
(~2.5 words/s; the first word is at 5.9 s, the outro's at 1.0 s). The gaps the user saw came from our SRT, not from the audio:

* The old cues were the segment boundaries of the first Whisper pass (`base.en`): a cue = a whole segment of two sentences, shown for up to 10 s as 3-4 lines;
  cue 1 started at 0.0 s although the first word is spoken at 5.9 s (6 s of text before any speech), later cues started early (the previous segment's end)
  and ended late. Text and speech were out of step by up to several seconds, so the pauses looked like holes in the middle of a long block.
* The runtime clock was started at the soundtrack's resource request plus a fixed 200 ms; the voice actually starts 30 ms .. 0.6 s after the request
  (streaming load time: 15 - 110 ms in the runs measured here, 0.2 s in the earlier one), so the cues were also up to ~0.2 s off depending on the run.
* One text error: cue "Mankind, the fallen age, had begun." is "For mankind, the fallen age had begun."

**New SRTs** (`dist/DrakanPC/subtitles/intro_english.srt` 25 cues, `outro_english.srt` 15 cues; sources `patches/proposed/subtitles/`, generator `analysis/scripts/sub_retime.py`
with the Whisper word times `intro_words.json` / `outro_words.json`): one cue per phrase (2.1 - 7.5 s, <= 2-3 lines of <= 46 characters, max 20 characters/s), start = first word - 80 ms,
end = last word + 300 ms; a gap shorter than 1.5 s is bridged (a cue stays until the next one starts, as standard subtitle practice), minimum 1.2 s, no overlaps, so the
on-screen gap between cues is <= 0.04 s: the 0.7 - 1.6 s sentence pauses are covered by the previous sentence (they are real pauses in the speech, not missing text).

**Sync mechanism.** `drakan_subtitles` no longer starts the clock at the resource request. The first request (offset 0) of the soundtrack arms the track, the next SPU RAM upload of
>= 64 KiB is the stream's buffer (address noted), and cue time 0 is the SPU2 frame counter at the **key-on of the voice that starts at that address** (`SpuTap::setSwitch` ->
`drakan_subtitles::onVoiceKeyOn`). The track's sample 0 plays at that moment, which is exactly what the Whisper times refer to (the audible volume rises later: the game fades the
voice in over ~1 s, so correlating envelopes is misleading). Request -> key-on was 15, 30 and 110 ms in three runs; if no key-on is seen within 4 s the request time + 100 ms is used.
`DRAKAN_SUB_OFFSET_MS` (default 0) shifts the cues; `DRAKAN_SUB_LOG=1` prints every cue change with the audio clock.

**Verification** (`analysis/scripts/sub_verify_run.py`, runs `build/recomp_snd/t/intro13`): Whisper on the audio dump of a run vs the cue times logged by the same run: the cue appears
**-0.055 s** before its first heard word (median; 25-75 % = -0.07 .. -0.03 s; the 80 ms lead is intended; Whisper word starts are +-0.1 s), 20 of 24 matched cues within -0.2 .. +0.05 s
(the other 4 are late Whisper word starts of fade-in / soft first words, e.g. -0.5 s for the very first "There"). On-screen gaps between cues: max 0.04 s. Not done: a human watch of the whole intro, and the outro (needs the game's end).

**The game's own dialogue bar.** Checked with a test aid (`DRAKAN_TEST_SUB="text"` queues `SetSubtitle(0x5102b0, text, 0)` 45 s after sound start, `DRAKAN_TEST_SUB_AT=<s>`): the bar appears
at the bottom (full-width dark band, text centred, wrapped to 2 lines of the 350 px "Subtitle Font" width, drawn over the HUD orbs like on the PS2, no stretch at 4:3). Its state machine
(`FUN_003afd18`) stays in state 1 until the speech controller ends it, so a test line stays until `ClearSubtitle`; in real dialogue it follows the sound. No NPC dialogue was reachable in the
scripted runs (Rynn's idle "Look at nails" has no text), so the end-of-line clearing was not exercised; the earlier agent's real-line check (`001AR03-B`) still stands.
The option "Show Subtitles" (word 0x4fb740, default 1) gates both the game's bar and our narration overlay.
