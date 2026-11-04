# OotF class-reference defaults and native heap pressure

The indexed terrain candidate removes one demonstrated texture working-set problem, but is not yet a complete
normal-start campaign acceptance. The following investigation keeps allocation evidence separate from that result.

## Cache and heap observations

`build/scratch/ootf-startup-probe/normal-phase/ram_phase_wait.ram` has texture manager `0x4db280` budget
`0x566666` and usage `0x564400`. Native `1bf3e0` checks usage + request + 200 against that budget; `12c850`
evicts oldest eligible descriptors. Eligibility requires zero references, flags `0x20`, and no pin flag `8`.
Large allocations inspect at most 96 candidates; buffer reuse requires the exact size. Converted 128-square RGBA
terrain entries of 65,600 bytes compete with this 5.4 MiB native cache. The manager mutex has zero contenders in
the three sampled RAM dumps; those samples do not demonstrate a stuck mutex.

The later indexed normal-opening fixture `build/scratch/ootf-indexed/normal-full` reaches the native heap ceiling.
A physical malloc-chunk walk from `0x5b8818` reaches the exact top in every snapshot, excluding the small bootstrap
prefix. In-use bytes exclude normal free chunks and fast-bin members:

| Snapshot | In-use MiB | Free bytes, excluding top |
| --- | ---: | ---: |
| RV +15 | 13.447 | `0x1550` |
| RV +75 | 16.783 | `0x32c30` |
| RV +135 | 19.541 | `0x3880` |
| RV +216 | 24.190 | `0x3fa50` |
| after walk | 25.036 | `0x3d7d0` |

Thus high-water growth is accompanied by growing in-use allocation, rather than a large accumulated free working
set. The largest growth is 1,278 additional `0x1610` malloc chunks: `0x1600` user bytes = 128 records of 44 bytes,
7,218,144 additional bytes. Pointer vectors lead to embedded native particle containers attached at actor + `0x58`.
Sampled owners resolve to native FX classes 6266 `part :dust devil` and 6380 `part: dust devil area`, each configured
for 1,000 particles. Their live actor counts grow from one pair at RV +15 to 79 pairs after the walk. The original
watchdog's fast-bin consolidation loop is downstream evidence; no allocator expansion or loop bypass was added.

## Converter defect and repair

The native generator template's `Spawn Object` is a typed class array (`0x1003`) naming those dust effects.
`mergeDefaults` handled only scalar class references. The pipeline supplied no class-default reference resolver and
planned placed classes only. Consequently source generators could retain unrelated template spawn lists.

The repair discovers the transitive class-default dependency closure before assigning target ids, preserving forward
and cyclic references. Reference-bearing clones retain distinct source identities during deduplication. After the full
plan exists, scalar and array defaults resolve through the source owning ODB's dependency table, then through the
destination ODB's dependency table. Array counts and indirect payload offsets are rebuilt. References without a safe
target are explicitly diagnosed and omitted/zeroed instead of retaining unrelated native effects.

All 323 formats tests and TypeScript checks pass. Synthetic coverage includes typed array relocation, explicit failure
diagnostics, referenced-only cyclic generators, and otherwise identical generator defaults that require distinct clones.

## Isolated candidate audit and remaining acceptance

The parent generated `build/scratch/ootf-classref/mods/ootf-levels.zip` from isolated formats source `ad867c7`,
indexed terrain and this class-reference repair. That source already contains cinematic speech absent from the older
137,794,044-byte baseline pack, so the resulting pack is not a one-variable class-reference comparison; see
[normal opening/control evidence](ootf-normal-opening-control.md). Its 1,990 synthesized class records have no dangling emitted class references
and no references to the native dust-devil classes. The Ruined Village Soul Flare generators now name Soul Flare
01/02/03; the dragon explosion generator names the 15 source dragon body parts.

This is not complete effect or campaign acceptance. Soul Lightning references `0xe47` / `0xe49` are explicitly
unresolved; other warnings include creature weapon objects and weapon effects. These need classification before
claiming full fidelity. The normal-start candidate must still demonstrate stable heap occupancy, no native dust swarm,
actual player control, and genuine progression. Prior isolated low-HP boss kills reaching credits do not establish an
uninterrupted campaign run.

## First candidate live result: dust producer remains

`build/scratch/ootf-classref/normal-control` mounts the repaired candidate (9,580 new entries). The RV +15/+75/+135
RAM snapshots still contain 1/6/24 pairs of native dust-devil actors, exactly the earlier baseline counts. In-use heap
occupancy is lower at 11.12/14.46/16.69 MiB, with 19/110/365 large particle pages, but the dust producer remains.
Therefore the class-default defect is real and repaired in the output; it is not established as the complete cause of
the dust swarm or heap exhaustion. A bounded native factory observation is needed to identify the remaining creator.

The parent separately reconstructs the placed RV generator classes and their object overrides: the Soul Flare/dragon
body references match the static audit, the unsupported Soul Lightning lists are empty, and no placed Spawn Object
override restores the dust references. Host logs using a cached player pointer also became stale; garbage at that
stale address is not evidence that the real `ctx + 0x4b0` player handle is corrupt.

## Referenced-only engine kinds

The first conversion reports 439 unresolved references across 138 classes. A second concrete gap is the engine-kind
table: it listed placed types only. Referenced-only source types 1 (expanding/fading effects), 8 (weapon projectiles),
and 58 (lightning) have matching native same-id class schemas and imported-model support, but were rejected as
unknown kinds. These are now classified, with tests covering class/model serialization and generator-to-lightning /
creature-to-projectile references. This includes source Soul Lightning 1/2, Ebon Dragon's Sonic Boom, and Suc Attack 2.
The revised isolated candidate must be audited and tested separately; the first live candidate stays unchanged.

Source types 69 and 81 have no matching native same-id class in the extracted full disc ODB set. Poison Gas 69 uses
cloud texture, spawn rate, effect/particle life, size, upward speed, color, damage per second and sounds. Native smoke
class 43 shares four fields, but its shape, rate and damage behavior need an explicit supported port. Solve Magic 81
is a procedural spiral/ribbon with cycles, radius, ellipse and motion controls; the nearest native schemas share only
Face Camera. No numeric substitute or unsupported claim of equivalent behavior was added.

The second isolated pack, `build/scratch/ootf-classref-kinds/mods/ootf-levels.zip`, contains 2,126 synthesized classes.
All emitted class references resolve. Of 1,816 cloned-class model references, none is missing against the generated
assets and available original archives; 38 use original archives not extracted locally and remain unverified by this
audit. The Soul Lightning generators now reference imported Soul Lightning 1/2. Ebon Dragon's Weapon Objects names
Sonic Boom, whose references name Sonic Wave, Sonic Ring and the source explosion effects. Succubus names Suc Attack 2
and its source explosion children. Remaining diagnostics number 430 references across 135 classes, including the
lightning strike explosion `0xeaf`; this is not a claim of complete visual/weapon fidelity. Live acceptance remains pending.


### Hidden companion class in native class headers

The factory trace narrowed the continuing dust spawns to the native object generator (return `0x2d1adc`, containing function `0x2cfb28`). Comparing native generator positions with the source RV objects found exact matches: object 2767, `river current` (rfl111), is at `(326867, 8024, 147714.34375)`, and object 2763 is at `(274430, 8024, 107693.3359375)`. Both positions held native FX6288 Dust Devil generators. The runtime resource registry, not a copied field default, identifies those generators: actor u16+0x10 indexes the 36-byte registry records, whose +0 resource is `0x41890` and +8 runtime class is `0x1003f`.

The generated current class FX12225 correctly declares rfl111 and its six field defaults contain no class references. Its raw header nevertheless retains the native template's auxiliary class reference: u16 at body+10 is 6288 (`0x1890`), and body+12 is dependency 0. Native current FX6394 and generated FX12225 retain the same companion reference and model-presence marker. Runtime current resource `0x42fc1` likewise resolves its companion to registry index `0xd14`, the Dust Devil generator. This explains why repairing field-list class references did not remove the dust.

PC class records explicitly contain group, model id/dependency, rfl id, icon, counts and data (`pc.ts: parsePcClassRecord`). They have no corresponding auxiliary header class reference. Synthesized PC-derived clones now clear the unrelated native companion at body+10/+12 while preserving body+14, which marks the model/bounds block. Actual PC class-typed field defaults continue through the owner-relative closure and relocation path. Air/water-current classes also import their source volume model and bounds rather than borrowing the native dust-current volume through the generic logic-class path. Named native stand-ins remain existing native records; this change concerns synthesized imported records.

Regression coverage checks the Dust Devil companion is absent, the source model/bounds are bound, the model marker is retained, and the same header rule applies to ordinary scenery. Full formats suite: 325 tests across 30 files passed; TypeScript compilation passed. This is a converter repair with a concrete native spawning mechanism; heap stability and normal campaign progress still require the next normal live run. The separate Trail Master (81) synthesis experiment was saved privately and excluded from this candidate.

The prior indexed/class-reference candidates are not a strict single-variable heap comparison with the older cached pack: regenerating the historical source baseline also added 110 previously committed RV1.1 sound actions (224 to 334). Lower measured occupancy in those candidates cannot be attributed solely to class-reference repair.

A production-path review also found that class bounds were always fetched from the root asset builder, even when the model id belonged to an FX/NPCS cross-builder. The bounds callback now receives the destination ODB and model dependency index, and chooses the matching asset builder. Equal numeric ids in different MOD namespaces no longer select unrelated root-model bounds. The focused current regression verifies that destination namespace reaches the callback; emitted header bounds must additionally be compared with their actual candidate MOD entries before live acceptance.

The source RV level contains exactly 61 rfl111 current objects, matching the 61 native Dust Devil generator actors counted in the RV75 snapshot. Together with the two exact position matches and the retained header's resource-registry binding, this identifies the producer independently of particle-page counts or the older audio comparison.

The rebuilt header candidate (CD09A5D52AFA45C6FBCBE148B0F9AE2B1162E9F513C4E2CFD51D18FC87C0825C) passed emitted-pack checks: every synthesized record has a zero auxiliary companion reference; all nine rfl111 class records (eight FX, one LEVEL1ON) use converted local models, retain model marker 1, and cache exactly the 64 bytes at destination MOD 0x0207 bounds payload[8:72]. Evidence: build/scratch/ootf-header-fixed/header-current-audit.json. Live stability remains pending.
