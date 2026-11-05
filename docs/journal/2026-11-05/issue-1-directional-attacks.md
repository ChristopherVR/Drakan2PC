# Issue #1: WASD directional attacks

Source: Controls follow-up in [issue #1](https://github.com/ChristopherVR/Drakan2PC/issues/1#issuecomment-6637936574).
The reported behavior is that directional attacks work through arrow keys but require leaving WASD.

## Evidence and correction

`runtime/input/src/Config.cpp` defaults bind WASD to `LSTICK_*`, the arrow keys to `DPAD_*`,
and mouse1 to `SQUARE`. Before this correction `PadSynth.cpp` converted `LSTICK_*` only into analog
stick bytes. Thus a WASD+attack sample lacked the digital directions present in arrow+attack samples.
This explains the host-input distinction without claiming a new reverse-engineering result about native moves.

The synthesizer now mirrors resolved movement-binding directions onto D-pad buttons while the
keyboard/mouse bindings produce `SQUARE`. It retains the analog movement vector. Opposite directions
cancel per axis; diagonals project both axes. Explicit arrow/D-pad bindings still pass through and release
normally. This follows remapped movement and attack bindings and requires no INI migration.
Menu-layer synthesis excludes this projection, and controller buttons merge afterwards so a controller's
attack does not cause held keyboard movement to acquire extra D-pad buttons.

## Regression coverage and limits

`runtime/input/tests/input_test.cpp::testDirectionalAttacks` covers each WASD direction held before the
attack, attack release, independent arrow direction/release, analog-vector preservation, diagonals,
opposite cancellation, simultaneous explicit arrows, short attack taps, controller-only attack,
custom movement/attack bindings and menu exclusion. Parent integration runs the shared runtime test
suite; this subtask intentionally does not build or launch the game. Native move timing and the exact
resulting attack animation are unverified until gameplay checks run from the rebuilt play folder.

Shared input paths: `PadSynth.cpp` and `input_test.cpp` will also receive the separate A/D spellcasting
follow-up. This change only projects directions during `SQUARE` attack and adds no spellcasting behavior.

## Maintained native directional-attack harness (source-ready checkpoint)

`scripts/harness/directional_attack_check.py` generates a neutral mouse1 attack followed by
arrow/mouse1 and WASD/mouse1 trials. Each trial records native animation samples before,
during and after attack, plus an ordinary movement/release check after the trials.
It reuses the deployed-game runner and native-card validation from `disc_combat_check.py`,
with a copied private card and INIs; it records the executable SHA-256.

The verdict requires an arrow attack animation absent from the neutral attack, the same
native directional animation through WASD, armed/living Rynn, settled attack release and
subsequent movement/release. Script completion or matching ordinary attacks cannot pass.
The existing profile field `player_attack_state` is the summon state at PAL +0x1040,
so this harness deliberately does not use it as melee evidence.

Bounded first trial after the canonical deployment and exclusive live-test slot:

```powershell
python scripts/harness/directional_attack_check.py --save build/coop2-fixture-si/mc0 --name issue1-directional-up1 --direction up --timeout 240
```

The fixture must contain native PAL Slot 1 with a usable melee weapon. The recipe taps
Q to draw it; the armed check fails if that instead holsters an already drawn weapon.
No magic is required. This first trial takes about 11 gameplay seconds plus native loading,
and retains about 1.6 GiB of private RAM snapshots. Omit `--direction up` for all four pairs.
An unsuccessful baseline remains a failed verification, not evidence that WASD works.
Five focused Python tests cover native parity, false ordinary/idle success, missing samples,
release/movement failures and ordered recipe generation. Those tests and focused Ruff pass.
Gameplay execution is pending the shared deployment/live-test slot at this checkpoint.

## Native clip evidence and correction after the first live trial

`build/harness/issue1-directional-up1` ran the deployed executable SHA-256
`9c38cb9042b43a916a7e4e49cffac0534f3ff6df20ede62e883c303c647f4c78`, copied
`build/coop2-fixture-si/mc0`, completed scripted quit with exit 0 and left the source
card unchanged. Armed Rynn and ordinary movement/release were observed. This was
**not a directional pass**.

The original observer's player+AEC remained zero for both neutral and arrow attacks;
it is a locomotion selector rather than the real animation clip. The harness now
reads root/node-0 and spine/node-2 skeleton channels through the same offsets as
`world::readPlayerPose`, including clip IDs/times and native player+94 attack flag80.
Reanalysis of the existing RAM established advancing native attack clocks:

| Input | Actual upper-body clip(s) | Result |
| --- | --- | --- |
| Neutral mouse1 | `0x20691` | Ordinary club attack |
| Up arrow + mouse1 | `0x20684` | Distinct native directional move |
| W + mouse1 | `0x2077c`, then `0x20691` when W releases | Moving attack, not arrow parity |

This evidence supersedes the earlier assumption that preserving the analog vector
would retain native special-move selection. The correction centers only synthesized
keyboard movement during a projected on-foot melee attack. `beforePadMgrUpdate`
checks the bounded current Rynn chain, native weapon-holder modes/references and
summon state before enabling projection/centering. Bow mode, mounted actors,
stowed/unknown actors and active summons retain movement. Physical stick merge
still happens afterwards. PAL/USA holder offsets are explicit profile data.

The harness now rejects an attack without native attack flag/advancing upper-body
clip time; ordinary or idle matching clips cannot pass. Seven Python oracle/reader
regressions and focused lint pass. Post-correction deployed validation remains
pending the canonical build and exclusive live-test slot.

## Native ordering probes and buffered onset

The first correction alone did not pass `issue1-directional-up2` on executable
`de57cf5f6badbc06d234debc28df7b178668660695e3b4c57adae9ed7e5a8415`.
Every native gate operand was eligible; attack samples showed PadMgr buttons
`0x1080` (Up + Square), centered stick bytes and zero native axes. The actual
attack still selected the moving/ordinary clips. Thus this was not a gate failure.

Native ordering explains it: `PadMgr_Update` dispatches button callbacks before
processing this sample's stick axes. `Player_AxesToDigitalCommands` then releases
the old analog forward command (`0x4019`) through `Game_OnInputCommand`, clearing
the same player+504 bit used by D-pad Up. Captured movement flags changed from
`0x101` to zero despite the new held Up button. The dispatch table also places
Square before D-pad directions; the complete controller tick must settle first.

Two source-free native experiments on the same deployed executable compared
explicit scripted controls, retaining full RAM under `build/harness/`:

| Sequence after held W | Actual attack clip | Evidence |
| --- | --- | --- |
| Release W, press Up and Square together | `0x2077c` then `0x20691` | `issue1-directional-prepare1` |
| Release W and press Up, wait 1 or 2 samples, Square | `0x20691` | same; the analog release already cleared the Up bit |
| Release W, **one neutral sample**, then Up+Square | **`0x20684`**, matching settled arrow baseline | `issue1-directional-prepare2` |
| One neutral sample, one Up-only sample, Square | `0x20684` | same; the extra sample is unnecessary |

The successful minimal sequence had the same advancing clip clock, approximately
0.0 to 0.72 seconds, as the arrow baseline. Both experiments completed scripted
quit with native exit 0 and preserved the source card.

The implementation now buffers only a new keyboard/mouse directional melee attack
for **one neutral preparation sample**, then sends its latched direction and Square.
It retains a short tap even if its movement/attack keys released before delivery;
held attacks do not repeatedly defer. Host/menu/focus/capture loss, pause input,
config/profile reload and native actor/weapon/context changes cancel pending intent.
The physical controller still merges after keyboard synthesis. Post-build actual
WASD validation is still required; explicit ordering experiments are not themselves
acceptance of the production WASD path.

## Final deployed native acceptance: all four directions passed

`build/harness/issue1-directional-final1` tested the actual buffered production path
from `dist/DrakanPC` on executable SHA-256
`4ce9dd0d611d595d0c1aa5b4e8bf0ba9e723e5f66152bfaad258349576ecf278`.
All **59 strict checks passed**, including native attack flags, advancing upper-body
clip times, drawn melee weapon, attack release, idle stability, subsequent W
movement and movement release. The native process completed scripted quit with
exit 0. Source card hashes matched before/after. The slot was handed off only
after confirming that no game processes remained.

| Direction | Arrow control clip | Actual WASD clip |
| --- | --- | --- |
| Up / W | `0x20684` | `0x20684` |
| Down / S | `0x2069c` | `0x2069c` |
| Left / A | `0x2069d` | `0x2069d` |
| Right / D | `0x2069e` | `0x2069e` |

Every directional clip differed from the neutral attack `0x20691`, and no extra
moving/ordinary attack clip appeared in any WASD trial. Captures and full guest
RAM remain private under the run folder; `directional-report.json` retains the
actual root/spine clips, times and drawn-holder modes for each sample. The
maintained invocation for repeating acceptance is:

```powershell
python scripts/harness/directional_attack_check.py --save build/coop2-fixture-si/mc0 --name issue1-directional-repeat --timeout 240
```

The live acceptance covers native PAL Rynn with this fixture's drawn club and the
normal WASD/arrow bindings. Other weapons and USA gameplay were not claimed by
this run. Remapped bindings, short taps, continuous holds, keyboard movement
restoration, physical-stick merge, bow/mount exclusion and buffered-attack
cancellation have input unit coverage. The rebuilt input and both disc profile
tests passed; seven focused Python observer/oracle tests, Ruff, changed C++
formatting and `git diff --check` also passed. No additional runtime correction
was made after the successful deployed test.
