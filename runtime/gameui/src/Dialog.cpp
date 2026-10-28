#include "gameui/Dialog.h"

#include <algorithm>
#include <set>

namespace gameui {
namespace {
bool span(uint32_t p, uint32_t n) {
    return p >= 0x100000u && p <= 0x2000000u - n;
}
} // namespace

DialogContent readDialog(const IGuestMemory& mem, uint32_t screen) {
    DialogContent out;
    if (!span(screen, 0x9c))
        return out;
    const uint32_t id = mem.read32(screen + 0x18);
    if (id != 1 && id != 2 && id != 3 && id != 0x1b)
        return out;
    std::set<uint32_t> visited;
    uint32_t w = mem.read32(screen + 0x7c);
    for (; w && visited.size() < 128; w = mem.read32(w + 4)) {
        if (!span(w, 0x60) || !visited.insert(w).second)
            return {};
        if (!mem.read32(w + 0x3c))
            continue;
        const unsigned type = mem.read16(w + 0xc);
        if (type == 10)
            continue; // native background / frame
        if (type != 1 && type != 3)
            return {}; // unsupported controls keep their native presentation
        const uint32_t str = mem.read32(w + 0x5c);
        if (!str)
            continue; // native message boxes retain unused, empty label widgets
        if (!span(str, 4096))
            return {};
        const std::string text = mem.readString(str, 4096);
        if (text.empty())
            continue;
        if (type == 1) {
            if (!out.text.empty())
                out.text += ' '; // native labels are pre-wrapped lines; reflow them with the modern font
            out.text += text;
        } else {
            const auto coord = [&](uint32_t offset) { return float(static_cast<int16_t>(mem.read16(w + offset))); };
            const Rect r{coord(0x1c), coord(0x1e), coord(0x20), coord(0x22)};
            if (r.empty() || out.buttons.size() >= 4)
                return {};
            out.buttons.push_back({text, r, w, mem.read32(screen + 0x84) == w, mem.read32(w + 0x40) != 0});
        }
    }
    out.valid = !w && !out.text.empty() && !out.buttons.empty();
    return out;
}

DrawList drawDialog(const DialogContent& content, const Rect& panel, const Viewport& view, const TextMeasure& measure) {
    DrawList d;
    if (!content.valid || panel.empty())
        return d;
    const Theme th;
    const float k = std::max(0.1f, std::min(view.h / 1080.0f, view.w / 1280.0f));
    d.rect({view.x, view.y, view.w, view.h}, withAlpha(th.backdropEdge, 90));
    d.panel(panel, PanelStyle::Main, withAlpha(th.panelRaised, 255));
    std::string title = content.buttons.size() == 1 ? "Information" : "Confirm";
    std::string body = content.text;
    const size_t colon = body.find(':');
    if (colon != std::string::npos && colon > 0 && colon < 32 && body.find('\n') > colon) {
        title = body.substr(0, colon);
        body.erase(0, colon + 1);
        const size_t first = body.find_first_not_of(" \t");
        body.erase(0, first == std::string::npos ? body.size() : first);
    }
    const float pad = 32 * k;
    const Rect inner = panel.inset(pad);
    d.text({inner.x, inner.y, inner.w, 42 * k}, title, FontRole::Heading, th.sizeHeading * k, th.heading);
    const float ruleY = inner.y + 54 * k;
    d.line({inner.x, ruleY}, {inner.right(), ruleY}, th.brassDim, k);
    float buttonsTop = panel.bottom();
    for (const auto& b : content.buttons)
        buttonsTop = std::min(buttonsTop, b.rect.y);
    const float bodyY = ruleY + 16 * k;
    const float bodyH = std::max(1.0f, buttonsTop - bodyY - 16 * k);
    float size = th.sizeBody * k;
    std::vector<std::string> lines;
    for (;;) {
        lines = wrapText(body, inner.w, FontRole::Body, size, measure);
        if (lines.size() * size * 1.3f <= bodyH || size <= 14 * k)
            break;
        size -= k;
    }
    d.pushClip({inner.x, bodyY, inner.w, bodyH});
    float y = bodyY;
    for (const auto& line : lines) {
        d.text({inner.x, y, inner.w, size * 1.3f}, line, FontRole::Body, size, th.text);
        y += size * 1.3f;
    }
    d.popClip();
    for (const auto& b : content.buttons) {
        d.panel(b.rect, PanelStyle::Card, b.focused && b.enabled ? th.panelRaised : th.panel);
        if (b.focused && b.enabled) {
            d.rect({b.rect.x, b.rect.y, 3 * k, b.rect.h}, th.select);
            d.outline(b.rect, th.select, 1.5f * k);
        }
        const Rect label = b.rect.inset(4 * k);
        float fontSize = std::min(th.sizeHeading * k, b.rect.h * 0.7f);
        const float width = measure ? measure(b.text, FontRole::Heading, fontSize)
                                    : estimateTextWidth(b.text, FontRole::Heading, fontSize);
        if (width > label.w)
            fontSize *= std::max(1.0f, label.w) / width;
        d.text(label, b.text, FontRole::Heading, fontSize,
               !b.enabled ? th.textDisabled : (b.focused ? th.heading : th.text), TextAlign::Center);
    }
    return d;
}
} // namespace gameui
