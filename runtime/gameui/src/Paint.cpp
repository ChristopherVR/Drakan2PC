#include "Paint.h"

#include <algorithm>

namespace gameui::detail {

float themeSize(const Theme& th, FontRole f) {
    switch (f) {
        case FontRole::Title:
            return th.sizeTitle;
        case FontRole::Heading:
            return th.sizeHeading;
        case FontRole::Numeric:
            return th.sizeNumeric;
        case FontRole::Small:
            return th.sizeSmall;
        case FontRole::Body:
            break;
    }
    return th.sizeBody;
}

LayoutFrame anchoredFrame(const Viewport& vp, float minDesignW, AnchorX ax, bool bottomAnchored) {
    LayoutFrame f;
    f.view = vp;
    const float w = std::max(vp.w, 1.0f), h = std::max(vp.h, 1.0f);
    f.scale = std::min(h / kDesignH, w / std::max(minDesignW, 1.0f));
    const float sw = kDesignW * f.scale, sh = kDesignH * f.scale;
    switch (ax) {
        case AnchorX::Left:
            f.ox = vp.x;
            break;
        case AnchorX::Centre:
            f.ox = vp.x + (w - sw) * 0.5f;
            break;
        case AnchorX::Right:
            f.ox = vp.x + w - sw;
            break;
    }
    f.oy = bottomAnchored ? vp.y + h - sh : vp.y + (h - sh) * 0.5f;
    f.safe = {f.ox, f.oy, sw, sh};
    return f;
}

float Paint::measure(std::string_view t, FontRole f, float sizePx) const {
    return cfg_.measure ? cfg_.measure(t, f, sizePx) : estimateTextWidth(t, f, sizePx);
}

float Paint::textWidth(std::string_view t, FontRole f, float size) const {
    return measure(t, f, fontPx(f, size)) / std::max(lf_.scale, 1e-6f);
}

std::vector<std::string> Paint::wrap(std::string_view t, float designW, FontRole f, float size) const {
    return wrapText(t, lf_.px(designW), f, fontPx(f, size), cfg_.measure);
}

void Paint::text(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c, TextAlign a,
                 float size) const {
    if (s.empty())
        return;
    const Rect r = lf_.map(design);
    const float px = fontPx(f, size);
    d.text(r, ellipsize(s, r.w, f, px, cfg_.measure), f, px, c, a);
}

void Paint::strike(DrawList& d, const Rect& design, const std::string& s, FontRole f, Color c, TextAlign a,
                   float size) const {
    if (s.empty())
        return;
    const Rect r = lf_.map(design);
    const float px = fontPx(f, size);
    const float w = std::min(r.w, measure(ellipsize(s, r.w, f, px, cfg_.measure), f, px));
    float x = r.x;
    if (a == TextAlign::Center)
        x = r.cx() - w * 0.5f;
    else if (a == TextAlign::Right)
        x = r.right() - w;
    const float y = r.cy() + px * 0.04f; // the x-height middle sits a little below the box centre
    d.line({x - px * 0.1f, y}, {x + w + px * 0.1f, y}, c, std::max(1.0f, px * 0.075f));
}

void drawFocus(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& r) {
    const float g = lf.px(4);
    d.rect({r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}, withAlpha(th.emberGlow, 40), lf.px(8));
    d.panel(r, PanelStyle::Focus, th.ember);
    d.outline(r, withAlpha(th.select, 225), lf.px(1.5f), lf.px(3));
}

void drawCellSelect(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& r) {
    const float g = lf.px(4);
    d.rect({r.x - g, r.y - g, r.w + 2 * g, r.h + 2 * g}, th.emberGlow, lf.px(6));
    d.panel(r, PanelStyle::Focus, th.select);
    d.outline(r, th.select, lf.px(1.75f), lf.px(2));
}

void drawLattice(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& r, float gap) {
    const Rect o{r.x - gap, r.y - gap, r.w + 2 * gap, r.h + 2 * gap};
    const float t = lf.px(1.5f);
    d.rect({o.x - 2 * t, o.y - 2 * t, o.w + 4 * t, o.h + 4 * t}, rgba(0, 0, 0, 150));
    d.rect(o, rgba(48, 29, 9));
    d.outline(o.inset(t), withAlpha(th.gridLine, 220), lf.px(1));
    d.outline(o, th.gilt, t);
}

void drawOrnateRule(DrawList& d, const Theme& th, const LayoutFrame& lf, float x0, float x1, float y, bool withCrest) {
    d.line({x0, y + lf.px(1)}, {x1, y + lf.px(1)}, rgba(0, 0, 0, 170), lf.px(3));
    d.line({x0, y}, {x1, y}, withAlpha(th.brassDim, 230), lf.px(1.5f));
    const float cx = 0.5f * (x0 + x1);
    if (withCrest) {
        // A gilt hairline brightens towards the crest.
        // A gilt hairline brightening towards the crest, in steps.
        const float half = std::min((x1 - x0) * 0.5f, lf.px(480));
        for (int i = 1; i <= 4; ++i) {
            const float k = half * static_cast<float>(5 - i) / 4.0f;
            d.line({cx - k, y}, {cx + k, y}, withAlpha(th.gilt, static_cast<uint8_t>(36 * i)), lf.px(1));
        }
        const float h = 30;
        d.image({cx - lf.px(h * 1.6f), y - lf.px(h * 0.5f), lf.px(h * 3.2f), lf.px(h)}, {ImageKind::SkinCrest, 0});
    }
}

void drawLabel(DrawList& d, const Theme& th, const LayoutFrame& lf, const Rect& design, std::string_view s, Color c,
               TextAlign a, float size) {
    (void)th;
    if (s.empty())
        return;
    std::string up(s);
    for (char& ch : up)
        if (ch >= 'a' && ch <= 'z')
            ch = static_cast<char>(ch - 'a' + 'A');
    const float px = lf.px(size > 0 ? size : 16);
    d.text(lf.map(design), std::move(up), FontRole::Small, px, c, a, px * 0.14f);
}

void Paint::label(DrawList& d, const Rect& design, std::string_view s, Color c, TextAlign a, float size) const {
    drawLabel(d, cfg_.theme, lf_, design, s, c, a, size);
}

void Paint::focus(DrawList& d, const Rect& r) const {
    drawFocus(d, cfg_.theme, lf_, r);
}

void Paint::selectCell(DrawList& d, const Rect& r) const {
    drawCellSelect(d, cfg_.theme, lf_, r);
}

void Paint::lattice(DrawList& d, const Rect& r, float gap) const {
    drawLattice(d, cfg_.theme, lf_, r, gap);
}

void Paint::crest(DrawList& d, float cx, float cy, float h) const {
    d.image({cx - lf_.px(h * 1.6f), cy - lf_.px(h * 0.5f), lf_.px(h * 3.2f), lf_.px(h)}, {ImageKind::SkinCrest, 0});
}

void Paint::ornateRule(DrawList& d, float x0, float x1, float y, bool withCrest) const {
    drawOrnateRule(d, cfg_.theme, lf_, x0, x1, y, withCrest);
}

void Paint::bar(DrawList& d, const Rect& design, int32_t cur, int32_t max, Color fill, const std::string& label,
                float labelSize) const {
    const Theme& t = cfg_.theme;
    const Rect b = lf_.map(design);
    d.panel(b, PanelStyle::Inset, t.inset);
    if (max > 0) {
        const float f = std::clamp(static_cast<float>(cur) / static_cast<float>(max), 0.0f, 1.0f);
        const Rect in = b.inset(lf_.px(3));
        d.gradient({in.x, in.y, in.w * f, in.h}, fill, withAlpha(fill, 170));
    }
    if (!label.empty()) {
        const Rect tr{design.x + 12, design.y, design.w - 24, design.h};
        text(d, tr, label, FontRole::Small, t.text, TextAlign::Left, labelSize);
        text(d, tr, max > 0 ? fmtPair(cur, max) : fmtInt(cur), FontRole::Small, t.text, TextAlign::Right, labelSize);
    }
}

void Paint::rule(DrawList& d, float x, float y, float w) const {
    // A recessed bronze line: dark below, gilt hairline on top.
    const Rect r = lf_.map({x, y, w, 0});
    d.line({r.x, r.y + lf_.px(1)}, {r.right(), r.y + lf_.px(1)}, rgba(0, 0, 0, 140), lf_.px(1.5f));
    d.line({r.x, r.y}, {r.right(), r.y}, withAlpha(cfg_.theme.brassDim, 220), lf_.px(1.25f));
}

void Paint::button(DrawList& d, const Rect& design, const std::string& label, bool focused, bool hovered, bool enabled,
                   TextAlign align) const {
    const Theme& th = cfg_.theme;
    const Rect rp = map(design);
    d.panel(rp, PanelStyle::Card, !enabled ? withAlpha(th.panel, 120) : (hovered ? th.panelRaised : th.panel));
    if (focused)
        focus(d, rp);
    const Rect inner{design.x + 32, design.y, design.w - 64, design.h};
    text(d, inner, label, FontRole::Heading, !enabled ? th.textDisabled : (focused ? th.heading : th.text), align);
    if (!enabled && align == TextAlign::Left)
        text(d, inner, "Unavailable", FontRole::Small, th.textDisabled, TextAlign::Right);
}

void Paint::confirmCard(DrawList& d, const Rect& c, const std::string& title, const std::string& body) const {
    const Theme& th = cfg_.theme;
    d.rect({lf_.view.x, lf_.view.y, lf_.view.w, lf_.view.h}, withAlpha(th.backdropEdge, 120));
    d.panel(map(c), PanelStyle::Main, th.panelRaised);
    constexpr float pad = 44;
    text(d, {c.x + pad, c.y + 28, c.w - 2 * pad, 52}, title, FontRole::Title, th.heading);
    rule(d, c.x + pad, c.y + 92, c.w - 2 * pad);
    const std::vector<std::string> lines = wrap(body, c.w - 2 * pad, FontRole::Body);
    float y = c.y + 104;
    for (size_t i = 0; i < lines.size() && i < 2; ++i, y += 30)
        text(d, {c.x + pad, y, c.w - 2 * pad, 30}, lines[i], FontRole::Body, th.textMuted);
}

std::vector<Rect> Paint::layoutHints(const std::vector<Hint>& hints, float anchorX, bool centred, float y, float h,
                                     float glyph, float gap, FontRole f, float size) const {
    std::vector<float> widths;
    float total = 0;
    for (const Hint& hi : hints) {
        widths.push_back(glyph + glyph * 0.25f + textWidth(hi.label, f, size));
        total += widths.back();
    }
    if (!hints.empty())
        total += gap * static_cast<float>(hints.size() - 1);
    float x = centred ? anchorX - total * 0.5f : anchorX - total;
    std::vector<Rect> out;
    for (float w : widths) {
        out.push_back({x, y, w, h});
        x += w + gap;
    }
    return out;
}

void Paint::drawHint(DrawList& d, const Hint& h, const Rect& r, bool hovered, float glyph, FontRole f,
                     float size) const {
    const Theme& t = cfg_.theme;
    const float g = lf_.px(glyph);
    const Color c = !h.enabled ? t.textDisabled : (hovered ? t.heading : t.text);
    d.image({r.x, r.cy() - g * 0.5f, g, g}, {ImageKind::Prompt, uint32_t(h.prompt)},
            h.enabled ? 0xffffffffu : withAlpha(0xffffffffu, 90));
    const float lx = g * 1.25f;
    d.text({r.x + lx, r.y, r.w - lx, r.h}, h.label, f, fontPx(f, size), c, TextAlign::Left);
}

const char* quantityLabelFor(QuantityKind k) {
    switch (k) {
        case QuantityKind::Durability:
            return "Durability";
        case QuantityKind::Ammo:
            return "Arrows";
        case QuantityKind::Charges:
            return "Charges";
        case QuantityKind::None:
            break;
    }
    return "";
}

std::string fmtInt(int32_t v) {
    return std::to_string(v);
}

std::string fmtPair(int32_t a, int32_t b) {
    return std::to_string(a) + " / " + std::to_string(b);
}

} // namespace gameui::detail
