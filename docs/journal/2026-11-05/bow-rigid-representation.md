# Imported rigid Bow representation

The model-only diagnostic `build/ootf/bow-source-pair-render/mods/ootf-levels.zip`
(SHA256 D6C38864F30D0041ED5ABBD5841A3F7276BB440947439B82DF3A7B38DA43187F)
derives from the actual Long fixture validated in run 76. Only Resources MOD
changes: model 7016 gets the equivalent padded draw layout and a strip display
list 0211. Its full authored 136-byte, three-sphere 0207 remains unchanged; no
0208 skeleton is added. Texture 7181 is already indexed 8-bit, 64 by 64, so the
TXD is byte-identical. Class, flags, LVL and physics are unchanged.

`build/harness/bow-source-pair80-render` cleanly quits. Direct image inspection
of `held_bow.png` and `jump_bow.png` shows the original dark wood Long visibly
held in the standing and airborne shooting poses; run 78's unpatched model
control showed empty hands. A real launch at frame 1345 follows a grounded
Space press at 1337 and fresh airborne mouse press at 1344. Another launch
occurs at 1511. The first standing press precedes final equip in this run, so
it is not standing-shot count acceptance. This comparison proves held drawing
and an actual jumping shot, not full original animation or special-bow fidelity.

Production eligibility now uses an internal `rigidBowTemplate` marker emitted
only by the verified complete native rigid Bow template selection path. A model
must actually be converted, source engine must be 79, and the source model must
be explicitly known to be rigid. Unknown/skinned sources and skill-only
same-name clones are not marked. The existing representation-demand pass
accepts native 79 only with this marker, preserving native 14 behavior.

The owning asset builder still independently requires a nonempty valid authored
sphere hierarchy, rejects a 0208 skeletal model, requires owned textures, and
upgrades indexed square representation in place without changing IDs. It keeps
the complete authored sphere chain. This is not blanket source Bow handling.

`build/ootf/emit-rigid-bow-draw-proof.ts` runs the production planner and demand
pass on actual original Long 3104. It selects native rigid template 7835 and
emits model 7016 records 0200/0203/0204/0207/0211, no 0208. Its complete 136-byte
sphere record is unchanged, texture 7181 remains 8-bit 64 by 64, and there are
no warnings. `compare-rigid-bow-draw.ts` compares all five generated model
records against the live-tested model-only candidate: every record is exactly
byte-identical (`rigid-bow-draw-emission/candidate-equality.json`).

Focused validation: 59 tests across class planning, representation demand and
asset/texture conversion pass; formats typecheck and diff check pass. Tests
cover marker eligibility, unmarked and other-class exclusions, unknown owner,
existing missing/empty/skeletal bounds rejection and stable texture upgrade
order. No runtime flight, velocity, damage or cooldown policy is introduced.
