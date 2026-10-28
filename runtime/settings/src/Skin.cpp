#include "settings/Skin.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace dsettings {

namespace {
// Source rectangles in the INTERFCE.TXD textures (upright pixels, measured with connected components, see
// docs/reference/settings-menu.md "Style").
struct R {
    float x0, y0, x1, y1;
};
constexpr R kFrameSrc = {37, 78, 218, 228}; // tex 118 (256x256): popup frame with the corner studs
constexpr float kFrameCorner = 26.0f;
constexpr R kBarSrc = {1, 4, 255, 35}; // tex 118: title bar with the spear tip caps
constexpr float kBarCap = 30.0f;
constexpr R kOrbSrc = {28, 2, 88, 60};                            // tex 128 (128x128): white sphere
constexpr R kArrL = {0, 19, 62, 109}, kArrR = {66, 19, 128, 109}; // tex 197: left / right triangle
constexpr R kArrU = {19, 0, 109, 62}, kArrD = {19, 66, 109, 128}; // tex 198: up / down triangle
constexpr R kBoxSrc = {5, 6, 26, 27};                             // tex 103 (32x32): empty check box

// tex 94 (128x128): the inventory icon atlas, upright, in gameui::UiIconId order.
constexpr R kIconSrc[] = {
    {1, 3, 42, 37},     // chest (Equipment)
    {45, 2, 80, 41},    // stars (Spells)
    {89, 0, 124, 46},   // Rynn's face (Skills)
    {1, 43, 41, 85},    // map
    {43, 44, 84, 84},   // book (Journal)
    {85, 48, 128, 84},  // handshake (trade)
    {84, 81, 128, 127}, // gold bag
};
constexpr R kGemSrc = {2, 3, 62, 61}; // tex 129 (64x64): the orb disc
constexpr int kOptIds[] = {GameSkin::kIcons, GameSkin::kGem, GameSkin::kParchment};

constexpr int kFireW = 128, kFireH = 64, kFireColumns = 8, kFireFrames = 32;

// A small immutable animation sheet avoids per-frame texture uploads. Noise
// wraps in X and time, so both adjacent tiles and the animation loop join.
GameImage flameSheet() {
    auto noise = [](float x, float y) {
        auto hash = [](int ix, int iy) {
            uint32_t n =
                (static_cast<uint32_t>(ix) & 15u) * 374761393u + (static_cast<uint32_t>(iy) & 15u) * 668265263u;
            n = (n ^ (n >> 13)) * 1274126177u;
            return static_cast<float>((n ^ (n >> 16)) & 65535u) / 65535.0f;
        };
        const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
        float fx = x - ix, fy = y - iy;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        const float a = hash(ix, iy) * (1.0f - fx) + hash(ix + 1, iy) * fx;
        const float b = hash(ix, iy + 1) * (1.0f - fx) + hash(ix + 1, iy + 1) * fx;
        return a * (1.0f - fy) + b * fy;
    };
    GameImage im;
    im.w = kFireW * kFireColumns;
    im.h = kFireH * (kFireFrames / kFireColumns);
    im.rgba.resize(static_cast<size_t>(im.w) * im.h);
    for (int frame = 0; frame < kFireFrames; ++frame) {
        const float phase = 16.0f * frame / kFireFrames;
        for (int y = 0; y < kFireH; ++y)
            for (int x = 0; x < kFireW; ++x) {
                const float u = static_cast<float>(x) / (kFireW - 1);
                const float v = static_cast<float>(y) / (kFireH - 1);
                // Increasing the noise coordinate over time makes the tongues
                // rise; heat falls away towards the top of the slot.
                const float n =
                    0.7f * noise(u * 16.0f, v * 3.0f + phase) + 0.3f * noise(u * 32.0f, v * 7.0f + phase * 2.0f);
                const float heat = std::clamp(v * 1.25f + n * 0.8f - 0.65f, 0.0f, 1.0f);
                const float coverage = std::clamp(heat * 5.0f, 0.0f, 1.0f);
                const unsigned r = 255, g = static_cast<unsigned>(70.0f + 180.0f * heat);
                const unsigned b = static_cast<unsigned>(12.0f + 70.0f * heat * heat);
                const unsigned a = static_cast<unsigned>(coverage * 230.0f);
                const int ox = (frame % kFireColumns) * kFireW + x;
                const int oy = (frame / kFireColumns) * kFireH + y;
                im.rgba[static_cast<size_t>(oy) * im.w + ox] = r | (g << 8) | (b << 16) | (a << 24);
            }
    }
    return im;
}

// Repeats texture `t` over a..b (the backend sampler clamps, so the wrap is done with one quad per tile). `offX/offY`
// shift the pattern in pixels.
void tiled(ImDrawList* dl, ImTextureID tex, ImVec2 a, ImVec2 b, float tw, float th, float offX, float offY, ImU32 col) {
    if (tw < 1.0f || th < 1.0f)
        return;
    offX = std::fmod(offX, tw);
    if (offX < 0)
        offX += tw;
    offY = std::fmod(offY, th);
    if (offY < 0)
        offY += th;
    int guard = 0;
    for (float y = a.y - offY; y < b.y && guard < 4000; y += th)
        for (float x = a.x - offX; x < b.x && guard < 4000; x += tw, ++guard) {
            const float x0 = std::max(x, a.x), x1 = std::min(x + tw, b.x), y0 = std::max(y, a.y),
                        y1 = std::min(y + th, b.y);
            if (x1 <= x0 || y1 <= y0)
                continue;
            dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2((x0 - x) / tw, (y0 - y) / th),
                         ImVec2((x1 - x) / tw, (y1 - y) / th), col);
        }
}

inline ImVec2 uv(const GameSkin::Tex& t, float x, float y) {
    return ImVec2(x / static_cast<float>(t.w), y / static_cast<float>(t.h));
}
inline bool isMarker(uint32_t p) {
    const unsigned r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255, a = p >> 24;
    return a == 0 && r > 240 && g > 240 &&
           b < 8; // palette entry 01ffff00 (B,G,R,A): the glyph boundary marker of the game's font loader
}
} // namespace

int GameSkin::slot(int id) {
    switch (id) {
        case kFrame:
            return 0;
        case kStone:
            return 1;
        case kOrb:
            return 2;
        case kArrows:
            return 3;
        case kArrowsUD:
            return 4;
        case kCheck:
            return 5;
        case kFire:
            return 6;
        case kCursor:
            return 7;
        case kFont:
            return 8;
        default:
            return -1;
    }
}

// Transparent texels carry arbitrary palette colours; with linear filtering they would leak a coloured fringe around
// the art. Copy the colour of the nearest visible texels into them (alpha stays 0).
static void bleedEdges(GameImage& im) {
    std::vector<uint32_t> out = im.rgba;
    std::vector<unsigned char> src(im.rgba.size());
    for (size_t i = 0; i < src.size(); ++i)
        src[i] = (im.rgba[i] >> 24) >= 8;
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<unsigned char> next = src;
        for (int y = 0; y < im.h; ++y)
            for (int x = 0; x < im.w; ++x) {
                const size_t i = static_cast<size_t>(y) * im.w + x;
                if (src[i])
                    continue;
                unsigned sr = 0, sg = 0, sb = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= im.w || yy >= im.h)
                            continue;
                        const size_t j = static_cast<size_t>(yy) * im.w + xx;
                        if (!src[j])
                            continue;
                        const uint32_t q = out[j];
                        sr += q & 255;
                        sg += (q >> 8) & 255;
                        sb += (q >> 16) & 255;
                        ++n;
                    }
                if (n) {
                    out[i] = (sr / n) | ((sg / n) << 8) | ((sb / n) << 16);
                    next[i] = 1;
                }
            }
        src.swap(next);
    }
    im.rgba.swap(out);
}

bool GameSkin::fetch(const ISettingsHost& host) {
    if (fetched_)
        return true;
    static const int ids[kN] = {kFrame, kStone, kOrb, kArrows, kArrowsUD, kCheck, kFire, kCursor, kFont};
    GameImage tmp[kN];
    for (int i = 0; i < kN; ++i) {
        if (!host.gameTexture(ids[i], tmp[i]) || tmp[i].w <= 0 || tmp[i].h <= 0 ||
            tmp[i].rgba.size() != static_cast<size_t>(tmp[i].w) * tmp[i].h) {
            static int lastFail = -1;
            if (lastFail != ids[i]) {
                lastFail = ids[i];
                std::fprintf(stderr, "[settings] game art: INTERFCE.TXD texture %d not available (yet)\n", ids[i]);
            }
            return false;
        }
    }
    if (tmp[0].w != 256 || tmp[0].h != 256 || tmp[2].w != 128 || tmp[8].w != 256 || tmp[8].h != 256)
        return false; // not the layout we measured
    // The cursor texture is keyed on blue (no alpha channel): make the key transparent, and neutral so linear filtering
    // does not fringe.
    for (uint32_t& p : tmp[7].rgba) {
        const unsigned r = p & 255, g = (p >> 8) & 255, b = (p >> 16) & 255;
        if (b > 100 && b > r * 2 && g < 24)
            p = 0;
    }
    for (int i = 0; i < kN; ++i) {
        if (i != kFontIdx)
            bleedEdges(tmp[i]);
        imgs_[i] = std::move(tmp[i]);
    }
    // The disc's 32x32 decorative tile is too small for the PC hover strip.
    // Use one upright transparent flame layer, animated without rescaling it.
    imgs_[6] = flameSheet();
    // Optional pieces: kept when they decode with the measured size, never a reason to fail.
    static const int optSize[kOpt] = {128, 64, 128};
    for (int i = 0; i < kOpt; ++i) {
        GameImage im;
        if (host.gameTexture(kOptIds[i], im) && im.w == optSize[i] && im.h == optSize[i] &&
            im.rgba.size() == static_cast<size_t>(im.w) * im.h) {
            if (i != 2)
                bleedEdges(im);
            optImgs_[i] = std::move(im);
        }
    }
    fetched_ = true;
    return true;
}

bool GameSkin::upload(const Upload& up) {
    if (!fetched_)
        return false;
    for (int i = 0; i < kN; ++i) {
        if (i == kFontIdx)
            continue;
        tex_[i].id = up(imgs_[i]);
        tex_[i].w = imgs_[i].w;
        tex_[i].h = imgs_[i].h;
        if (!tex_[i].id) {
            ready_ = false;
            return false;
        }
    }
    for (int i = 0; i < kOpt; ++i) {
        optTex_[i] = Tex();
        if (optImgs_[i].w > 0) {
            optTex_[i].id = up(optImgs_[i]);
            optTex_[i].w = optImgs_[i].w;
            optTex_[i].h = optImgs_[i].h;
        }
    }
    ready_ = true;
    return true;
}

void GameSkin::reset() {
    for (int i = 0; i < kN; ++i) {
        tex_[i] = Tex();
        imgs_[i] = GameImage();
    }
    for (int i = 0; i < kOpt; ++i) {
        optTex_[i] = Tex();
        optImgs_[i] = GameImage();
    }
    fetched_ = ready_ = false;
    font = nullptr;
}

// ---- drawing
// ----------------------------------------------------------------------------------------------------------------------
void GameSkin::stone(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) const {
    const Tex& t = tex_[1];
    const float s = std::max(8.0f, tilePx);
    tiled(dl, t.id, a, b, s, s, 0.0f, 0.0f, tint);
}

void GameSkin::frame(ImDrawList* dl, ImVec2 a, ImVec2 b, float corner, ImU32 col) const {
    const Tex& t = tex_[0];
    const float c = std::min(corner, std::min(b.x - a.x, b.y - a.y) * 0.5f);
    const float xs[4] = {a.x, a.x + c, b.x - c, b.x}, ys[4] = {a.y, a.y + c, b.y - c, b.y};
    const float us[4] = {kFrameSrc.x0, kFrameSrc.x0 + kFrameCorner, kFrameSrc.x1 - kFrameCorner, kFrameSrc.x1};
    const float vs[4] = {kFrameSrc.y0, kFrameSrc.y0 + kFrameCorner, kFrameSrc.y1 - kFrameCorner, kFrameSrc.y1};
    for (int r = 0; r < 3; ++r)
        for (int cI = 0; cI < 3; ++cI) {
            if (r == 1 && cI == 1)
                continue;
            dl->AddImage(t.id, ImVec2(xs[cI], ys[r]), ImVec2(xs[cI + 1], ys[r + 1]), uv(t, us[cI], vs[r]),
                         uv(t, us[cI + 1], vs[r + 1]), col);
        }
}

void GameSkin::plate(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) const {
    const Tex& t = tex_[0];
    const float h = b.y - a.y, k = h / (kBarSrc.y1 - kBarSrc.y0);
    float cap = kBarCap * k;
    cap = std::min(cap, (b.x - a.x) * 0.5f);
    const float xs[4] = {a.x, a.x + cap, b.x - cap, b.x};
    const float us[4] = {kBarSrc.x0, kBarSrc.x0 + kBarCap, kBarSrc.x1 - kBarCap, kBarSrc.x1};
    for (int i = 0; i < 3; ++i)
        dl->AddImage(t.id, ImVec2(xs[i], a.y), ImVec2(xs[i + 1], b.y), uv(t, us[i], kBarSrc.y0),
                     uv(t, us[i + 1], kBarSrc.y1), col);
}

void GameSkin::plateSlot(ImVec2 a, ImVec2 b, ImVec2& sa, ImVec2& sb) {
    const float h = b.y - a.y, k = h / (kBarSrc.y1 - kBarSrc.y0);
    const float cap = std::min(kBarCap * k, (b.x - a.x) * 0.5f);
    sa = ImVec2(a.x + cap, a.y + 8.0f * k);
    sb = ImVec2(b.x - cap, a.y + 24.0f * k);
}

void GameSkin::fire(ImDrawList* dl, ImVec2 a, ImVec2 b, float alpha, double time) const {
    const Tex& t = tex_[6];
    const float h = b.y - a.y, w = b.x - a.x;
    if (h <= 0.0f || w <= 0.0f || t.w <= 0 || t.h <= 0)
        return;
    const float tileW = h * static_cast<float>(kFireW) / static_cast<float>(kFireH);
    const int frame = static_cast<int>(std::fmod(std::max(0.0, time) * 24.0, static_cast<double>(kFireFrames)));
    const float atlasX = static_cast<float>((frame % kFireColumns) * kFireW);
    const float atlasY = static_cast<float>((frame / kFireColumns) * kFireH);
    const float fade = std::min(w * 0.15f, h * 0.75f);
    const float opacity = std::clamp(alpha, 0.0f, 1.0f);
    auto colour = [&](float x) {
        const float edge = std::clamp(std::min(x - a.x, b.x - x) / fade, 0.0f, 1.0f);
        return IM_COL32(255, 255, 255, static_cast<int>(opacity * edge * 255.0f));
    };
    // Split at fade boundaries as well as tile boundaries. Fade the flame's
    // own alpha so the brass underneath stays visible, including at alpha=0.
    for (float x = a.x; x < b.x; x += tileW) {
        const float left = std::max(x, a.x), right = std::min(x + tileW, b.x);
        float x0 = left;
        while (x0 < right) {
            float x1 = right;
            if (x0 < a.x + fade)
                x1 = std::min(x1, a.x + fade);
            else if (x0 < b.x - fade)
                x1 = std::min(x1, b.x - fade);
            const int start = dl->VtxBuffer.Size;
            dl->AddImage(t.id, ImVec2(x0, a.y), ImVec2(x1, b.y),
                         uv(t, atlasX + 0.5f + (x0 - x) / tileW * (kFireW - 1), atlasY + 0.5f),
                         uv(t, atlasX + 0.5f + (x1 - x) / tileW * (kFireW - 1), atlasY + kFireH - 0.5f));
            const ImU32 c0 = colour(x0), c1 = colour(x1);
            dl->VtxBuffer[start].col = dl->VtxBuffer[start + 3].col = c0;
            dl->VtxBuffer[start + 1].col = dl->VtxBuffer[start + 2].col = c1;
            x0 = x1;
        }
    }
}

void GameSkin::orb(ImDrawList* dl, ImVec2 c, float r, ImU32 tint) const {
    const Tex& t = tex_[2];
    dl->AddImage(t.id, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), uv(t, kOrbSrc.x0, kOrbSrc.y0),
                 uv(t, kOrbSrc.x1, kOrbSrc.y1), tint);
}

void GameSkin::box(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col) const {
    const Tex& t = tex_[5];
    dl->AddImage(t.id, a, b, uv(t, kBoxSrc.x0, kBoxSrc.y0), uv(t, kBoxSrc.x1, kBoxSrc.y1), col);
}

void GameSkin::arrow(ImDrawList* dl, ImVec2 c, float h, int dir, ImU32 col) const {
    const bool ud = dir >= 2;
    const Tex& t = tex_[ud ? 4 : 3];
    const R& s = dir == 0 ? kArrL : dir == 1 ? kArrR : dir == 2 ? kArrU : kArrD;
    const float sw = s.x1 - s.x0, sh = s.y1 - s.y0;
    const float k = h / (ud ? sw : sh); // `h` is the long axis... keep the aspect ratio
    const float w = sw * k, hh = sh * k;
    dl->AddImage(t.id, ImVec2(c.x - w * 0.5f, c.y - hh * 0.5f), ImVec2(c.x + w * 0.5f, c.y + hh * 0.5f),
                 uv(t, s.x0, s.y0), uv(t, s.x1, s.y1), col);
}

void GameSkin::cursor(ImDrawList* dl, ImVec2 p, float size) const {
    const Tex& t = tex_[7];
    dl->AddImage(t.id, ImVec2(p.x + size * 0.06f, p.y + size * 0.08f), ImVec2(p.x + size * 1.06f, p.y + size * 1.08f),
                 ImVec2(0, 0), ImVec2(1, 1), IM_COL32(0, 0, 0, 90)); // shadow
    dl->AddImage(t.id, p, ImVec2(p.x + size, p.y + size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
}

bool GameSkin::icon(ImDrawList* dl, ImVec2 a, ImVec2 b, int which, ImU32 tint) const {
    const Tex& t = optTex_[0];
    if (!t.id || which < 0 || which >= static_cast<int>(sizeof kIconSrc / sizeof kIconSrc[0]))
        return false;
    const R& s = kIconSrc[which];
    const float sw = s.x1 - s.x0, sh = s.y1 - s.y0;
    const float k = std::min((b.x - a.x) / sw, (b.y - a.y) / sh);
    const float w = sw * k, h = sh * k, x = 0.5f * (a.x + b.x - w), y = 0.5f * (a.y + b.y - h);
    dl->AddImage(t.id, ImVec2(x, y), ImVec2(x + w, y + h), uv(t, s.x0, s.y0), uv(t, s.x1, s.y1), tint);
    return true;
}

bool GameSkin::gem(ImDrawList* dl, ImVec2 c, float r, ImU32 tint) const {
    const Tex& t = optTex_[1];
    if (!t.id)
        return false;
    dl->AddImage(t.id, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), uv(t, kGemSrc.x0, kGemSrc.y0),
                 uv(t, kGemSrc.x1, kGemSrc.y1), tint);
    return true;
}

bool GameSkin::parchment(ImDrawList* dl, ImVec2 a, ImVec2 b, float tilePx, ImU32 tint) const {
    const Tex& t = optTex_[2];
    if (!t.id)
        return false;
    const float s = std::max(8.0f, tilePx);
    tiled(dl, t.id, a, b, s, s, 0.0f, 0.0f, tint);
    return true;
}

// ---- the game's font
// -------------------------------------------------------------------------------------------------------------------
// Font 96 of INTERFCE.TXD (docs/reference/ui-prompts.md 1.1): 12 lines of 21 rows, the first row of a line holds marker
// pixels (palette entry 01ffff00) at the left edge of every glyph (the last marker is the right edge of the last
// glyph); the 20 rows below it are the glyph. Glyph 0 is the space, then the printable ASCII range in order (line 0 =
// 22 glyphs, line 1 = 19, ...).
bool prepareGameGlyphs(ImGuiIO& io, ImFont* font, const GameImage& img, float bakedPx, GameFontJob& job) {
    if (!font || img.w != 256 || img.h != 256)
        return false;
    constexpr int kScale = 3, kLine = 21, kRows = 20;
    int code = 0x20;
    for (int line = 0; line < 12 && code <= 0x7e; ++line) {
        const int my = line * kLine;
        std::vector<int> ms;
        for (int x = 2; x < img.w; ++x)
            if (isMarker(img.rgba[static_cast<size_t>(my) * img.w + x]))
                ms.push_back(x);
        for (size_t j = 0; j + 1 < ms.size() && code <= 0x7e; ++j, ++code) {
            const int x0 = ms[j], x1 = ms[j + 1], sw = x1 - x0;
            if (sw < 2 || sw > 40)
                continue;
            const int dw = sw * kScale, dh = kRows * kScale;
            std::vector<unsigned char> al(static_cast<size_t>(dw) * dh);
            auto sample = [&](int sx, int sy) {
                sx = std::clamp(sx, x0, x1 - 1);
                sy = std::clamp(sy, my + 1, my + kRows);
                return static_cast<float>(img.rgba[static_cast<size_t>(sy) * img.w + sx] >> 24) / 255.0f;
            };
            for (int y = 0; y < dh; ++y)
                for (int x = 0; x < dw; ++x) {
                    const float fx = (x + 0.5f) / kScale - 0.5f, fy = (y + 0.5f) / kScale - 0.5f;
                    const int ix = static_cast<int>(std::floor(fx)), iy = static_cast<int>(std::floor(fy));
                    const float tx = fx - ix, ty = fy - iy;
                    const float a =
                        (sample(x0 + ix, my + 1 + iy) * (1 - tx) + sample(x0 + ix + 1, my + 1 + iy) * tx) * (1 - ty) +
                        (sample(x0 + ix, my + 2 + iy) * (1 - tx) + sample(x0 + ix + 1, my + 2 + iy) * tx) * ty;
                    const float s =
                        std::clamp((a - 0.08f) / 0.78f, 0.0f, 1.0f); // a little contrast: the 1x art is soft
                    al[static_cast<size_t>(y) * dw + x] =
                        static_cast<unsigned char>(s * s * (3.0f - 2.0f * s) * 255.0f + 0.5f);
                }
            const int idx = io.Fonts->AddCustomRectFontGlyph(font, static_cast<ImWchar>(code), dw, dh,
                                                             std::floor(dw * 0.90f), ImVec2(0.0f, 0.0f));
            job.rects.push_back(idx);
            job.codes.push_back(code);
            job.alpha.push_back(std::move(al));
            job.w.push_back(dw);
            job.h.push_back(dh);
        }
    }
    (void)bakedPx;
    return !job.rects.empty();
}

void finishGameGlyphs(ImGuiIO& io, const GameFontJob& job) {
    unsigned char* pix = io.Fonts->TexPixelsAlpha8;
    if (!pix)
        return;
    for (size_t i = 0; i < job.rects.size(); ++i) {
        const ImFontAtlasCustomRect* r = io.Fonts->GetCustomRectByIndex(job.rects[i]);
        if (!r || !r->IsPacked())
            continue;
        for (int y = 0; y < job.h[i]; ++y)
            std::copy(job.alpha[i].begin() + static_cast<size_t>(y) * job.w[i],
                      job.alpha[i].begin() + static_cast<size_t>(y + 1) * job.w[i],
                      pix + static_cast<size_t>(r->Y + y) * io.Fonts->TexWidth + r->X);
    }
}

} // namespace dsettings
