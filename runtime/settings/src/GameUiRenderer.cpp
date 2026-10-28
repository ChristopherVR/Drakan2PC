#include "settings/GameUiRenderer.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

#include "gameui/Model.h"
#include "gameui/Screen.h"

namespace dsettings {

namespace {

using gameui::Color;
using gameui::DrawCmd;
using gameui::DrawKind;
using gameui::ImageKind;
using gameui::PanelStyle;
using gameui::Rect;

ImU32 col(Color c, float fade = 1.0f) {
    const float a = static_cast<float>(c & 0xffu) * std::clamp(fade, 0.0f, 1.0f);
    return IM_COL32((c >> 24) & 0xffu, (c >> 16) & 0xffu, (c >> 8) & 0xffu, static_cast<int>(a + 0.5f));
}
ImU32 rgbaA(int r, int g, int b, float a) {
    return IM_COL32(r, g, b, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f));
}
ImVec2 v(float x, float y) {
    return ImVec2(x, y);
}
float snap(float x) {
    return std::floor(x + 0.5f);
}

// A quad with a colour per corner (top-left, top-right, bottom-right, bottom-left) and UVs; the draw list's current
// texture applies (the font atlas' white pixel for untextured quads).
void quad(ImDrawList* dl, ImVec2 a, ImVec2 b, ImVec2 uva, ImVec2 uvb, ImU32 c00, ImU32 c10, ImU32 c11, ImU32 c01) {
    dl->PrimReserve(6, 4);
    const ImDrawIdx i = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimWriteIdx(i);
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 1));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
    dl->PrimWriteIdx(i);
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 2));
    dl->PrimWriteIdx(static_cast<ImDrawIdx>(i + 3));
    dl->PrimWriteVtx(a, uva, c00);
    dl->PrimWriteVtx(v(b.x, a.y), v(uvb.x, uva.y), c10);
    dl->PrimWriteVtx(b, uvb, c11);
    dl->PrimWriteVtx(v(a.x, b.y), v(uva.x, uvb.y), c01);
}

// A gilded relief edge (procedural, used when the game's frame art is missing): dark recess, bronze body, a lit gilt
// line on the top / left, and small diamond studs at the corners.
void gildedEdge(ImDrawList* dl, const Rect& r, float px, float fade) {
    const float x0 = snap(r.x), y0 = snap(r.y), x1 = snap(r.right()), y1 = snap(r.bottom());
    dl->AddRect(v(x0 - px, y0 - px), v(x1 + px, y1 + px), rgbaA(0, 0, 0, 0.55f * fade), 0, 0, 2 * px);
    dl->AddRect(v(x0, y0), v(x1, y1), rgbaA(148, 99, 38, 0.95f * fade), 0, 0, 2.5f * px);
    dl->AddLine(v(x0 + px, y0 + px), v(x1 - px, y0 + px), rgbaA(240, 200, 118, 0.85f * fade), px);
    dl->AddLine(v(x0 + px, y0 + px), v(x0 + px, y1 - px), rgbaA(240, 200, 118, 0.6f * fade), px);
    dl->AddRect(v(x0 + 3 * px, y0 + 3 * px), v(x1 - 3 * px, y1 - 3 * px), rgbaA(0, 0, 0, 0.45f * fade), 0, 0, px);
    const float d = 5 * px;
    const ImVec2 cs[4] = {v(x0, y0), v(x1, y0), v(x1, y1), v(x0, y1)};
    for (const ImVec2& c : cs) {
        dl->AddQuadFilled(v(c.x, c.y - d), v(c.x + d, c.y), v(c.x, c.y + d), v(c.x - d, c.y),
                          rgbaA(226, 182, 92, fade));
        dl->AddQuad(v(c.x, c.y - d), v(c.x + d, c.y), v(c.x, c.y + d), v(c.x - d, c.y), rgbaA(60, 36, 10, fade), px);
    }
}

// A tapered crescent: the band between a circle of radius `ro` around c and a radius shrinking towards the horns,
// spanning `span` radians around `dir` (0 = pointing right, pi = left).
void crescent(ImDrawList* dl, ImVec2 c, float ro, float thick, float dir, float span, ImU32 col) {
    constexpr int kSteps = 18;
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve(kSteps * 6, (kSteps + 1) * 2);
    const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    for (int i = 0; i <= kSteps; ++i) {
        const float t = static_cast<float>(i) / kSteps;
        const float a = dir - span * 0.5f + span * t;
        const float w = thick * std::sin(t * 3.14159265f);
        const float ca = std::cos(a), sa = std::sin(a);
        dl->PrimWriteVtx(v(c.x + ca * ro, c.y + sa * ro), white, col);
        dl->PrimWriteVtx(v(c.x + ca * (ro - w), c.y + sa * (ro - w)), white, col);
    }
    for (int i = 0; i < kSteps; ++i) {
        const ImDrawIdx o = static_cast<ImDrawIdx>(base + i * 2);
        dl->PrimWriteIdx(o);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(o + 1));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(o + 3));
        dl->PrimWriteIdx(o);
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(o + 3));
        dl->PrimWriteIdx(static_cast<ImDrawIdx>(o + 2));
    }
}

struct Feather {
    // 1-D weight: 1 outside [a, b], ramping to 0 over `f` inside it.
    float a = 0, b = 0, f = 1;
    bool on = false;
    float operator()(float x) const {
        if (!on || x <= a || x >= b)
            return 1.0f;
        const float d = std::min(x - a, b - x);
        return std::clamp(1.0f - d / f, 0.0f, 1.0f);
    }
};

} // namespace

ImFont* GameUiRenderer::font(gameui::FontRole role) const {
    int i = 0;
    switch (role) {
        case gameui::FontRole::Body:
            i = 0;
            break;
        case gameui::FontRole::Heading:
            i = 1;
            break;
        case gameui::FontRole::Title:
            i = 2;
            break;
        case gameui::FontRole::Numeric:
            i = 3;
            break;
        case gameui::FontRole::Small:
            i = 4;
            break;
    }
    if (fonts_.screen[i])
        return fonts_.screen[i];
    if ((role == gameui::FontRole::Heading || role == gameui::FontRole::Title) && fonts_.heading)
        return fonts_.heading;
    return fonts_.body ? fonts_.body : ImGui::GetFont();
}

gameui::TextMeasure GameUiRenderer::measure() const {
    return [this](std::string_view t, gameui::FontRole role, float px) {
        ImFont* f = font(role);
        if (!f || t.empty())
            return 0.0f;
        return f->CalcTextSizeA(px, FLT_MAX, 0.0f, t.data(), t.data() + t.size()).x;
    };
}

void GameUiRenderer::draw(ImDrawList* dl, const gameui::DrawList& list, const GameUiCanvas& canvas,
                          const IconSource& icons, double time, float fade) const {
    for (const DrawCmd& c : list.commands()) {
        const ImVec2 a(c.rect.x, c.rect.y), b(c.rect.right(), c.rect.bottom());
        switch (c.kind) {
            case DrawKind::Rect:
                dl->AddRectFilled(a, b, col(c.color, fade), c.rounding);
                break;
            case DrawKind::Outline:
                dl->AddRect(a, b, col(c.color, fade), c.rounding, 0, c.thickness);
                break;
            case DrawKind::Gradient:
                dl->AddRectFilledMultiColor(a, b, col(c.color, fade), col(c.color, fade), col(c.color2, fade),
                                            col(c.color2, fade));
                break;
            case DrawKind::Panel:
                if (c.panel == PanelStyle::Backdrop)
                    backdrop(dl, c, list, canvas, fade);
                else
                    panel(dl, c, time, fade);
                break;
            case DrawKind::Image:
                if (!image(dl, c, canvas, icons, fade) && c.fallback.kind != ImageKind::None) {
                    DrawCmd f = c;
                    f.image = c.fallback;
                    f.fallback = {};
                    f.color = c.color2;
                    image(dl, f, canvas, icons, fade);
                }
                break;
            case DrawKind::Text:
                text(dl, c, fade);
                break;
            case DrawKind::Line:
                dl->AddLine(a, b, col(c.color, fade), c.thickness);
                break;
            case DrawKind::ClipPush:
                dl->PushClipRect(a, b, true);
                break;
            case DrawKind::ClipPop:
                dl->PopClipRect();
                break;
            case DrawKind::WorldView:
                break;
        }
    }
}

// The world behind the screen: the frame copy blurred (a 17-tap disc, summed additively over black), then dimmed and
// vignetted. Inside the WorldView holes (Rynn) the world stays sharp and lighter, with feathered edges.
void GameUiRenderer::backdrop(ImDrawList* dl, const DrawCmd& c, const gameui::DrawList& list,
                              const GameUiCanvas& canvas, float fade) const {
    const Rect r = c.rect;
    Feather fx, fy;
    for (const DrawCmd& h : list.commands())
        if (h.kind == DrawKind::WorldView && !h.rect.empty()) {
            const float f = std::max(8.0f, std::min(h.rect.w, h.rect.h) * 0.3f);
            fx = {h.rect.x, h.rect.right(), f, true};
            fy = {h.rect.y, h.rect.bottom(), f, true};
            break;
        }
    // Breakpoints of the weight mesh.
    float xs[6] = {r.x, fx.a, fx.a + fx.f, fx.b - fx.f, fx.b, r.right()};
    float ys[6] = {r.y, fy.a, fy.a + fy.f, fy.b - fy.f, fy.b, r.bottom()};
    int nx = 6, ny = 6;
    if (!fx.on) {
        xs[1] = r.right();
        ys[1] = r.bottom();
        nx = ny = 2;
    }
    const auto weight = [&](float x, float y) {
        if (!fx.on)
            return 1.0f;
        return std::max(fx(x), fy(y));
    };
    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    const auto mesh = [&](auto&& colourAt, bool textured, ImVec2 uvOffset) {
        for (int j = 0; j + 1 < ny; ++j)
            for (int i = 0; i + 1 < nx; ++i) {
                const float x0 = xs[i], x1 = xs[i + 1], y0 = ys[j], y1 = ys[j + 1];
                if (!(x1 > x0 && y1 > y0))
                    continue;
                const ImVec2 uva =
                    textured ? v((x0 + uvOffset.x) / canvas.frameW, (y0 + uvOffset.y) / canvas.frameH) : white;
                const ImVec2 uvb =
                    textured ? v((x1 + uvOffset.x) / canvas.frameW, (y1 + uvOffset.y) / canvas.frameH) : white;
                quad(dl, v(x0, y0), v(x1, y1), uva, uvb, colourAt(weight(x0, y0)), colourAt(weight(x1, y0)),
                     colourAt(weight(x1, y1)), colourAt(weight(x0, y1)));
            }
    };
    if (canvas.frame && canvas.additive && canvas.frameW > 0 && canvas.frameH > 0) {
        // sharp * (1 - w) ...
        mesh([&](float w) { return rgbaA(0, 0, 0, w * fade); }, false, v(0, 0));
        // ... + blur * w
        dl->AddCallback(canvas.additive, nullptr);
        dl->PushTextureID(canvas.frame);
        const float rad = std::max(2.0f, canvas.frameH * 0.0045f);
        constexpr int kTaps = 17;
        const float k = 255.0f / kTaps;
        for (int t = 0; t < kTaps; ++t) {
            ImVec2 o(0, 0);
            if (t > 0) {
                const int ring = t <= 8 ? 1 : 2;
                const float ang = static_cast<float>(t % 8) * 0.785398f + (ring == 2 ? 0.392699f : 0.0f);
                o = v(std::cos(ang) * rad * static_cast<float>(ring), std::sin(ang) * rad * static_cast<float>(ring));
            }
            mesh(
                [&](float w) {
                    const int g = static_cast<int>(k * w * fade + 0.5f);
                    return IM_COL32(g, g, g, 255);
                },
                true, o);
        }
        dl->PopTextureID();
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }
    // Dim: full outside, light over Rynn.
    const float full = static_cast<float>(c.color & 0xffu) / 255.0f;
    const float hole = full * 0.22f;
    const int cr = static_cast<int>((c.color >> 24) & 0xffu), cg = static_cast<int>((c.color >> 16) & 0xffu),
              cb = static_cast<int>((c.color >> 8) & 0xffu);
    mesh([&](float w) { return rgbaA(cr, cg, cb, (hole + (full - hole) * w) * fade); }, false, v(0, 0));
    // Vignette.
    const float vw = r.w * 0.18f, vh = r.h * 0.22f;
    const ImU32 edge = rgbaA(0, 0, 0, 0.55f * fade), clear = rgbaA(0, 0, 0, 0);
    dl->AddRectFilledMultiColor(v(r.x, r.y), v(r.x + vw, r.bottom()), edge, clear, clear, edge);
    dl->AddRectFilledMultiColor(v(r.right() - vw, r.y), v(r.right(), r.bottom()), clear, edge, edge, clear);
    dl->AddRectFilledMultiColor(v(r.x, r.y), v(r.right(), r.y + vh), edge, edge, clear, clear);
    dl->AddRectFilledMultiColor(v(r.x, r.bottom() - vh), v(r.right(), r.bottom()), clear, clear, edge, edge);
}

void GameUiRenderer::panel(ImDrawList* dl, const DrawCmd& c, double time, float fade) const {
    const Rect r = c.rect;
    const ImVec2 a(snap(r.x), snap(r.y)), b(snap(r.right()), snap(r.bottom()));
    const float px = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f); // one design pixel
    const bool skin = skin_ && skin_->ready();
    switch (c.panel) {
        case PanelStyle::Main: {
            // Dark warm stone with recessed edges in the game's own brass frame (INTERFCE.TXD 118, the message box
            // frame with its corner studs), or a procedural gilded edge without the art.
            const Color tint = c.color ? c.color : gameui::rgba(21, 14, 10, 232);
            const Color lower = gameui::withAlpha(tint, uint8_t(std::min(255, int(tint & 0xff) + 18)));
            dl->AddRectFilledMultiColor(a, b, col(tint, fade), col(tint, fade), col(lower, fade), col(lower, fade));
            if (skin)
                skin_->stone(dl, a, b, 256.0f * px, rgbaA(255, 235, 210, 0.06f * fade));
            const float e = std::min(std::min(b.x - a.x, b.y - a.y) * 0.18f, 26 * px);
            const ImU32 sh = rgbaA(0, 0, 0, 0.42f * fade), clr = rgbaA(0, 0, 0, 0);
            dl->AddRectFilledMultiColor(a, v(b.x, a.y + e), sh, sh, clr, clr);
            dl->AddRectFilledMultiColor(a, v(a.x + e, b.y), sh, clr, clr, sh);
            dl->AddRectFilledMultiColor(v(b.x - e, a.y), b, clr, sh, sh, clr);
            if (skin) {
                const float k = 9 * px; // the frame's bar sits about 9 px inside its outer edge at this corner size
                skin_->frame(dl, v(a.x - k, a.y - k), v(b.x + k, b.y + k), 24 * px, rgbaA(220, 196, 160, 0.9f * fade));
            } else {
                gildedEdge(dl, r, px, fade);
            }
            break;
        }
        case PanelStyle::Inset: {
            // Recessed slot: dark red-brown (Order of the Flame's cells), faintly mottled, shadowed at the top.
            const Color tint = c.color ? c.color : gameui::rgba(31, 11, 5, 240);
            dl->AddRectFilled(a, b, col(tint, fade));
            if (skin)
                skin_->stone(dl, a, b, 192.0f * px, rgbaA(255, 220, 190, 0.035f * fade));
            const float sh = std::min(r.h * 0.25f, 8 * px);
            dl->AddRectFilledMultiColor(a, v(b.x, a.y + sh), rgbaA(0, 0, 0, 0.5f * fade), rgbaA(0, 0, 0, 0.5f * fade),
                                        rgbaA(0, 0, 0, 0), rgbaA(0, 0, 0, 0));
            dl->AddRect(a, b, rgbaA(0, 0, 0, 0.55f * fade), 0, 0, px);
            dl->AddLine(v(a.x + px, b.y - px), v(b.x - px, b.y - px), rgbaA(156, 103, 21, 0.28f * fade), px);
            break;
        }
        case PanelStyle::Card: {
            const Color tint = c.color ? c.color : gameui::rgba(38, 25, 17, 238);
            dl->AddRectFilled(a, b, col(tint, fade), 2 * px);
            dl->AddLine(v(a.x + 2 * px, a.y + px), v(b.x - 2 * px, a.y + px), rgbaA(255, 220, 160, 0.09f * fade), px);
            dl->AddRect(a, b, rgbaA(118, 78, 30, 0.75f * fade * static_cast<float>(c.color & 0xffu) / 255.0f), 2 * px,
                        0, px);
            break;
        }
        case PanelStyle::Parchment: {
            // The game's cream parchment (INTERFCE.TXD 86) over the tint, edges burnt, a gilded edge.
            const Color tint = c.color ? c.color : gameui::rgba(217, 201, 163, 245);
            dl->AddRectFilled(a, b, col(tint, fade));
            if (skin)
                skin_->parchment(dl, a, b, 192.0f * px, rgbaA(255, 246, 225, 0.42f * fade));
            const float e = std::min(r.w, r.h) * 0.08f;
            const ImU32 edge = rgbaA(96, 58, 24, 0.42f * fade), clear = rgbaA(96, 58, 24, 0);
            dl->AddRectFilledMultiColor(a, v(a.x + e, b.y), edge, clear, clear, edge);
            dl->AddRectFilledMultiColor(v(b.x - e, a.y), b, clear, edge, edge, clear);
            dl->AddRectFilledMultiColor(a, v(b.x, a.y + e), edge, edge, clear, clear);
            dl->AddRectFilledMultiColor(v(a.x, b.y - e), b, clear, clear, edge, edge);
            gildedEdge(dl, r, px, fade);
            break;
        }
        case PanelStyle::Focus: {
            // A red-brown wash rising from the bottom edge. Rows and cards (large) also carry the game's flame low
            // along it, as the native menus highlight their entries; grid cells keep only the wash (their red trace
            // is drawn by the screen).
            const float h = b.y - a.y;
            const bool large = h > 130 * px || (b.x - a.x) > 1.6f * h;
            const float wash = large ? 0.24f : 0.32f;
            dl->AddRectFilledMultiColor(a, b, rgbaA(120, 38, 14, 0.0f), rgbaA(120, 38, 14, 0.0f),
                                        rgbaA(120, 38, 14, wash * fade), rgbaA(120, 38, 14, wash * fade));
            if (large && skin) {
                const float fireTop = b.y - std::max(16 * px, h * 0.34f);
                dl->PushClipRect(v(a.x, fireTop), b, true);
                skin_->fire(dl, v(a.x, fireTop), b, 0.16f * fade, time);
                dl->PopClipRect();
            }
            break;
        }
        case PanelStyle::Header:
        case PanelStyle::Backdrop:
            dl->AddRectFilled(a, b, col(c.color ? c.color : gameui::rgba(0, 0, 0, 160), fade));
            break;
    }
}

void GameUiRenderer::text(ImDrawList* dl, const DrawCmd& c, float fade) const {
    if (c.text.empty())
        return;
    ImFont* f = font(c.font);
    if (!f)
        return;
    const float size = c.fontSize > 0 ? c.fontSize : f->FontSize;
    const char* s = c.text.c_str();
    const char* e = s + c.text.size();
    if (c.tracking > 0) {
        trackedText(dl, c, f, size, fade);
        return;
    }
    const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s, e);
    float x = c.rect.x;
    if (c.align == gameui::TextAlign::Center)
        x = c.rect.x + (c.rect.w - ts.x) * 0.5f;
    else if (c.align == gameui::TextAlign::Right)
        x = c.rect.right() - ts.x;
    const float y = c.rect.y + (c.rect.h - ts.y) * 0.5f;
    const ImVec2 p(snap(x), snap(y));
    const float sh = std::max(1.0f, size * 0.06f);
    const ImVec4 clip(c.rect.x - size, c.rect.y - size * 0.5f, c.rect.right() + size, c.rect.bottom() + size * 0.5f);
    dl->AddText(f, size, v(p.x + sh, p.y + sh),
                rgbaA(0, 0, 0, 0.75f * static_cast<float>(c.color & 0xffu) / 255.f * fade), s, e, 0.0f, &clip);
    dl->AddText(f, size, p, col(c.color, fade), s, e, 0.0f, &clip);
}

// Letter-spaced text (section labels): glyph by glyph with `tracking` pixels between them, same shadow as text().
void GameUiRenderer::trackedText(ImDrawList* dl, const DrawCmd& c, ImFont* f, float size, float fade) const {
    const char* s = c.text.c_str();
    const char* e = s + c.text.size();
    std::vector<std::pair<const char*, const char*>> glyphs;
    std::vector<float> adv;
    float total = 0;
    for (const char* p = s; p < e;) {
        const auto lead = static_cast<unsigned char>(*p);
        const int n = lead < 0x80 ? 1 : (lead >> 5) == 6 ? 2 : (lead >> 4) == 14 ? 3 : (lead >> 3) == 30 ? 4 : 1;
        const char* q = std::min(p + n, e);
        glyphs.emplace_back(p, q);
        adv.push_back(f->CalcTextSizeA(size, FLT_MAX, 0.0f, p, q).x);
        total += adv.back();
        p = q;
    }
    if (glyphs.empty())
        return;
    total += c.tracking * static_cast<float>(glyphs.size() - 1);
    float x = c.rect.x;
    if (c.align == gameui::TextAlign::Center)
        x = c.rect.x + (c.rect.w - total) * 0.5f;
    else if (c.align == gameui::TextAlign::Right)
        x = c.rect.right() - total;
    const float y = snap(c.rect.y + (c.rect.h - size) * 0.5f);
    const float sh = std::max(1.0f, size * 0.06f);
    const ImU32 shadow = rgbaA(0, 0, 0, 0.75f * static_cast<float>(c.color & 0xffu) / 255.f * fade);
    const ImU32 colour = col(c.color, fade);
    for (size_t i = 0; i < glyphs.size(); ++i) {
        const float gx = snap(x);
        dl->AddText(f, size, v(gx + sh, y + sh), shadow, glyphs[i].first, glyphs[i].second);
        dl->AddText(f, size, v(gx, y), colour, glyphs[i].first, glyphs[i].second);
        x += adv[i] + c.tracking;
    }
}

// The shared ornament in r (3.2 : 1): the pale blue-grey gem in a gilt diamond setting, flanked by two bone-ivory
// crescents opening towards it, as the gem and crescent wings of Order of the Flame's inventory frame.
void GameUiRenderer::crest(ImDrawList* dl, const Rect& r, float fade) const {
    const ImVec2 c(r.cx(), r.cy());
    const float h = r.h;
    const float gr = h * 0.30f; // gem radius
    const float dd = h * 0.50f; // setting half-diagonal
    const float ro = h * 0.58f; // crescent outer radius
    const ImU32 ivory = rgbaA(223, 214, 188, fade), ivoryDark = rgbaA(40, 26, 12, 0.85f * fade);
    for (int side = 0; side < 2; ++side) {
        const float dir = side == 0 ? 3.14159265f : 0.0f;
        const ImVec2 cc(c.x + (side == 0 ? -1.0f : 1.0f) * h * 0.30f, c.y);
        crescent(dl, cc, ro + h * 0.05f, h * 0.22f, dir, 2.3f, ivoryDark);
        crescent(dl, cc, ro, h * 0.16f, dir, 2.15f, ivory);
    }
    const ImVec2 q[4] = {v(c.x, c.y - dd), v(c.x + dd, c.y), v(c.x, c.y + dd), v(c.x - dd, c.y)};
    dl->AddQuadFilled(v(q[0].x, q[0].y - h * 0.06f), v(q[1].x + h * 0.06f, q[1].y), v(q[2].x, q[2].y + h * 0.06f),
                      v(q[3].x - h * 0.06f, q[3].y), rgbaA(30, 18, 6, 0.9f * fade));
    dl->AddQuadFilled(q[0], q[1], q[2], q[3], rgbaA(184, 128, 46, fade));
    dl->AddTriangleFilled(q[3], q[0], c, rgbaA(246, 204, 120, fade)); // lit facet
    dl->AddTriangleFilled(q[1], q[2], c, rgbaA(112, 72, 24, fade));   // shaded facet
    if (!(skin_ && skin_->ready() && skin_->gem(dl, c, gr, rgbaA(214, 226, 230, fade))))
        dl->AddCircleFilled(c, gr, rgbaA(116, 128, 133, fade), 24);
    dl->AddCircle(c, gr, rgbaA(40, 26, 12, 0.9f * fade), 24, std::max(1.0f, h * 0.04f));
    dl->AddCircleFilled(v(c.x - gr * 0.35f, c.y - gr * 0.38f), gr * 0.18f, rgbaA(255, 255, 255, 0.75f * fade), 12);
}

bool GameUiRenderer::image(ImDrawList* dl, const DrawCmd& c, const GameUiCanvas& canvas, const IconSource& icons,
                           float fade) const {
    const Rect r = c.rect;
    const float alpha = static_cast<float>(c.color & 0xffu) / 255.0f * fade;
    switch (c.image.kind) {
        case ImageKind::ItemIcon:
        case ImageKind::SpellIcon:
        case ImageKind::MapBackground: {
            IconCrop crop;
            if (!canvas.frame || !canvas.additive || !icons || !icons(c.image, crop) || !(crop.w > 0 && crop.h > 0))
                return false;
            if (crop.opaque && !canvas.opaque)
                return false;
            // Fit the crop into the rectangle (keep its aspect), centred.
            const float sc = std::min(r.w / crop.w, r.h / crop.h);
            const float w = crop.stretch ? r.w : crop.w * sc, h = crop.stretch ? r.h : crop.h * sc;
            const ImVec2 p0(snap(r.cx() - w * 0.5f), snap(r.cy() - h * 0.5f));
            const ImVec2 p1(p0.x + snap(w), p0.y + snap(h));
            // Additive crops fade through the vertex colour; opaque ones through the blend factor (the callback data).
            const int g = crop.opaque ? 255 : static_cast<int>(alpha * 255.0f + 0.5f);
            if (crop.opaque)
                dl->AddCallback(canvas.opaque, const_cast<float*>(&alpha), sizeof alpha);
            else
                dl->AddCallback(canvas.additive, nullptr);
            dl->AddImage(crop.texture ? crop.texture : canvas.frame, p0, p1, crop.uv0, crop.uv1,
                         IM_COL32(g, g, g, 255));
            dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
            return true;
        }
        case ImageKind::CategoryIcon:
            glyph(dl, r, c.image.index, rgbaA(222, 186, 118, alpha));
            return true;
        case ImageKind::PageIcon: {
            // The native section icons (INTERFCE.TXD 94): Inventory = the chest (Equipment), Character = Rynn's face,
            // Spells = the stars, Journal = the book, Map = the map, Merchant = the handshake. Skills (its native face
            // is Character's here) and a missing atlas draw a gilt glyph.
            int which = -1;
            switch (static_cast<gameui::Page>(c.image.index)) {
                case gameui::Page::Inventory:
                    which = static_cast<int>(gameui::UiIconId::Chest);
                    break;
                case gameui::Page::Character:
                    which = static_cast<int>(gameui::UiIconId::Face);
                    break;
                case gameui::Page::Spells:
                    which = static_cast<int>(gameui::UiIconId::Stars);
                    break;
                case gameui::Page::Journal:
                    which = static_cast<int>(gameui::UiIconId::Book);
                    break;
                case gameui::Page::Map:
                    which = static_cast<int>(gameui::UiIconId::Map);
                    break;
                case gameui::Page::Shop:
                    which = static_cast<int>(gameui::UiIconId::Trade);
                    break;
                case gameui::Page::Skills:
                    break;
            }
            if (which >= 0 && skin_ && skin_->ready() &&
                skin_->icon(dl, v(r.x, r.y), v(r.right(), r.bottom()), which, col(c.color, fade)))
                return true;
            if (static_cast<gameui::Page>(c.image.index) == gameui::Page::Skills)
                skillsGlyph(dl, r, rgbaA(226, 182, 92, alpha));
            else
                spellGlyph(dl, r, rgbaA(222, 186, 118, alpha));
            return true;
        }
        case ImageKind::UiIcon:
            return skin_ && skin_->ready() &&
                   skin_->icon(dl, v(r.x, r.y), v(r.right(), r.bottom()), static_cast<int>(c.image.index),
                               col(c.color, fade));
        case ImageKind::SkinCrest:
            crest(dl, r, alpha);
            return true;
        case ImageKind::Prompt:
            drawPrompt(dl, r, c.image.index, col(c.color), fade);
            return true;
        case ImageKind::SkinOrb:
            if (skin_ && skin_->ready())
                skin_->orb(dl, v(r.cx(), r.cy()), std::min(r.w, r.h) * 0.5f, col(c.color, fade));
            else
                dl->AddCircleFilled(v(r.cx(), r.cy()), std::min(r.w, r.h) * 0.45f, col(c.color, fade));
            return true;
        case ImageKind::SkinFire:
            if (skin_ && skin_->ready())
                skin_->fire(dl, v(r.x, r.y), v(r.right(), r.bottom()), alpha, ImGui::GetTime());
            return true;
        case ImageKind::SkinStone:
            if (skin_ && skin_->ready())
                skin_->stone(dl, v(r.x, r.y), v(r.right(), r.bottom()), 256.0f, col(c.color, fade));
            return true;
        case ImageKind::SkinFrame:
            if (skin_ && skin_->ready())
                skin_->frame(dl, v(r.x, r.y), v(r.right(), r.bottom()), std::min(r.w, r.h) * 0.1f, col(c.color, fade));
            return true;
        case ImageKind::SavePreview: // a save's picture (HD companion or the native thumbnail) or the title art: a
        case ImageKind::TitleArt: {  // texture of the glue's (IconCrop::texture), drawn to fill the rect (centre crop)
            IconCrop crop;
            if (!icons || !icons(c.image, crop) || !crop.texture || !(crop.w > 0 && crop.h > 0) || r.w <= 0 || r.h <= 0)
                return false;
            ImVec2 uv0 = crop.uv0, uv1 = crop.uv1;
            if (!crop.stretch) {
                const float ca = crop.w / crop.h, ra = r.w / r.h;
                if (ca > ra) { // wider than the rect: trim the sides
                    const float k = ra / ca, cu = (uv0.x + uv1.x) * 0.5f, hu = (uv1.x - uv0.x) * 0.5f * k;
                    uv0.x = cu - hu;
                    uv1.x = cu + hu;
                } else { // taller: trim top and bottom
                    const float k = ca / ra, cv = (uv0.y + uv1.y) * 0.5f, hv = (uv1.y - uv0.y) * 0.5f * k;
                    uv0.y = cv - hv;
                    uv1.y = cv + hv;
                }
            }
            const ImVec2 p0(snap(r.x), snap(r.y)), p1(snap(r.right()), snap(r.bottom()));
            const ImU32 tint = col(c.color, fade);
            dl->AddImage(crop.texture, p0, p1, uv0, uv1, tint);
            return true;
        }
        case ImageKind::None:
            break;
    }
    return false;
}

// A spell's stand-in glyph (no native spell icon captured): an eight-pointed star.
void GameUiRenderer::spellGlyph(ImDrawList* dl, const Rect& r, ImU32 c) const {
    const float s = std::min(r.w, r.h) * 0.5f;
    const ImVec2 o(r.cx(), r.cy());
    const float t = std::max(1.5f, s * 0.1f);
    for (int i = 0; i < 4; ++i) {
        const float a = static_cast<float>(i) * 0.785398f;
        const float k = (i % 2) ? 0.55f : 0.8f;
        dl->AddLine(v(o.x - std::cos(a) * s * k, o.y - std::sin(a) * s * k),
                    v(o.x + std::cos(a) * s * k, o.y + std::sin(a) * s * k), c, t);
    }
    dl->AddCircle(o, s * 0.22f, c, 16, t);
}

// Skills: three rising gilt bars and a small diamond (the page's level pips).
void GameUiRenderer::skillsGlyph(ImDrawList* dl, const Rect& r, ImU32 c) const {
    const float s = std::min(r.w, r.h);
    const float x0 = r.cx() - s * 0.36f, base = r.cy() + s * 0.36f, bw = s * 0.18f, gap = s * 0.09f;
    for (int i = 0; i < 3; ++i) {
        const float x = x0 + static_cast<float>(i) * (bw + gap);
        const float top = base - s * (0.28f + 0.2f * static_cast<float>(i));
        dl->AddRectFilled(v(x, top), v(x + bw, base), c);
    }
    const ImVec2 t(r.cx() + s * 0.26f, r.cy() - s * 0.34f);
    const float k = s * 0.12f;
    dl->AddQuadFilled(v(t.x, t.y - k), v(t.x + k * 0.6f, t.y), v(t.x, t.y + k), v(t.x - k * 0.6f, t.y), c);
}

// Category silhouettes (empty slots, missing icons): simple strokes in the rectangle.
void GameUiRenderer::glyph(ImDrawList* dl, const Rect& r, uint32_t category, ImU32 c) const {
    const float s = std::min(r.w, r.h);
    const float cx = r.cx(), cy = r.cy(), t = std::max(1.5f, s * 0.07f);
    const auto P = [&](float x, float y) { return v(cx + x * s * 0.5f, cy + y * s * 0.5f); };
    switch (static_cast<gameui::ItemCategory>(category)) {
        case gameui::ItemCategory::Melee: // sword
            dl->AddLine(P(-0.7f, 0.7f), P(0.75f, -0.75f), c, t);
            dl->AddLine(P(-0.62f, 0.28f), P(-0.28f, 0.62f), c, t);
            dl->AddLine(P(-0.85f, 0.85f), P(-0.62f, 0.62f), c, t * 1.4f);
            break;
        case gameui::ItemCategory::Bow:
            dl->PathArcTo(P(-0.9f, 0.0f), s * 0.6f, -1.0f, 1.0f, 24);
            dl->PathStroke(c, 0, t);
            dl->AddLine(P(0.18f, -0.84f), P(0.18f, 0.84f), c, t * 0.5f);
            break;
        case gameui::ItemCategory::Ammo:
            dl->AddLine(P(-0.75f, 0.75f), P(0.7f, -0.7f), c, t * 0.7f);
            dl->AddTriangleFilled(P(0.8f, -0.8f), P(0.45f, -0.65f), P(0.65f, -0.45f), c);
            dl->AddLine(P(-0.75f, 0.75f), P(-0.5f, 0.85f), c, t * 0.6f);
            dl->AddLine(P(-0.75f, 0.75f), P(-0.85f, 0.5f), c, t * 0.6f);
            break;
        case gameui::ItemCategory::Armour: { // shield
            dl->PathLineTo(P(-0.6f, -0.75f));
            dl->PathLineTo(P(0.6f, -0.75f));
            dl->PathLineTo(P(0.6f, 0.05f));
            dl->PathBezierQuadraticCurveTo(P(0.55f, 0.6f), P(0.0f, 0.85f), 12);
            dl->PathBezierQuadraticCurveTo(P(-0.55f, 0.6f), P(-0.6f, 0.05f), 12);
            dl->PathStroke(c, ImDrawFlags_Closed, t);
            break;
        }
        case gameui::ItemCategory::Consumable: // flask
            dl->AddCircle(P(0.0f, 0.3f), s * 0.27f, c, 24, t);
            dl->AddRect(P(-0.14f, -0.75f), P(0.14f, -0.22f), c, 0, 0, t);
            break;
        case gameui::ItemCategory::SpellScroll:
            dl->AddRect(P(-0.5f, -0.6f), P(0.5f, 0.6f), c, s * 0.04f, 0, t);
            dl->AddLine(P(-0.28f, -0.25f), P(0.28f, -0.25f), c, t * 0.6f);
            dl->AddLine(P(-0.28f, 0.0f), P(0.28f, 0.0f), c, t * 0.6f);
            dl->AddLine(P(-0.28f, 0.25f), P(0.16f, 0.25f), c, t * 0.6f);
            break;
        case gameui::ItemCategory::Other:
            dl->AddQuad(P(0.0f, -0.75f), P(0.6f, 0.0f), P(0.0f, 0.75f), P(-0.6f, 0.0f), c, t);
            break;
    }
}

// A filled arrowhead centred on c pointing along (dx, dy) (unit axis), `s` = its length.
static void arrowHead(ImDrawList* dl, ImVec2 c, float dx, float dy, float s, ImU32 colour) {
    const float hw = s * 0.62f; // half width at the base
    const ImVec2 tip(c.x + dx * s * 0.5f, c.y + dy * s * 0.5f);
    const ImVec2 b0(c.x - dx * s * 0.5f - dy * hw, c.y - dy * s * 0.5f + dx * hw);
    const ImVec2 b1(c.x - dx * s * 0.5f + dy * hw, c.y - dy * s * 0.5f - dx * hw);
    dl->AddTriangleFilled(tip, b0, b1, colour);
}

// The direction prompts: two arrowheads (up / down for Navigate, left / right for Adjust) drawn as shapes. The screen
// fonts (Gelasio) have no arrow glyphs, so a font arrow came out as a missing-glyph mark.
static void arrowPair(ImDrawList* dl, ImVec2 c, float h, bool horizontal, ImU32 colour) {
    const float s = h * 0.26f, gap = h * 0.17f;
    if (horizontal) {
        arrowHead(dl, v(c.x - gap, c.y), -1, 0, s, colour);
        arrowHead(dl, v(c.x + gap, c.y), 1, 0, s, colour);
    } else {
        arrowHead(dl, v(c.x, c.y - gap), 0, -1, s, colour);
        arrowHead(dl, v(c.x, c.y + gap), 0, 1, s, colour);
    }
}

// Button glyphs: Xbox face buttons / shoulder pills, or keyboard caps (the README's mapping).
void GameUiRenderer::drawPrompt(ImDrawList* dl, const Rect& r, uint32_t prompt, ImU32 tint, float fade) const {
    enum P { Confirm, Back, Secondary, Tertiary, PagePrev, PageNext, FilterPrev, FilterNext, Navigate, Adjust };
    const float a = static_cast<float>((tint >> IM_COL32_A_SHIFT) & 0xffu) / 255.0f * fade;
    ImFont* f = font(gameui::FontRole::Small);
    const float h = std::min(r.w, r.h);
    const ImVec2 c(r.cx(), r.cy());
    const auto label = [&](const char* s, float size, ImU32 colour) {
        const ImVec2 ts = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        dl->AddText(f, size, v(snap(c.x - ts.x * 0.5f), snap(c.y - ts.y * 0.5f)), colour, s);
    };
    if (prompt == Navigate || prompt == Adjust) {
        const bool horizontal = prompt == Adjust;
        const ImU32 ink = rgbaA(240, 225, 190, a);
        if (gamepad_) {
            // The D-pad: a dark cross, the two arms in use lit with their arrowheads.
            const float arm = h * 0.25f, len = h * 0.44f;
            const ImU32 body = rgbaA(18, 15, 12, 0.92f * a), edge = rgbaA(222, 186, 118, a);
            dl->AddRectFilled(v(c.x - arm * 0.5f, c.y - len), v(c.x + arm * 0.5f, c.y + len), body, arm * 0.2f);
            dl->AddRectFilled(v(c.x - len, c.y - arm * 0.5f), v(c.x + len, c.y + arm * 0.5f), body, arm * 0.2f);
            dl->AddRect(v(c.x - arm * 0.5f, c.y - len), v(c.x + arm * 0.5f, c.y + len), edge, arm * 0.2f, 0,
                        std::max(1.0f, h * 0.04f));
            dl->AddRect(v(c.x - len, c.y - arm * 0.5f), v(c.x + len, c.y + arm * 0.5f), edge, arm * 0.2f, 0,
                        std::max(1.0f, h * 0.04f));
            const float s = arm * 0.62f, at = len - arm * 0.45f;
            if (horizontal) {
                arrowHead(dl, v(c.x - at, c.y), -1, 0, s, ink);
                arrowHead(dl, v(c.x + at, c.y), 1, 0, s, ink);
            } else {
                arrowHead(dl, v(c.x, c.y - at), 0, -1, s, ink);
                arrowHead(dl, v(c.x, c.y + at), 0, 1, s, ink);
            }
            return;
        }
        const float w = h * 0.86f;
        const ImVec2 p0(snap(c.x - w * 0.5f), snap(c.y - h * 0.42f)), p1(snap(c.x + w * 0.5f), snap(c.y + h * 0.42f));
        dl->AddRectFilled(v(p0.x, p0.y + h * 0.05f), v(p1.x, p1.y + h * 0.05f), rgbaA(0, 0, 0, 0.6f * a), h * 0.14f);
        dl->AddRectFilled(p0, p1, rgbaA(40, 34, 28, 0.95f * a), h * 0.14f);
        dl->AddRect(p0, p1, rgbaA(176, 141, 87, a), h * 0.14f, 0, std::max(1.0f, h * 0.04f));
        arrowPair(dl, c, h, horizontal, ink);
        return;
    }
    if (gamepad_) {
        static const char* face[] = {"A", "B", "Y", "X"};
        static const ImU32 tone[] = {IM_COL32(110, 200, 90, 255), IM_COL32(220, 80, 70, 255),
                                     IM_COL32(230, 190, 60, 255), IM_COL32(80, 140, 230, 255)};
        if (prompt <= Tertiary) {
            const float rad = h * 0.42f;
            dl->AddCircleFilled(c, rad, rgbaA(18, 15, 12, 0.92f * a), 28);
            const ImU32 t = (tone[prompt] & ~IM_COL32_A_MASK) | (static_cast<ImU32>(a * 255.0f) << IM_COL32_A_SHIFT);
            dl->AddCircle(c, rad, t, 28, std::max(1.0f, h * 0.06f));
            label(face[prompt], h * 0.52f, t);
            return;
        }
        static const char* sh[] = {"LB", "RB", "LT", "RT"};
        const char* s = sh[std::min<uint32_t>(prompt - PagePrev, 3)];
        const float w = h * 1.05f, hh = h * 0.62f;
        dl->AddRectFilled(v(c.x - w * 0.5f, c.y - hh * 0.5f), v(c.x + w * 0.5f, c.y + hh * 0.5f),
                          rgbaA(18, 15, 12, 0.92f * a), hh * 0.5f);
        dl->AddRect(v(c.x - w * 0.5f, c.y - hh * 0.5f), v(c.x + w * 0.5f, c.y + hh * 0.5f), rgbaA(222, 186, 118, a),
                    hh * 0.5f, 0, std::max(1.0f, h * 0.045f));
        label(s, h * 0.40f, rgbaA(240, 225, 190, a));
        return;
    }
    static const char* keys[] = {"Enter", "Esc", "X", "F", "Q", "E", "Z", "C"};
    const char* s = keys[std::min<uint32_t>(prompt, FilterNext)];
    float size = h * 0.46f;
    float tw = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
    // Long names ("Enter", "Esc") shrink to fit the glyph box rather than run into the label beside it.
    const float maxW = std::max(r.w, h) * 1.08f;
    if (tw + h * 0.34f > maxW && tw > 0) {
        size *= (maxW - h * 0.34f) / tw;
        tw = f->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x;
    }
    const float w = std::max(h * 0.86f, tw + h * 0.34f);
    const ImVec2 p0(snap(c.x - w * 0.5f), snap(c.y - h * 0.42f)), p1(snap(c.x + w * 0.5f), snap(c.y + h * 0.42f));
    dl->AddRectFilled(v(p0.x, p0.y + h * 0.05f), v(p1.x, p1.y + h * 0.05f), rgbaA(0, 0, 0, 0.6f * a), h * 0.14f);
    dl->AddRectFilled(p0, p1, rgbaA(40, 34, 28, 0.95f * a), h * 0.14f);
    dl->AddRect(p0, p1, rgbaA(176, 141, 87, a), h * 0.14f, 0, std::max(1.0f, h * 0.04f));
    label(s, size, rgbaA(240, 225, 190, a));
}

} // namespace dsettings
