# Original PC bow reference and adapter changes

**Scope correction, 2026-11-04:** the user clarified that aiming/shooting behavior
is the issue, and arrow velocity was never a problem. The damage, gravity,
acceleration and lifetime experiments recorded below were therefore reverted,
including their native hook registrations, shared helper and dedicated tests.
They are historical investigation evidence, not the delivered behavior. The
owner isolation fix, press/sight defaults and aiming work remain. Current work
focuses on original click/hold cadence and movement/jump firing evidence.

Primary reference: [original publisher manual](https://www.arokhslair.net/downloads/Drakan_Manual.pdf),
printed page 15 (PDF page 17), visually inspected on 2026-11-04. The bow section
specifies attack on press, third-person automatic aiming toward a creature near
screen centre, and an alternate-attack toggle into and out of a zoomed first-person
sight. This contradicts the port's previous default hold-LMB/aim/release-to-fire
adapter. The manual does not establish exact cadence, cone angle, range, or damage.

Default input is now `bow_aim = ootf` (`classic` remains an accepted alias): the
native attack receives the press immediately and RMB release leaves the sight
open. Explicit `bow_aim = modern` retains the former optional hold/release behavior
and held-RMB sight. Settings serialization, defaults and the Controls description
agree. Existing explicit modern preferences are not automatically overwritten.

Private static analysis of the user's PAL engine: native launch `0x20e6c8` reads
the owner actor handle at bow+0x60, resolves player data through +0xfc/+0x10,
and reads player target handle +0x4da. With a target it retains native ballistic
calculation and arrow property handling. Ordinary arrows with no selected target
do not acquire one; homing arrows use target selector `0x49ec60`. That selector
uses engine active actor lists, checks visibility/collision and life state, and
accepts an angle, range, owner actor, ray position and direction. Research outputs
remain untracked under `build/ootf/bow-analysis`.

The new third-person adapter calls that selector along the camera ray, temporarily
supplies its selected target during the native launch, then restores the player's
target. It avoids creating a persistent lock or changing the camera. The native
launch runs without scheduler preemption while borrowing that target. Sight shots,
explicitly locked shots, controller-owned shots and other actors' bows are excluded.
Follow-up local asset parsing (`build/ootf/bow-properties.jsonl`): original PC
Long / Heavy / Speed Bow classes explicitly store cone 25 degrees, range 16000,
FOV 40 and rate 1.75 / 1.75 / 2.25. The adapter now uses that standard cone/range,
superseding its initial 10-degree/20480 tuning. Energy Bow stores cone 35, FlameBow
range 18000; these special values are not yet preserved as runtime metadata.
The PS2 bow class has no matching cone/range/accuracy fields, so merging shared
field names cannot preserve these extras. Ammo, trajectory solver and animation
cooldown remain native. Their equality to PC values is open.

Read-only comparison attempt: retail executable under `build/ootf/cab_disc` was
launched with `+setup`; it did not expose a usable window and exited. Historical
`build/ootf/install` noted in older journal entries is absent. This attempt does
not establish an original-game runtime comparison.

Focused validation: `bow_aim_test` **59 checks, 0 failures**; `input_test` **3441
checks, 0 failures** after the press/sight/default changes. Bow tests cover actor
versus data ownership, camera ray arguments, selection exclusions, preserved
explicit lock and modern compatibility. Live gameplay validation is pending the
coordinated integration build at that checkpoint; unit tests do not prove the native
selector or temporary target launch integration succeeds in game.

## Live checks and damage follow-up

All runs use a private copied card. `bow-ootf-input01` sent F8 before the title
initialized; `02`/`03` stalled at Load screen selection and were stopped. `04`
loaded and picked up the bow, but inherited modern-inventory cursor coordinates
failed to equip it in the native inventory. These are not bow combat passes.
The corrected native sequence is Enter to dismiss help, Right to the bow, E
(Square) to equip. The integration play folder was copied to `build/ootf/bow-play`
with the established harness helper before later runs.

`build/harness/bow-ootf-input05`: clean scripted quit at frame 1603. Screenshot
shows Short Wood Bow equipped, 200/200 arrows. RAM: ready 200, LMB still held
199, released 199. RMB released: camera flags 0x220 / FOV 40; second RMB tap:
flags 0x200 / FOV 80. A strafe shot consumes the second arrow (198). `controls08`
repeats this on the bow-equipped saved card. A jump-time attack in both runs
does not consume another arrow; jump firing parity is not established.

`bow-ootf-hostile07`: clean scripted quit at frame 1107; ammo 200 -> 199 -> 198
-> 197. NPC slot 83 health 40 -> 40 -> 36 -> 32, player 100 -> 90. This establishes
native projectile damage, but no automatic target-success log was emitted. RAM
puts slot 83 2.45 degrees from the camera ray at the initial snapshot. The native
selector explicitly converts input degrees to radians before cosine. Additional
entry/result logging distinguishes a skipped gate from visibility rejection.

`bow-modern-owner10`: explicit modern configuration, clean scripted quit at
frame 1386; two real `[bow] aimed shot` messages prove the corrected actor/data
ownership guard permits local shots. The accidental `owner09` test used an
incorrect config path, was stopped, and its newly created test config removed;
it supplies no modern-mode evidence.

Further original data: Standard Arrow damage 10, speed 6 lu/s, gravity 0,
lifetime 15 s. Native PS2 class 95 property **0** is Damage; property **0x1a** is
Armor Piercing, not damage. Private native collision analysis identifies
`0x1fe3c8(projectileData, impactMessage)`, owner handle `data+0x130`, source actor
`data+0xb4`, and mutable damage in impactMessage[0]. RAM confirms owner handle 4
resolves to the local actor. The new hook doubles only local standard wooden
arrow (`0x3379d`, class `0x1005f`) impact amounts from native base 5 to PC base
10, preserving native situational multipliers. It writes the per-impact message
which native armor handling already mutates, not shared class defaults. Resume
PC guards prevent applying the multiplier again on scheduler continuation.
Spells, NPC shots, other ammunition resources and modern mode are excluded.
Expanded focused suite: **64 checks, 0 failures**. Live damage verification of
this latest hook is pending its coordinated integration build.

`build/harness/bow-ootf-impact12` supersedes that pending-damage checkpoint:
clean scripted quit at frame 2912, three native automatic acquisitions (84, 83,
84), and per-impact logs of **5 -> 10**. NPC 83 health **40 -> 32** on the second
shot, compared with **40 -> 36** at the same checkpoint in `hostile07`. Native
armor therefore receives the new baseline and reduces it to 8. Ammo is 200 ->
199 -> 198 -> 197. Every sampled target handle +0x4da remains 0, proving the
temporary acquisition is restored. No impact log shows 10 -> 20, so this run
does not exhibit repeated multiplication on resume. NPC 84 health 40 -> 40 ->
39 is a separate native collision/block outcome; this is not a three-hit damage
or kill claim. The run forces D2 plus native Title/Save menus for the private
fixture; production menu preferences are unchanged. `impact11` had stalled at
the newer modern title with the inherited native-menu recipe and supplies no
combat evidence.

## Standard arrow flight caches

Further native asset comparison: Short Arrow `0x3379d` has speed 6 lu/s (same
as PC Standard Arrow), gravity 1, gravity delay 0.9 s and lifetime 7.5 s. The PC
baseline is gravity 0 and lifetime 15 s. The earlier 7400-unit straight-flight
observation is within the native gravity grace period, so it did not prove
absence of long-range drop.

Native property cache `0x397398` copies gravity to projectileData+0xec and life
definition to +0xf4; `0x397650` initializes remaining life +0x17c. The nocked
arrow in `impact12/ram_hostile_before.ram` has gravity 1, definition 7.5 and
remaining 7.5. Native launch gravity helper `0x39f048` returns zero for a positive
gravity delay, so this standard arrow's initial ballistic solver already assumes
zero gravity during its grace interval.

The new flight adapter runs at native projectile factory `0x1fdbd8`, event 8,
after launch initialization. It excludes inactive/nocked arrows (+0x134 clear),
NPC owners, other resources and modern mode. For the exact native cache values
1 / 7.5 only, it sets gravity 0 and definition 15, adding 7.5 to remaining life
to preserve elapsed time. The resulting zero-gravity cache prevents later ticks
from extending life again. This writes instance caches, not resource defaults.
Focused suite: **72 checks, 0 failures**, including repeated tick and exclusions.
Live flight-cache verification awaits the next coordinated integration build.

`bow-ootf-flight13` converted the real arrow once but it collided immediately
under the merchant's canopy. `flight14/ram_flight_a.ram` independently shows the
active arrow's gravity 0, definition 15, lifetime estimate 15, alongside a newly
nocked arrow retaining native 1 / 7.5 / 7.5. Subsequent canopy collisions limit
that run's flight evidence.

`build/harness/bow-ootf-flight15` uses the same bow-equipped private card relocated
to the original open-field spawn and aims upward through the native sight.
Clean scripted quit at frame 3677. The active arrow (slot 4432, actor 0xaca450,
data 0xde70a0) appears in all five dumps, with gravity 0 and life caches 15. Its
velocity remains (-10261.799,6732.690,601.140) world units/s while it travels over
37000 units. This spans more than three seconds, beyond the old 0.9-second
gravity grace period; straight flight is now measured beyond that period.
The simultaneous nocked arrow remains native, confirming no shared default edit.

Correction to earlier field naming: +0x17c remains constant in those dumps and
is a lifetime estimate, not decreasing time remaining. Further analysis of native
`0x39a108` proves that age at +0x84 increments by delta time and is compared with
life definition +0xf4 for expiry. The adapter now requires the exact native
estimate 7.5, sets definition/estimate 15, and leaves age unchanged. Tests verify
unchanged age and repeated tick behavior. Original acceleration 240 is copied
directly from the same class field as native 300 into +0xdc; only an exact cached
300 is replaced, preserving custom values. Focused suite: **76 checks, 0 failures**.
The final acceleration/expiry check awaits the consolidated build.

Final `build/harness/bow-ootf-flight16`: **clean scripted quit, frame 1813**.
Executable copied from the canonical build's implementation staging folder to
the isolated bow play folder; SHA256
`5525CB29959D9E777BB51E5670EDE46FE1B11D8C1A2FF5AE9E5195903D0FE229`.
`flight_report.jsonl` is the read-only extraction from this run's RAM dumps.
The active arrow (4432 / 0xaca450 / data 0xde70a0) has gravity 0, acceleration
240, life definition/estimate 15 and unchanged velocity throughout. Its age
advances **0.217 -> 1.167 -> 3.267 -> 8.167 -> 12.670 -> 14.654 seconds**.
It is absent from the next samples past 15 seconds and at approximately 17
seconds. This proves extended finite lifetime, not a timer reset on every tick.
Only one flight-conversion log appears (age 0.017). The simultaneous nocked
arrow 4609 retains gravity 1, acceleration 300, lifetime 7.5 and age 0 throughout,
independently proving shared class defaults stay native. The sight remains open
after RMB release and accepts later mouse elevation; shot velocity magnitude
12288 world units/s equals the original data's 6 lu/s.

Acceptance at this checkpoint: press-started shots, persistent sight toggle and
mouse elevation, a strafe shot, actual automatic acquisition with restored
target lock, standard-arrow damage 10 through native armor, and standard flight
gravity/acceleration/lifetime are established. This is not complete original-PC
combat equivalence: exact draw/reload cadence, jump firing, separate PC arrow
quivers/bow durability and every special bow/ammunition's metadata remain open.
The PC executable was not made playable for a side-by-side timing comparison;
the original reference is its publisher manual and the user's parsed class data.
