# Scene creatures: audit of every cut-scene actor and the orc rows (2026-11-09)

Report: "The grunts / wartoks are still Drakan 1 in the cutscenes." The scene Wartoks were mapped in formats `d052dd4`
([scene Wartoks](ootf-scene-wartoks.md)); this note audits every creature the scenes drive and maps the rest. Probe scripts,
packs and runs are private under `build/scenecre/` and `build/ootf-run/scenecre*`; evidence frames in `build/review/scenecre/`.

## Audit method

For every OotF level, every sequence of every `.ssd` beside the level (Intro: three SSDs, Ruined Village: two, the rest one each,
Mountain World Common is shared) was parsed (`parsePcSequence`); each level-object actor (`actorType 1`) was resolved to its
class name, engine class and model. A class is shown with a disc model when it has a row in `DEFAULT_PS2_MODEL_SUBSTITUTIONS`
or a name alias (`PC_CLASS_ALIASES`), checked against the converter log ("drakan 2 model", "scene ...") of the full campaign pack.

## Result (creature-like actors only; decor, cameras, timers, effects and props left out)

| Level | Class (engine class) | Before | Now |
| --- | --- | --- | --- |
| Ruined Village 1.1 / 1.2a | Wartock Leather / Chain / Plate (17) | disc (since `d052dd4`) | disc |
| Ruined Village 1.1 / 1.2a, Volcanic | Heuge Purple, Heuge Yellow (17; same model numbers as the gameplay orcs 3713 / 3714) | converted PC model | disc Purple Orc Center / Yellow Orc : Center |
| Ruined Village 1.2a | Orc Leather (purple) (49), Scavenger (41) | disc | disc |
| Ruined Village 1.2a | male slave2 (49) | PC model, no disc counterpart | unchanged |
| Grimstone Mines, Grotto | Troll Leather (+ defense side attack), Troll Chain (49) | disc | disc |
| Volcanic | Orc Plate (yellow), Orc Leather (purple) (49) | disc | disc |
| Volcanic | Heuge Sailor (17) | PC model | PC model: the disc sailor rig matches 23 of 57 joints and has no spine, kept on purpose |
| Volcanic | Death Mage, Death Mage Shamen (17); Death Mage (82) | PC model | PC model: the disc Death Mage shares under half of the joints |
| Alwarren | starving wargiant (59) | disc Primitive Giant (alias) | disc |
| Ruined Village, Volcanic | Ebon Knight, Succubus (17), villagers, Delon, fake julian, Farmer, Warrior | PC models | unchanged, no disc counterpart (NPC or a creature the disc lacks) |

The user's "grunts" are the OotF orcs: in Ruined Village scenes 1.1 and 1.2a the ambush party is Wartoks plus `Heuge Purple /
Yellow` orcs, and only the Wartok scene rows existed, so the orcs were the one creature left on the converted PC model. The
Intro, Wartok Canyons, Rift World and the Islands scenes hold no orc, Wartok or giant scene prop.

## Change

Two rows (`sceneOrc` in `ootfps2model.ts`, `scene: true`): `Heuge Purple` to `Purple Orc Center`, `Heuge Yellow` to
`Yellow Orc : Center`, the same mechanism as the scene Wartoks (retargeted scene clips in NPCS.ADB, channel table by joint name,
native pose for unmatched disc joints, the class's model reference naming the disc model in NPCS.MOD). The converter log reads
`27/55 joints driven, 28 on a native pose, 59 / 60 clips` for the two. The PC orc has 17 scene channels: root, weapon (PC 2 to
disc 8, `rweapon`), spine, chest, neck, head map by name; the flail-head attach pairs with a disc attach leaf by position; the
forearm and hand channels have no disc channel. Opt-out unchanged (`--ps2-model-map` with `{"ootf":"Heuge Purple","ps2":null}`,
`--ps2-models 0`, the import dialog). The impale carry (`ootfcarry.ts`) and the Wartok rows are untouched.

## In game

Private copy `build/ootf-run/play-scenecre` (dist copy, no textures), the full campaign pack, Intro movie off, taps stopped after
the load: the Intro plays, then Ruined Village 1.1, the impale and the villager scene. Shots every 4 s in
`build/review/scenecre/rv-*.png`. Seen: the ambush line-up (Wartoks with spears, the two tall orcs behind), the bridge crossing,
the attack and the village run show orcs and Wartoks in walk and attack poses, nothing in a T-pose. Honest limit: the disc orc
and the converted PC orc look the same in a still (Drakan 2 ships the Order of the Flame orc; the model, texture and colours match), so a before / after pair
(`build/review/scenecre/before-*.png`, opt-out pack) differs only in rig, not in appearance; the proof that the row applies is
the converter log and the new retargeted clips.

## Not verified

* Later levels: a direct start of Volcanic and Grimstone Mines (`DRAKAN_START_LEVEL`, `scenecre-volc`, `scenecre-mines`) loads
  with control and no scene running (their scenes are trigger-based), so no scene creature of a later level was seen in
  game. The audit and the converter log cover them (the Mines and Grotto Wartoks are gameplay classes already on the disc
  model); the Volcanic scene orcs share the Ruined Village rows.
* Whether the red orcs in the ambush line-up are the `Heuge` actors (they look the same with and without the row).

## Follow-up: are the line-up trolls PC models? (no, A/B and native reference)

Report: the big red trolls of the scene 1.1 spear line-up and the ambush look like PC models. Checked three ways, all in
`build/review/scenecre/`:

* Class: scene 1.1 (sequence 381) holds `Wartock Chain x2`, `Wartock Plate x1`, `Wartock Leather x1` (objects 1858 to 1862,
  all class 17 with the Wartok model, `row=ChainTock / PlateTock / LeatherWartock : Center (scene)`) plus `Heuge Purple x4`
  and `Heuge Yellow x3`. The red trolls are the Wartock actors; the smaller pink and yellow kilted ones are the Heuge orcs.
* Hide-one-class experiment: the same scene with only the three Wartock rows turned off (`--ps2-model-map`) shows the red
  trolls as the dark brown PC trolls with a grey vest; with the rows on they are red with a mane, bare arms and a white vest.
  So the actors do load the disc model and nothing else (second model slot, per-object override) overrides it.
* Native reference: a Drakan 2 Wartok in disc level 3B (`play_run.py --level 3b`) is red skinned with a mane, the same
  look as the rows-on trolls (`wartok_native_vs_lineup.png`).

A pack converted before `d052dd4` (or before the orc rows) shows the PC trolls, so the user's installed pack was most
likely stale; the coordinator's reinstall of the current converter output carries both. No converter change was needed.
Not done: RAM read of actor+0x50 (the A/B above is decisive), pulling scene props into view (scenes move them).
