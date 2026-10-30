# USA combat, movement and mounted flight

Continued the settings/direct-mouse migration using the existing subprocess, scripted-input, RAM-dump and
render-thread inbox harnesses. No desktop input or computer-use tooling was used. Game runs used the deployed
PAL and USA executables and isolated cards/INIs; private game data and captures remain under `build/`.

## Runtime changes and audit

- Added `MovementInput`, independent of PAL-only `AdvancedInput`. The shared native walk helper now runs after
  the USA PadMgr update. Keyboard strafe is no longer forcibly disabled on USA, and PC Settings enables its row.
- Profile-owned movement addresses: PadMgr `0x4e90c0 -> 0x4e9f40`, callback `0x131fa8 -> 0x131f48`, timer
  `0x4af7f8 -> 0x4aefd0`. The callback and timer match normalized bodies of 68 and 120 bytes; the PadMgr match
  is 1544 bytes. Stick map/mode remain the already verified PadMgr `+0x498/+0x4d4` fields.
- Walk command `0x4010` selects native walking, beyond merely scaling analog input. It releases on host ownership,
  physical-stick steering and mounted input. Strafe restores the native axis/scale for physical-stick and flight
  control. Unsupported profiles cannot invoke the movement helper; init/reload pin all migrated addresses.
- Basic input previously accepted a mode-1/6 camera as proof of gameplay. During the opening scripted walk this
  stopped autostart early and admitted premature pause tests. Both backends now require recent native look activity
  on foot or the controlled dragon class, plus the existing flow checks. Autopress stops at the first controlled
  frame; script readiness uses the common 100-look/80-play or 240-play threshold.
- Read-only `combat_observer` and `save_fixture` metadata live in each disc profile. USA Rynn weapon/active weapon,
  attack state, guard and animation offsets differ by four bytes (`ea8/ecc/103c/aac/ae8`). NPC health fields remain
  `24c/250`, self handle `23c`. Stats/key mappings are `4fbac8 -> 4fc948`, `4fe86c -> 4ff6ec`; native walk flags
  `4fbb34 -> 4fc9b4` have 17 matched references and no conflicts. Profile validation checks required observer
  addresses, offsets and serialization classes.

## Native fixtures

Renaming a copied PAL card to the USA prefix is not save conversion: an exploratory load reached invalid native
sizes/targets. The public harness rejects a wrong-prefix card. No production cross-release save workaround was added.

`usa-native-save4` waited for actual control, opened F1 over native pause 23, closed to the native pause menu and
created Slot 1 through the retail Save Game screen. `combat_save_fixture.py --region=usa` copied that native card
and moved only Rynn/nearby camera positions to the existing encounter coordinates. Serialized player/camera classes
were confirmed on both native formats and moved to the profiles. The source save SHA-256 was
`ce9e2407fcf17d434dc881b9e7a9a05a27c019c47cc5a6158d2a24b4e97b045c`; 806 other resources stayed byte-identical.
The preparation process was stopped after the save completed rather than waiting out its long idle recipe.

The older forced ground-dragon fixture is unsuitable for proving flight: it can switch control while state/position
remain stuck. These checks instead start the native `Level 4A - Ravenshold` mounted scene, without actor/state edits.

## Paired harness evidence

Private reports: `build/harness/{pal,usa}-combat-melee1`, `*-movement1`, `*-flight1`.
Each completed with the script quit marker and exit code zero. All original input cards remained unchanged.
Reports were evaluated with the shared `check_evidence` assertions after the observer was completed.

| Check | PAL | USA |
| --- | --- | --- |
| Melee equip / target lock | native weapon 4626 / target 83 | native weapon 4623 / target 83 |
| Target 83 health | 40 -> 26 | 40 -> 22 |
| Block held / released | guard mask 0x2 set / cleared | guard mask 0x2 set / cleared |
| Rynn survived | 30 HP | 50 HP |
| Native walk / release, run, strafe without yaw | passed | passed |
| 64-count direct yaw / idle | -0.128 rad / stable | -0.128 rad / stable |
| Mounted neutral position | stable | stable |
| 30-frame climb | +12,290 world units | +12,184 world units |
| Mouse flight yaw / pitch | -0.2165 / -0.0856 rad | -0.1945 / -0.0785 rad |
| Primary / secondary requests | set and released | set and released |
| Native walk injected during flight | no | no |

These are behavioral parity checks, not deterministic combat or identical rate-based flight traces. Differences in
native timing, stick quantization and encounter state remain observable. The evidence establishes actual climb,
forward movement and steering, beyond mount identity or button delivery. Attack request bits do not establish
breath damage, projectile creation or successful attack animation. Landing, dismounting, direct dragon writes,
modern bow aim, dual wield and quick slots are not covered by these runs.

## Harness changes and validation

`disc_combat_check.py` uses profile observation fields, native-card validation/source hashes and optional
`--check melee|movement|flight` assertions. `disc_movement.txt` and `disc_flight.txt` are reusable recipes.
`combat_save_fixture.py` selects the native prefix and serialized classes from profile metadata.

The first USA mounted settings attempt reached native pause but its 10-second wall-clock recipe expired while
first-use settings art loaded; several timed events caught up in one EE frame. The PAL attempt passed. This was a
harness timing failure, not proof of a runtime pause failure. The gameplay settings recipe now uses native pause /
resume barriers, with an explicit final resume only after checking that an existing pause was preserved. Named
runs and configurable timeouts allow mounted coverage without overwriting earlier evidence.

Both final game builds deployed through `Build-LocalGame.ps1 -Incremental`. All 155 Python tests and nine focused
C++ tests passed (input, bow, both profiles, settings core/schema/menu and game UI pause/settings). Profile tests
cover callback addresses, walk edges, overlay release, physical-stick ownership, flight map restoration, reload
pinning and disabled movement capability. Ruff, changed C++ formatting, `git diff --check` and the tracked-source
audit passed. Mounted settings barrier reruns are recorded below when complete.

### Mounted settings barrier reruns

`usa-flight-settings2` and `pal-flight-settings2` both passed: native pause with live menu sampling, no actor/camera
motion from W/mouse while the overlay owns input, resume-on-close, preservation of a preexisting pause, and clean
script exit. These ran the final deployed binaries. The default harness timeout is now 300 seconds so USA can
finish its native opening sequence before the new control-readiness gate admits tests. Recreating the USA encounter
fixture through the final profile-based serializer produced the same `14933dbf...69b0` SHA-256 as the tested fixture.
The additional untracked-source audit approved all 19 new source files; no game content was added to source.

`pal-flight-settings-stay-paused` also passed the revised harness with resume-on-close disabled, including the
final explicit resume after verifying that both closes retained the native pause. All game test processes exited.
