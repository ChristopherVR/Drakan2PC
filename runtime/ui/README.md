# drakan_ui: keyboard/mouse prompts + override folder

Static library (depends on `runtime/hle` only). `drakan_ui::install(hle::ResourceService&, Options::fromEnvironment())` registers the resource overrides:
`overrides/` folder files, generated keyboard/mouse prompt textures (font glyphs, controller diagram, inventory page tile) and XDB text rewrites, all driven by
`drakan_input.ini` (`[bindings]`). Details, formats and reverse engineering: `docs/reference/ui-prompts.md`.

```
include/ui/Txd.h        TXD texture editor (palette-preserving) + PNG writer
include/ui/Bindings.h   drakan_input.ini reader: DualShock action -> key/mouse label
include/ui/Draw.h       canvas, 5x7 font, key cap / mouse icon renderer
include/ui/Prompts.h    PromptOverride, text rules, install()
src/TextRules.cpp       XDB wording rules for en/de/fr/it/es (localised key names, grammar of the surrounding sentence)
tests/ui_test.cpp       unit tests (+ real-game checks when an ISO path is given)
tests/ui_preview.cpp    ui_preview <iso> <outdir> [ini] [text]: before/after PNGs, rewritten strings
```
Build/test: `cmake -S decomp/runtime/ui -B decomp/build/recomp_ui/ui -G "Visual Studio 17 2022" -A x64`, `cmake --build ... --config Release`,
`ui_test.exe "<iso>"` (65 checks), `ui_preview.exe "<iso>" out`. Env: `DRAKAN_INPUT_INI`, `DRAKAN_OVERRIDES`, `DRAKAN_UI_PROMPTS=0` (disable the generated prompts).

Analysis helpers (decomp/analysis/scripts): `ui_font_probe.py` (palette map around the font markers), `ui_txd_sheets.py` / `ui_txd_small.py` / `ui_txd_export.py` / `ui_txd_colorscan.py`
(search all TXDs for controller art), `ui_xdb_scan.py` (controller wording in all XDBs). Preview with the polished bindings: `decomp/build/ui_polish/`.
