# Bow cadence continuation and inherited weapon skill

The current scoped fix concerns conversion eligibility, not projectile statistics.
Native equip predicate `341858` reads the item's scalar property `0x13` (Skill
Level), chooses stats row 2 for Bow `1004f` or row 1 for melee, and compares
required level <= player level. Native `31e460` calls this predicate before
equipping. Private source exports remain under
`build/scratch/ootf-held-weapon-native`. In RV fixture71 the actual stats vector
at `4fbac8` has Archery 1 and Melee 1; the imported Long Bow requires 2.

The latest immutable source pack is
`build/scratch/ootf-rotation-fixed/mods/ootf-levels.zip`, SHA256
`7DBB6D420C6A15EF64915D9C63573AF5F7B9484961BF26FA60993545B3F72AA9`.
Its Long Bow is Resources class `43e2` (full runtime resource `343e2`), not
`43df` from the older primary pack. Actual emitted properties are Skill Level
2, fire rate 1.75, reload rate approximately 1.15, energy charges 0 and Ammo
Object 0. The original PC Bow schema has no Skill Level field. Fire rate is
already copied correctly by field matching. The older primary pack's retained
native Ammo Object 14271 is not evidence about this latest pack.

Conversion now clears an inherited skill gate only for same-engine original
melee14/Bow79 classes whose source schema is known and lacks that field. The
target must have exactly one scalar-int field named Skill Level. Unknown schemas,
other engines, duplicate/array/indirect/float/enum target fields, and explicit
source requirements remain unchanged. A same-name restricted native weapon is
cloned using its existing native model, preserving both the shared native record
and the earlier model-selection behavior. Explicit diagnostic name mappings
retain their requested target. No ammunition, flight, damage, fire/reload rate or
player skill values change.

Validation: 34 class-mapping/reference/starting-inventory tests and formats
typecheck pass. Model-backed same-name regression proves an unavailable source
model callback is not called and the native model survives cloning. A private
real-schema audit (`build/ootf/audit-long-skill-fix.ts`) compares the production
merge with an otherwise identical unsupported-engine control merge: its only
new difference is Skill Level 2 to 0. It also emits a diagnostic class record
with only that scalar patched, preserving the actual pack's other data. This
is an eligibility correction; actual imported Long equip/fire remains unproven,
and its external-ammunition path is a separate blocker.

## Measured cadence and rejected timer experiment

Private `bow-cadence61` uses the previously validated family executable and
native Short fixture. A nine-second hold consumes 200 to 199; release consumes
nothing further. Eight 100 ms presses 750 ms apart consume all eight arrows
(199 to 191), with native launch entries 45 guest frames apart. A genuine jump
press consumes 191 to 190 while flags94 is `90`; the later grounded press reaches
189. `cadence_metrics.json` uses the Bow DATA address from actual native launch
entries and decodes its quantity word, not the actor transform.

A diagnostic canonical build produced executable SHA256
`111E38EE40AB2ED98EC82EC79880716ACE3628B531EFB37F3493393909C41DF4`,
retained privately in `build/ootf/bow-cadence-play`. A gated candidate divided the
new native sight deadline by the actual Bow fire-rate field, with exact caller,
owner, class, reader, finite-value and repeat-write guards. Its 174 focused
checks passed, but that does not establish original timing. The candidate was
removed from all four source files; the private patch remains
`build/ootf/cadence-candidate.patch`. No test toggle remains in source.

Native Short controls `bow-cadence-short-off65` and `bow-cadence-short-on66`
cleanly quit. Both identify actual fire rate 1 and consume 200 to 193, with
rapid sight-launch intervals 56-57 frames off and 57 frames on. Both retain ammo
193 through a real held-fire jump (flags `10`), later running and final stop; a
hold does not synthesize repeated shots. The third-person post-shot reload
pose runs at speed 1.25, independently of the fire-rate field. These are native
controls, not original-PC runtime comparisons or proof of rapid new presses
while airborne.

Original PC `1005e9d0` supplies property17 as Archer playback speed;
`1005d990` passes it to the animation setter. Original `100b1c20` observes Archer
phase to create the replacement nock. Native `20e6c8` instead sets sight deadline
B38 using the unscaled Archer Reload clip duration; `20ff00` waits for it.
However original ArcherMedium719 is approximately 1.933333 s and native
Bow_Reload2198 approximately 0.933333 s. Dividing the latter by 1.75 cannot by
itself reproduce the original cycle. Original Re-attack Window property165 is
0.06 s; attack queuing and actual launch-event timing still need tracing.
No new cooldown is justified from animation lengths alone. Local original
retail assets/executable exist, but the earlier patched runtime install is no
longer present and the prior native Windows attempt rendered black; no original
runtime launch-event comparison is claimed.

## Imported Long diagnostic fixture boundary

A raw native-save class-word substitution failed load and is not a supported
editor export. RV-first native creation probes70/71 do instantiate the actual
latest Long class `343e2`, confirmed by registry class `1004f`, but it remains
owner 0 with empty Rynn inventory and no native launch. Probe71 reuses the
original earlier linked item 1059 before Rynn 1740, rather than appending an item
after Rynn. The parsed emitted LVL links are correct; global `4fbaf8` is 0 at the
later snapshot, which does not establish its value at initialization. These
failed fixtures are not campaign or cadence acceptance. Current PC-derived
Long has Ammo Object 0 / energy charges 0, so the native external-ammunition path
must be established before interpreting a missing shot as a cadence failure.
