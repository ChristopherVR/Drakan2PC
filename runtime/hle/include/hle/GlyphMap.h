// Text encoding for translation packs: the game's code -> glyph rule for the INTERFCE.TXD bitmap fonts (96/97), a
// mod-provided glyph map (Unicode code point -> glyph index) and a UTF-8 -> game byte transcoder used by text.ini files
// that declare `encoding = utf-8`. Platform free; see docs/reference/modding.md ("Translations") and
// docs/reference/ui-prompts.md section 1.1 for the reverse-engineered rule.
#pragma once
#include "Interfaces.h"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace hle {

// Glyph indices a font can have: 0..93 ASCII 0x21..0x7e, 94..188 Latin-1 0xa1..0xff, 189..223 the 35 entries of the
// special-code table (Windows-1252 punctuation, then the control codes 0x17..0x1f = the button pictures).
constexpr int kGlyphCount = 224;
// Longest text the transcoder emits (bytes, without the NUL). The 256-byte line buffer is the figure from issue #9 and
// from our own replacement texts; it has not been traced in the guest code.
constexpr size_t kXdbTextMaxBytes = 255;

// The engine's rule (FUN_002865d8 and the four other text functions): glyph index of a text byte, -1 for the bytes
// that only advance the pen (0x20 space, 0x09 tab), kGlyphCount for codes that resolve to no glyph.
int glyphIndexForCode(u8 code);
// Inverse: the byte that draws `glyph`, or -1 when no byte reaches it (glyph 202: 0x92 is redirected to glyph 6).
int codeForGlyph(int glyph);

class GlyphMap {
public:
    // Lines "<char> = <glyph>": <char> is one UTF-8 character or U+XXXX, <glyph> a decimal glyph index 0..223.
    // ';' / '#' start comments, a UTF-8 BOM is skipped. Bad lines are skipped and described in `errors`.
    // Returns the number of entries accepted.
    size_t parse(const std::string& content, std::vector<std::string>* errors = nullptr);
    // Glyph index for a code point, -1 when the map has no entry.
    int glyphFor(char32_t cp) const;
    size_t size() const { return map_.size(); }
    bool empty() const { return map_.empty(); }

private:
    std::map<char32_t, int> map_;
};

struct TranscodeStats {
    size_t unmapped = 0;  // code points with no map entry and no Latin-1 / Windows-1252 byte (written as '?')
    size_t invalid = 0;   // malformed UTF-8 sequences (written as '?')
    size_t clampedTo = 0; // non-zero when the text was cut to this many bytes
};

// UTF-8 -> game bytes. Each code point: the map entry if any (emitted as the byte of that glyph, glyphs 220..223 and
// the other control codes as the game's literal "\0oo" escape), else ASCII / Latin-1 0xa1..0xff / the Windows-1252
// punctuation the font has, else '?'. Literal "\ooo" escapes already in the text pass through. The result is clamped
// with clampXdbText(maxBytes).
std::string transcodeUtf8(const std::string& utf8, const GlyphMap* map, TranscodeStats* stats = nullptr,
                          size_t maxBytes = kXdbTextMaxBytes);
// Cuts `text` to at most maxBytes without splitting a "\ooo" escape (the game reads up to 3 digits after '\').
// Returns true when it cut.
bool clampXdbText(std::string& text, size_t maxBytes = kXdbTextMaxBytes);

} // namespace hle
