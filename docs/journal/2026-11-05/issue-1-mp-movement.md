# Issue 1: Inquisitor's Lair multiplayer movement investigation

## Status

Not reproduced in the current deployed build at an actual menu-hosted Inquisitor spawn: W and S both move the
controlled actor with zero heading change. The reported older failure's cause remains unidentified. No movement-code
change was made; this is current-build evidence, not attribution of a fix.

## Existing implementation checked

- The 2026-11-04 arena pre-initialization fix removes campaign actors before initialization. Its documented live
  menu acceptance covered the first map (Surdana), not an Inquisitor host spawn.
- `InputModule::beforePadMgrUpdate` changes only the left-stick X record to axis 3 (strafe), gated by keyboard
  ownership, gameplay and on-foot state. It leaves the left-stick Y record alone. The documented native ground
  schemes all map left-stick Y to negative axis 2 (forward/back). This does not identify a map-specific W/S fault.
- `filterArenaLevel` retains native player/camera placements and copies their property payloads unchanged, except
  for pruning links to removed actors. There is no Inquisitor-specific movement mapping in that filter.
- Arena spawn placement writes position and yaw and then refiles the actor; placement retries can repeat that
  write. No observed spawn-retry evidence yet ties those writes to the reported rotation.
- Existing `arena_two_instances.py` acceptance focuses on pickups and fighting, and its recipe does not exercise
  W/S. Prior Inquisitor arena runs therefore cannot be treated as movement acceptance.

## Bounded live diagnostic

Launched the existing `dist/DrakanPC/drakan.exe`, SHA256
`30BC5FDB6A950877E4D296C4D92111DBDC6C9F1D32283E217B625ADC57D4EE3C`, through `play_run.py`.
This deployed executable predates the other changes under investigation in the shared checkout. No game or build
process was present before launch. Native options, card and output were isolated under
`build/harness/issue1-inquisitor-movement/`; the process exited normally with script quit after 51 seconds.

The recipe holds W for 3.5 seconds, S for 3.5 seconds and D for 3.5 seconds, with separate screenshots. It uses
`DRAKAN_ARENA_FORCE=inquisitor-depths` and redirects the intro to Level 1E. The log confirms the filtered Lair loaded
with 529 actors. The `start.png` / `forward.png` views show visible displacement relative to the archway; later
screens show distinct backward and strafe poses. The starting weapon equips during this sequence.

This is **not** the reported host-spawn case: FORCE arms the loadout without a network spawn event. The log's native
start is `(583073, 150002, 506608)`, outside the configured arena bounds, and no assigned-spawn placement is logged.
The screenshots cannot measure world displacement or isolate heading changes from animation/camera movement.
No RAM snapshots were collected. This run must not be counted as a reproduction or a multiplayer fix acceptance.

## Required next evidence

Launch the actual Multiplayer > Host > Inquisitor's Lair > Start flow, wait for assigned-spawn placement and loadout
completion, and record guest RAM before/during/after independent W and S holds and release. Compare controlled actor
identity, world position, heading basis, controller and camera flags, all four stick-map records, resulting axes and
placement retry logs. Include a Surdana control with the same settings. A fix should follow the first demonstrated
incorrect state; a map-specific axis override is not justified by the evidence above.

## Follow-up: actual menu-hosted spawn

Completed a second run through the actual title Multiplayer > Host > third arena (Inquisitor's Lair) > Begin Game >
Start flow, using `host_keys(arena_index=2)`. No FORCE or level-redirect environment variable was present.
Evidence: `build/harness/issue1-inquisitor-host-movement/` (`recipe.txt`, `run.json`, `stdout_1.log`, screenshots,
five `ram_*.ram` dumps and parsed `movement.json`). The executable hash is unchanged from above. The process exited
0 with script quit at frame 9364; no game process remained after completion.

Log evidence confirms native New Game with Level 1E, server respawn at spawn 0, assigned placement
`(579166,128750,440175)`, placement accepted on its first try (628 units from the point after settling), and a
successfully armed right hand before the baseline snapshot. Selected character is Sevoth (`177d`), with the controlled
handle unchanged. The recipe waits 10 seconds after gameplay synchronization before baseline, holds W from 12 to
14 seconds, then holds S separately from 18 to 20 seconds.

| RAM snapshot | Position X / Y / Z | Heading |
| --- | --- | --- |
| baseline | 579049.125 / 128554.3125 / 439572.5 | 64.494914909 degrees |
| W held | 580833.3125 / 128623.9609375 / 438708.25 | 64.494914909 degrees |
| W released | 582006 / 128555.296875 / 437805.5625 | 64.494914909 degrees |
| S held | 581438.625 / 128538.28125 / 438154.78125 | 64.494914909 degrees |
| S released | 580439.9375 / 128555.3359375 / 438617.3125 | 64.494914909 degrees |

W's final displacement is 3444.59 units. S moves 1763.94 units from the W-release position in the opposite X/Z
direction. All five full rotation matrices are identical, not merely rounded headings. Controlled handle `8146e0`
and player data `8b5590` stay identical across snapshots. The left-Y map remains axis 2, scale -1; its output is
1 during W, -0.984436 during S, and 0 in both release snapshots. Other stick outputs remain zero. Thus these two
keys produce translation rather than a rotation at this actual multiplayer spawn.

Settings: German language, controls version 2, direct mouse look, keyboard strafe enabled, native controller scheme 0,
sensitivity 1 and invert-Y disabled. Exact native options and observed input/multiplayer INIs are retained in the run
folder; input INI SHA256 is `3ECF546900E9858A587EF9C4B62385B074C9281431D3EAC0EF73619BE0DD078F`, native-options SHA256 is
`B802B6E5B9397D1F820EBEBD7FB5C72608AA80BD25B259DC56B30C8B3BE22A85`. Cards/options/logs are isolated under build;
the executable ran from `dist/DrakanPC`. Audio is disabled, window requested at 1280x720, no activation, autopress and
autostart disabled, and no physical movement or mouse yaw was injected.

This closes the native-start diagnostic gap above. Scope is one host, Sevoth and spawn 0; other avatars/spawns,
physical gamepad input and a joined client were not tested. No Surdana control was needed to characterize this
nonreproduction: the actual Lair W/S translation and unchanged orientation are measured directly. If the original
failure persists for the reporter, their build/settings/avatar/spawn are needed to distinguish that path.

## Maintained follow-up harness (source-ready, not live acceptance)

Added `scripts/harness/arena_movement.py`. It launches only the canonical `dist/DrakanPC/drakan.exe` when explicitly
passed `--run`; its default prepares private receipts without launching. `--joined` prepares a menu host and LAN
joiner. `--arena 0/1/2`, `--skin-steps`, `--look direct/stick` and `--strafe 0/1` permit bounded comparisons. It seeds
model/character 0 rather than inheriting the user's multiplayer selection, and isolates main, input, native-options,
multiplayer INIs and memory cards. The multiplayer isolation requires the accompanying `DRAKAN_MULTIPLAYER_INI`
runtime override; deploy that source before running. Title, pause, save and inventory menus are explicitly modern.

Each role captures baseline, held-W, released-W, held-S and released-S RAM, screenshots and native logs. The report
requires native menu launch, assigned placement, settled placement, armed hand, script completion, stable actor,
W/S translation in opposing directions, unchanged heading and correct signed native axis-2 output. A missing native
target/load stall or incomplete spawn/loadout fails separately from measured movement/routing. A process timeout
attempts a full process dump before owned-process termination. A LAN heartbeat alone does not satisfy acceptance.

Validation performed here: Ruff check/format passed; two-role dry preparation completed at
`build/harness/issue1-movement-preflight-v1/`; offline `--report build/harness/issue1-inquisitor-host-movement` passed
against the earlier real-host receipts and reproduced 3444.59 / 1763.94 horizontal units with zero yaw change. The
maintained report additionally records native camera mode 6, flags 0x200 at rest / 0x201 during movement, summon 0,
and player flags 0x62000 / 0 throughout that old run. No new game was launched, and joined-client / Rynn / alternate
control configurations remain unvalidated. No movement runtime change is justified by the available evidence.

Next canonical live command after deployment and reserving the game slot:
`python scripts/harness/arena_movement.py --joined --name issue1-movement-joined-rynn --run`.
Use fresh names for subsequent controls (`--look stick --strafe 0`) and Surdana (`--arena 0`) comparisons. The optional
skin-step selection must be confirmed from native actor/lobby evidence before attributing a result to a named NPC.


## Joined default-Rynn probe and invalid private-options fixture

`build/harness/issue1-movement-joined-rynn-r1` exercised actual HostMatch and
JoinMatch on Inquisitor's Lair, using canonical deployed SHA256
`de57cf5f6badbc06d234debc28df7b178668660695e3b4c57adae9ed7e5a8415`.
Host StartMatch occurred at 85.054 wall seconds, following a successful client
JoinMatch and both player-join receipts at 53.395 seconds. These timings use the
pickup harness's proven host start at 60.5 seconds after title synchronization
and client pre-game timestamps delayed by 20 seconds. Gameplay W/S timing remains
relative to the native play synchronization. Both processes completed script quit
and exited 0; no process remained.

The host controlled native Rynn (resource `0x20a50`, class `0x10009`), with assigned
spawn 0, settled placement and armed hand. W and S each produced zero horizontal
displacement. W changed heading by -83.84668 degrees; S changed it by +71.31216
degrees. The left-Y record was axis 0 / scale +1, rather than native forward/back
axis 2 / scale -1. W produced -1 on that incorrect turn axis, S +0.984436. This
matches the reported symptom **under this private fixture**, but the following
configuration defect prevents attributing it to a normal current-build run.

The original maintained harness wrote only sensitivity, invert_y and
controller_scheme into its private native-options file. `writeGuestOptions()` in
`drakan_menus.cpp` intentionally validates all twelve serialized fields before
writing the game's startup settings. The partial file therefore returned false,
produced no native settings card file, and emitted no native-options-prepared
receipt. The earlier successful real-host run had a complete twelve-field file.
Normal `nativeOptionsTick()` writes all twelve fields, and the deployed public
sidecar is complete. Thus the partial fixture is not evidence that a complete
normal settings file was applied incorrectly.

The harness now seeds all twelve standard native options and requires the
startup-loader preparation receipt. It preserves the input recipe helper's old
default timings for the pickup harness while its own joined run uses the verified
60.5 / +20 second schedule. Dry preparation with the complete settings succeeded
at `build/harness/issue1-movement-complete-options-preflight`; no game launched in
that preflight. A corrected joined r2 is required before a movement-runtime fix
is justified.

The client is a separate failed spawn-lifetime observation, not a proven load
stall. Native spawn 2 was assigned and accepted (108 units from the point), but
Rynn reached native HP -307/100, flags94 `0x202`, and controlled actor 0. The
baseline dump still contains the original Rynn body at
`625954.4375 / 128612.5 / 442244`, resource `0x20a50`, while subsequent snapshots
no longer expose it in the actor registry. The current reporter distinguishes
this native death from missing native loading evidence and never treats a dead,
uncontrolled body as movement acceptance. The original summary is retained;
`summary-native-options-audited.json` classifies the host as
`native_options_not_prepared` and client as `native_controlled_actor_dead`.

Source inspection shows arena teleport currently writes the actor transform and
refiles its spatial membership; no native landing/fall transition is established
by that alone. Client telemetry starts at the campaign height about 150008 and
lands near 128612, but the damage callsite was not captured, so fall damage is a
hypothesis rather than an established cause. No spawn coordinates, health,
physics flags or runtime relocation behavior were changed. The next probe adds
1/3/5-second RAM checkpoints and optional bounded native health-write/damage
tracing to capture the actual transition before the 10-second baseline.


## Complete-options joined r2 and native landing-damage trace

`build/harness/issue1-movement-joined-rynn-r2` ran canonical deployed SHA256
`4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278` with the
complete private native settings and bounded native health tracing. Both processes
exited 0 with script quit. Both emitted the normal startup-loader options-prepared
receipt; the private cards contain the generated native settings file. The host
passes every movement check: W 3453.0868 horizontal units, S 1811.8133 units in the
opposite direction, zero yaw change, axis 2 / scale -1, correct signed held input,
and HP 100 at the 1/3/5-second spawn checkpoints. The r1 mapping failure was an
invalid-options fixture, not a demonstrated movement runtime defect.

Joined acceptance remains incomplete because the client again dies before its
movement baseline. It already has HP -306 at 1 second; the 3/5-second samples
retain that value. The correct axis 2 / scale -1 was applied on the client too,
so this is separate from W/S routing. Native tracing captures the actual health
write at return address `0x2fc620`, in native landing routine `0x2fc330`, setting
health to -306. This is native landing damage rather than an observed enemy hit.

The native landing routine reads the cached impact-speed magnitude at
`player+0xb9c`, populated from the native velocity query `0x325d30`; the client
cache is 20094.22 versus host 3485.09 in the first spawn sample. The velocity query
reads the actor's skeleton/controller at handle +0x50, previous position +0x174,
and collision-resolved tick displacement +0xc8. Arena `teleportOnce()` previously
changed the actor transform and spatial registration without rebasing those
motion records. This lets a relocation contaminate native landing-speed input.
The unchanged `player+0xc30` value alone does not prove a fall-origin field and
has not been written as a speculative correction.

A scoped source correction now rebases only that motion history during an explicit
arena teleport, before the next physics query. It additionally clears PAL Rynn's
cached speed (+8) and cached landing impact speed (+0xb9c). The caller verifies the
actual resource class, not actor type 3 (which a mount can share). Generic motion
writes require skeleton +0xd4 to reference the same actor; Rynn cache writes also
require data +0x80 ownership. All accesses are bounded. Non-PAL player cache fields
are deliberately untouched until audited. No health, damage threshold, airborne
flag, airborne time, gravity, spawn coordinate or normal per-frame fall processing
is changed.

The helper lives in `drakan_arena_placement.h`; regression coverage was added to
the existing `character_roster_test`, including preserved health/flags/airborne
time, non-PAL and non-Rynn cache exclusion, and stale/invalid pointer rejection.
The changed C++ was clang-formatted. At this checkpoint it is source-ready only:
no build or test was run during the requested performance window, and corrected
joined r3 gameplay remains required. No successful client movement or spawn
survival is claimed yet.


## Final deployed joined r3 acceptance

After parent integration passed 289 Python tests and ten focused C++ tests
(including the placement helper in `character_roster_test`), the canonical build
was deployed as SHA256
`fce25c698b06cc77a233aea741c3f46c318a3c06fa40b565df9813002ed02503`.
`build/harness/issue1-movement-joined-rynn-r3` then ran the same menu-hosted and
joined Inquisitor flow, complete private native options, early RAM captures and
bounded health tracing. Both games completed native script quit and exited 0.
All nineteen host and twenty client movement/spawn checks pass.

| Observation | Host | Joined client |
| --- | --- | --- |
| Controlled native Rynn | Resource 0x20a50, class 0x10009; stable handle 0x844ca0 | Resource 0x20a50, class 0x10009; stable handle 0x7e4b60 |
| Assigned spawn | 0 | 2 (same previously failing point) |
| First stable position | 579166 / 128612.30 / 440175 | 625657.5 / 128607.17 / 442694.09 |
| HP at 1 / 3 / 5 seconds | 100 / 100 / 100 | 100 / 100 / 100 |
| HP in every movement sample | 100 | 100 |
| W horizontal displacement | 3755.7346 | 2307.4574 |
| S horizontal displacement | 1870.4582, opposite direction | 2096.7294, opposite direction |
| W / S heading change | 0 / 0 degrees | 0 / 0 degrees |
| Native left-Y mapping | Axis 2 / scale -1 | Axis 2 / scale -1 |
| Native placement/loadout | Assigned, settled, armed | Assigned, settled, armed |

The client trace contains its normal startup health setter to 100 and no fatal
landing write at `0x2fc620`. No health value was repaired or suppressed by the
placement correction. The velocity cache is allowed to be repopulated by native
processing after the placement; it is **not** held at zero or capped, and the
1-second cached values are not claimed to remain low. Native movement and damage
threshold code is unchanged. No separate damaging natural fall was playtested,
so preservation of that later path is established by source scope and regression
state guards rather than a new gameplay damage claim.

This closes joined default-Rynn W/S acceptance at Inquisitor spawns 0 and 2 for
the final deployed build. It does not identify the reporter's original settings
or certify other spawns, other characters, gamepad input or unaudited USA player
cache fields. The invalid-options r1 and failed pre-correction client r2 receipts
remain intact; neither was relabelled as a pass.

Both native processes were confirmed absent after completion, then the live slot
was transferred directly to the authorized remote-motion baseline/adverse probe.
