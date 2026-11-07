# Issue #1: directional attacks need a modifier

Source: tester follow-up on [issue #1](https://github.com/ChristopherVR/Drakan2PC/issues/1) (RabeOhneNamen):
WASD directional combos work "almost too well". Walking or strafing through an ordinary melee attack was
impossible, because any held W / A / S / D at the attack press became a directional special move.

## Cause

`PadSynth::synthesizePad` (2026-11-05, [issue-1-directional-attacks](../2026-11-05/issue-1-directional-attacks.md))
projected every held `LSTICK_*` direction onto the D-pad while a keyboard / mouse `SQUARE` was down, and centered
the keyboard left stick for that attack so the native special move would win over the moving attack. That
centering is the "eaten" movement: with any movement key held, every swing stopped Rynn.

## Design

`[keyboard] directional_attack` decides, once per attack press (kept until release), whether held movement makes
the attack directional:

| Value | Directional when | Notes |
| --- | --- | --- |
| `modifier` (default) | a `DIRECTIONAL_ATTACK` binding is held | `LALT = DIRECTIONAL_ATTACK`, rebindable in the key editor |
| `tap` | a movement direction turned on at most 0.25 s before, or with, the attack | changing direction counts as a fresh tap |
| `always` | any movement key is held | the 2026-11-05 behaviour |
| `off` | never | arrow keys (D-pad bindings) only |

Why a held modifier by default: it is unambiguous (no timing guesses, no misfires on quick direction changes,
which is what the double-tap idea risks), it is what the tester suggested, and plain attacks behave exactly like
a controller's Square with the stick held. A plain attack now adds no D-pad bits and leaves the keyboard stick
vector untouched. Everything inside a directional attack is unchanged: the native melee gate, the one neutral
preparation sample, tap buffering and the cancellation rules. The decision latches on the press, so releasing Alt
mid-swing keeps the special move and pressing Alt mid-swing does not change a plain one. Left Alt alone is safe:
`drakan_window.cpp` already swallows `SC_KEYMENU`, and Alt+Enter stays the host's fullscreen toggle.

Older `drakan_input.ini` files get `LALT = DIRECTIONAL_ATTACK` through `Config::migrate` (and the settings store's
implicit-binding list, like F5 / F8 / F3) unless the file already binds Left Alt, writes `LALT = NONE`, or binds the
action elsewhere. The gamepad path is untouched (physical buttons merge after keyboard synthesis; a controller has
the D-pad for special moves and never had the problem).

Surfaces: `drakan_input.ini` comment and key, Settings > Controls > Keyboard movement > **Directional attacks**
(the F1 overlay and the modern in-game pages share the schema), the key editor's Actions group (**Directional
attack**), `runtime/input/README.md`, `runtime/settings/README.md`, `docs/reference/input.md`.

## Tests

`runtime/input/tests/directional_attack_mode_tests.cpp` (new `directional_attack_mode_test`): plain W / A / S / D /
diagonal attacks keep their full stick vector with Square only; Alt + each direction gives the preparation sample
then Square + direction; latching on press; buffered short click with Alt; a rebound modifier (MOUSE4) and
`LALT = NONE`; tap window inside / outside / on a direction change; `always`; `off` with arrows; no melee weapon;
controller merge; menu layer; ini parsing and migration. `input_test.cpp::testDirectionalAttacks` now runs in
`always` mode (it covers the projection itself). Settings tests cover the round trip and the implicit binding.
`scripts/harness/directional_attack_check.py` now holds Left Alt for the `wasd` control and adds a `plain` control
(movement key only) that must not produce the arrow move and must keep Rynn moving through the attack window.

## Deployed native check

`build/harness/issue1-dirmod1` ran `dist/DrakanPC` (executable SHA-256
`afce168c4464edf78a856e286c5f62d5f1382236a8d241985330aa06b97e3cab`) with the copied
`build/coop2-fixture-si/mc0` card: all **87 strict checks passed**, scripted quit completed and the source card was
unchanged. The harness input ini predates the modifier (no `LALT` line, no `directional_attack` key), so this also
exercised the migration and the default `modifier` mode.

| Direction | Arrow clip | Left Alt + WASD clip | Plain WASD clip | Plain: Rynn moved during the attack |
| --- | --- | --- | --- | --- |
| Up / W | `0x20684` | `0x20684` | `0x2077c` | 3301 units |
| Down / S | `0x2069c` | `0x2069c` | `0x2077c` | 1204 units |
| Left / A | `0x2069d` | `0x2069d` | `0x20686` | 784 units |
| Right / D | `0x2069e` | `0x2069e` | `0x20687` | 1588 units |

The neutral attack stayed `0x20691`. Plain W / S gave the native moving attack (`0x2077c`) and plain A / D gave
strafing attack clips; none matched the arrow special move. `tap` and `always` are covered by unit tests only;
weapons other than the fixture's club and USA gameplay were not run.
