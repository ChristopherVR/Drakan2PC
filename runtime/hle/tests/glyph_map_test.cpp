// glyph_map_test: the game's code -> glyph rule, the translation glyph map, UTF-8 transcoding, the line clamp and the
// text.ini `encoding` directive. Synthetic strings only, no game data.
#include "hle/GlyphMap.h"
#include "hle/ResourceOverride.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace hle;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                      \
        }                                                                                                              \
    } while (0)

static std::string applied(XdbTextOverride& t, u16 id) {
    std::vector<u8> d = xdb::encode({"orig", "o", "o", "o", "o"});
    std::vector<std::string> s;
    ResourceId rid{"INTERFCE.XDB", "", 0x400, id, 0};
    if (!t.apply(rid, d) || !xdb::decode(d, s))
        return "<unchanged>";
    return s[0];
}

int main() {
    // ---- the engine rule
    CHECK(glyphIndexForCode('!') == 0 && glyphIndexForCode('A') == 32 && glyphIndexForCode('a') == 64);
    CHECK(glyphIndexForCode('~') == 93);
    CHECK(glyphIndexForCode(0xa1) == 94 && glyphIndexForCode(0xc0) == 125 && glyphIndexForCode(0xff) == 188);
    CHECK(glyphIndexForCode(0xd7) == 0xd7 - 0x43 && glyphIndexForCode(0xf7) == 0xf7 - 0x43); // contiguous run
    CHECK(glyphIndexForCode(0x8c) == 189 && glyphIndexForCode(0x99) == 214);
    CHECK(glyphIndexForCode(0x17) == 215 && glyphIndexForCode(0x18) == 216 && glyphIndexForCode(0x1f) == 223);
    CHECK(glyphIndexForCode(0x92) == 6); // right quote draws the apostrophe
    CHECK(glyphIndexForCode(0x20) == -1 && glyphIndexForCode(0x09) == -1);
    for (int c : {0x00, 0x01, 0x16, 0x7f, 0x80, 0x81, 0x8d, 0x8e, 0x8f, 0x90, 0xa0})
        CHECK(glyphIndexForCode(static_cast<u8>(c)) == kGlyphCount);
    // inverse
    bool roundTrip = true;
    for (int g = 0; g < kGlyphCount; ++g) {
        int c = codeForGlyph(g);
        if (g == 202)
            roundTrip &= c == -1; // 0x92 is redirected to glyph 6
        else
            roundTrip &= c >= 0 && glyphIndexForCode(static_cast<u8>(c)) == g;
    }
    CHECK(roundTrip);
    CHECK(codeForGlyph(6) == '\'' && codeForGlyph(-1) == -1 && codeForGlyph(kGlyphCount) == -1);

    // ---- glyph map parsing
    {
        GlyphMap m;
        std::vector<std::string> err;
        size_t n = m.parse("\xEF\xBB\xBF; comment\n"
                           "U+0416 = 125\n"
                           "\xD0\x94 = 126\r\n" // U+0414 written as the character
                           "u+2190 = 220\n"     // control-code glyph
                           "U+005C = 59\n"      // backslash glyph
                           "= = 28\n"           // the '=' character itself
                           "U+0417 = 202\n"     // unreachable glyph
                           "U+0418 = 224\n"     // out of range
                           "AB = 3\n"           // two characters
                           "U+0419 = x\n"       // not a number
                           "no equals sign\n",
                           &err);
        CHECK(n == 5 && m.size() == 5 && err.size() == 5);
        CHECK(m.glyphFor(0x416) == 125 && m.glyphFor(0x414) == 126 && m.glyphFor(0x2190) == 220);
        CHECK(m.glyphFor('=') == 28 && m.glyphFor(0x417) == -1 && m.glyphFor('A') == -1);

        // ---- transcoding
        TranscodeStats st;
        CHECK(transcodeUtf8("Hello \\033 \xC3\xA9", nullptr, &st) == "Hello \\033 \xE9" && !st.unmapped);
        CHECK(transcodeUtf8("\xE2\x80\x99\xE2\x80\x9C\xC2\xA0", nullptr) == "\x92\x93 "); // 1252 quotes, nbsp
        st = {};
        CHECK(transcodeUtf8("\xE2\x82\xAC\xD0\x96", nullptr, &st) == "??" && st.unmapped == 2); // euro: no glyph
        st = {};
        CHECK(transcodeUtf8("\xD0\x96\xD0\x94 a", &m, &st) == "\xC0\xC1 a" && !st.unmapped);
        CHECK(transcodeUtf8("\xE2\x86\x90\\", &m) == "\\034\\134"); // glyph 220 -> \034, backslash -> \134
        st = {};
        // truncated lead, surrogate, stray continuations, overlong: each bad byte becomes one '?'
        CHECK(transcodeUtf8("a\xC3(\xED\xA0\x80\xC0\x80z", nullptr, &st) == "a?(?????z" && st.invalid == 6);

        // ---- clamp
        std::string s(300, 'x');
        st = {};
        CHECK(transcodeUtf8(s, nullptr, &st).size() == kXdbTextMaxBytes && st.clampedTo == kXdbTextMaxBytes);
        std::string esc = std::string(253, 'x') + "\\033";
        CHECK(clampXdbText(esc) && esc == std::string(253, 'x')); // never cut inside the escape
        esc = std::string(251, 'x') + "\\033y";
        CHECK(clampXdbText(esc) && esc == std::string(251, 'x') + "\\033"); // the escape ends at the limit
        esc = std::string(254, 'x') + "\\ab";
        CHECK(clampXdbText(esc) && esc == std::string(254, 'x') + "\\"); // '\' + non-digit is a plain glyph
        esc = std::string(200, 'x');
        CHECK(!clampXdbText(esc) && esc.size() == 200);
    }

    // ---- text.ini directive
    {
        bool utf8 = true;
        std::string bad;
        CHECK(!textIniEncoding("1 = a\n", utf8));
        CHECK(textIniEncoding("; x\nEncoding = UTF-8\n", utf8) && utf8);
        CHECK(textIniEncoding("encoding = raw\n", utf8) && !utf8);
        CHECK(textIniEncoding("encoding = koi8\n", utf8, &bad) && !utf8 && bad == "koi8");
        CHECK(isTextIniDirective(" encoding ") && !isTextIniDirective("156"));

        GlyphMap m;
        m.parse("U+0416 = 125\n");
        // default (no directive): bytes as written, byte-identical to the old behaviour
        XdbTextOverride raw;
        CHECK(raw.loadIni("INTERFCE.XDB", "156 = \xD0\x96\\\\033\n", &m) == 1);
        CHECK(applied(raw, 156) == "\xD0\x96\\033");
        // utf-8: transcoded through the map; the directive is not a text line
        XdbTextOverride u;
        CHECK(u.loadIni("INTERFCE.XDB",
                        "\xEF\xBB\xBF"
                        "156 = \xD0\x96\\\\033\nencoding = utf-8\n",
                        &m) == 1);
        CHECK(applied(u, 156) == "\xC0\\033");
        XdbTextOverride longText;
        CHECK(longText.loadIni("INTERFCE.XDB", "encoding = utf-8\n157 = " + std::string(400, 'z') + "\n") == 1);
        CHECK(applied(longText, 157).size() == kXdbTextMaxBytes);
    }

    printf("glyph_map_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
