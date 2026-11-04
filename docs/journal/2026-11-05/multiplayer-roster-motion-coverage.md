# Remaining selectable NPC motion coverage

Five additional two-instance motion runs cover the ten NPC choices not individually
covered by the earlier Monk/Northmen Archer and Monk Boss/Ranwulf runs. Each pair
passes all 14 existing harness checks and both processes quit normally. Combined
with those earlier runs, all 14 curated NPC choices now have bounded live motion
coverage; this is not certification of every multiplayer mode or combat matchup.

| Private output under `build/harness/` | Host | Joiner | Checks |
| --- | --- | --- | --- |
| `root-roster-pair02` | Denman, 1642 | Zola Dane (Passive), 1643 | 14/14 |
| `root-roster-pair03` | Guard 1, 16e1 | Captain, 16ec | 14/14 |
| `root-roster-pair04` | Sevoth, 177d | Nichols The Bold, 177f | 14/14 |
| `root-roster-pair05` | Atlus, 1797 | Capn Serris, 1798 | 14/14 |
| `root-roster-pair06` | Male Villager, ee7 | Blacksmith, 1626 | 14/14 |

These runs use the preserved executable already validated for NPC melee against
both rig families, SHA256
`9573C6575D92A3B00AC13D8CFA861A3248946C5578E5EAC943F1D37009EF217D`,
and character-motion pack SHA256
`0CD4633EDB79C376D6C305191E5DB5A51ABA404493DFB7BD3B47F50183792118`.
They do not claim validation of every concurrent renderer revision. An initial
Male Villager/Blacksmith control, `pair01`, also passes, but uses the older
`7ECB` executable; its melee mask was not the final accepted version. Pair06
repeats those characters on the accepted executable and is the reported result.

The existing `coop_characters.py --motion` recipe exercises standing and
directional jumps, left/right evasions, strafing attacks and native save/recreation.
Checks require both local and remote motion transitions, identical generated
source-to-target clip mappings, the loaded retarget pack and independent native
torso channels on both clients. Direct inspection of each pair's
`a/a_left_3.png` and `b/b_right_3.png` confirms visible, textured bodies in the
opposite rotating/tucked phases with held-weapon grips. No body disappears in
these inspected phases. The network mapping checks complement those local-view
images; they do not inspect every remote rendered frame.

The fixtures copy the original Slot1 from the existing armed-motion card, retaining
its inventory and health while relocating the two test participants. Namespaced
private output, play and fixture paths are checked beneath `build/` and refused
if already present. Existing cards, earlier evidence and running user games are
untouched. The wrapper and batch driver are private under `build/scratch/`; the
machine-readable result is `roster-live-checks.json` there (70/70 checks).

This batch runs the co-op motion harness with audio disabled. The earlier genuine
projectile/male-voice waveform and NPC melee acceptance remain the separate
combat/audio evidence in [the implementation journal](../2026-11-04/multiplayer-animation-voices.md).
Long-duration play, all pairwise combat combinations and all presentation states
remain outside this bounded motion check. No runtime behavior changed in this batch.
