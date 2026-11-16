# Bitmap font code -> glyph rule and translation encoding (issue #9)

Issue #9 (Russian fan translation): text overrides work, but Cyrillic drawn into the Latin-1 cells of `INTERFCE.TXD` 96/97
renders with slips (`Загрузить игру` -> `ЗаиреШоть`), and no static 256-entry code table was found in the ELF.

## Findings

* There is no per-font table. Every text function (loader 0x285aa8, measure 0x2865d8, draw 0x286170 and three more at
  0x2872xx / 0x2881xx / 0x2884xx..0x2885xx, found by scanning the ELF for users of the 35-entry table at 0x4eac30) inlines the
  same arithmetic rule. Full rule in [ui-prompts.md 1.1](../../reference/ui-prompts.md). The earlier note there was incomplete:
  it missed `0xa1..0xff -> code - 0x43` (one contiguous run directly after ASCII), the `0x92 -> 6` redirect, and that table
  position p gives glyph 189 + p (the old "0xde + position" is the value before the final `- 0x21`).
* The glyph index is the order of the marker pixels in the font texture, read upright, line by line at a fixed pitch, line 0
  starting after the calibration ruler (pitch, space width). Lines hold different glyph counts (font 97: 26 21 21 27 26 19 19 24
  22 15 4 = 224, font 96: ... 21 7 = 216). A grid or `cell = code - N` fit therefore matches inside a line and slips at line
  ends, which explains the reporter's symptom; the Latin-1 run itself is contiguous in both fonts.
* Checked by a scratch Python simulation of the loader on the player's disc (not committed): it finds exactly 224 cells in font
  97 (= 94 + 95 + 35, the arrows are the 4 cells of the last line) and 216 in font 96, and cropping the cells for
  `ABCabc019!?' ÀÉÖßàéöÿ¿` with the rule spells the string in both fonts.
* The reporter's font 97 pairs fit the rule as positions inside the texture minus the 26 cells of line 0 (`A` = glyph 32 -> 6,
  `a` = 64 -> 38). Their font 96 pairs do not fit any offset; they likely came from a different calibration.
* Escape detail (measure and draw parse alike): an escape needs exactly 3 digits, evaluated as octal in a signed byte, so `\200`
  and above give no glyph. With fewer digits (`\33x`) the backslash glyph is drawn and the digits are skipped; a `\` before a
  non-digit draws the backslash glyph.
* The 256-byte line buffer limit is the reporter's figure (and the bound our own replacement texts were written to); the copy
  into a fixed buffer was not traced.

## Implemented (runtime/hle)

* `hle/GlyphMap.h`: `glyphIndexForCode` / `codeForGlyph` (the rule and its inverse), `GlyphMap` (`<char> = <glyph index>`),
  `transcodeUtf8` (map, then ASCII / Latin-1 / Windows-1252 fallback, else `?`; control-code and backslash glyphs as `\0oo`
  escapes), `clampXdbText` (255 bytes, never inside an escape).
* `XdbTextOverride::loadIni`: an `encoding = utf-8` line switches the file to UTF-8 (off by default; without it the bytes are
  unchanged). Dev folder and mod packs read `overrides/INTERFCE.TXD/glyphs.ini` first (mod packs by explicit path, independent of
  the archive order). The mod conflict scan skips the directive.
* Tests: `glyph_map_test` (rule round trip, parser errors, transcoding, invalid UTF-8, clamp at escape boundaries, directive,
  byte-identical default), plus a `mod_test` block with two zip packs (map stored after `text.ini`, no false conflict).

## Left for the reporter / later

* Only 224 cells are reachable from a byte, so a Cyrillic pack still gives up Latin-1 cells (homoglyphs reduce the count);
  several translations at once need more cells, i.e. a change to the guest text functions.
* SDB subtitle text (dialogue) is not handled by `text.ini`.
* Not verified in the running game yet: needs a pack with a redrawn font, the map and a UTF-8 `text.ini`.
