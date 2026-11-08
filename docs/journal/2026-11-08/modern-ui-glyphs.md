# Modern UI shows glyph escapes as numbers (GitHub issue 1) - 2026-11-08

Report (v0.1.9, German): with the Modern UI the keyboard/mouse icons were replaced by codes such as
\034\035\036\037; the original UI draws the caps correctly.

Cause: the original UI draws the escapes with the patched INTERFCE.TXD 97 font (`PromptOverride::patchFont`). The
modern screens read the same strings from guest memory (`IGuestMemory::readString`, used by `Dialog.cpp`,
`Reader.cpp` item / quest / detail text) and draw them with ImGui fonts, so the literal text was shown. Nothing was
language specific; the DE tester simply met a tutorial line that uses the stick glyphs.

Fix: `gameui/GlyphText` expands both escape forms (literal backslash form and control bytes) inside `readString`,
using names built by `drakan_ui::glyphLabels` from the active bindings / prompt style. Tests:
`gameui_glyph_text_test` (all eight escapes in both forms, runs, look-alikes, EN/DE/FR/ES/IT wording, readString) and
the glyph label block of `ui_test`. See `docs/reference/ui-prompts.md` section 12.

Not done: inline key-cap pictures in modern text (the names are plain text); a prompt style change in the settings
still needs a restart, as for the generated font.
