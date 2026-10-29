// Host HUD layers shared by the D3D11 overlay and the portable renderer, see settings/Hud.h.
#include "settings/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "imgui.h"

namespace dsettings {

void HudLayers::set(HudSlot slot, HudLayer layer) {
    layers_[static_cast<int>(slot)] = std::move(layer);
}

bool HudLayers::anyActive(bool menuOpen) const {
    for (int i = 0; i < static_cast<int>(HudSlot::Count); ++i) {
        const auto& layer = layers_[i];
        if (menuOpen && i >= static_cast<int>(HudSlot::Crosshair))
            break;
        if (layer.draw && (!layer.active || layer.active()))
            return true;
    }
    return false;
}

void HudLayers::draw(const HudView& view, bool menuOpen) const {
    for (int i = 0; i < static_cast<int>(HudSlot::Count); ++i) {
        const auto& layer = layers_[i];
        if (menuOpen && i >= static_cast<int>(HudSlot::Crosshair))
            break;
        if (layer.draw && (!layer.active || layer.active()))
            layer.draw(view);
    }
}

HudLayers& hudLayers() {
    static HudLayers layers;
    return layers;
}

float hudScaleFor(float outputHeight) {
    return std::clamp(outputHeight / 1080.0f, 0.62f, 3.0f);
}

void drawStatsPanel(const std::vector<std::string>& lines, int corner, float w, float h, float scale) {
    if (lines.empty())
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    const float fs = std::max(13.0f, 17.0f * std::max(0.8f, scale));
    float tw = 0.0f;
    for (const auto& l : lines)
        tw = std::max(tw, font->CalcTextSizeA(fs, 100000.0f, 0.0f, l.c_str()).x);
    const float pad = fs * 0.45f, lh = fs * 1.2f;
    const float pw = tw + 2.0f * pad, ph = static_cast<float>(lines.size()) * lh + 2.0f * pad - (lh - fs);
    const float margin = fs * 0.7f;
    const float x0 = (corner == 1 || corner == 3) ? w - margin - pw : margin;
    const float y0 = (corner == 2 || corner == 3) ? h - margin - ph : margin;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + pw, y0 + ph), IM_COL32(0, 0, 0, 150), fs * 0.25f);
    float y = y0 + pad;
    for (const auto& l : lines) {
        dl->AddText(font, fs, ImVec2(x0 + pad + 1.0f, y + 1.0f), IM_COL32(0, 0, 0, 220), l.c_str());
        dl->AddText(font, fs, ImVec2(x0 + pad, y), IM_COL32(120, 255, 140, 255), l.c_str());
        y += lh;
    }
}

void drawMenuCursor(float x, float y, float scale) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = std::max(0.8f, scale) * 22.0f; // arrow height in pixels
    // classic arrow, tip at (x, y): gold fill, dark outline, soft shadow (matches the game's brass menu frame)
    const ImVec2 pts[7] = {ImVec2(0.0f, 0.0f),   ImVec2(0.0f, 0.86f),  ImVec2(0.22f, 0.66f), ImVec2(0.38f, 1.0f),
                           ImVec2(0.52f, 0.94f), ImVec2(0.36f, 0.62f), ImVec2(0.64f, 0.62f)};
    ImVec2 p[7], sh[7];
    for (int i = 0; i < 7; ++i) {
        p[i] = ImVec2(x + pts[i].x * s, y + pts[i].y * s);
        sh[i] = ImVec2(p[i].x + s * 0.07f, p[i].y + s * 0.07f);
    }
    dl->AddTriangleFilled(sh[0], sh[1], sh[2], IM_COL32(0, 0, 0, 80));
    dl->AddTriangleFilled(sh[0], sh[2], sh[6], IM_COL32(0, 0, 0, 80));
    const ImU32 gold = IM_COL32(240, 204, 110, 255);
    dl->AddTriangleFilled(p[0], p[1], p[2], gold);
    dl->AddTriangleFilled(p[0], p[2], p[6], gold);
    dl->AddTriangleFilled(p[2], p[5], p[6], gold);
    dl->AddTriangleFilled(p[2], p[3], p[4], gold);
    dl->AddTriangleFilled(p[2], p[4], p[5], gold);
    dl->AddPolyline(p, 7, IM_COL32(20, 14, 8, 255), ImDrawFlags_Closed, std::max(1.2f, s * 0.06f));
}

void drawCrosshair(float x, float y, float scale) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float s = std::max(0.8f, scale) * 9.0f; // arm length in pixels
    const float gap = s * 0.45f, th = std::max(1.5f, s * 0.2f);
    const ImU32 ink = IM_COL32(240, 204, 110, 230), shadow = IM_COL32(0, 0, 0, 150);
    const auto arms = [&](float ox, float oy, ImU32 c, float t) {
        dl->AddLine(ImVec2(x + ox - s - gap, y + oy), ImVec2(x + ox - gap, y + oy), c, t);
        dl->AddLine(ImVec2(x + ox + gap, y + oy), ImVec2(x + ox + gap + s, y + oy), c, t);
        dl->AddLine(ImVec2(x + ox, y + oy - s - gap), ImVec2(x + ox, y + oy - gap), c, t);
        dl->AddLine(ImVec2(x + ox, y + oy + gap), ImVec2(x + ox, y + oy + gap + s), c, t);
    };
    arms(1.0f, 1.0f, shadow, th + 1.0f);
    arms(0.0f, 0.0f, ink, th);
    dl->AddCircleFilled(ImVec2(x, y), th * 0.75f, ink);
}

void drawSubtitleBand(const std::string& s, float alpha, float w, float h, float sizeFactor) {
    if (s.empty() || alpha <= 0.0f)
        return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();
    if (!(w > 0 && h > 0))
        return;
    const float factor = std::isfinite(sizeFactor) && sizeFactor > 0 ? std::clamp(sizeFactor, 0.5f, 3.0f) : 1.0f;
    float fs = std::max(12.0f, h * 0.036f * factor);
    std::vector<std::string> lines;
    auto wrap = [&]() {
        lines.clear();
        const float maxW = std::min(w * 0.80f, fs * 40.0f);
        const char* p = s.c_str();
        const char* end = p + s.size();
        while (p < end) {
            const char* nl = static_cast<const char*>(memchr(p, '\n', end - p));
            const char* paragraphEnd = nl ? nl : end;
            if (p == paragraphEnd)
                lines.emplace_back();
            while (p < paragraphEnd) {
                const char* cut = font->CalcWordWrapPositionA(fs / font->FontSize, p, paragraphEnd, maxW);
                if (cut <= p) { // guarantee progress without splitting UTF-8
                    cut = p + 1;
                    while (cut < paragraphEnd && (static_cast<unsigned char>(*cut) & 0xc0) == 0x80)
                        ++cut;
                }
                const char* inkEnd = cut;
                while (inkEnd > p && (inkEnd[-1] == ' ' || inkEnd[-1] == '\r' || inkEnd[-1] == '\t'))
                    --inkEnd;
                lines.emplace_back(p, inkEnd);
                p = cut;
                while (p < paragraphEnd && (*p == ' ' || *p == '\t'))
                    ++p;
            }
            p = nl ? nl + 1 : end;
        }
    };
    wrap();
    // Reflow at a smaller size if a long cue would cover the whole image.
    for (int i = 0; i < 12 && fs > 8 && (lines.size() * 1.25f + 1.1f) * fs > h * 0.90f; ++i) {
        fs = std::max(8.0f, fs * 0.85f);
        wrap();
    }
    const float padY = fs * 0.55f;
    const float bandH = lines.empty() ? 2 * padY : fs + (lines.size() - 1) * fs * 1.25f + 2 * padY;
    const float y1 = h * 0.965f, y0 = y1 - bandH;
    const int a = static_cast<int>(255.0f * std::clamp(alpha, 0.0f, 1.0f));
    dl->AddRectFilled(ImVec2(0.0f, y0), ImVec2(w, y1), IM_COL32(0, 0, 0, a * 165 / 255));
    // centre every line on its own
    float y = y0 + padY;
    dl->PushClipRect(ImVec2(0, std::max(0.0f, y0)), ImVec2(w, y1), true);
    for (const auto& line : lines) {
        const char* p = line.c_str();
        const char* le = p + line.size();
        const ImVec2 ls = font->CalcTextSizeA(fs, 100000.0f, 0.0f, p, le);
        const ImVec2 pos((w - ls.x) * 0.5f, y);
        dl->AddText(font, fs, ImVec2(pos.x + 1.5f, pos.y + 1.5f), IM_COL32(0, 0, 0, a), p, le);
        dl->AddText(font, fs, pos, IM_COL32(245, 245, 240, a), p, le);
        y += fs * 1.25f;
    }
    dl->PopClipRect();
}

} // namespace dsettings
