#include "gameui/Layout.h"

#include <algorithm>

namespace gameui {

// ---- DrawList ----

void DrawList::rect(const Rect& r, Color c, float rounding) {
    DrawCmd d;
    d.kind = DrawKind::Rect;
    d.rect = r;
    d.color = c;
    d.rounding = rounding;
    cmds_.push_back(std::move(d));
}

void DrawList::outline(const Rect& r, Color c, float thickness, float rounding) {
    DrawCmd d;
    d.kind = DrawKind::Outline;
    d.rect = r;
    d.color = c;
    d.thickness = thickness;
    d.rounding = rounding;
    cmds_.push_back(std::move(d));
}

void DrawList::gradient(const Rect& r, Color top, Color bottom) {
    DrawCmd d;
    d.kind = DrawKind::Gradient;
    d.rect = r;
    d.color = top;
    d.color2 = bottom;
    cmds_.push_back(std::move(d));
}

void DrawList::panel(const Rect& r, PanelStyle s, Color tint) {
    DrawCmd d;
    d.kind = DrawKind::Panel;
    d.rect = r;
    d.panel = s;
    d.color = tint;
    cmds_.push_back(std::move(d));
}

void DrawList::image(const Rect& r, ImageRef img, Color tint, Rect uv) {
    DrawCmd d;
    d.kind = DrawKind::Image;
    d.rect = r;
    d.image = img;
    d.color = tint;
    d.uv = uv;
    cmds_.push_back(std::move(d));
}

void DrawList::imageOr(const Rect& r, ImageRef img, ImageRef fallback, Color tint, Color fallbackTint) {
    DrawCmd d;
    d.kind = DrawKind::Image;
    d.rect = r;
    d.image = img;
    d.fallback = fallback;
    d.color = tint;
    d.color2 = fallbackTint;
    cmds_.push_back(std::move(d));
}

void DrawList::text(const Rect& r, std::string s, FontRole f, float size, Color c, TextAlign a, float tracking) {
    if (s.empty())
        return;
    DrawCmd d;
    d.kind = DrawKind::Text;
    d.rect = r;
    d.text = std::move(s);
    d.font = f;
    d.fontSize = size;
    d.color = c;
    d.align = a;
    d.tracking = tracking;
    cmds_.push_back(std::move(d));
}

void DrawList::line(Vec2 a, Vec2 b, Color c, float thickness) {
    DrawCmd d;
    d.kind = DrawKind::Line;
    d.rect = {a.x, a.y, b.x - a.x, b.y - a.y};
    d.color = c;
    d.thickness = thickness;
    cmds_.push_back(std::move(d));
}

void DrawList::pushClip(const Rect& r) {
    DrawCmd d;
    d.kind = DrawKind::ClipPush;
    d.rect = r;
    cmds_.push_back(std::move(d));
}

void DrawList::popClip() {
    DrawCmd d;
    d.kind = DrawKind::ClipPop;
    cmds_.push_back(std::move(d));
}

void DrawList::worldView(const Rect& r) {
    DrawCmd d;
    d.kind = DrawKind::WorldView;
    d.rect = r;
    cmds_.push_back(std::move(d));
}

// ---- layout frame ----

LayoutFrame computeFrame(const Viewport& v) {
    LayoutFrame f;
    f.view = v;
    const float w = std::max(v.w, 1.0f), h = std::max(v.h, 1.0f);
    f.scale = std::min(w / kDesignW, h / kDesignH);
    const float sw = kDesignW * f.scale, sh = kDesignH * f.scale;
    f.ox = v.x + (w - sw) * 0.5f;
    f.oy = v.y + (h - sh) * 0.5f;
    f.safe = {f.ox, f.oy, sw, sh};
    return f;
}

// ---- text ----

namespace {

size_t glyphCount(std::string_view s) {
    size_t n = 0;
    for (char c : s)
        if ((static_cast<unsigned char>(c) & 0xc0) != 0x80)
            ++n;
    return n;
}

float measureWith(const TextMeasure& m, std::string_view t, FontRole f, float px) {
    return m ? m(t, f, px) : estimateTextWidth(t, f, px);
}

} // namespace

float estimateTextWidth(std::string_view text, FontRole font, float sizePx) {
    const float em = font == FontRole::Title || font == FontRole::Heading ? 0.56f : 0.5f;
    return static_cast<float>(glyphCount(text)) * sizePx * em;
}

std::vector<std::string> wrapText(std::string_view text, float width, FontRole font, float sizePx,
                                  const TextMeasure& measure) {
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= text.size()) {
        size_t nl = text.find('\n', p);
        if (nl == std::string_view::npos)
            nl = text.size();
        std::string_view para = text.substr(p, nl - p);
        std::string cur;
        size_t q = 0;
        while (q < para.size()) {
            while (q < para.size() && para[q] == ' ')
                ++q;
            if (q >= para.size())
                break;
            size_t e = para.find(' ', q);
            if (e == std::string_view::npos)
                e = para.size();
            std::string_view word = para.substr(q, e - q);
            std::string trial = cur.empty() ? std::string(word) : cur + " " + std::string(word);
            if (!cur.empty() && measureWith(measure, trial, font, sizePx) > width) {
                lines.push_back(std::move(cur));
                cur = std::string(word);
            } else {
                cur = std::move(trial);
            }
            q = e;
        }
        if (!cur.empty() || para.empty())
            lines.push_back(std::move(cur));
        if (nl >= text.size())
            break;
        p = nl + 1;
    }
    while (!lines.empty() && lines.back().empty())
        lines.pop_back();
    if (lines.empty() && !text.empty())
        lines.emplace_back(text);
    return lines;
}

std::string ellipsize(std::string_view text, float width, FontRole font, float sizePx, const TextMeasure& measure) {
    if (measureWith(measure, text, font, sizePx) <= width)
        return std::string(text);
    std::string s(text);
    while (!s.empty()) {
        // drop one UTF-8 glyph
        size_t cut = s.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xc0) == 0x80)
            --cut;
        s.erase(cut);
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
        if (measureWith(measure, s + "...", font, sizePx) <= width)
            break;
    }
    return s + "...";
}

} // namespace gameui
