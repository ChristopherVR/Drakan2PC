// Glyph escapes (\030..\037) in strings read from guest memory: both forms, every language's wording around them.
#include "FakeGame.h"
#include "gameui/GlyphText.h"
#include "gameui/GuestMemory.h"

#include <cstdio>
#include <cstdlib>

using namespace gameui;
static void check(bool value, const char* name) {
    if (!value) {
        std::fprintf(stderr, "FAIL %s\n", name);
        std::exit(1);
    }
}

static GlyphNames keys() {
    GlyphNames n;
    n.name = {"Q", "E", "F", "Enter", "A", "D", "W", "S"};
    return n;
}

int main() {
    const GlyphNames pad = defaultGlyphNames();
    const GlyphNames kb = keys();

    // Every escape, literal form and control byte form.
    for (int i = 0; i < kGlyphCount; ++i) {
        const std::string literal = std::string("x \\03") + char('0' + i) + " y";
        const std::string raw = std::string("x ") + char(0x18 + i) + " y";
        const std::string want = "x " + kb.name[size_t(i)] + " y";
        check(expandGlyphEscapes(literal, kb) == want, "literal form");
        check(expandGlyphEscapes(raw, kb) == want, "control byte form");
    }

    // The movement run, keyboard: one cap name per direction; pad: one phrase.
    check(expandGlyphEscapes("Move with \\034\\035\\036\\037.", kb) == "Move with A D W S.", "keyboard run literal");
    check(expandGlyphEscapes("Bewegen mit \x1c\x1d\x1e\x1f.", kb) == "Bewegen mit A D W S.", "keyboard run raw");
    check(expandGlyphEscapes("Move with \\034\\035\\036\\037.", pad) == "Move with Left Stick.", "pad run");
    check(expandGlyphEscapes("\\034 or \\035", pad) == "Left or Right", "single directions keep their names");
    check(expandGlyphEscapes("\\030\\031", pad) == "Circle Square", "face buttons are never collapsed");
    check(expandGlyphEscapes("\\030\\034\\035", pad) == "Circle Left Right", "mixed run not collapsed");
    check(expandGlyphEscapes("\\034\x1d\\036\x1f", pad) == "Left Stick", "mixed forms in one run");

    // Text without escapes, and look-alikes, pass through untouched (accents are UTF-8 already).
    const std::string plain = "Dr\xc3\xbc"
                              "cken Sie \\ \\0 \\038 \\040 \\3 \\03";
    check(expandGlyphEscapes(plain, kb) == plain, "plain text and look-alikes");
    check(expandGlyphEscapes("", kb).empty(), "empty");
    check(expandGlyphEscapes("\\03", kb) == "\\03", "truncated escape");

    // The other languages around the same escapes.
    check(expandGlyphEscapes("Utilisez \\034\\035\\036\\037 pour vous d\xc3\xa9placer", kb) ==
              "Utilisez A D W S pour vous d\xc3\xa9placer",
          "FR");
    check(expandGlyphEscapes("Usa \\034\\035\\036\\037 para moverte", kb) == "Usa A D W S para moverte", "ES");
    check(expandGlyphEscapes("Premi \\033 per continuare", kb) == "Premi Enter per continuare", "IT");

    // Guest memory: readString applies the names set by the host, in both forms.
    setGlyphNames(kb);
    test::FakeGame g;
    constexpr uint32_t a = 0x100000, b = 0x100100;
    g.wstr(a, "Bewegen: \\034\\035\\036\\037");
    g.wstr(b, "Springen: \x1b");
    check(g.mem().readString(a, 256) == "Bewegen: A D W S", "readString literal");
    check(g.mem().readString(b, 256) == "Springen: Enter", "readString raw");
    setGlyphNames(pad);
    check(g.mem().readString(a, 256) == "Bewegen: Left Stick", "readString follows the device");
    return 0;
}
