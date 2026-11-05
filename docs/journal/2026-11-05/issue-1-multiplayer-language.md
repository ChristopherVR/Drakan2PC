# Issue 1 multiplayer and language follow-up

## German selection and recorded speech

Two PAL boots used the same private card and INI. In the initial English session the settings harness opened F1, applied `general.language german` through the menu's Apply path, verified the persisted INI and closed the overlay. The second session restarted the game. Neither session set `DRAKAN_LANG`. Both played the intro and exited through their input scripts. Private evidence is in `build/harness/issue1-language-f1-voice`.

Decoded the English and German source narration independently from the owned disc with `analysis/scripts/sub_adpcm_wav.py` (intro record 99, subrecord 14). Compared two-second windows from each reference against the captured game output after resampling to 8 kHz and filtering to 350–3200 Hz. The first second shared by both soundtracks is unsuitable for identification. Using reference windows beginning 10, 15, 20 and 25 seconds into narration:

| Game output | German reference, normalized correlation | English reference, normalized correlation |
| --- | --- | --- |
| English boot | 0.172, 0.250, 0.128, 0.138 | 0.907, 0.763, 0.844, 0.955 |
| German restart | 0.871, 0.825, 0.897, 0.765 | 0.100, 0.096, 0.121, 0.124 |

The matching windows occur at successive five-second intervals in each capture. This verifies spoken output in addition to the guest archive path and the previously observed localized text. The file-swapped ISO used by the reporter remains unavailable. The supported selection uses the original PAL image.

## Saved game to multiplayer

The German INI-only singleplayer run returned to title, hosted co-op, selected Northmen Archer, loaded Surdana again and moved 5,047 units. Starting with a populated private card, the joined test selected Continue, loaded Surdana from slot 1, returned to the title screen, joined the other instance and entered the Inquisitor arena. The maintained `--join-from-save` option checks native load and join ordering. Earlier attempts v1–v3 did not complete that route: the modern title initially focuses New Game, then remembers Continue after it was chosen. Their menu-navigation failures are excluded.

Run `issue1-saved-join-inq-v4` used a three-field host sidecar and a client with a malformed controller scheme and two valid existing card options. The former startup path abandoned all twelve overrides as soon as one field was missing or invalid. Recovery now works field by field, preserving valid card values. The client retained MusicVolume 37 and ScreenXOffset -1, applied Vibrate 0 and received the default controller scheme. Both W/S tests passed with zero change of heading, nonzero native movement and clean exits. The SP-to-MP freeze on the reporter’s binary has no confirmed cause from these runs.

## Arena teleports and network motion

The old thumbnail path waited about twelve seconds, then physically moved the live player to the map picture point and returned them to a spawn. Both instances of the archived Inquisitor run under `build/harness/arena_inq_r1` logged that path. The regular thumbnail now captures the current view; explicit offline preview authoring still supports its configured camera. Both v4 peers wrote fresh thumbnails during their active match.

The avatar replay clock also rewound after a local stall when the sender's latest pose had already been reached. The added regression failed in nine sender/receiver cadence combinations before the fix. A receiver catching up now keeps its play position monotonic within a sender timeline. A new sender timeline clears stale samples, and a respawn which clears the network pose no longer leaves that old pose published to the game.

## Close player detail

The previous co-op tests sampled remote Rynn at high detail nearby. A fresh **versus** RAM comparison caught the coarse mesh on the host at a selector-camera distance of 2,730 units: 208 vertices for remote Rynn and 1,628 locally, with both descriptors available. Nearer samples on the same remote body showed the full 1,628. This explains why sampling only at shorter distances missed the behavior. The PAL network body now requests fine detail through the native selector within 4,000 units. It releases its own request at 4,500 units so normal distant selection can resume. The native controller handles mesh and skinning buffer transitions.

The detail acceptance executable was `098adbdb29318f9c04400fd169704a872c657f4922683f66bfeef9a1380f31fe`. On `issue1-saved-join-inq-lod-final`, the host had no native-options sidecar and a fresh card. The client used the malformed/partial sidecar, existing valid card options and the copied singleplayer save. Both loaded and joined through the menus, with clean exits. Host W/S moved 1,677/1,735 units and client W/S moved 3,071/1,828 units with zero yaw change. Each participant wrote a current-view thumbnail. The host now showed 1,628 remote vertices at 2,731 camera units, and the client showed 1,628 at 1,640. Both peers' valid near movement samples used the full mesh.

In `issue1-network-detail-final`, co-op used Rynn and Northmen Archer through a localhost UDP relay with 65 ms one-way delay, ±35 ms jitter and 410 of 5,132 datagrams dropped. Both processes exited 0. The receiver sampled remote Archer at 1,020 vertices at 1,099 units and 352 at 6,624. No adjacent receiver position step exceeded 500 units in that run.

An earlier run (`issue1-mixed-roster-network-nov5`) with the same relay settings recorded one 5,468-unit step in 12 ms during gameplay. Sender history shows the host's own published X/Z jump to the peer's X/Z about a quarter-second before the receiver replayed it. This happened after inventory had closed, outside level loading, and without a logged death or respawn. The other two step candidates span multi-second startup gaps. The single gameplay jump is evidence for a separate sender-side investigation. Removing thumbnail relocation, improving post-stall playback and a clean final run do not establish its cause.

## Host transform during co-op AI targeting

The host had logged a creature targeting the remote Rynn. The host-side AI adapter temporarily lends the controlled actor's 48-byte transform to the partner so original guest perception can see the partner. It used to restore the bytes and damage-routing flag only on normal C++ return. The EE scheduler can unwind a guest call through C++. A scoped restore now returns all twelve words and the previous routing flag, including nested target calls and exception unwinds. A synthetic regression exercises a nested target and thrown dispatcher-style transfer. Normal gameplay and a forced-target exercise at the same delay/jitter/loss completed on `6ae2bb6b…` with clean exits and no greater-than-500-unit receiver steps. Neither logged an interrupted handler, so those runs do not establish the trigger for the earlier jump.

The forced-target exercise separately showed a creature picking the host’s local cosmetic Archer as “player 0.” The co-op partner list now uses the network-body identity and excludes that local body, including for the client respawn fallback. The motion harness records both sender and receiver step candidates with positions and elapsed time.

After that ownership change the forced-target run `issue1-realtarget-stress-final` selected the actual peer as player 2. Both processes exited 0. It exposed a different failure: the host walked continuously at about 2,600 units/s, but the receiver held a pose across a gap and then advanced 2,081 units between adjacent samples 10.7 ms apart. Unlike the earlier sender-side jump, there was no matching large sender step. The native movement buffer treated every gap longer than 1,500 spatial units as a teleport, regardless of elapsed time. The playback clock could also jump directly to the latest target after a half-second stall.

Network poses now carry snapshot wall time separately from sender animation ticks. If a large displacement across a short outage matches the preceding velocity, playback can interpolate between the two received points without extrapolating past the newest. A spatial discontinuity unrelated to that motion still snaps. The clock limits catch-up after shorter interruptions and reanchors after prolonged inactivity. The pose regression exercises both a moving and stationary sender across the same 2,080-unit gap. The paired UDP harness can now apply a timed `--burst-ms` outage while the host is walking.

The controlled comparison ran `--burst-ms 850 --delay-ms 65 --jitter-ms 35 --loss .08` with the same seed and Rynn/Archer route. In `issue1-walk-burst-before`, the relay dropped 64 datagrams during the outage. The sender had no step over 500 units; the receiver jumped 2,349 units in 57 ms. In `issue1-walk-burst-after`, the outage dropped 63 datagrams and the largest receiver step was 178 units in 36 ms. Both clients and both hosts exited 0. The fitted delay over the whole trace was 280 ms before and 300 ms afterward, so this test demonstrates smoother recovery rather than lower network latency. The final canonical deployment hash is `3c256322e2017b18bbc6b7d8089f7fb3dac784744ccfcd7a2548cfbed9492c73`.

## Final checks

The complete runtime preset rebuilt and ran 108 tests with zero failures and six data-dependent skips (114 registered). All 296 Python tests passed, along with Ruff and diff whitespace checks. The repository-wide guard still reports the previously tracked root release configuration `cliff.toml`. No issue comment, commit or release was published.

## Follow-up on the sending player

The two-player route uses a Surdana save, an Archer host and a Rynn client. The relay adds 65 ms delay, 35 ms jitter and 8% loss. The host walks, opens and closes inventory twice, and uses the existing probe that makes nearby AI choose the partner. This is available as `remote_motion_check.py --sender-probe`.

In `issue1-sender-long-1`, a native data breakpoint watched writes to the controlled actor's X coordinate after inventory closed. During targeting the host's X changed from 482,833 to the peer's 477,302 and back. The host recorded two interrupted calls to handler 0x0045ba20 at 104.463 s and 104.474 s. Published host samples immediately before, between and after these calls all retained X 482,833. Neither peer's published or received trace had a step over 500 units. Both processes exited normally.

For comparison, two private builds/runs used the former return-only cleanup, with an exception log that rethrew the original transfer. The data-breakpoint run exceeded its time limit and recorded no interrupted call. The repeat without the debugger completed on both peers and also recorded no interrupted call. Its large-step candidates spanned roughly seven seconds during startup. Neither comparison recreated the short gameplay jump. The live test establishes that the scoped restore runs during real interruptions. The original sender-side teleport still lacks a before/after reproduction. After restoring the source and rebuilding, the same no-debugger route passed on both peers with zero steps over 500 units and zero recorded interruptions (issue1-sender-fixed-no-watch).

## Singleplayer-to-multiplayer load coverage

The two-process arena harness accepts separate host and client saves and a fresh-campaign client. Each role has a private card and settings. The report follows a successful singleplayer load, a new title session, HostMatch or JoinMatch, the requested arena launch, its native load entry and successful return, and script completion. Movement and receipt of partner poses are checked separately. A missed menu input can therefore be distinguished from a hang inside the level loader.

The D3D11 pair (`issue1-dual-continue-city-dx`) continued separate copies of the Surdana save and requested the pause sequence immediately after gameplay synchronization. Both loaded City of the Damned, received partner poses, walked forward and backward, and exited 0. Forward/backward displacement was 5,028/1,915 units on the host and 5,045/1,899 on the client.

The first WebGPU attempt (`issue1-dual-continue-meadow-wgpu`) opened Load in the host's pause menu. We closed both instances after the wrong action was confirmed. The host never returned to title. The client reached the join menu without a host to join. The script now waits for eight frames of the pause screen and spaces the navigation taps before confirming Quit to Title.

On the retry (`issue1-dual-continue-meadow-wgpu-2`), both roles loaded the requested Surdana Meadow arena on the Vulkan WebGPU backend, exchanged poses, moved forward/backward and exited 0. The separate starting-weapon check failed on both roles: the final RAM samples showed empty hands. The host capture ended 153 frames after gameplay resumed, before the 420-boundary arena sweep and later equipment checks would normally finish. This run verifies the loading transition; it does not establish eventual equipment state.

The first fresh-campaign client (`issue1-newgame-join-inq-dx`) entered the intro and loaded singleplayer Surdana, then printed `Error during program execution: bad allocation` and exited 0. We closed the waiting host. The client never opened multiplayer. The harness now reports the execution error separately from the route stage and exit code. A separate native-graphics profile allows testing the route at 1x without replacement textures, HD movies, DLSS or PC ray effects.

The fresh-campaign retry (`issue1-newgame-join-inq-native`, D3D11 at native scale) completed the intro, loaded singleplayer Surdana, returned to title and joined the live Inquisitor's Lair match. The client entered the multiplayer loader at 209.724 s and it returned successfully at 209.937 s. Both processes exchanged poses and reached script quit with exit code 0. The host moved in both directions. The client's assigned spawn 2 was accepted, but its first one-second RAM sample already showed HP -46. By the movement window the controlled actor was gone. This reproduces loss of control after loading. The native damage callsite was not traced in this first run. The broader loading-freeze item stays open.

On the traced repeat, periodic Start presses in the campaign recipe could leave singleplayer paused. The harness now explicitly chooses Solo and skips the intro movie and in-engine cinematic at their respective screen transitions. The final `issue1-newgame-join-inq-health-cutscene` pair both exited 0. Both exchanged peer poses, and the loader returned successfully on each. After assignment and placement at spawn 2, the native landing routine's callsite 0x2fc620 set the client's health to -47. The one-second RAM sample confirmed -47, while the host had 100 HP and passed the movement checks. This reproduces a post-load landing death twice and localizes the health write. It leaves the reported in-loader freeze unconfirmed. To replay the traced case, add `--native-graphics --trace-health` to the fresh-campaign Inquisitor command in the netcode reference.

The full Python suite passed all 300 tests. Ruff, diff whitespace checks and the source audit passed. The diagnostic game code is excluded from the follow-up commit.
