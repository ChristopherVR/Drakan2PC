# Inventory categories, Equipment and movement validation

## Recorded result

On 2026-10-17, `inventory-category-movement-final-01` passed all 23 native
RAM/log assertions against executable SHA256
`9E622D7EB0B72E9BAEA16F67B1C156813A94E5DA7FDCC1B48DEF24F55BB734DC`:

| Suite | Result | Observation |
| --- | --- | --- |
| Categories | 7/7 | All five native pages; outside icon gap leaves page/selection/equipment unchanged and emits no adapter tap; return to Equipment |
| Equipment | 7/7 | Fixture cells, dagger selection/equip, empty-cell nonactivation, bow selection/equip |
| Movement/bow | 9/9 | Mouse pitch/heading and return, forward/strafe, native bow, ammo consumption, released attack state, Quick Inventory closure |

The run used pacing 2, scale 4, SR off and NR off. Scripted exit was clean
(exit code 0) after 94.5364119 seconds; executable hash was unchanged.
The encounter runner verified both original saves and fixture saves unchanged.
Bow quantity fell from 200 to 198; settled attack state was zero.
Observed pitch change was 0.0400000028 radians and heading change
-0.0800000221 radians for expected changes +0.04 and -0.08.

Private evidence was recorded under
`build/t/inventory-category-movement-final-01/`, including
`combined-validation.json`, `completion.json`, `encounter-manifest.json`,
`input.log`, RAM dumps and screenshots. These private artifacts are not committed.

## Final SR Quality and NR confirmation

The completed `followup-final-combined-srnr` run used the same executable hash
and exact command sequence, with SR Quality, NR initially enabled, pacing 2
and scale 4. All three published read-only validators passed again: categories
7/7, Equipment 7/7 and movement/bow 9/9 (23/23 total). Normal scripted quit
occurred at native frame 2297, exit code 0, after 122.446667 seconds; executable
hash was unchanged. Bow quantity fell from 200 to 197 and settled attack state
was zero. NR test records show off/on/off/on at ticks 1900/2001/2201/2300.

After completion, full file lists and SHA256 values for both `mc0` and `mc1`
matched across all four roots: the native run's `fixture-bow-item` source,
its `inventory-category-movement-final-01` private cards, the SR/NR run's
`inventory-phase-calibration` source and its `followup-final-combined-srnr`
private cards. Each root contained the same four files, including icon metadata.
Original saves also matched the native run's recorded prelaunch baseline.
Independent during-run and after-run hashes agreed for original, SR/NR source
and SR/NR private cards. No independent prelaunch SR/NR source hash was available;
the cross-root comparison records the completed files, not a separate prelaunch
observation.

Private JSON evidence under `build/t/followup-final-combined-srnr/`:

- `category-validation.json`, `equipment-validation.json`, `movement-validation.json`
- `independent-23-check-summary.json`
- `native-srnr-card-comparison.json`, `independent-save-validation.json`
- `completion.json`, `manifest.json`

PNG captures were present, but these native state checks do not establish
visual quality or performance. No additional GPU run was needed for validation.

## Reproduce

Use an owned supported game and an existing `fixture-bow-item` save with dagger
in cell 0, bow in cell 1 and empty cell 7. The runner makes private card copies.
Use the default direct mouse sensitivity .002 on both axes, no inversion, and
the whole inventory page horizontal scale .75. The recipe's virtual client is
640 by 512. Its positions came from live native category/grid bounds, rather
than guessed screenshot hotspots. Recalibrate if page mapping/layout changes.

From the repository root in PowerShell, after obtaining the GPU slot:

```powershell
$env:PS2X_GS_PACING='2'
$env:PS2X_GS_SCALE='4'
$env:DRAKAN_DLSS_LOG='1'
./scripts/harness/combat_encounter_run.ps1 `
  -DecompRoot $PWD.Path `
  -BinDir "$PWD/build/integration_followup" `
  -Script "$PWD/scripts/archive/2026-10-17/combat_category_movement_final.txt" `
  -SaveRoot "$PWD/build/recomp_combat/encounter/fixture-bow-item" `
  -Name 'inventory-category-movement-recheck-01' `
  -TimeoutSeconds 155 -DlssMode off -AuditEvery 0
```

Choose a fresh evidence name. No game launch is needed for the following
read-only checks; each returns nonzero on failure:

```powershell
$run='build/t/inventory-category-movement-recheck-01'
python -B scripts/archive/2026-10-17/combat_inventory_categories_ram_check.py $run
python -B scripts/harness/combat_inventory_mouse_ram_check.py $run
python -B scripts/harness/combat_movement_followup_check.py $run
```

The category checker requires `input.log` and the eight
`ram_cat_{baseline,equipment,spells,skills,map,journal,outside,return}.ram`
snapshots. It reads native screen 21, current page, native key handler,
Equipment controller state and Journal state. The outside-gap interval must
contain no `inventory mouse input` record. Missing or malformed evidence fails.

The recipe confirms first-visit native help using ordinary Enter, retaining
prehelp RAM snapshots. After Equipment return and grid checks, it closes
inventory with Tab and appends the original `combat_movement_followup.txt`
490..850 segment shifted by +855 frames. Existing checker RAM labels are
preserved. It exits at script frame 1705.

## Scope and limits

This establishes absolute selection of the five top category icons. It does
not establish absolute selection within Spells, Skills, Map or Journal contents.
The bow assertions establish native weapon/ammo outcomes, not an enemy hit or
damage. The 23 checks do not validate image quality, SR/NR toggles, performance
or presentation. An SR/NR run preserving these RAM labels can reuse the same
read-only validators, with separate rendering evidence required.

The outside-gap regression was observed before `53e0946`: moving across the
category row leaked a relative navigation tap despite unchanged native page
state. The final recorded run includes that fix and passes the stricter
no-adapter-tap assertion.
