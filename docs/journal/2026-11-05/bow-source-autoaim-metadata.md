# Source Bow automatic-aiming metadata

## Source and native consumption

Original Bow launch `100b0360` reads source fields 21 (Cone of AutoAim, degrees)
and 23 (Maximum Range, world units), converts their scalar integer values to
floats and passes them to `100e1f10`. That selector computes cosine of degrees
with factor 0.01745329238474369, rejects distances beyond the range, and ranks
angular/distance error with weights 0.35 and 0.65. Native selector `49ec60`
consumes the same degree/range units and uses the same weighting structure.
Read-only exports and instruction evidence remain under
`build/ootf/rfl-cadence-followup`; original assets/code are not committed.

Actual authored source Long/Heavy/Speed use cone 25 and range 16000. Energy Bow
4092 uses cone 35, range 16000; FlameBow 4329 uses cone 25, range 18000. Native
Bow has no equivalent cone/range fields, so ordinary named-property merging
cannot retain them. No trajectory or other stat policy is added here.

## Bridge and scope

The converter now emits `gameplay/bows.ini` for synthesized original Bow
classes with verified scalar integer cone/range fields. It omits absent,
ambiguous, indirect, foreign or invalid schemas and any target shared by
unknown/conflicting profiles. It never annotates a native class merely reused
by name. Emission is restricted to the verified Resources bank 3 namespace.
`build/ootf/bow-level-resource-namespace.json` checks all 23 preserved converted
LVLs: Resources is dependency 3 everywhere and all 18 placed imported bows
(six classes) use that bank. Native live resource 343e2 matches the same identity.

The optional sidecar has `format = 1`, with `[bow 0x<full-resource-id>]` sections
and `cone_degrees`/`maximum_range` keys. Enabled packs supply it in load order;
later valid profiles win. Files are bounded to 64 KiB and the input table to
4096 profiles. Nonfinite values, negative ranges, cones outside 0..180, invalid
IDs, duplicates within a section, incomplete profiles and unknown versions are
rejected. Empty reload/removal clears the prior table. An authored zero range
means no automatic selection. Unknown/unmapped bows retain 25/16000.

Only the existing local-player third-person automatic-aim path consumes these
properties, after its ownership, mode, lock, gameplay and scripted-state guards.
It verifies the actual actor's registered native Bow class 1004f and full
resource identity before lookup. It passes the chosen values to the real native
selector, without writing shared class fields or creating a persistent target
lock. Existing modern/controller/manual-sight/NPC exclusions remain.

## Actual imported-bow comparisons

Canonical build passes, immutable private executable
`build/ootf/bow-metadata-play/drakan.exe`, SHA256
A5AF374F62F333566D99906D81C791DF9E33E1E42F04FD1B0D13A0711712643C.
Built through `dist/DrakanPC-implementation-next`; the immutable private copy
retains A5AF while staging may advance for other work. Primary play folders
were not deployed by this batch. Existing packs need re-export for the new
sidecar. Live tests use D3D11/ray effects off to isolate gameplay.

`make-native-bow-metadata-fixture.ts` creates actual source classes through the
production asset builder, class planner, reference relocation and native
creation/equip path. Energy includes its authored Energy Arrow Quiver 4094 and
projectile 5013; Flame uses its authored external-ammunition behavior. These are
injected native-Surdana diagnostics, not earned campaign acceptance. Energy's
seven referenced effects/explosion classes remain explicitly omitted in this
bounded fixture; it is not complete special-arrow effects acceptance.

For each bow, the enabled/disabled candidate archives are byte-identical except
for `gameplay/bows.ini` (`sidecar-only-difference.json`). Each native run cleanly
quits and its `metadata_acceptance.json` contains exact launch/property/count
measurements:

- `bow-metadata82-energy-on`: actual Energy resource 3488e, two launches at
  frames 1831/1981. Both native selector calls consume cone 35/range 16000.
  Owner is 4; current charges decrease 50 to 49 to 48. Charge limit +14c stays
  50; encoded current charges are DATA +14. External quiver remains 10.
- `bow-metadata83-energy-off`: same source class/pack/executable, launches
  1843/1993 and charges 50 to 49 to 48; selector uses fallback 25/16000.
- `bow-metadata84-flame-on`: actual Flame resource 3488e in a separate fixture,
  launches 1663/1813; both selector calls consume 25/18000. External arrows
  decrease 10 to 9 to 8; Bow durability decreases 30 to 29 to 28.
- `bow-metadata85-flame-off`: byte-identical class/model pack without sidecar,
  launches 1851/2001 with the same counts; selector falls back to 25/16000.

The calls return no target in this empty-floor diagnostic. The live proof is
that authored values reach the exact native selection function, whose degree,
range and ranking consumption is established by source analysis. It does not
claim an enemy-acquisition/occlusion comparison or complete special-bow combat.

Validation: bow_aim_test 176 checks, zero failures; mod_test 140 checks, zero
failures; three new converter tests pass and formats typecheck passes. Root's
broader editor run also passes. Tests check actual native GuestCall arguments,
full resource/class identity, ownership exclusion, malformed files, fallback,
source-schema uncertainty, conflicting deduplication and enabled-pack load order.

## Remaining fidelity boundary

Original launch uses the shooter's position (1001acb0) and its arrow pose's
negative first basis axis (10001f10 then 100073b0) as selection ray. The current
adapter keeps the camera ray. The parameter bridge does not resolve that
origin/basis difference, sight FOV/accuracy or exact original reattack/cadence.
The local original executable still exposes no usable comparison window.

Early native diagnostic `bow-source-pair81-early-rays` follows the known nocked
actor through launch rather than enumerating object handles: free arrows
release their table handle while remaining live. The actual Standard Arrow
resource 3488d moves across the 44/61/78/100 ms samples. Its standing/airborne
launch direction is identical and about 10.79 degrees from the camera ray;
pre-launch airborne nock pose differs by about 64.9 degrees. Thus a nock pose
is not launch-direction proof. Private inline bounds were corrected to the
production builder bounds before that measurement. No velocity was changed.
