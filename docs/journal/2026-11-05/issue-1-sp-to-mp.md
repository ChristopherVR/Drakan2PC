# Issue #1: singleplayer to multiplayer loading freeze

Source: [issue #1 follow-up](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6637936574).

## Result

The reported sequence did not reproduce in a canonical deployment run on 2026-11-05.
No production source change is justified by this investigation. This is one successful
Surdana host sequence, not a claim that every multiplayer loading freeze is resolved.

## Exact exercised path

Ran `dist/DrakanPC/drakan.exe` from its own play folder. SHA-256:
`30bc5fdb6a950877e4d296c4d92111dbdc6c9f1d32283e217b625adc57d4ee3c`.
No build or deployment was performed by this investigation. Output, private memory card,
native options, environment manifest, input recipe and summary remain under the ignored
`build/harness/issue1-sp-mp-original/`. Audio was disabled; language English, game D2,
D3D11 scale 2, background pause disabled. HD assets and installed mods came from the normal
play folder. The game settings/input files in that folder were read, not edited.

1. Boot to the title, choose native New Game in singleplayer. Normal Intro and Surdana
   loaded successfully; automatic Start/Cross taps skipped the introductory presentation.
   No `DRAKAN_START_LEVEL`, forced arena, network autohost or level-load call was used.
2. Wait under control in Surdana; capture `sp_ready.png`.
3. Open the modern pause menu, choose Quit, then Quit to Title. The log records the explicit
   modern Quit to Title and title session 2. Capture `title_returned.png`.
4. Through the title Multiplayer pages, Host Game with the first stock map (Surdana Meadow),
   Begin Game, then Start Match. Capture `host_form.png` and `lobby.png`.
5. Wait for normal native gameplay, capture `mp_ready.png`, hold W for two seconds,
   capture `mp_moved.png`, then script quit.

## Evidence

The lifecycle log distinguishes the original singleplayer load from the new arena load:

| Event | Lifecycle seconds | Outcome |
| --- | ---: | --- |
| SP Intro loaded | 21.384 | result 0 |
| SP Surdana loaded | 27.129 | result 0 |
| MP Loading / menu launch | 149.114 | direct Surdana map, no intro |
| MP native level-load entry | 149.327 | mapped Surdana, native Intro request path |
| MP native level-load return | 149.610 | result 0 |
| Server Playing | 149.615 | match entered Playing |

The active versus filter ran, the arena recorded 3309 actors, then spawned its pickups.
No arena first-pass processing was observed before the new native level-load entry.
`mp_ready.png` was visually inspected: gameplay, character and running match HUD at 9:54.
Local position progressed from X/Z 450761.75/340174.81 at 153.4582 seconds to
446037.84/338665.88 at 168.1109 seconds, a horizontal displacement of 4959.05 units.
The script reached its quit at frame 32385, renderer shutdown and final log flush marker;
PID 42308 no longer existed. The launcher did not retain the process exit status, so an
exit code is not asserted.

## Remaining limits

This run used a fresh private card and hosted a one-player versus match after a new SP
session. It did not test a loaded save, OotF, joining a remote host, other maps, other
languages, or repeated SP/MP cycles. The deployed binary predates the separate same-day
source changes; integrated deployment acceptance remains separate. Earlier campaign-free
arena filtering already landed on November 4, as recorded in
`../2026-11-04/arena-preinit-and-avatar-shadows.md`; the successful run does not identify
which earlier change, if any, explains the difference from the report.

## Integrated validation checkpoint

`scripts/harness/sp_mp_transition.py` now preserves the original successful native
New Game -> modern Quit to Title -> multiplayer host sequence as a maintained,
direct-`dist/DrakanPC` harness. It supports the three versus arenas, co-op with default
Rynn or the first NPC selected through the lobby, and the five original-disc languages.
Audio is enabled and installed play-folder content/settings are retained; the native
options file and fresh memory card are private per run. Environment overrides are
recorded after clearing inherited game/renderer overrides.

The harness defaults to a dry-run manifest. No integrated game was launched at this
checkpoint because the canonical deployment and exclusive live slot belong to the
coordinating task. Ruff check/format and English/default plus German/NPC dry runs pass.
This adds reproduction coverage, not a production fix or a new acceptance result.

After deployment, run these separately with the exclusive live slot (each is bounded
to 300 seconds plus dump capture, and needs a new output name on repetition):

```powershell
python scripts/harness/sp_mp_transition.py --name issue1-integrated-en-rynn --run
python scripts/harness/sp_mp_transition.py --name issue1-integrated-de-npc --language german --coop --npc --run
python scripts/harness/sp_mp_transition.py --name issue1-integrated-en-inquisitor --arena 2 --run
```

Acceptance additionally requires inspection of `sp_ready.png`, `mp_ready.png`,
`mp_moved.png`, native level-load entry/return, and movement in `ram_before` / `ram_after`.
The summary explicitly leaves that review pending: continuing network packets do not
establish gameplay progress. On timeout the harness writes `timeout-full.dmp` containing
guest RAM and host thread contexts before killing only its owned PID. A capture failure
leaves that process alive for manual inspection. Full process dumps and RAM/screenshots
remain ignored game-derived artifacts under `build/harness/`.

Remaining coverage includes loaded saves, OotF and a remote join; no current source
evidence justifies changing the SP-to-MP lifecycle itself.

The source-ready harness was corrected before any integrated launch to isolate all
four settings files: main, input, native options and multiplayer profile. Main settings
are copied with modern title/pause/save menus explicitly selected to match the recipe;
input settings are copied unchanged. The private multiplayer profile starts at model 0,
character 0 (Rynn), so the installed player's persisted NPC cannot contaminate default
coverage. The NPC variation changes that baseline through the co-op lobby. The new
`DRAKAN_MULTIPLAYER_INI` runtime override is required by this harness. All installed INI
hashes must remain unchanged. The automated summary now also requires no guest
missing-target/stall, both valid PAL guest RAM samples, and more than 100 horizontal
units of native player displacement; screenshot/lifecycle review remains required.
`PS2X_WATCHDOG_DUMP_DIR` requests immediate runtime RAM/thread snapshots on a detected
silent-input stall, in addition to the harness's full-process timeout capture.

## Integrated English/default-Rynn run

`build/harness/issue1-integrated-en-rynn` completed on executable `9C38CB90...`: normal SP startup, manual title
return, menu-hosted Surdana Meadow phase Loading -> Playing, and 5042.62 units of measured horizontal movement.
Root inspected `mp_ready.png` and `mp_moved.png`: native arena gameplay and changed position are visible. Scripted
quit exited 0, no missing-target or guest-stall markers occurred, and installed INI hashes remained unchanged.
This repeats successful reproduction of the reported transition on current source with default Rynn; it does not
cover loaded-save/remote-join/other-level permutations. Detailed automated checks are in the run's `summary.json`.
