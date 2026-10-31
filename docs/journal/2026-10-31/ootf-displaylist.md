# Does the engine need MOD record 0x0211? (2026-10-31)

Short answer: **not for ordinary static props.** A converted PC model without 0x0211 draws in the real game, textured, upright
and with sane winding. Two things do need it (skinned models, Birch tree class), so no generator was written (yet); the layout
notes in `ootf-models.md` stand.

## Method

Whole-file replacement packs (`files/ENGLISH/.../X.MOD`, 0x0211 entries removed by rewriting the SRSC), New Game on Level 1A
(Surdana), screenshot after ~300 frames (`play_run.py` via `build/ootf-run/run.sh`). Packs and scripts: `build/ootf-run/m-*`,
`build/dl/*.py|ts` (private). Screenshots: `build/ootf-run/shots/dl_*.png`. A pack folder must sit in a mods dir with
`mods.ini` (`pack = on`); the log line `replaced ...MOD (n bytes)` proves the file was served.

| Pack | Change | Result |
| --- | --- | --- |
| `no211` | LEVEL1ON.MOD without any 0x0211 (261 entries) | identical scene to baseline (`dl_levelon_no0211`) |
| `no2034` | LEVEL1ON.MOD without 0x0203/0x0204, 0x0211 kept | tent, flowers, HUD orb vanish (`dl_levelon_no0203_0204`): proves the file is used and that 0x0203/0x0204 are what is drawn |
| `nosys` | SYSTEM.MOD + RESOURES.MOD without 0x0211 | **Rynn is not drawn** (skinned, 0x0208); static scenery unchanged |
| `wc2` | WORLDCON.MOD rewritten unchanged | identical to baseline (control for the rewrite) |
| `wc0` | WORLDCON.MOD without any 0x0211 | all Birch trees and the flowers vanish |
| `wc1` | only model 893 (Birch Tree01_H) without 0x0211 | all trees vanish, flowers stay |
| `wc4` | as `wc1` and polygon flags set to 0 (not double sided) | trees still vanish: not the double-sided flag |
| `conv1` | LEVEL1ON model 905 (TentRoundBB) replaced by the converted PC `MarketStall01(H)` (Ruined Village 3530) through `OotfAssetBuilder` + `pcToNeutral`, textures as new TXD entries (ids +5000), **no 0x0211 anywhere in LEVEL1ON** | stall draws: chest, crate with green bottles, barrel with wood and metal bands correct, upright, not inside-out, UVs plausible (`dl_converted_marketstall_no0211`), scale about 1 m |
| `conv3` | WORLDCON 893 (tree) replaced by the stall, no 0x0211 | nothing drawn (same as `wc1`) |

## Conclusions

* 0x0203/0x0204 are authoritative for static props; 0x0211 is an optimisation copy there. No generator needed for converted
  static props (level pieces, class 0x100d3-style objects, camp props).
* The Birch tree class (`Birch Tree 01`, class 0x10011) and skinned models do not draw without 0x0211. Cause unknown (not the
  double-sided flag, not the model header words, which equal the tent's). So converted trees or characters would need 0x0211
  (the skinned side belongs to `ootfanim.ts`); either map OotF trees onto an ordinary prop class or decode 0x0211 first.
* The converter output is correct for orientation, winding and textures on a non-alpha prop. Not yet exercised: leaf cards
  (alpha, double sided) outside the tree class, the PC-to-PS2 scale of large models.
* Harness notes: Rynn's route with `key down W` is not deterministic from run to run, so far objects are hard to frame; pick
  an object near the start or use a longer recipe with several shots (`walk4.txt`).
