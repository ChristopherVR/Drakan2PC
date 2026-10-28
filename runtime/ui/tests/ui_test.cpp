// ui_test [iso]   - unit tests of the prompt generator; with an ISO also checks the real textures / all XDB texts.
#include "hle/IsoFs.h"
#include "ui/Draw.h"
#include "ui/Prompts.h"
#include "ui/Txd.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

using namespace drakan_ui;
static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                 \
        }                                                                                                              \
    } while (0)

static std::vector<hle::u8> makeTex8(int w, int h, const std::vector<Rgba>& pal, const std::vector<hle::u8>& idx) {
    std::vector<hle::u8> b(0x40 + 16 + 1024 + idx.size(), 0);
    uint32_t hdr[4] = {(uint32_t)w, (uint32_t)h, (uint32_t)w, 8};
    std::memcpy(b.data(), hdr, 16);
    uint32_t ph[2] = {1, 256};
    std::memcpy(&b[0x40], ph, 8);
    for (size_t i = 0; i < pal.size() && i < 256; ++i) {
        b[0x50 + i * 4] = pal[i].b;
        b[0x51 + i * 4] = pal[i].g;
        b[0x52 + i * 4] = pal[i].r;
        b[0x53 + i * 4] = pal[i].a;
    }
    std::memcpy(&b[0x50 + 1024], idx.data(), idx.size());
    return b;
}

int main(int argc, char** argv) {
    // ---- bindings
    {
        Bindings d = Bindings::defaults();
        CHECK(d.primary("SQUARE").kind == Label::Mouse && d.primary("SQUARE").mouseButton == 1 &&
              d.primary("SQUARE").cap == "LMB");
        CHECK(d.primary("L1").cap == "E" && d.primary("L1").name == "E");
        CHECK(d.sources("TRIANGLE").size() == 1 && d.sources("TRIANGLE")[0] == "SPACE");
        CHECK(d.primary("CROSS").name == "Left Ctrl");
        CHECK(d.primary("CROSS", true).name == "Enter");
        CHECK(d.primary("START").cap == "ESC" && d.primary("SELECT").cap == "TAB");
        CHECK(d.primary("LSTICK_UP").cap == "W" && d.primary("DPAD_LEFT").kind == Label::ArrowKey &&
              d.primary("DPAD_LEFT").arrow == 2);
        CHECK(!d.primary("L3").bound());
        Bindings c;
        c.parse("[bindings]\nF = L1 | R1   ; comment\nWHEELUP = L2\nNUMPAD5 = CROSS\nUNKNOWNKEY = CIRCLE\n");
        CHECK(c.primary("L1").cap == "F" && c.primary("R1").cap == "F" && c.primary("L2").kind == Label::Wheel);
        CHECK(c.primary("CROSS").cap == "N5" && c.primary("CIRCLE").cap == "UNK");
        CHECK(c.primary("SQUARE").kind == Label::None);
    }
    // ---- text rewrite
    {
        Bindings d = Bindings::defaults();
        std::string t;
        auto rw = [&](const char* file, unsigned id, int lang, const char* in) {
            t = in;
            rewriteText(d, file, id, lang, t);
            return t;
        };
        CHECK(rw("INTERFCE.XDB", 73, 0, "Press \\033 to continue.") == "Press Enter to continue.");
        CHECK(rw("LEVEL1ON.XDB", 29, 0, "Press \\033 to crouch") ==
              "Press \\033 to crouch"); // gameplay glyphs stay glyphs
        CHECK(rw("LEVEL1ON.XDB", 24, 0, "Press the R1 button to draw your weapon.") ==
              "Press the Q key to draw your weapon.");
        CHECK(rw("LEVEL1ON.XDB", 49, 0, "pressing the SELECT.  ") == "pressing the Tab key.  ");
        CHECK(rw("LEVEL1ON.XDB", 24, 3, "Premi R1 per impugnare l'arma.") == "Premi il tasto Q per impugnare l'arma.");
        CHECK(rw("LEVEL1ON.XDB", 24, 2, "Appuyez sur R1 pour sortir.") == "Appuyez sur la touche Q pour sortir.");
        CHECK(rw("LEVEL1ON.XDB", 24, 4,
                 "Pulsa el bot\xf3"
                 "n R1 para x.") == "Pulsa la tecla Q para x.");
        CHECK(rw("INTERFCE.XDB", 35, 1, "\\033 Laden, START zum Verlassen") == "Eingabetaste Laden, Esc zum Verlassen");
        CHECK(rw("INTERFCE.XDB", 73, 1,
                 "Dr\xfc"
                 "cken Sie \\033, um") == "Dr\xfc"
                                          "cken Sie die Eingabetaste, um");
        CHECK(rw("INTERFCE.XDB", 148, 1, "mit der L1- oder der R1-Taste.") ==
              "mit der Bild auf-Taste oder der Bild ab-Taste."); // the inventory help names the MENU layer keys (Page
                                                                 // Up / Page Down)
        CHECK(rw("INTERFCE.XDB", 107, 0, "Reading Save Games on Memory Card (PS2)...") ==
              "Loading saved games..."); // memory card wording -> PC save wording
        CHECK(rw("INTERFCE.XDB", 129, 2, "x") == "Lecture des sauvegardes...");
        CHECK(rw("INTERFCE.XDB", 74, 0, "x") ==
              "Please make sure the save folder is available or press Enter to proceed anyway.");
        CHECK(rw("INTERFCE.XDB", 130, 1, "Controller") == "Steuerung");
        CHECK(rw("LEVEL1ON.XDB", 24, 1,
                 "Dr\xfc"
                 "cke Sie die R1-Taste.") == "Dr\xfc"
                                             "cke Sie die Q-Taste.");
        CHECK(rw("LEVEL1ON.XDB", 1, 0, "R10 L1X START") == "R10 L1X Esc"); // no partial words
        CHECK(rw("LEVEL1ON.XDB", 22, 0, "with the left analog stick.") == "with the W A S D keys.");
        CHECK(rw("LEVEL1ON.XDB", 23, 0, "using the right stick.") == "using the mouse.");
        CHECK(rw("INTERFCE.XDB", 130, 0, "Controller") == "Controls");
        CHECK(rw("INTERFCE.XDB", 41, 0, "Select Skill") == "Select Skill");
        Bindings m;
        m.parse("[bindings]\nMOUSE2 = L2\n");
        t = "Lock on with the L2 button.";
        rewriteText(m, "X.XDB", 1, 0, t);
        CHECK(t == "Lock on with the Right Mouse Button.");
    }
    // ---- canvas / TxdEdit on a synthetic palettised texture
    {
        std::vector<Rgba> pal(256);
        pal[0] = Rgba{1, 1, 1, 0};
        pal[7] = Rgba{1, 255, 255, 0};
        pal[8] = Rgba{255, 255, 255, 0};
        pal[10] = Rgba{200, 0, 0, 0x7f};
        pal[11] = Rgba{0, 200, 0, 0x7f};
        std::vector<hle::u8> idx(16 * 16, 8);
        for (int i = 0; i < 16; ++i)
            idx[i] = 7; // marker row (stored top row)
        for (int i = 0; i < 16 * 16; ++i)
            if ((i % 16) < 4 && i >= 16)
                idx[i] = 10;   // old "icon" at the left
        idx[16 * 8 + 12] = 11; // a pixel that is the only user of palette entry 11
        auto blob = makeTex8(16, 16, pal, idx);
        TxdEdit t;
        CHECK(t.load(blob) && t.width() == 16 && t.bpp() == 8);
        CHECK(t.get(0, 15) == pal[7]); // stored row 0 = upright bottom
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < 4; ++x)
                t.set(x, y, Rgba{5, 6, 7, 0x7f}); // overwrite the icon with a new colour
        t.set(12, 7, Rgba{200, 0, 0, 0x7f});      // reuse of a colour that stays in the palette? (entry 10 is free now)
        auto out = t.finish();
        CHECK(out.size() == blob.size() && t.overflow() == 0);
        for (int i = 0; i < 16; ++i)
            CHECK(out[0x50 + 1024 + i] == 7); // marker row untouched
        // all untouched pixels keep their index; marker/background palette entries unchanged
        int diffs = 0;
        for (size_t i = 0; i < idx.size(); ++i)
            if (out[0x50 + 1024 + i] != idx[i])
                ++diffs;
        CHECK(diffs == 15 * 4 + 1);
        CHECK(std::memcmp(&out[0x50 + 0 * 4], &blob[0x50], 4) == 0 &&
              std::memcmp(&out[0x50 + 7 * 4], &blob[0x50 + 28], 8) == 0);
        TxdEdit t2;
        CHECK(t2.load(out) && t2.get(1, 3) == (Rgba{5, 6, 7, 0x7f}));
    }
    // ---- key cap rendering sanity
    {
        Canvas c(22, 21, Rgba{});
        KeyStyle st = defaultStyle(Rgba{}, 0x7f);
        Bindings d = Bindings::defaults();
        drawLabelCap(c, 0, 2, 22, 17, d.primary("TRIANGLE"), st);
        int lit = 0;
        for (auto& p : c.px)
            if (p.a)
                ++lit;
        CHECK(lit > 200 && c.get(0, 0).a == 0 && c.get(11, 10).a != 0);
        CHECK(textWidth("LMB") == 17 && textWidth("W", 2) == 10);
    }
    // ---- real game data
    if (argc > 1) {
        hle::IsoFs fs;
        std::string err;
        CHECK(fs.openIso(argv[1], &err));
        fs.setLanguageRoot("\\ENGLISH");
        auto readAll = [&](const char* path) {
            hle::IsoFs::Node n;
            return fs.lookup(path, n) ? fs.readAll(n) : std::vector<hle::u8>();
        };
        auto d = readAll("COMMON\\INTERFCE\\INTERFCE.TXD");
        CHECK(d.size() == 5607812);
        uint32_t idx, cnt;
        std::memcpy(&idx, &d[6], 4);
        std::memcpy(&cnt, &d[10], 4);
        std::map<unsigned, std::vector<hle::u8>> tex;
        for (uint32_t i = 0; i < cnt; ++i) {
            const hle::u8* e = &d[idx + i * 14];
            uint16_t t, k;
            uint32_t off, sz;
            std::memcpy(&t, e, 2);
            std::memcpy(&k, e + 2, 2);
            std::memcpy(&off, e + 6, 4);
            std::memcpy(&sz, e + 10, 4);
            if (t == 0x40)
                tex[k] = std::vector<hle::u8>(d.begin() + off, d.begin() + off + sz);
        }
        Bindings b = Bindings::defaults();
        for (unsigned id : {97u, 124u, 94u}) {
            auto& o = tex[id];
            auto n = id == 97    ? PromptOverride::patchFont(o, b)
                     : id == 124 ? PromptOverride::patchControllerDiagram(o, b)
                                 : PromptOverride::patchPageTile(o, b);
            CHECK(!n.empty() && n.size() == o.size());
            if (n.empty())
                continue;
            CHECK(std::memcmp(n.data(), o.data(), 0x40) == 0); // header untouched
            if (id == 97) { // glyph metric markers (palette index 7) and everything outside the icon boxes must be
                            // bit-identical
                int diff = 0, outside = 0;
                const size_t po = 0x50 + 1024;
                for (int y = 0; y < 256; ++y)
                    for (int x = 0; x < 256; ++x) {
                        if (n[po + y * 256 + x] == o[po + y * 256 + x])
                            continue;
                        ++diff;
                        bool inIcon =
                            (y >= 36 && y <= 56 && x >= 136 && x <= 236) || (y >= 14 && y <= 34 && x >= 5 && x <= 89);
                        if (!inIcon)
                            ++outside;
                    }
                CHECK(diff > 500 && outside == 0);
                // outside the icon boxes every pixel keeps its exact colour
                TxdEdit to, tn;
                CHECK(to.load(o) && tn.load(n));
                int colourDiff = 0;
                for (int y = 0; y < 256; ++y)
                    for (int x = 0; x < 256; ++x) {
                        int sy = 255 - y;
                        bool inIcon = (sy >= 36 && sy <= 56 && x >= 136 && x <= 236) ||
                                      (sy >= 14 && sy <= 34 && x >= 5 && x <= 89);
                        if (!inIcon && to.get(x, y) != tn.get(x, y))
                            ++colourDiff;
                    }
                CHECK(colourDiff == 0);
            }
        }
        // every XDB text entry survives decode/encode and the rewrite keeps a valid layout
        std::set<std::string> files;
        int entries = 0, changed = 0, bad = 0;
        for (auto& kv : fs.entries()) {
            const std::string& k = kv.first;
            if (k.size() < 4 || k.compare(k.size() - 4, 4, ".XDB") != 0 || k.rfind("ENGLISH\\", 0) != 0)
                continue;
            auto x = readAll(k.c_str());
            uint32_t xi, xc;
            std::memcpy(&xi, &x[6], 4);
            std::memcpy(&xc, &x[10], 4);
            std::string fname = k.substr(k.find_last_of('\\') + 1);
            for (uint32_t i = 0; i < xc; ++i) {
                const hle::u8* e = &x[xi + i * 14];
                uint16_t t, id;
                uint32_t off, sz;
                std::memcpy(&t, e, 2);
                std::memcpy(&id, e + 2, 2);
                std::memcpy(&off, e + 6, 4);
                std::memcpy(&sz, e + 10, 4);
                if (t != 0x400)
                    continue;
                std::vector<hle::u8> p(x.begin() + off, x.begin() + off + sz);
                std::vector<std::string> s;
                ++entries;
                if (!hle::xdb::decode(p, s)) {
                    ++bad;
                    continue;
                }
                if (hle::xdb::encode(s) != p)
                    ++bad;
                bool ch = false;
                for (int l = 0; l < 5; ++l) {
                    std::string tt = s[l];
                    ch |= rewriteText(b, fname, id, l, tt);
                }
                changed += ch;
            }
        }
        std::printf("XDB: %d text entries, %d need rewriting, %d bad\n", entries, changed, bad);
        CHECK(entries > 500 && bad == 0 && changed > 20);
    }
    std::printf("ui_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
