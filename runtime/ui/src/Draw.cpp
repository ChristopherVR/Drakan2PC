#include "ui/Draw.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace drakan_ui {

namespace {
struct G {
    char ch;
    const char* rows[7];
};
// 5x7 pixel font, '#' = set
const G kFont[] = {
    {'A', {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'B', {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."}},
    {'C', {".###.", "#...#", "#....", "#....", "#....", "#...#", ".###."}},
    {'D', {"####.", "#...#", "#...#", "#...#", "#...#", "#...#", "####."}},
    {'E', {"#####", "#....", "#....", "####.", "#....", "#....", "#####"}},
    {'F', {"#####", "#....", "#....", "####.", "#....", "#....", "#...."}},
    {'G', {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"}},
    {'H', {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"}},
    {'I', {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'J', {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."}},
    {'K', {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"}},
    {'L', {"#....", "#....", "#....", "#....", "#....", "#....", "#####"}},
    {'M', {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"}},
    {'N', {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"}},
    {'O', {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'P', {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."}},
    {'Q', {".###.", "#...#", "#...#", "#...#", "#.#.#", "#..#.", ".##.#"}},
    {'R', {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"}},
    {'S', {".####", "#....", "#....", ".###.", "....#", "....#", "####."}},
    {'T', {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."}},
    {'U', {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."}},
    {'V', {"#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."}},
    {'W', {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"}},
    {'X', {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"}},
    {'Y', {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."}},
    {'Z', {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"}},
    {'0', {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."}},
    {'1', {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."}},
    {'2', {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"}},
    {'3', {"####.", "....#", "....#", ".###.", "....#", "....#", "####."}},
    {'4', {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."}},
    {'5', {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."}},
    {'6', {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."}},
    {'7', {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."}},
    {'8', {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."}},
    {'9', {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."}},
    {'+', {".....", "..#..", "..#..", "#####", "..#..", "..#..", "....."}},
    {'-', {".....", ".....", ".....", "#####", ".....", ".....", "....."}},
    {'=', {".....", ".....", "#####", ".....", "#####", ".....", "....."}},
    {'/', {"....#", "....#", "...#.", "..#..", ".#...", "#....", "#...."}},
    {'\\', {"#....", "#....", ".#...", "..#..", "...#.", "....#", "....#"}},
    {'.', {".....", ".....", ".....", ".....", ".....", ".##..", ".##.."}},
    {',', {".....", ".....", ".....", ".....", ".##..", "..#..", ".#..."}},
    {';', {".....", ".##..", ".##..", ".....", ".##..", "..#..", ".#..."}},
    {'\'', {".##..", "..#..", ".#...", ".....", ".....", ".....", "....."}},
    {'`', {".#...", "..#..", "...#.", ".....", ".....", ".....", "....."}},
    {'[', {".###.", ".#...", ".#...", ".#...", ".#...", ".#...", ".###."}},
    {']', {".###.", "...#.", "...#.", "...#.", "...#.", "...#.", ".###."}},
    {'*', {".....", "#.#.#", ".###.", "#####", ".###.", "#.#.#", "....."}},
    {'?', {".###.", "#...#", "....#", "...#.", "..#..", ".....", "..#.."}},
};
const G* findGlyph(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (const G& g : kFont)
        if (g.ch == c)
            return &g;
    return nullptr;
}
} // namespace

int textWidth(const std::string& s, int scale, int gap) {
    if (s.empty())
        return 0;
    return (int)s.size() * (kGlyphW + gap) * scale - gap * scale;
}

void drawText(Canvas& c, int x, int y, const std::string& s, int scale, Rgba col, int gap) {
    for (char ch : s) {
        if (const G* g = findGlyph(ch))
            for (int j = 0; j < kGlyphH; ++j)
                for (int i = 0; i < kGlyphW; ++i)
                    if (g->rows[j][i] == '#')
                        c.rect(x + i * scale, y + j * scale, scale, scale, col);
        x += (kGlyphW + gap) * scale;
    }
}

KeyStyle defaultStyle(Rgba background, uint8_t alpha) {
    KeyStyle s;
    s.bg = background;
    s.fill = Rgba{44, 46, 60, alpha};
    s.border = Rgba{214, 218, 232, alpha};
    s.shade = Rgba{22, 22, 32, alpha};
    s.text = Rgba{255, 255, 255, alpha};
    s.accent = Rgba{255, 176, 40, alpha};
    return s;
}

void drawArrow(Canvas& c, int x, int y, int w, int h, int dir, Rgba col) {
    int s = std::max(3, std::min(w, h) - 5) | 1; // odd base width
    int th = (s + 1) / 2;                        // triangle height
    int cx = x + w / 2, cy = y + h / 2;
    int left = x + (w - th) / 2, top = y + (h - th) / 2;
    for (int j = 0; j < th; ++j)
        for (int dx = -j; dx <= j; ++dx) {
            switch (dir) {
                case 0:
                    c.set(cx + dx, top + j, col);
                    break;
                case 1:
                    c.set(cx + dx, top + th - 1 - j, col);
                    break;
                case 2:
                    c.set(left + j, cy + dx, col);
                    break;
                default:
                    c.set(left + th - 1 - j, cy + dx, col);
                    break;
            }
        }
}

void drawMouse(Canvas& c, int x, int y, int w, int h, int button, int wheelDir, const KeyStyle& st) {
    // rounded body; the upper ~40% holds two buttons with the wheel in the gap, the lower part is the palm area
    int bw = std::min(w, std::max(10, h * 2 / 3)) | 0, bh = h;
    int bx = x + (w - bw) / 2;
    const int rad = bw >= 16 ? 2 : 1;
    auto inside = [&](int i, int j, int rw, int rh) {
        int cx = std::min(i, rw - 1 - i), cy = std::min(j, rh - 1 - j);
        if (cx < rad && cy < rad)
            return (cx + cy) >= rad - (rad > 1 ? 0 : 0) && !(rad == 1 && cx == 0 && cy == 0);
        return true;
    };
    for (int j = 0; j < bh; ++j)
        for (int i = 0; i < bw; ++i)
            if (inside(i, j, bw, bh))
                c.set(bx + i, y + j, st.border);
    for (int j = 1; j < bh - 1; ++j)
        for (int i = 1; i < bw - 1; ++i)
            if (inside(i, j, bw, bh))
                c.set(bx + i, y + j, st.fill);
    int btnH = std::max(4, bh * 2 / 5);
    int mid = bx + bw / 2;
    auto field = [&](int fx, int fw, bool on) {
        if (on)
            for (int j = 1; j < btnH; ++j)
                for (int i = 0; i < fw; ++i)
                    c.set(fx + i, y + j, st.accent);
    };
    field(bx + 1, mid - (bx + 1), button == 1);
    field(mid + 1, (bx + bw - 1) - (mid + 1), button == 2);
    for (int i = 1; i < bw - 1; ++i)
        c.set(bx + i, y + btnH, st.border); // under the buttons
    for (int j = 1; j < btnH; ++j)
        c.set(mid, y + j, st.border); // split
    // wheel (two pixels wide, in the split)
    int wh = std::max(3, btnH - 3);
    Rgba wcol = (button == 3 || wheelDir >= 0) ? st.accent : st.text;
    for (int j = 0; j < wh; ++j) {
        c.set(mid - 1, y + 2 + j, wcol);
        c.set(mid, y + 2 + j, wcol);
    }
    if (wheelDir == 0)
        c.set(mid - 1, y + 1, st.text), c.set(mid, y + 1, st.text);
    if (wheelDir == 1)
        c.set(mid - 1, y + 2 + wh, st.text), c.set(mid, y + 2 + wh, st.text);
    if (button == 4 || button == 5)
        for (int j = 0; j < 4; ++j) {
            c.set(bx - 1, y + btnH + 3 + j, st.accent);
            c.set(bx, y + btnH + 3 + j, st.accent);
        }
}

int capWidth(const Label& l, int minW) {
    int tw = 0;
    switch (l.kind) {
        case Label::Key:
        case Label::ArrowKey:
        case Label::Pad:
            tw = textWidth(l.cap, 1);
            break;
        case Label::Mouse:
            tw = 14;
            break;
        case Label::Wheel:
            tw = 14;
            break;
        default:
            tw = 5;
            break;
    }
    return std::max(minW, tw + 6);
}

namespace {
// Button colours of the controller badges (opaque alpha taken from the style).
Rgba padTone(const Label& l, uint8_t a) {
    static const Rgba xbox[] = {{0, 0, 0, 0}, {82, 176, 62, 0}, {214, 64, 56, 0}, {52, 118, 214, 0}, {236, 190, 36, 0}};
    static const Rgba ps5[] = {
        {0, 0, 0, 0}, {104, 156, 236, 0}, {236, 96, 92, 0}, {232, 124, 188, 0}, {88, 200, 140, 0}};
    Rgba t = (l.padPs5 ? ps5 : xbox)[std::max(0, std::min(4, l.padTone))];
    t.a = a;
    return t;
}

void padLine(Canvas& c, int x0, int y0, int x1, int y1, Rgba col) {
    const int n = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    for (int i = 0; i <= n; ++i) {
        const int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
        c.set(x, y, col);
        c.set(x + 1, y, col);
    }
}

// A round badge for a face button: a coloured disc with the letter (Xbox), a dark disc with the coloured symbol
// (PlayStation 5). Flat colours only (the target textures are palettised).
void drawPadBadge(Canvas& c, int x, int y, int w, int h, const Label& l, const KeyStyle& st) {
    const int d = std::min(w, h);
    const float cx = x + w * 0.5f - 0.5f, cy = y + h * 0.5f - 0.5f, r = d * 0.5f;
    const Rgba tone = padTone(l, st.text.a);
    const Rgba dark{26, 28, 36, st.text.a};
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const float dx = x + i - cx, dy = y + j - cy;
            if (dx * dx + dy * dy <= r * r)
                c.set(x + i, y + j, l.padPs5 ? dark : tone);
        }
    if (!l.padPs5) {
        const std::string s = l.cap.substr(0, 1);
        const int scale = d >= 16 ? 2 : 1;
        const int tw = textWidth(s, scale), th = kGlyphH * scale;
        const int tx = x + (w - tw) / 2, ty = y + (h - th) / 2;
        const Rgba ink = l.padTone == 4 ? Rgba{30, 24, 6, st.text.a} : st.text; // dark letter on the yellow Y
        drawText(c, tx, ty, s, scale, ink);
        if (scale == 2)
            drawText(c, tx + 1, ty, s, 2, ink);
        return;
    }
    const int mx = (int)std::lround(cx), my = (int)std::lround(cy);
    const int s = std::max(3, (int)(r * 0.5f));
    switch (l.padSymbol) {
        case 1: // cross
            padLine(c, mx - s, my - s, mx + s, my + s, tone);
            padLine(c, mx - s, my + s, mx + s, my - s, tone);
            break;
        case 2: // circle
            for (int j = -s - 1; j <= s + 1; ++j)
                for (int i = -s - 1; i <= s + 1; ++i) {
                    const float dist = std::sqrt((float)(i * i + j * j));
                    if (dist <= s + 0.9f && dist >= s - 1.1f)
                        c.set(mx + i, my + j, tone);
                }
            break;
        case 3: // square
            c.frame(mx - s, my - s, 2 * s + 1, 2 * s + 1, tone);
            c.frame(mx - s + 1, my - s + 1, 2 * s - 1, 2 * s - 1, tone);
            break;
        case 4: { // triangle
            const int top = my - s, bot = my + s - 1;
            padLine(c, mx, top, mx - s, bot, tone);
            padLine(c, mx, top, mx + s, bot, tone);
            padLine(c, mx - s, bot, mx + s, bot, tone);
            break;
        }
        default:
            break;
    }
}
} // namespace

void drawLabelCap(Canvas& c, int x, int y, int w, int h, const Label& l, const KeyStyle& st) {
    // transparent surround so the cell is fully defined
    c.rect(x, y, w, h, st.bg);
    if (l.kind == Label::Pad && l.padTone != 0) {
        drawPadBadge(c, x, y, w, h, l, st);
        return;
    }
    if (l.kind == Label::Mouse || l.kind == Label::Wheel) {
        int bh = h, bw = std::min(w, std::max(10, h * 2 / 3));
        drawMouse(c, x + (w - bw) / 2, y, bw, bh, l.kind == Label::Mouse ? l.mouseButton : 3,
                  l.kind == Label::Wheel ? l.arrow : -1, st);
        return;
    }
    // key cap: border, fill, darker bottom edge for a slight 3D look, rounded corners
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            bool corner = (i == 0 || i == w - 1) && (j == 0 || j == h - 1);
            if (corner)
                continue;
            bool edge = i == 0 || j == 0 || i == w - 1 || j == h - 1;
            Rgba col = edge ? st.border : (j >= h - 3 ? st.shade : st.fill);
            if (edge && j == h - 1)
                col = st.shade;
            c.set(x + i, y + j, col);
        }
    // keep the shade band only where there is room for text
    int innerW = w - 2, innerH = h - 4;
    if (l.kind == Label::None) {
        drawText(c, x + w / 2 - 2, y + h / 2 - 3, "?", 1, st.text);
        return;
    }
    if (l.kind == Label::ArrowKey) {
        drawArrow(c, x + 1, y + 1, w - 2, h - 3, l.arrow, st.text);
        return;
    }
    // text: scale 2 (emboldened) when it fits (single characters on tall caps), else scale 1; at most 3 characters
    std::string s = l.cap.substr(0, 3);
    int scale = 1;
    if (textWidth(s, 2) + 1 <= innerW - 2 && kGlyphH * 2 <= innerH)
        scale = 2;
    int tw = textWidth(s, scale) + (scale == 2 ? 1 : 0), th = kGlyphH * scale;
    int tx = x + (w - tw) / 2, ty = y + (h - 1 - th) / 2;
    drawText(c, tx, ty, s, scale, st.text);
    if (scale == 2)
        drawText(c, tx + 1, ty, s, 2, st.text);
}

} // namespace drakan_ui
