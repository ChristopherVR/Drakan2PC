#include "ui/Prompts.h"
#include "ui/Draw.h"
#include "ui/FontAtlas.h"
#include "ui/Txd.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace drakan_ui {
using hle::u8;

namespace {

struct Cell {
    int x, y, w, h;
}; // stored (bottom-up texture) pixel rectangle, as measured on the disc image

// ---- INTERFCE.TXD 97: the plain UI font. Icon pixel boxes measured with analysis/scripts/txd_decode.py (stored
// coordinates, y grows downwards in the stored image which is upside-down on screen). Glyph metrics live in marker
// pixels of rows 35/57 and are untouched.
struct FontIcon {
    const char* action;
    bool dirGlyph;
    Cell cell;
};
const FontIcon kFontIcons[] = {
    {"CIRCLE", false, {136, 36, 22, 21}},   // \030
    {"SQUARE", false, {161, 36, 22, 21}},   // \031
    {"TRIANGLE", false, {187, 36, 22, 21}}, // \032
    {"CROSS", false, {215, 36, 22, 21}},    // \033
    // The original art only used 13-15 px boxes, but the glyph cells (marker pixels at x = 3, 26, 49, 70, 91) are 21-23
    // px wide and 21 rows high (stored rows 14..34 under the marker row 35), so the caps use 19x21 px with 1-2 px
    // margin to the neighbouring glyph.
    {"LEFT", true, {5, 14, 19, 21}},   // \034 (left stick directions W A S D)
    {"RIGHT", true, {28, 14, 19, 21}}, // \035
    {"UP", true, {50, 14, 19, 21}},    // \036
    {"DOWN", true, {71, 14, 19, 21}},  // \037
};

Label directionLabel(const Bindings& b, const char* dir) {
    Label l = b.primary(std::string("LSTICK_") + dir);
    if (!l.bound())
        l = b.primary(std::string("DPAD_") + dir);
    return l;
}

Label directionLabelDpad(const Bindings& b, const char* dir) {
    return b.primary(std::string("DPAD_") + dir);
}

// stored rectangle -> upright rectangle
Cell upright(const Cell& c, int texH) {
    return Cell{c.x, texH - c.y - c.h, c.w, c.h};
}

void blit(TxdEdit& t, const Canvas& cv, int x0, int y0) {
    for (int j = 0; j < cv.h; ++j)
        for (int i = 0; i < cv.w; ++i)
            t.set(x0 + i, y0 + j, cv.px[(size_t)j * cv.w + i]);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
std::vector<u8> PromptOverride::patchFont(const std::vector<u8>& blob, const Bindings& b) {
    TxdEdit t;
    if (!t.load(blob) || t.width() != 256 || t.height() != 256 || t.bpp() != 8)
        return {};
    const Rgba bg = t.get(0, 255); // stored (0,0): the transparent background colour (palette entry 0)
    if (bg.a != 0)
        return {};
    KeyStyle st = defaultStyle(bg, 0x7f);
    for (const FontIcon& ic : kFontIcons) {
        Cell c = upright(ic.cell, t.height());
        // the original icon must be there (some opaque pixel inside the box), otherwise this is a different font
        // version
        bool any = false;
        for (int j = 0; j < c.h && !any; ++j)
            for (int i = 0; i < c.w; ++i)
                if (t.get(c.x + i, c.y + j).a != 0) {
                    any = true;
                    break;
                }
        if (!any)
            continue;
        Label l = ic.dirGlyph ? directionLabel(b, ic.action) : b.primary(ic.action);
        Canvas cv(c.w, c.h, bg);
        if (!l.bound()) { // nothing bound: dim cap with a dash
            Label dash;
            dash.kind = Label::Key;
            dash.cap = "-";
            drawLabelCap(cv, 0, 2, c.w, c.h - 4, dash, st);
        } else if (l.kind == Label::Mouse || l.kind == Label::Wheel) {
            drawLabelCap(cv, 0, 0, c.w, c.h, l, st);
        } else {
            int ch = std::min(c.h, ic.dirGlyph ? c.h : 20);
            drawLabelCap(cv, 0, (c.h - ch) / 2, c.w, ch, l, st);
        }
        blit(t, cv, c.x, c.y);
    }
    return t.finish();
}

// ---------------------------------------------------------------------------------------------------------------------------------
std::vector<u8> PromptOverride::patchControllerDiagram(const std::vector<u8>& blob, const Bindings& b) {
    TxdEdit t;
    if (!t.load(blob) || t.width() != 256 || t.height() != 256 || t.bpp() != 8)
        return {};
    const Rgba bg = t.get(120, 100); // transparent body interior
    if (bg.a != 0)
        return {};
    // 1. erase the pad body: keep transparent pixels, the blue leader lines and the faint brown stubs left/right of the
    // picture
    auto isStub = [](Rgba c) { return c.a != 0 && c.r == 64 && c.g == 32 && c.b == 32; };
    for (int y = 0; y < 256; ++y) {
        for (int x = 0; x < 256; ++x) {
            Rgba c = t.get(x, y);
            if (c.a == 0)
                continue;
            if (c.b > c.r + 25) { // leader line, except the two stick rings (radius ~13 around (90,172) and (165,172))
                auto ring = [&](int cx, int cy) { return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= 18 * 18; };
                if (!ring(90, 172) && !ring(165, 172))
                    continue;
            }
            if (isStub(c)) { // keep only long horizontal runs (the leader stubs), not the faint body outline
                int a = x, e = x;
                while (a > 0 && isStub(t.get(a - 1, y)))
                    --a;
                while (e < 255 && isStub(t.get(e + 1, y)))
                    ++e;
                if (a == 0 || e == 255)
                    continue;
            }
            t.set(x, y, bg);
        }
    }
    // 2. draw the key caps at the anchors of the leader lines (tex pixels, upright)
    auto textual = [](Label l) {
        if (l.kind == Label::Mouse || l.kind == Label::Wheel) {
            l.kind = Label::Key;
        }
        return l;
    }; // text caps read better at this size
    Canvas cv(256, 256, Rgba{});
    for (auto& p : cv.px)
        p = Rgba{0, 0, 0, 255}; // 255 = "untouched" marker (alpha is never > 0x80 in real colours)
    KeyStyle st = defaultStyle(bg, 0x78);
    auto cap = [&](const char* action, int x, int y, int minW, int h) {
        Label l = b.primary(action);
        int w = capWidth(l, minW);
        drawLabelCap(cv, x, y, w, h, l, st);
        return w;
    };
    auto capLabel = [&](const Label& l, int x, int y, int minW, int h) {
        int w = capWidth(l, minW);
        drawLabelCap(cv, x, y, w, h, l, st);
        return w;
    };
    const int H = 14;
    // shoulder buttons: leaders end at x = 50 (L2) / 47 (L1) on the left and x = 208 (R2) / 212 (R1) on the right
    capLabel(textual(b.primary("L2")), 50, 44, 16, H);
    capLabel(textual(b.primary("L1")), 47, 61, 16, H);
    {
        Label l = textual(b.primary("R2"));
        int w = capWidth(l, 16);
        drawLabelCap(cv, 208 - w, 44, w, H, l, st);
    }
    {
        Label l = textual(b.primary("R1"));
        int w = capWidth(l, 16);
        drawLabelCap(cv, 212 - w, 61, w, H, l, st);
    }
    // select / start: leaders end at (105,131) and (151,134)
    {
        Label l = textual(b.primary("SELECT"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 105 - w / 2, 130, w, H, l, st);
    }
    {
        Label l = textual(b.primary("START"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 151 - w / 2, 134, w, H, l, st);
    }
    // face buttons: leaders come from the right and end at x = 217 (Triangle, y 114), 233 (Circle, y 132), 212 (Cross,
    // y 154); Square's leader ends at (190,138) from below
    {
        Label l = textual(b.primary("TRIANGLE"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 217 - w, 107, w, H, l, st);
    }
    {
        Label l = textual(b.primary("CIRCLE"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 233 - w, 125, w, H, l, st);
    }
    {
        Label l = textual(b.primary("CROSS"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 212 - w, 147, w, H, l, st);
    }
    {
        Label l = textual(b.primary("SQUARE"));
        int w = capWidth(l, 18);
        drawLabelCap(cv, 190 - w / 2, 124, w, H, l, st);
    }
    // D-pad (leader ends at (50,134) from the left): inverted-T of four arrow-key caps (or their labels)
    capLabel(directionLabelDpad(b, "UP"), 63, 114, 12, 13);
    capLabel(directionLabelDpad(b, "LEFT"), 50, 128, 12, 13);
    capLabel(directionLabelDpad(b, "DOWN"), 63, 128, 12, 13);
    capLabel(directionLabelDpad(b, "RIGHT"), 76, 128, 12, 13);
    // left stick (leader ends at (84,182)): W / A / S / D
    capLabel(directionLabel(b, "UP"), 85, 156, 14, 13);
    capLabel(directionLabel(b, "LEFT"), 70, 170, 14, 13);
    capLabel(directionLabel(b, "DOWN"), 85, 170, 14, 13);
    capLabel(directionLabel(b, "RIGHT"), 100, 170, 14, 13);
    // right stick (leader ends at (165,182)): the mouse
    {
        Label m;
        m.kind = Label::Mouse;
        m.mouseButton = 0;
        drawLabelCap(cv, 152, 152, 26, 34, m, st);
    }
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            Rgba c = cv.px[(size_t)y * 256 + x];
            if (c.a != 255)
                t.set(x, y, c);
        }
    return t.finish();
}

// ---------------------------------------------------------------------------------------------------------------------------------
std::string PromptOverride::pageCaption(const Label& l) {
    if (!l.bound())
        return "-";
    if (l.name == "Page Up")
        return "PgUp";
    if (l.name == "Page Down")
        return "PgDn";
    return l.cap;
}

std::vector<u8> PromptOverride::patchPageTile(const std::vector<u8>& blob, const Bindings& b) {
    TxdEdit t;
    if (!t.load(blob) || t.width() != 128 || t.height() != 128 || t.bpp() != 32)
        return {};
    // The stone tile (kPageTileRect) holds "R1" above "L1"; the two arrow buttons only show the windows
    // kPageTileNext / kPageTilePrev of it. Sanity: the tile is a light grey stone.
    const int x0 = (int)kPageTileRect.x0, y0 = (int)kPageTileRect.y0, x1 = (int)kPageTileRect.x1,
              y1 = (int)kPageTileRect.y1;
    if (t.get(52, 100).a < 0x40)
        return {};
    // Paint the dark "R1" / "L1" ink out first (the arrow tips show stone beside the caps): every ink texel takes the
    // mean of its stone neighbours, growing inwards.
    auto luma = [](Rgba c) { return c.r * 3 + c.g * 4 + c.b; };
    std::vector<int> stone;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            stone.push_back(luma(t.get(x, y)));
    std::nth_element(stone.begin(), stone.begin() + stone.size() / 2, stone.end());
    const int inkBelow = stone[stone.size() / 2] * 3 / 4;
    const int tw = x1 - x0, th = y1 - y0;
    std::vector<char> ink((size_t)tw * th);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            ink[(size_t)(y - y0) * tw + (x - x0)] = luma(t.get(x, y)) < inkBelow;
    for (bool left = true; left;) {
        left = false;
        std::vector<char> next = ink;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                if (!ink[(size_t)(y - y0) * tw + (x - x0)])
                    continue;
                int n = 0, r = 0, g = 0, bl = 0, a = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < x0 || yy < y0 || xx >= x1 || yy >= y1 || ink[(size_t)(yy - y0) * tw + (xx - x0)])
                            continue;
                        const Rgba c = t.get(xx, yy);
                        r += c.r, g += c.g, bl += c.b, a += c.a, ++n;
                    }
                if (!n) {
                    left = true;
                    continue;
                }
                t.set(x, y, Rgba{(uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(bl / n), (uint8_t)(a / n)});
                next[(size_t)(y - y0) * tw + (x - x0)] = 0;
            }
        if (next == ink)
            break; // isolated ink with no stone neighbour at all: leave it
        ink.swap(next);
    }
    // Captions: the menu-layer keys (PgDn / PgUp by default). A cap fills the visible part of its window; a caption
    // too wide for a bordered cap is drawn on the cap fill without border and with tight glyphs (HD runs replace the
    // whole tile with buildPageTileOverlay).
    KeyStyle st = defaultStyle(t.get(52, 100), 0x80);
    auto button = [&](const TileWindow& win, const Label& l) {
        const int bx = (int)std::ceil(win.x0 - .2f), by = (int)std::ceil(win.y0 - .2f);
        const int bw = (int)std::floor(win.x1 + .2f) - bx, bh = (int)std::floor(win.y1 + .2f) - by;
        Canvas cv(bw, bh, Rgba{0, 0, 0, 255});
        std::string s = l.bound() ? l.cap.substr(0, 3) : "-";
        if (l.kind != Label::Key || capWidth(l, 0) <= bw)
            drawLabelCap(cv, 0, 0, bw, bh, l, st);
        else {
            const int gap = textWidth(s, 1, 1) <= bw ? 1 : 0;
            cv.rect(0, 0, bw, bh, st.fill);
            cv.rect(0, bh - 2, bw, 2, st.shade);
            drawText(cv, (bw - textWidth(s, 1, gap)) / 2, (bh - 2 - kGlyphH) / 2, s, 1, st.text, gap);
        }
        for (int j = 0; j < bh; ++j)
            for (int i = 0; i < bw; ++i) {
                const Rgba c = cv.px[(size_t)j * bw + i];
                if (c.a != 255 && !(c == st.bg))
                    t.set(bx + i, by + j, c);
            }
    };
    button(kPageTileNext, b.primary("R1", true));
    button(kPageTilePrev, b.primary("L1", true));
    return t.finish();
}

bool PromptOverride::wants(const hle::ResourceId& id) {
    if (id.file != "INTERFCE.TXD" || id.type != 0x40)
        return false;
    if (id.id != 96 && id.id != 97 && id.id != 124 && id.id != 94)
        return false;
    return !(user_ && user_->wants(id));
}

bool PromptOverride::apply(const hle::ResourceId& id, std::vector<u8>& data) {
    std::vector<u8> out;
    if (id.id == 96)
        out = data; // notify the host to build the high-resolution atlas
    else if (id.id == 97)
        out = patchFont(data, b_);
    else if (id.id == 124)
        out = patchControllerDiagram(data, b_);
    else if (id.id == 94)
        out = patchPageTile(data, b_);
    if (out.empty()) {
        hle::logf("ui: texture %u of %s has an unexpected layout, left unchanged", id.id, id.file.c_str());
        return false;
    }
    if (hook_) {
        auto decode = [](const std::vector<u8>& blob, int& w, int& h) {
            std::vector<uint32_t> px;
            TxdEdit t;
            if (!t.load(blob))
                return px;
            w = t.width();
            h = t.height();
            px.resize((size_t)w * h);
            for (int ys = 0; ys < h; ++ys)
                for (int x = 0; x < w; ++x) {
                    const Rgba c = t.get(x, h - 1 - ys); // stored row ys = upright row h-1-ys
                    px[(size_t)ys * w + x] =
                        (uint32_t)c.r | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16) | ((uint32_t)c.a << 24);
                }
            return px;
        };
        Generated g;
        int ow = 0, oh = 0;
        const std::vector<uint32_t> px = decode(out, g.w, g.h), orig = decode(data, ow, oh);
        if (!px.empty()) {
            g.id = id.id;
            g.rgba = px.data();
            g.original = ow == g.w && oh == g.h ? orig.data() : nullptr;
            if (id.id == 94) {
                g.pageNext = pageCaption(b_.primary("R1", true));
                g.pagePrev = pageCaption(b_.primary("L1", true));
            }
            hook_(g);
        }
    }
    data = std::move(out);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
Options Options::fromEnvironment() {
    namespace fs = std::filesystem;
    Options o;
    std::string exeDir;
#ifdef _WIN32
    char buf[MAX_PATH * 2] = {};
    DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof buf);
    if (n > 0 && n < sizeof buf)
        exeDir = fs::path(buf).parent_path().string();
#endif
    auto pick = [&](const char* env, const char* name, bool dir) {
        if (const char* e = std::getenv(env))
            if (*e)
                return std::string(e);
        std::error_code ec;
        std::vector<fs::path> cands;
        if (!exeDir.empty())
            cands.push_back(fs::path(exeDir) / name);
        cands.push_back(fs::path(name));
        for (auto& c : cands)
            if (dir ? fs::is_directory(c, ec) : fs::is_regular_file(c, ec))
                return c.string();
        return cands.front().string();
    };
    o.iniPath = pick("DRAKAN_INPUT_INI", "drakan_input.ini", false);
    o.overridesDir = pick("DRAKAN_OVERRIDES", "overrides", true);
    if (const char* e = std::getenv("DRAKAN_UI_PROMPTS"))
        o.prompts = std::atoi(e) != 0;
    return o;
}

std::string install(hle::ResourceService& res, const Options& opt) {
    auto dir = std::make_shared<hle::DirectoryOverride>();
    size_t nfiles = dir->load(opt.overridesDir);
    auto text = std::make_shared<hle::XdbTextOverride>();
    size_t ntext = text->loadDirectory(opt.overridesDir);
    bool found = false;
    if (nfiles)
        res.addOverride(dir);
    if (opt.prompts) {
        Bindings b = Bindings::load(opt.iniPath, &found);
        auto po = std::make_shared<PromptOverride>(b);
        po->setUserOverride(dir);
        if (opt.onGenerated)
            po->setGeneratedHook(opt.onGenerated);
        res.addOverride(po);
        text->addRule(makeTextRule(b));
    }
    if (opt.prompts || ntext)
        res.addOverride(text);
    char line[512];
    std::snprintf(line, sizeof line, "ui: prompts %s (bindings: %s), overrides '%s': %zu file(s), %zu text line(s)",
                  opt.prompts ? "keyboard/mouse" : "original", found ? opt.iniPath.c_str() : "built-in defaults",
                  opt.overridesDir.c_str(), nfiles, ntext);
    return line;
}

} // namespace drakan_ui
