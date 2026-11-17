# Issue #9: reproducible font cell numbering

The reporter confirmed UTF-8 and glyph-map support works, but their font repaint still permutes letters.
Their latest comment asks for an exact scan rule or exporter, without sharing copyrighted translation assets.

Rechecked the local PAL loader at 0x285aa8. The reference documentation's horizontal-ruler wording was wrong:
the first horizontal marker selects a column to scan vertically for height; the next selects a column to scan
vertically for pitch. Neither horizontal position is itself height or pitch. Corrected `reference/ui-prompts.md`
and added `analysis/scripts/font_cells.py`, which reads original TXD containers or raw overridden font entries.
It exports CSV geometry and JSON metadata, compares raw BGRA colours including transparent RGB, and explicitly
reports upright and stored marker-row coordinates. Calibration/space cells and per-line end markers do not get
indices; row 0's normal glyphs and the later special rows do.

Validation on the player's PAL ISO, extracted only under ignored `build/font-cells`:

| Font | Cells | Height | Pitch | Space width | Marker BGRA |
| --- | --- | --- | --- | --- | --- |
| 96 | 216 | 15 | 21 | 13 | 01ffff00 |
| 97 | 224 | 17 | 22 | 6 | 01ffff00 |

Font 97's final four entries match the independently documented direction-icon markers at stored row 35,
x=3,26,49,70. Synthetic regression tests separate horizontal and vertical ruler measurements, exercise
bottom-up coordinates and duplicate-colour palette indices, exclude space/end markers, stop at missing line
markers, reject malformed input, and cover container/raw-entry paths. An optional local-disc test uses
`DRAKAN_FONT_TXD`; no game files or complete cell dumps are committed.

Checks passed: all 567 Python tests (2 unrelated skips), including all four exporter tests with the local-disc
fixture enabled; Ruff lint/format checks for both new files; `git diff --check`; repository source audit.
The CLI's raw font-97 entry export is identical to its container export.

This resolves the tooling/documentation gap. The reporter's private repainted Russian pack was not available,
so its final visual result still needs their confirmation. No runtime behavior changed and no game rebuild is needed.
