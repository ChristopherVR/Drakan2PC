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

## Resource references: names, pictures, sounds (later the same day)

Model, sound, texture, animation and sequence fields (and arrays of them) no longer show `CONTAINER #id`. Resolution and the
container/entry types are in `level-format.md` section 12b. Editor: `formats/src/resources.ts` (SDB descriptor, SPU-ADPCM to
PCM, ADB animation header, SSD sequence name; tests `resources.test.ts`), `app/src/refdata.ts` (worker: stem to file map,
container listings, model/texture/sound loading), `app/src/refs.ts` (main thread: cached listings, thumbnail queue, Web Audio
playback, recent lists in localStorage), `app/src/editor/{refpicker,vlist,popover}.ts` (picker, virtualised list, popover),
`fieldsui.ts` (field row and array elements use `refButton`).

Picker: Suggested (what other classes use for the field, most used first), Recent, Browse (one container or all reachable
ones, sorted by name); search by name, id or hex; Up/Down/PageUp/PageDown/Enter/Esc, Ctrl+Enter plays; a preview pane (3D
render for models at 192 px, the texture, a waveform with play for sounds, frame count for animations); None, Default and a raw
value box in the footer. Edits go through `ctx.edit` as before, so undo/redo, multi-selection and mod export are unchanged.

Browser check (Surdana, Rynn, Playwright, build/portable ISO copy): sound field shows "Magic Fizzle  SYSTEM #891  1.40 s",
the picker lists 301 SYSTEM sounds, "sword" narrows to two, ArrowDown + Enter stores 0x23e, Ctrl+Z restores 0xfc8-style value
(field back to unchanged); Ctrl+Enter starts playback (button shows the playing state). Models: 38 SYSTEM models with
rendered thumbnails (Rynnplate, Ornateplate), large preview. Textures: MAGIC.TXD 31 textures with size and picture. Animations:
615 in SYSTEM.ADB with frames and key size. Sound array "Death Sound": elements show "Rynn Death 03 SYSTEM #412 3.47 s" with play.
Screenshots: `build/scratch/shot-{sound-picker,sound-search,model-picker2,texture-picker,animation-picker,sound-array}.png`.

Not done: audible playback was not heard (headless; PCM decode is a port of the Python tool that the subtitle work verified);
sequences resolve through SSD 0x311 by inference only (no non-zero sequence value exists on the disc to confirm); animations have
no pose preview (joint data undecoded); textures are a list, not a grid; Esc in the picker also reaches the editor's global Esc
(deselects the object); `Suggested` is empty for array fields; texture `db` uses the model container's `.DB`.
