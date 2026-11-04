# Bow launch owner isolation

Reviewed `runtime/input/src/BowAim.cpp`, its fake-guest tests, the native bow evidence in
`docs/reference/input.md` section 16, and the earlier combat investigation notes.

The modern mouse adapter reserves a 120-input-frame launch window after an aimed
release. `beforeBowLaunch` resolved the launching bow's owner, but only checked
that an owner existed. It then used the local player's camera and target lock.
Consequently another actor's bow launch during that window could receive the
player's retarget and consume the window before the player's arrow launched.

Require the resolved owner to equal the current context player **actor** before retargeting.
The regression simulates another valid owner launching first, checks that the
arrow matrix is unchanged, and then verifies the local player's launch still
receives its aimed direction. Existing native target-lock and one-retarget checks
remain in place.

Validation: rebuilt target `bow_aim_test` in the existing `build/runtime-tests`
Release tree and ran `build/runtime-tests/bin/Release/bow_aim_test.exe`:
**40 checks, 0 failures** for the initial fixture. No game build, deployment, or live combat run was done
for this change.

Evidence limit: this corrects isolation of the existing mouse aiming adapter.
The local bow documentation establishes PS2 launch animation and measured aiming
behavior, not exact Order of the Flame combat parity. Original-PC charge timing,
damage, ammunition, and collision fidelity were not established by this test.

Follow-up correction during native launch review: the initial guard compared the
owner actor to the player's data object, and the initial fixture incorrectly used
one address for both. That would suppress the modern retarget in the real game.
The guard now compares with `ctx+0x4b0`, while player data still comes through
`actor+0xfc` / `inner+0x10`; the fixture uses distinct actor/data addresses. The
expanded bow suite passes **59 checks** after this correction and the default
input/autoaim changes described in `bow-ootf-reference.md`.
