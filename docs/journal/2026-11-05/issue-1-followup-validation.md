# Issue 1: second investigation and gameplay validation

The user requested a separate GPT-6.1 Sol agent for each outstanding report from the first integration pass,
with renewed attention to the Drakan pickup logs and graphics configuration, use of the local Order of the Flame
installation, and harness coverage for directional attacks. This entry records the new work; earlier evidence and
limits remain in `issue-1-followup-integration.md` and the individual item journals.

Work is serialized into batches of three workers in the shared checkout. Source changes from the previous pass
and the concurrent performance investigation are preserved. Native tests use the canonical deployed play folder
and private settings/cards. Build, deployment and live process ownership are coordinated by the parent.

## Work queue

| Item | Focus |
| --- | --- |
| Multiplayer pickup freeze | Re-read available original, current and harness logs; reproduce City of the Damned pickup if evidence permits |
| Remote low-poly appearance | Inspect actual graphics settings/persistence and distinguish mesh LOD from texture/render quality |
| Drakan 1 import | Convert the owned local Order of the Flame files, inspect campaign output and exercise native loading |
| WASD directional attacks | Extend the maintained harness to check native attack behavior against arrow controls |
| A/D spell gestures | Check actual native gesture recognition and spell release through a maintained harness |
| Remote lag/teleporting | Paired motion and network-condition evidence, including respawn/ghost-lifetime distinctions |
| German stuttering | Controlled original-disc language comparison; diagnose available logs without inventing modified-image evidence |
| Inquisitor movement | Broaden the controlled reproduction to relevant configuration and host/client cases |
| SP to multiplayer freeze | Repeat the actual menu transition with diagnostics and relevant settings/character cases |

Final outcomes and validation will be appended after integration. No commits, releases or GitHub replies are requested.

## First integration checkpoint

- Re-reading archived City/Inquisitor logs found a native null item-property call while UDP logging continued.
  Arena inventory detachment now runs at the common destruction boundary, covering campaign sweeps. The old
  logs do not identify the exact freed item or prove ordinary pickup was the trigger.
- Settings now retain and restore `mip_bias = auto`. This is a texture-sharpness control defect, not evidence
  that remote character meshes select the wrong LOD.
- The owned D1 install at `build/ootf/cab_disc` converted all 23 levels through the full CLI pipeline. An actual
  editor-context Intro pack reproduced German level-registration failure. Portable English mod files now also
  mount in the active language with tested explicit-localized and later-pack precedence.
- Canonical build/deploy succeeded: `9C38CB9042B43A916A7E4E49CFFAC0534F3FF6DF20EDE62E883C303C647F4C78`.
  Logs are `build/issue1-round2-build.log` and `build/issue1-round2-deploy.log`. The build also includes the
  concurrently developed performance changes; they are not attributed to these issue agents.
- Parent added `DRAKAN_MULTIPLAYER_INI` to isolate each direct-dist harness profile. Default player behavior is
  unchanged. New paired/menu harnesses use private main, input, native-options and multiplayer files.
- `play_run.py` now resolves output/executable paths before exporting environment variables. A relative
  `--out-root` had placed runtime paths under the play folder and prevented the recipe from loading. Run-folder
  cleanup also rejects paths escaping the selected output root.

Initial Python integration passed 273 tests. Focused rebuilt mod/settings tests passed, and the combined seven
mod, settings, inventory, input, netplay-policy and character-roster CTest cases passed. Further harness edits and
live acceptance are recorded below; these checkpoints are not a blanket gameplay pass.

The corrected German editor Intro run (`build/ootf-run/issue1-local/editor-fixed-absolute`) registered and loaded
OotF, reached native control and scripted quit with exit 0. Its screenshots were black except for HUD. The earlier
relative-path run was closed after the missing-recipe problem was identified and is excluded from acceptance.
A real terrain-level/campaign-handoff check remains necessary; registration is not visual playability.

## Native acceptance so far

- D1 exact-editor two-level campaign: actual German game-choice UI selection, Intro registration/load, timed
  Ruined Village handoff, textured Rynn/terrain and 1072.40 units of measured W movement passed. Evidence:
  `build/ootf-run/issue1-local/editor-campaign-script`. Intro remains black until the converted timer hands off;
  this does not certify its cinematic rendering or all 23 converted levels.
- English/default-Rynn SP -> title -> multiplayer: current `9C38CB90...` run reached Surdana arena and moved
  5042.62 units, with clean scripted exit and unchanged installed INIs. Root inspected both gameplay screenshots.
- Directional attack RAM exposed an actual earlier-fix defect: retained analog movement selected a moving-attack
  clip instead of the arrow-key directional clip. The revised synth neutralizes only keyboard movement during
  a native drawn on-foot melee context; bow, dragon, summon, stowed and unknown-actor gates are regression-tested.
  Rebuilt input test passed. Canonical updated deployment is
  `DE57CF5F6BADBC06D234DEBC28DF7B178668660695E3B4C57ADAE9ED7E5A8415`.

City pickup runs r1/r2 are not passes: missing adopted pickups and terrain-blocked approach prevented real claims.
The harness now rejects initial-loadout inventory changes as pickup evidence and requires server/peer receipts.
Spell fixture discovery unlocked ten native spells but did not enter gesture preparation; its false result is
retained. Both areas need the next actual interaction tests rather than a no-crash-only verdict.

## Native acceptance update

The paired spell test at `build/harness/issue1-spell-native1` verifies both WASD and arrow
prefixes U/UL/ULD/ULDR, native full-match recognition (buffer clears, ready state 5 and
selected Fireball), casting state 4 and mana 100 -> 85. The audited report passes all 31
core checks. Sparse snapshots did not observe projectile travel; that remains a separate
unverified visual/damage check rather than a failure of A/D recognition.

City r4 completed both native scripts with exit 0 but still failed the strict pickup oracle:
neither participant gained the Blade or sent a native claim. RAM instead exposed factory
items with collision cell -1 and no spatial membership. A targeted registration repair is
being prepared and must be checked with actual native collection before acceptance.

Melee ordering probes establish that centering analog movement and pressing the projected
D-pad in the same sample lets the analog release erase the digital direction. One neutral
preparation sample followed by projected direction + attack reproduces the settled arrow
control's actual native clip 0x20684 and advancing animation clock. The implementation
will buffer a short keyboard/mouse attack onset through that preparation sample; final
all-direction deployed acceptance remains pending.

Auto mip-bias live application, save and restart passed in two private sessions
(`build/harness/issue1-mip-auto-live`). The settings screenshot does not expose
the scrolled mip row; GPU bias itself was not measured. Focused rebuilt input,
world inventory/spatial retry and both PAL/USA profile tests pass. The next
canonical deployment is `4CE9DD0D611D595D0C1AA5B4E8BF0BA9E723E5F66152BFAAD258349576ECF278`.

The first joined Inquisitor run must not be counted as reproducing the player's
normal configuration: the harness supplied an incomplete native-options file,
which prevented creation of native controller settings. Its host W/S-to-turn
symptom occurred with an uninitialized left-Y mapping. The corrected fixture
requires all 12 fields and an actual native-options-prepared receipt. The client
also died near spawn (HP -307); that is a separate event requiring health/position
inspection, not a missing-branch or load-stall conclusion.

## Verified results before final spawn integration

- All four native directional attacks pass on 4CE9: all 59 checks, exact arrow/WASD
  clip parity, advancing animation, completed attacks and restored movement. Evidence:
  `build/harness/issue1-directional-final1/directional-report.json`.
- City R5 proves real native pickup by both participants, accepted server claims,
  removal of each peer copy, valid Blade ownership, native insufficient-skill equip
  rejection without a crash, subsequent movement and clean scripted exits. The original
  report hit a reader bug on native integer cell tags; the corrected `review.json`
  retains that original failure and validates the same captured RAM transparently.
  The near-spawn fixture does not certify original-coordinate approach or the reporter's
  exact binary. Final source adds the original resource fallbacks without changing locations.
- Inquisitor r2 with complete options verifies host W/S movement (3453.09 / 1811.81
  units, zero yaw change). Client death is independently traced to native landing damage
  from stale teleport velocity. A scoped placement-history reset is prepared, preserving
  normal damage/health/airborne logic; its build and joined r3 acceptance remain pending.
- Matched original-disc EN/DE/DE/EN runs passed on unchanged 4CE9, all with valid native
  options and language archive receipts. Host-present p95 was 10.597 / 7.110 / 7.462 /
  8.659 ms, no >50 ms gaps in the measured final 20 seconds and no I/O errors. This is
  not the unavailable modified image and not a world-FPS measurement.
- Remote baseline1 captured native movement and lifecycle with no >500-unit receiver
  step or body recreation after startup. It exposed incorrect RAM dump labels in the
  checker; corrected baseline/adverse captures and descriptor readback remain pending.

The performance chat requested the shared GPU/game window. It was explicitly released
immediately after the language sequence; no additional native tests/builds were started.
Current deployment remains 4CE9 while that chat runs its bounded same-executable checks.

## Final integration build

Canonical incremental build/deploy succeeded at
`FCE25C698B06CC77A233AEA741C3F46C318A3C06FA40B565DF9813002ED02503`;
log `build/issue1-final-deploy.log`. This includes City resource fallbacks,
the dying-actor spatial guard and the reviewed arena placement-motion reset.
The performance chat's capped-completion wake is retained, while its unsuccessful
opt-in early-Flush experiment was removed by that chat before this build.

Final normal Python suite: **289 passed** (`build/issue1-final-python.log`).
Ten focused CTest cases pass: mod, input, settings, settings schema, inventory,
spatial registration, PAL/USA disc profiles, character roster/placement and arena
metadata. All changed Python files pass Ruff formatting (17 files), repository-wide
Ruff lint passes, diff whitespace checks pass, and the tracked source audit reports
2329 approved / 0 prohibited files. Earlier diagnostic-wrapper import errors and
an affinity timeout under load were rerun successfully before this final full pass.
Final joined spawn and paired motion/LOD acceptance follow on this deployed hash.

## Final native spawn acceptance

Joined Inquisitor r3 on FCE25 passed all 19 host and 20 client criteria. Both players
retain HP 100 at 1/3/5 seconds and across the movement samples. Host W/S travel is
3755.73 / 1870.46 units; client is 2307.46 / 2096.73, in opposite directions with
zero unwanted yaw. The same formerly fatal client spawn now survives, is armed
and remains controllable. Both scripts quit with exit 0. Native health tracing no
longer contains the fatal landing write from r2. The reset is limited to explicit
teleports; later natural damaging falls were not separately playtested.

Final motion baseline2/adverse captures also completed on unchanged FCE25, all four
processes exiting cleanly with complete native receipts/RAM and unchanged source
cards/deployed INIs. The adverse relay applied 40 ms one-way delay, +/-15 ms jitter
and 253 drops in 5151 received datagrams (4.91%). No >500-unit receiver position
candidate appeared in that run. Baseline2's single 660.8-unit candidate spans a
7.116-second startup level-transition/body-recreation gap; it is not evidence of
an instantaneous steady-gameplay teleport or the later inventory interaction.

Near native remote Rynn models use resource 0x20f25, LOD 0 and all 1628 available
vertices on both peers. The alternative descriptor has 208 vertices; the distant
remote selects it at roughly 6600 units. Parent inspected the host's near screenshot,
which visibly contains both Rynns. These measurements do not establish a stuck low
LOD, nor reproduce every aspect of the reporter's lighting/appearance screenshot.
The Auto mip-bias persistence defect is fixed independently of mesh detail.

The shared GPU/build window was explicitly returned to the performance chat after
the final pair. That chat is preparing further renderer diagnostics in source;
those later edits are outside the FCE25 build and this acceptance record.
