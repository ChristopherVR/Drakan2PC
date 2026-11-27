#include "hle/GlyphMap.h"
#include <cstdio>
#include <cstdlib>
#include "common/Text.h"

namespace hle {

using drakan::common::trim;
namespace {

// The game's 35-entry special-code table (code -> glyph 189 + position). Windows-1252 punctuation, then the control
// codes 0x17..0x1f (0x18..0x1f are the 8 button pictures of font 97).
constexpr u8 kSpecialCodes[35] = {0x8c, 0x9c, 0x8a, 0x9a, 0x9f, 0x9d, 0x9e, 0x83, 0x88, 0x98, 0x96, 0x97,
                                  0x91, 0x92, 0x82, 0x93, 0x94, 0x84, 0x86, 0x87, 0x95, 0x85, 0x89, 0x8b,
                                  0x9b, 0x99, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
constexpr int kFirstSpecialGlyph = 0xbd; // 189
constexpr int kFirstLatin1Glyph = 94;    // 0xa1 - 0x43

// Unicode -> Windows-1252 byte for the 0x80..0x9f block (0 = unassigned).
struct Cp1252 {
    char32_t cp;
    u8 byte;
};
constexpr Cp1252 kCp1252[] = {
    {0x20AC, 0x80}, {0x201A, 0x82}, {0x0192, 0x83}, {0x201E, 0x84}, {0x2026, 0x85}, {0x2020, 0x86}, {0x2021, 0x87},
    {0x02C6, 0x88}, {0x2030, 0x89}, {0x0160, 0x8a}, {0x2039, 0x8b}, {0x0152, 0x8c}, {0x017D, 0x8e}, {0x2018, 0x91},
    {0x2019, 0x92}, {0x201C, 0x93}, {0x201D, 0x94}, {0x2022, 0x95}, {0x2013, 0x96}, {0x2014, 0x97}, {0x02DC, 0x98},
    {0x2122, 0x99}, {0x0161, 0x9a}, {0x203A, 0x9b}, {0x0153, 0x9c}, {0x017E, 0x9e}, {0x0178, 0x9f}};

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// Decodes one UTF-8 sequence at s[i]; advances i. Returns false (and skips one byte) on a malformed sequence.
bool nextCodePoint(const std::string& s, size_t& i, char32_t& cp) {
    const u8 b0 = static_cast<u8>(s[i]);
    int len = 0;
    char32_t min = 0;
    if (b0 < 0x80) {
        cp = b0;
        ++i;
        return true;
    } else if ((b0 & 0xe0) == 0xc0) {
        len = 2, cp = b0 & 0x1f, min = 0x80;
    } else if ((b0 & 0xf0) == 0xe0) {
        len = 3, cp = b0 & 0x0f, min = 0x800;
    } else if ((b0 & 0xf8) == 0xf0) {
        len = 4, cp = b0 & 0x07, min = 0x10000;
    } else {
        ++i;
        return false;
    }
    if (i + len > s.size()) {
        ++i;
        return false;
    }
    for (int k = 1; k < len; ++k) {
        const u8 b = static_cast<u8>(s[i + k]);
        if ((b & 0xc0) != 0x80) {
            ++i;
            return false;
        }
        cp = (cp << 6) | (b & 0x3f);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++i;
        return false;
    }
    i += len;
    return true;
}

// Byte for a code point without a map entry: ASCII, Latin-1 with a glyph, the Windows-1252 punctuation the font has.
int fallbackByte(char32_t cp) {
    if (cp < 0x80)
        return static_cast<int>(cp);
    if (cp == 0xA0)
        return 0x20; // no-break space: 0xa0 has no glyph
    if (cp >= 0xA1 && cp <= 0xFF)
        return static_cast<int>(cp);
    for (const auto& e : kCp1252)
        if (e.cp == cp)
            return glyphIndexForCode(e.byte) < kGlyphCount ? e.byte : -1;
    return -1;
}

void appendGlyph(std::string& out, int glyph) {
    const int code = codeForGlyph(glyph);
    if (code < 0x20 || code == '\\') {
        // The text functions read "\ooo" as one code. Only codes below 0x80 survive (the escape value is a signed
        // byte), which covers the control codes and the backslash glyph.
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\%03o", code);
        out += buf;
    } else {
        out += static_cast<char>(code);
    }
}

} // namespace

int glyphIndexForCode(u8 code) {
    if (code == 0x20 || code == 0x09)
        return -1;
    if (code >= 0xa1)
        return code - 0x43;
    if (code == 0x92)
        return 6; // right single quote draws the apostrophe
    if (code < 0x20 || code >= 0x7f) {
        int pos = 0;
        while (pos < 35 && kSpecialCodes[pos] != code)
            ++pos;
        return kFirstSpecialGlyph + pos; // 35 (not in the table) -> 224 = no glyph
    }
    return code - 0x21;
}

int codeForGlyph(int glyph) {
    if (glyph < 0 || glyph >= kGlyphCount)
        return -1;
    if (glyph < kFirstLatin1Glyph)
        return glyph + 0x21;
    if (glyph < kFirstSpecialGlyph)
        return glyph + 0x43;
    const u8 code = kSpecialCodes[glyph - kFirstSpecialGlyph];
    return code == 0x92 ? -1 : code;
}

size_t GlyphMap::parse(const std::string& content, std::vector<std::string>* errors) {
    size_t n = 0, pos = 0, lineNo = 0;
    if (content.compare(0, 3, "\xEF\xBB\xBF") == 0)
        pos = 3;
    auto fail = [&](const std::string& why) {
        if (errors)
            errors->push_back("line " + std::to_string(lineNo) + ": " + why);
    };
    while (pos < content.size()) {
        size_t e = content.find('\n', pos);
        std::string line = content.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = e == std::string::npos ? content.size() : e + 1;
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#')
            continue;
        size_t eq = t.find('=', 1); // start at 1 so the character '=' itself can be a key ("= = 28")
        if (eq == std::string::npos) {
            fail("no '='");
            continue;
        }
        std::string key = trim(t.substr(0, eq)), val = trim(t.substr(eq + 1));
        char32_t cp = 0;
        if (key.size() > 2 && (key[0] == 'U' || key[0] == 'u') && key[1] == '+') {
            char* end = nullptr;
            unsigned long v = std::strtoul(key.c_str() + 2, &end, 16);
            if (!end || *end || v > 0x10FFFF) {
                fail("bad code point '" + key + "'");
                continue;
            }
            cp = static_cast<char32_t>(v);
        } else {
            size_t i = 0;
            if (key.empty() || !nextCodePoint(key, i, cp) || i != key.size()) {
                fail("key must be one UTF-8 character or U+XXXX");
                continue;
            }
        }
        char* end = nullptr;
        long g = val.empty() ? -1 : std::strtol(val.c_str(), &end, 10);
        if (val.empty() || !end || *end || g < 0 || g >= kGlyphCount) {
            fail("glyph index must be 0.." + std::to_string(kGlyphCount - 1));
            continue;
        }
        if (codeForGlyph(static_cast<int>(g)) < 0) {
            fail("glyph " + std::to_string(g) + " cannot be reached by any byte");
            continue;
        }
        map_[cp] = static_cast<int>(g);
        ++n;
    }
    return n;
}

int GlyphMap::glyphFor(char32_t cp) const {
    auto it = map_.find(cp);
    return it == map_.end() ? -1 : it->second;
}

bool clampXdbText(std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes)
        return false;
    size_t cut = maxBytes;
    // An escape "\ddd" that starts at most 3 bytes before the cut may be split by it.
    for (size_t p = cut; p-- > 0 && p + 4 > cut;) {
        if (text[p] != '\\')
            continue;
        size_t digits = 0;
        while (digits < 3 && p + 1 + digits < text.size() && isDigit(text[p + 1 + digits]))
            ++digits;
        if (p + 1 + digits > cut)
            cut = p;
        break;
    }
    text.resize(cut);
    return true;
}

std::string transcodeUtf8(const std::string& utf8, const GlyphMap* map, TranscodeStats* stats, size_t maxBytes) {
    TranscodeStats local;
    TranscodeStats& st = stats ? *stats : local;
    std::string out;
    out.reserve(utf8.size());
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        if (!nextCodePoint(utf8, i, cp)) {
            ++st.invalid;
            out += '?';
            continue;
        }
        const int g = map ? map->glyphFor(cp) : -1;
        if (g >= 0) {
            appendGlyph(out, g);
            continue;
        }
        const int b = fallbackByte(cp);
        if (b < 0) {
            ++st.unmapped;
            out += '?';
        } else {
            out += static_cast<char>(b);
        }
    }
    if (clampXdbText(out, maxBytes))
        st.clampedTo = out.size();
    return out;
}

} // namespace hle
