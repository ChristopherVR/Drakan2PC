# Bow cadence and airborne evidence

User scope correction: aiming and shooting are the problem under investigation.
The temporary damage, gravity, acceleration and lifetime adapters were removed;
they are not part of the retained implementation. No projectile-stat change is
proposed here.

## Measured port behavior

Private run `build/harness/bow-cadence17`, recipe
`build/ootf/bow-cadence17.txt`, used the actually equipped native bow save in
`build/ootf/bow-openfield/mc0`. The run quit normally at frame 2004. Its executable
preceded the source stat reversion, so this is input evidence, not acceptance of
the current deployed executable or projectile statistics.

- A nine-second uninterrupted attack hold consumed one arrow (200 to 199).
  Releasing the button consumed no additional arrow.
- Eight 100 ms attack presses spaced 750 ms apart consumed seven arrows. The
  fourth press was refused. This is an observed acceptance sequence, not a
  measured exact cooldown or an original-game cadence comparison.
- A jump followed 300 ms later by an attack press consumed no arrow. RAM samples
  at 100, 500 and 1000 ms after the jump had airborne flag `flags94=0x10` and
  elevated player Y. After landing, a fresh attack press consumed one arrow.
  This establishes the current port's airborne refusal in this fixture.

Earlier `bow-ootf-input05` and `bow-ootf-controls08` prove press firing before
release, persistent alternate-attack sight, sight FOV 40, and a strafe shot.
They do not establish complete run/jump compatibility. The open-field run above
supersedes the ambiguous merchant-ledge jump attempt in controls08.

## Original-game evidence obtained

The original publisher manual, printed page 15, describes attack press firing
with third-person autoaim and alternate attack toggling the accurate zoomed
sight. The private scan is `build/ootf/manual17.png`. Printed page 14 describes
running/jumping attacks in the melee section; that wording alone does not prove
bow shooting while airborne.

The installed original PC `Dragon.rfl` is imported into the private Ghidra
project `build/ootf/rfl-project/Original`. Private analysis output remains under
`build/ootf/rfl-analysis`; no original executable content or translated function
body belongs in the repository.

The original attack-animation setup at `0x1005d990` dispatches an equipped bow
branch (weapon mode 6) to helper `0x1005e9d0`. The helper reads the equipped bow's
class property 17, identified independently from the original class schema as
Rate of Fire. It writes that value through a caller-supplied float output. The
caller has animation setup parameters rather than a simple seconds-per-shot
timer. This supports tracing animation-rate semantics before changing cadence;
it does not yet establish the full timing equation. In particular, the original
Long Bow property value 1.75 must not be interpreted as a 1.75-second cooldown.

The original `0x1005ce40` includes fall-damage/landing handling and a pending
sight-attack retry. It is not sufficient evidence for bow airborne eligibility.
Likewise, the original animation state-name table contains “Jump Attack”, but
the name is shared character machinery and does not identify bow support.

## Remaining comparison

Trace the original player input/state dispatcher through the bow attack branch,
identify its movement/airborne refusal conditions, and connect Rate of Fire to
the animation playback argument. A new port input adapter should follow that
evidence. No jump-gate bypass or guessed cooldown has been added. The original
retail executable has not yet produced a usable local comparison session.

The next live acceptance run must use the coordinated canonical rebuild that
removes the historical stat adapters from the executable, with the retained
owner guard, press firing, sight toggle and autoaim work intact.

## Rebuilt executable and original jump gate

`build/harness/bow-movement18` used the coordinated rebuild with executable
SHA256 `7ECB43CECF083BF2D9C96DF8AA7BC212513D9D47416B107EAA6C60242D7645C5`.
The run quit normally at frame 1404. Its `ram_report.json` and raw RAM establish:

- Forward movement from X 450247 to 448333 with a shot consuming 200 to 199.
- The released alternate-attack sight remained open (`cam+48=0x220`, FOV 40).
- A sight shot while strafing consumed 199 to 198, with Z moving 343259 to 344988.
- Airborne attack at Y 171963 with `flags94=0x10` consumed no arrow; a fresh
  post-landing attack consumed 198 to 197. Three launch entries match those shots.

Further original RFL analysis found the actual attack entry `0x1006f9d0`, which
was not initially recognized as a function. Its refusal mask is `0x960`, or
`0x160` in an alternate actor state. These masks contain falling bit `0x20` but
exclude jumping bit `0x10`. The original jump transition at `0x1005a830`, case 6,
sets bit `0x10` and explicitly sets up a bow upper-body animation channel using
actor properties `0x72` and `0x2c`. This is substantially stronger evidence for
jumping bow attacks than the generic manual sentence or state-name string.

Native PS2 attack entry `0x31da90` instead chooses refusal mask `0x104b70` for a
bow, including jump bit `0x10`. Before applying that mask it invokes the actor
reset callback `0x307198`, returning to `0x31de5c`, with the mask in saved register
`s2`. The native actor vtable in movement18 confirms that callback address.

The proposed fix removes only jump bit `0x10` from this known bow mask in that
callback. It preserves the actor's real airborne flags, falling refusal and all
other native gates. It is limited to the local on-foot bow in normal gameplay,
default OOTF behavior, the exact callback/caller and recognized native mask.
Modern mode, NPCs, other weapons, unknown masks and scripted play stay native.
Installation requires PAL identity addresses and AdvancedInput capability.
The focused bow test now passes 71 checks, including these negative guards and
verification that airborne actor flags are unchanged. Live acceptance of this
new jump-gate fix awaits its coordinated build; movement18 is the before case.

## Jump-gate live acceptance

`build/harness/bow-jump19` used executable SHA256
`25FBE3463740488A596552AFF0463596B0F5B511001E13D28563C57FD0D717A6`.
It quit normally at frame 1401. The callback diagnostic records mask
`104b70 -> 104b60`. The airborne shot consumes 198 to 197 while `flags94=0x90`
and Y is 172194; a later sample remains airborne (`0x10`, Y 172594) with 197.
The screenshot `jump_attack.png` visibly shows airborne Rynn holding the bow
and ammo 197. The forward shot and sight-strafe shot still consume 200 to 199
to 198. A later fresh shot consumes 197 to 196 after landing. No projectile
statistics are changed by this build.

`build/harness/bow-modern20` repeats with explicit `bow_aim=modern` and a held
attack released during the jump. It records `modern=1`, no adjusted-gate log,
and ammo remains 198 throughout the airborne/landing samples. A fresh ground
shot consumes 198 to 197. This is a live negative check for preserving the
existing modern option; non-bow and foreign-player guards are focused tests.

`build/harness/bow-combined21` deliberately opens the sight, holds forward,
steers with the mouse, jumps and shoots in one sequence. It identifies a
separate remaining restriction: sight remains open at FOV 40, mouse pitch
changes from zero to -0.628 radians and heading from 1.524 to 1.368 radians,
and a shot consumes 200 to 199, but the position remains unchanged and no
airborne flag appears. Forward and jump were refused before the attack gate.
This is **not** a passing combined running/jumping sight test. Native sight
strafe works in jump19/movement18; sight-forward and sight-jump require their
own original/native comparison before changing another gate.
