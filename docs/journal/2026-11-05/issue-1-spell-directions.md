# Issue #1: horizontal spell gestures on WASD

The Controls follow-up reports that W/S can enter spell directions but A/D cannot,
while the arrows work in every direction.

## Native routing evidence

The PAL Circle command (`Game_OnInputCommand`, `0x23ce18`, command `0x4007`) can
start spell preparation through `0x3895c0(player+0x1024)` and clear the Circle-held
flag immediately. Consequently holding Circle is not a reliable casting lifetime.
The summon state at `player+0x1040` is already documented as 1..4 during preparation
and gesture entry, with 0/5 allowing normal inventory use.

Inspection of local generated output for native gesture routine `0x38a548` shows
that it reads InputDev axes **0** and **2**, scaling by the relevant stick scales.
It also adds the horizontal digital direction flags from global `0x4fbb0c`, and
the forward/back digital flags from `player+0x504`. It does not read strafe axis 3.
The PC keyboard strafe feature remaps left-stick X from axis 0 to axis 3 in
`beforePadMgrUpdate`; W/S still reach axis 2. This explains the reported asymmetry.
These findings describe behavior and addresses only; no translated game code is tracked.

## Correction

`beforePadMgrUpdate` now restores the saved native left-stick X mapping and scale
while the current actor's summon state is 1..4. It resumes the normal PC strafe
mapping at state 0/5. The state is read every update from the current actor chain,
so releasing RMB does not prematurely end gesture input and replacing the actor
does not retain a stale casting latch. Menu, mounting, host ownership, disabled
strafe, and physical-stick ownership retain their previous mapping behavior.

The offset is supplied through `CameraLayout::playerSummonState` and the generated
disc profile (PAL `0x1040`, USA `0x103c`). A zero/unmapped offset is skipped; each
actor-chain pointer and final state read is bounded to guest RAM. This change does
not add Circle-dependent D-pad synthesis; the separate Square directional attack
projection remains unchanged.

## Validation and limits

`testSpellMovementAxes` covers all four movement axes through states 1..4 with no
mouse button held, restoration of the native scale, ending/cancelling a cast,
menu and mount transitions, actor replacement, an invalid actor pointer, the USA
offset, an unmapped offset, and physical-stick ownership. It verifies host output
and axis routing, not the guest gesture recognizer itself.

`python -m unittest scripts.tests.test_regions`: 15 tests passed. Changed C++ was
formatted with the repository's clang-format. Parent integration owns the shared
runtime build/test and deployed gameplay check. No game was launched in this subtask;
actual gesture recognition and spell release remain subject to that check.


## Native-evidence harness checkpoint

`scripts/harness/spell_gesture_check.py` adds an isolated PAL runner and a strict
read-only checker. It uses the existing `play_run` lifecycle, copies the supplied
native card, hashes the source card, and launches `dist/DrakanPC`. Settings, card,
recipe and RAM outputs stay under the named `build/harness/` directory.

The gesture routine appends XY records to the native vector at `player+0x1060`
(pointer, capacity, count; 8-byte records). The observer bounds each pointer,
vector length and float. Paired WASD/arrow samples must show native preparation,
empty initial history, growth for every requested direction, opposite horizontal
records, matching completed histories, native release, and a new spell projectile.
Neither script completion nor changed PadMgr axes establishes a pass.

The runner currently requires a supplied, known-valid native spell recipe and its
explicit `--sequence` (U/D/L/R, containing both L and R). Required dumps are
`ram_{wasd,arrows}_{before,prepared,step1..stepN,recognized,released,projectile}`.
No recipe has yet been validated against an eligible native save, so this is a
source/test checkpoint and **does not close actual gesture recognition**. The
recognized-history label is checked against completed history and a subsequent
real launch; it does not independently assert a decoded spell ID. Live evidence
must confirm the vector lifetime and timing before treating this as a maintained
passing regression recipe.

`DRAKAN_ARENA_SPELL` is unsuitable as gesture proof: it unlocks through native
`shaman`, calls the native spell selector with commit enabled, and refills mana.
Such setup may prepare a fixture but must precede the checker baseline and must
not substitute for the input-driven recognition and release checks.

Validation: six checker tests pass, including no-evidence, routing-only, absent or
stale projectile, identical horizontal directions, missing release and actor
replacement failures. Both new Python files pass ruff checks and formatting.
No live game run was performed in this checkpoint; no additional runtime fix is
claimed.


## Native setup and consumed-queue correction

Further audit of the native consumer at `0x38ae68` corrects the first harness
checkpoint above: `player+0x1060` is a **queue**, and the game removes XY records
as the hand animation consumes them. It is retained only as diagnostic data.
The native recognizer (`0x38bf40`) appends processed ASCII U/D/L/R at
`player+0x1034`. The checker now requires exact native string prefixes after
each requested direction and the complete string before firing; it no longer
requires persistent queue growth. `0x38f900` confirms the XY-to-letter mapping.
A synthetic RAM test specifically covers an empty consumed queue with a retained
processed string, preventing the earlier incorrect assumption from returning.

The harness now contains an executable native preparation stage. `--discover`
uses the same retail pad cheat sequence already maintained in
`combat_encounter_cheatscout.txt`, followed by RMB and A to cause the native
recognizer to lazily populate learned spell-direction records. It reads these
records from `player+0x1028`, 20-byte entries with XY pointer at +8 and length at
+16. No spell direction or success state is written. Discovery reports
`fixture_discovery_only` and `gesture_validation=false`; its successful exit
means sequences were discovered, never that gestures passed.

Run when the shared live slot is free:

```powershell
python scripts/harness/spell_gesture_check.py --save build/coop2-fixture-si/mc0 --discover --name issue1-spell-discover
python scripts/harness/spell_gesture_check.py --save build/coop2-fixture-si/mc0 --from-ram build/harness/issue1-spell-discover/ram_discovery.ram --spell-index 0 --name issue1-spell-native
```

`--from-ram` generates the paired RMB, native directions, and LMB recipe from the
learned native table into the ignored run directory. Spell 0 is the historical
Fireball candidate. The runner refuses an unlearned/empty sequence or a sequence
without both L and R; inspect the discovery report and choose a suitable native
projectile spell if necessary. Authored sequences remain in ignored native data,
not tracked source. Preparation and per-direction delays still require live
confirmation; these commands are runnable candidates, not a reported pass.

Eight Python tests pass, including the consumed-queue and generated-input recipe
checks. Both new Python files pass ruff checks and formatting. No live game was
launched in this source-only follow-up because the shared live slot was reserved
for external performance work.


## First discovery result and fixture-context adjustment

`build/harness/issue1-spell-discover` ran the deployed executable through actual
native save loading and script quit. The original card hashes were unchanged.
The shaman input sequence worked: all ten native spell records had level 1 in
`ram_unlocked` and `ram_discovery`. Nevertheless, neither sample had cached XY
sequences, the summon state stayed 0, and the processed string was empty.
Therefore discovery correctly returned failure; this is no recognition pass.

The owned fixture has melee resource `0x3217b` equipped in inventory cell 1,
mana 100/100, and controller scheme 0. Its Circle input can follow the native
parry context; eligibility fields alone do not establish actual spell preparation.
The preparation recipe now reuses the native inventory Fireball selection and
initial LMB fire route from `combat_encounter_magic_cast.txt`. This establishes
magic context before the actual new gesture baseline. Separate
`ram_fixture_selected`, `ram_fixture_launch`, and `ram_fixture_idle` checkpoints
keep that setup auditable. Early RMB-held/released and direction-input dumps plus
input logging distinguish later native preparation from the initial selection.
The selection and initial fixture fire are never counted as gesture success.
This revised context setup awaits a shared live slot.


## Deployed native recognition and cast acceptance

`issue1-spell-discover2` completed with the native inventory setup. It discovered
the learned spell sequences from the native table without injecting a direction,
recognizer state, or guest call. Early samples established that the short RMB
press was cancelling preparation: state 1 while held, then state 0 immediately
after release. The paired recipe was therefore corrected to hold RMB through
gesture entry and firing, with 20-frame direction intervals.

`build/harness/issue1-spell-native1` ran the deployed executable and completed
with native exit 0. The original card hashes remained unchanged. It supplies
actual native evidence on both WASD and arrows:

| Checkpoint | WASD | Arrows |
| --- | --- | --- |
| Preparation | State 1; selected spell reset to -1 | State 1; selected spell reset to -1 |
| Intermediate directions | Exact successive native U/D/L/R string prefixes; both horizontal directions accepted | Same exact successive prefixes |
| Final direction | State 5; native string cleared; selected spell 0 | State 5; native string cleared; selected spell 0 |
| Attack acceptance | Native casting state 4 | Native casting state 4 |
| Native mana cost | 100 to 85 | 100 to 85 |
| After release | Native state 5 | Native state 5 |

The native matcher at `0x38bf40` stores the matched index at summon-base +72
(`player+0x106c`). Its full-match path calls `0x38d730` and resets the processed
string on successful preparation. Thus requiring the full string to persist
in the final sample was another incorrect observer assumption. The checker now
requires every intermediate prefix and, for the final direction, the native
ready state, cleared string, correct learned-spell index, and the reset selection
at the preparation baseline. The initial fixture selection cannot satisfy these
checks. A subsequent actual attack must enter native casting state 4 and spend
mana. An empty final string alone cannot pass.

The original `report.json` is retained with its obsolete full-string expectation.
`report-native-state-audited.json` re-reads the same untouched RAM evidence with
the corrected native-state observer: all 31 recognition/cast acceptance checks
pass. The current runner reports this precise scope as
`native_recognition_and_cast_acceptance`.

**Separate limitation:** no spell projectile appears in the samples 15, 25, or
40 frames after attack. Native state 4 and mana cost prove cast acceptance, not
visible projectile travel, collision or damage. `projectile_checks` remain false
and `projectile_validation` remains false; these are explicitly separate from
native gesture acceptance. Optional `--trace-release` enables the existing
bounded function tracer for native release (`0x38cca0`) and Fireball setup/update
(`0x397650`, `0x398370`) for a later emission investigation. This trace has not yet
been run. No additional runtime defect or fix is inferred from the missed
projectile snapshots.

Ten Python tests now pass, including final success/reset behavior, rejection of
an incorrect selected spell, and independent projectile-visibility status. Ruff
checks and formatting pass. After the paired run, zero `drakan` processes were
confirmed and the live slot was transferred to the directional-attack probe.
