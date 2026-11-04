# Imported bow ammunition and rigid template validation

This work addresses actual original Long Bow creation, nocking and shooting.
It adds no runtime cooldown, velocity, damage or flight override. The game
fixtures inject genuine converted Long Bow and Arrows objects into native
Surdana for diagnosis; they do not establish earned OotF campaign acceptance.

## Projectile mapping acceptance

`build/harness/bow-source-pair75-arrow95` quits cleanly with exit 0. The private
pack derives from the earlier rigid-header diagnostic and adds a real
production-converted Standard Arrow with collision-free class ID 18573 and
model ID 30000. Its field schema is appended to the existing field-list entry;
Arrows 343b2 field 22 points to this class. The authored Standard Arrow model
converts to 71 triangles and two textures. Three unresolved effect/piece
references are explicitly cleared in this bounded fixture; this does not
establish special-arrow effects fidelity.

Native logs identify actual equipped Long actor cf68c0, Bow DATA e8eae0 and
resource 343e2. A real nock handle 2981 is created. Six launch entries occur at
combat-audit frames 1190, 1247, 1304, 1361, 1418 and 1520. The five rapid sight
shots are 57 frames apart under the current native behavior. The final launch
occurs during running/airborne flags 10 with sight retained and the corrected
shot-basis log. No experimental cadence adapter is enabled or logged.

`external_ammo_metrics.json` records native quiver DATA e8ec70: count at +2c
falls from 10 at sight entry to 5 after five sight shots, then 4 after the
running airborne shot; it stays 4 through held run, release and stop. DATA +14
is encoded durability, which remains 100; it is not arrow count. Bow durability
independently falls from 75 to 69. The earlier first third-person press precedes
actual equip and does not count as third-person launch acceptance.

## Production rigid Bow template scope

Original Long is rigid with no model 0208 skeleton/controller record. Native
animated Bow templates have class flags 33; inheriting them leaves actor +50
null and causes the native scene walker 10822c to dispatch through a null
controller. Native Villiager Cross Bow 7835 supplies a valid rigid comparison:
class flags 1, model 4570 without 0208, and load/loaded animation IDs 0.

The converter now restricts native templates only when engine 79 actually
requests model conversion and source `modelSkinned` explicitly returns false.
It requires a complete raw native rigid template with header flags 0 or 1,
rather than clearing an animated template's flag. No compatible template is a
reported unsupported conversion. Same-name skill-only cloning, unknown source
model layouts and skinned models keep their existing behavior.

The regression verifies selection of the full rigid template, source fire-rate
merging, native rigid animation defaults, unchanged unknown/skinned behavior,
and a clear unsupported result when no rigid template exists. 28 focused tests
and formats typecheck pass.

## Complete-template live acceptance

`build/ootf/make-source-pair-rigid-template.ts` uses the production planner on
actual original Long 3104 and all native Resources templates. It selects
Villiager Cross Bow 7835, not an animated Bow patched by hand. The output retains
the original converted Long model 7016 and inline bounds, source fire rate 1.75,
zero internal charges/external-ammo semantics, native rigid flags 1, and native
load/loaded animation IDs 0. The emitted native field schema exactly matches
the replaced Bow record; the private fixture retains resource 343e2. Report:
`build/ootf/bow-source-pair-rigid-template/report.json`.

`build/harness/bow-source-pair76-rigid-template` quits cleanly with exit 0. The
same authentic Long actor cf68c0 equips, creates nock 2981 and produces six real
launches at frames 1180, 1237, 1294, 1351, 1408 and 1510. External count again
falls 10 to 5 to 4 and remains 4 through held run and stop. At frame 1509 the
native audit records a new mouse press, forward movement and airborne flags
10; the launch at frame 1510 records flags 90 (airborne plus attack), retained
sight and a coherent corrected shot basis. This proves a real shot from the
converted rigid Long while running and airborne, without changing shot speed.

Current rapid native sight intervals remain 57 frames despite authored fire
rate 1.75. This is characterization, not a claim of original cadence parity:
original input acceptance, reattack-window and launch-event timing still need
comparison. The inherited native reload and other unmatched defaults, source
sounds/effects, held-bow visual representation and other special-bow behavior
remain separate fidelity work. No cooldown experiment is present in source.
