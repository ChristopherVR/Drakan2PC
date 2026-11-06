# Issues 2 and 4: in-game checks left open

Follow-up of [issue-2-new-mod-packs.md](issue-2-new-mod-packs.md) and [issue-4-portable-editor.md](issue-4-portable-editor.md).
Every run used `dist/DrakanPC/drakan.exe` with a private ini, input ini, memory card, mods folder and log folder under
`build/harness/verify-2-4/<run>` (driver `build/harness/verify-2-4/drive.py`, which copies the dist ini files, sets
`game = ask`, `title_menu = modern`, windowed 960x540, and points `DRAKAN_INI`, `DRAKAN_MEMCARD_DIR`, `DRAKAN_MODS`,
`DRAKAN_LOG_DIR` and the shot folders there). German menus, as in the dist ini. Nothing in `dist/` was edited by hand.
All runs ended with a clean scripted quit (exit 0). Evidence stays private under `build/` (ignored).

Executables: the first round ran on SHA256 `613d0492...f56408d` (HEAD `e435da85`); after the fixes below, an
incremental `Build-LocalGame.ps1 -WorkDir build/final-game -Incremental -Jobs 8` deployed `861946f2...99d37`, which
ran every later check.

## Issue 2

### Test packs

Small folder packs under `build/harness/verify-2-4/packs/<scenario>`, each enabled (`on`) in its own `mods.ini`, plus an
undecided text pack `zz-new-notes = new` so the "New mod packs found" card opens (`DRAKAN_MODS_NOTICE=1`, since the
input is scripted). Recipe `note-recipe.txt`: wait for title screen 8, shot of the card, Down + Enter (Open Mods menu),
shot of the Mods page, Esc, shot of the title.

| Scenario | Pack | Loader | Title line (German) / log `[game]` line |
| --- | --- | --- | --- |
| skip-disc | `target.profile = "usa"`, campaign `discvalley` | skipped: targets disc profile usa | "Test Disc Campaign ist aktiviert, wurde aber nicht geladen ..." / "... Reason: targets disc profile usa but this build is pal (b9f60ba118125a54)." |
| skip-dep | `dependencies = ["no.such.core>=1.0"]` | skipped: needs 'no.such.core' | same pattern, reason "needs 'no.such.core', which is not installed, not enabled or was skipped" |
| skip-format | `format = 2` (a pack this runtime cannot read) | skipped: needs mod format 2 | same pattern, reason "needs mod format 2 (this runtime reads up to 1)" |
| skip-zip | a corrupt `test-zip.zip` | skipped: invalid pack: not a zip | no line (see below) |
| refuse-id | `campaign = "d1"` | loaded, campaign refused | "Test Bad Id Campaign ist geladen, aber seine Kampagne konnte nicht angeboten werden." / "Reason: campaign id 'd1' is not valid." |
| refuse-start | `campaign_start = "Nowhere Valley"`, no levels | loaded, campaign refused | same pattern, "Reason: campaign start level 'Nowhere Valley' is not one of its levels." |
| refuse-dup | the issue-4 OotF pack plus `test-dup` with `campaign = "ootf"` | loaded, `test-dup` refused | **no line** before the fix; after it "Test Duplicate Campaign is loaded, but its campaign could not be offered. Reason: campaign 'ootf' is already given by another pack." |

The Mods page (title PC Settings pages) showed the Game choice row in every scenario that has a title line.

### Defects found and fixed (commit `ec768238`)

1. **A refused duplicate campaign was not explained.** `gameChoiceNote` treated a loaded pack as "on the card" when its
   campaign id was on the card. A second pack with an id that is already taken shares the id, so the note said nothing
   (run `refuse-dup`: no title line, no `[game]` line), and the Mods page's Campaign row for `test-dup` claimed "On the
   title's Choose your game screen". `dsettings::campaignOffered` now also requires that the pack is not in the
   refused list; the note and the Mods page use it. Re-run `refuse-dup2`: the log line above, and the Mods page shows
   Game choice + Reason for Test Duplicate Campaign.
2. **The Mods page cut the reason off.** The Game choice row is an Info row: one line, not focusable, so the loader's
   reason ended in "Reason: c..." (`refuse-id/mods_page.png`). The reason now has its own **Reason** row
   (`refuse-dup2/mods_page.png`, `fallback/mods_page.png`). The log line keeps both in one line.
3. Tests: `mods_model_test` gains the duplicate case (note, `campaignOffered`, one "On the title" and one "Not
   offered" Campaign row) and checks the Reason row; the new checks fail on the old code (3 failures) and pass now.

### Open Mods menu fallback (F1 overlay)

`TitleConfig::pcSettingsPages` is `g_pcs != nullptr`, and `g_pcs` is created in the same overlay set-up that installs
the title layer, so the fallback is unreachable in the Windows build. New test aid `DRAKAN_DEBUG_TITLE_PC_PAGES=0`
(`env-flags.md`) clears it for the title. Run `fallback` (wrong-disc pack + new pack): the card's Open Mods menu logged
`title action OpenMods(0)` and `[mods] title notice: Mods page (overlay)`, `state.txt` reported the overlay open, and
`fallback/mods_page.png` shows the F1 "PC Settings" overlay opened directly on its Mods page with the New, Game choice
and Reason rows and the `[New]` pack. Pass. The scripted Esc afterwards did not close the overlay: script keys feed the
host screens, not the ImGui overlay (harness limit, not a defect; a real Esc / F1 closes it).

### Not covered / remarks

- A pack whose zip or manifest cannot be read (`skip-zip`) gives no title line: without a manifest the runtime cannot
  know it is a campaign. The Mods page still lists it as "Skipped: invalid pack: not a zip (too small)". Left as is.
- A pack listed `on` but missing from the folder has no manifest either, so it has no title line (Mods page only).
- Cosmetic: the title's note plate (about 1140 of 1920 units wide) covers the left part of the native art's copyright
  line and leaves "...ights reserved" visible to its right (`skip-disc/title_note.png`). Not changed.
- Only German title strings were seen; the line is translated, the Mods page and the log stay English.

## Issue 4

### Sound (two-level pack from `build/issue-4/play/mods`)

Runs `issue4-audio` and `issue4-audio-snd`: audio on (WASAPI shared, "Speakers (Realtek)", 48 kHz), the runtime's
mixed output written with `DRAKAN_AUDIO_DUMP` (what is sent to the device) and `DRAKAN_SND_LOG=1` voice snapshots every
1.5 s. Same recipe as issue 4 (card, Order of the Flame, New Game, Intro, automatic handoff).

- The pack mount reports `RESOURES.SDB rebuilt with 306 entry patch(es)`.
- `audio.wav`, 86 s: silent only for the first 6 s (before the logo), RMS -30.9 dBFS overall, peak 7727; 78 of 86
  seconds have a peak above 200.
- Intro (loaded at 26.97 s, voice clock about 7.6 s behind): every snapshot from load to the handoff has output
  (rms 0.011 to 0.040, peaks 2183 to 4348), two looping stream voices (`c0.v23`, `c1.v22`) and one-shot voices at the
  start of the Intro and around 55-58 s (`c1.v21` from SPU address 0x14e516 on).
- Ruined Village (loaded at 70.22 s): output continues (rms 0.011 to 0.027) with up to six voices at once
  (`c0.v17`, `c0.v22`, `c0.v23`, `c1.v18`, `c1.v19`, `c1.v21`), so its own sounds play, not only the music.

Pass: neither level is silent. Not checked: that each cinematic sound is the right one (nobody listened).

### Movement after the handoff

First round (`issue4-audio*`, recipe from issue 1: 3 s of W at 50 s after gameplay start, RAM before and after),
decoded with `settings_check.camera_state`: 1,125 and 1,238 units. But the end position was
`(267333.094, 20502.736, 205496.359)`, yaw 1.6569, in both runs and in the earlier issue-1 and issue-4 runs to the last
digit, while the start differed. A run with an idle pair (`full`, see below) showed why: Rynn moved 895 units between two
dumps 2 s apart **without input**, and ended on the same point after 1.5 s of W. That walk is the Ruined Village's
opening scripted move (the screenshot is the orc ambush scene), not player control. The earlier movement claims
(issue-1 `movement-report.json`, issue-4 first round) therefore did not prove control.

The real handoff to the player comes much later. Ruined Village opens with a long scripted scene (orc ambush,
captive villagers, close-ups of Rynn; no player control). Recipe `m4-walk.txt` / `m4-idle.txt` (RAM dumps from 140 to
174 s after gameplay start, W held 153-156 s and 171-174 s; times are after the `sync play` point, Village loads at
about +44 s):

| Run | Pack | Input | Result |
| --- | --- | --- | --- |
| `full3` | 23-level | none until +152.5 | scene ended between +145 and +150: Rynn at `(229075.2, 19257.6, 188356.8)`; idle 2 s: 7.4 units; W 1.5 s: 8,248; W 3 s total: 12,678 |
| `full4i` | 23-level | none | still in the scene at +174 (position unchanged since +140, a static forest view) |
| `full4w` | 23-level | W twice | W at +153 inside the scene: 0 units; scene ended between +165 and +170 (`(229061.2, 19257.6, 188357.7)`); W at +171: 8,247 in 1.5 s, 12,680 in 3 s |
| `issue4-move2` | two-level (issue 4) | W twice | same: 0 inside the scene; ended between +165 and +170 (`(229114.7, 19257.6, 188351.7)`); W: 8,249 in 1.5 s, 12,692 in 3 s |

Pass for both packs: after the scene Rynn stands still without input and runs about 5,500 units per second with W
(`full4w/s174.png`, `full3/village_walk.png`: Rynn in the open village). Measurements in each run's `measure.json`.

Open observation: the scene's end time varied between +145 and later than +174 (about 100 to over 130 s after the
Village load). A run without any input (`full4i`) had not ended it at +174, so it is not yet clear whether it ends on
its own every time or needs a key press. Worth a long idle run (several minutes) before calling it a defect.

## Check 3: full 23-level pack (`build/issue-4-full`)

`READY.txt` appeared during this work: `ootf-levels.zip`, SHA256 `6044fa9a...7d2cb1`, 141,886,218 bytes, made by the
CI-equivalent portable editor. Copied into `build/harness/verify-2-4/packs/full` (`on`), audio on.

- Mount: `loaded 'ootf-levels' ... 284 entries, 80 files, 10183 new entries, 23 levels`, the campaign `ootf` with
  New Game on `OotF 00 - Intro`, `RESOURES.SDB` patched.
- Game card (`full/game_card_ootf.png`): "Spiel auswählen" with Drakan and Drakan: Order of the Flame; picking it logs
  `[game] switched to ootf` and New Game opens the Intro (loaded at 28.66 s).
- Automatic handoff: `OotF 01 - Ruined Village` loaded at 72.03 s (43.4 s after the Intro), no null target.
- Sound (`full/audio.wav`, `DRAKAN_SND_LOG`): 88 s, -30.9 dBFS, 79 seconds with a peak above 200, Intro snapshots
  rms 0.017 to 0.037 with three voices, Village rms 0.010 to 0.028 with up to seven voices. The two-level run
  `issue4-move` (187 s) keeps a per-second peak of at least 1,496 over its last 80 s, through the scene and into play.
- Movement after the scene: see the table above (`full3`, `full4w`). Pass.

Not checked: levels after Ruined Village, saving and loading in the campaign, and the installer build.

## Follow-up

The open observation (Ruined Village scene end), the unreadable `skip-zip` pack and the note plate over the copyright
line are resolved in [issue-5-village-scene-and-title-notes.md](issue-5-village-scene-and-title-notes.md).
