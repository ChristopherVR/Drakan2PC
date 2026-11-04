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

The parent generated `build/scratch/ootf-classref/mods/ootf-levels.zip` from a private baseline with only indexed
terrain and this class-reference repair. Its 1,990 synthesized class records have no dangling emitted class references
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
