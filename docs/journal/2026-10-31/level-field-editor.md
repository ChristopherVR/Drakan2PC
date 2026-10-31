# Level editor: class fields in the Inspector (2026-10-31)

Problem: the Inspector showed "Fields 0" for most objects because only the LVL parameter overrides were known. The class
field list and defaults live in the ODB (0x0102 data area + 0x0103 blob); format and evidence in
`docs/reference/level-format.md` section 12 (`analysis/scripts/odb_fields.py`, 6520/6520 classes, 29970/29970 overrides).

Editor: `formats/src/{fields,odb,lvl}.ts` (class fields, `setObjectParams`), `editor-core/src/{schema,lvl-doc}.ts`
(`SchemaProvider`, `setObjectField`, fieldIndex-based diff/export), `app/src/editor/{fieldsui,scrub,inspector}.ts`.
Worker returns each ODB's `.DB` dependency list (`odbDeps`) because class references are relative to it.

Browser check (Surdana, Playwright): Purple Orc Center shows 241 fields in 7 groups; Initial Health edit marks the row, undo
and redo restore it; two orcs multi-edited as one step; Weapon Object shows "Tenderizer" and the class picker lists
thumbnails; Export mod writes `overrides/LEVEL1NA.LVL/0020_0_0.bin` that differs from the disc entry in exactly those two
objects (field 46 added).

Game check: private folder `build/field-edit-play` (dist top-level files plus `build/final-game/bin/drakan.exe`, which has
LVL pack support; the dist exe predates it), pack in `mods/`, `play_run.py --recipe` with `DRAKAN_WORLD=2`,
`DRAKAN_WORLD_DUMP_FRAME=800`. Dump: two Purple Orc Center (0x50c19) with hp 500/500, ten with 35/35.

Not done: names for model/sound/texture/animation references (shown as `CONTAINER #id`; only a "values other classes use"
browser), enum item names (not in the data), array editing for multi-selection with different lists, adding a field to a
brand-new class instance is untested in game.
