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
