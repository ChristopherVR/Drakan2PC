# Referenced-only OotF engine coverage

The November 5 source census starts from all 1,983 distinct placed classes across
the 23 source levels and follows typed class defaults using the owning ODB's
dependency namespace. It reaches 2,875 classes. Before the Arrow/Spell Projectile
95 catalogue repair, 310 of those classes have engine IDs absent from
`ENGINE_CLASSES`, spanning 27 engine IDs. All 310 are referenced-only.

This is a catalogue-coverage audit, not a count of 310 failed conversions:
same-name native mapping can succeed before catalogue classification. A native
engine ID or matching field schema alone also does not establish equivalent
runtime behavior. The imported quiver's lost Standard Arrow reference is separately
confirmed in emitted data and the native nocking path.

Private reproducible inputs and output are
`build/scratch/referenced-engine-census.mts` and `.json`, using the user's extracted
source install and PAL ODBs. No source assets or field-default payloads are included
here.

| Missing engine | Referenced classes | Matching source field names/types in native schema | Examples |
| --- | ---: | --- | --- |
| 3 | 23 | 8/9 | Explosion radius damage |
| 21 | 3 | 9/9 | Flame Breath Stream |
| 42 | 3 | 31/32 | Ice weapons |
| 48 | 4 | 32/33 | Fire Ball |
| 70 | 2 | 33/34 | Poison breath |
| 85 | 1 | 31/32 | Lightning ball |
| 95 | 11 | 37/38 | Standard, flame and explosive arrows |
| 106 | 35 | 25/26 | Projectile trail links |
| 107 | 37 | 9/9 | Projectile trails |
| 108 | 8 | 34/35 | Magma shots |
| 122 | 44 | 44/45 | Following projectiles |

Counts in the schema column use the first source class and first native schema
for each engine; they are not a proof for every class default. The unmatched
field for these damage/projectile rows is a sound frequency-shift range, except
the fully matching stream/trail rows. Other missing engines are 5, 31, 32, 38,
50, 67, 69, 90, 96, 113, 119, 124, 125, 135, 139 and 140. Native candidates were
found for 23 of the 27 IDs; none were found for 38, 69, 96 or 113 in the extracted
ODB set. Those four require separate behavior investigation, not arbitrary
same-number substitution.

The next repairs should retain source references and defaults through the existing
typed merge and relocation paths, then verify the relevant native behavior.
Converting a new referenced class can change generated class IDs; regenerated
packs must be audited before loading earlier earned cards. Neither catalogue
coverage nor a successful isolated projectile test establishes ordinary campaign
completion.
