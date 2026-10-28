#include "ui/FontAtlas.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

// Private symbols avoid colliding with Dear ImGui's stb implementation.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#include "imstb_truetype.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace drakan_ui {
namespace {
constexpr uint32_t kScale = 8, kSize = 256, kRow = 22;
bool marker(uint32_t p) {
    return !(p >> 24) && (p & 255) > 240 && ((p >> 8) & 255) > 240 && ((p >> 16) & 255) < 8;
}
std::vector<uint32_t> markers(const uint32_t* source, uint32_t storedY) {
    std::vector<uint32_t> result;
    for (uint32_t x = 2; x < kSize; ++x)
        if (marker(source[storedY * kSize + x]))
            result.push_back(x);
    return result;
}

// Only a local trusted font file is parsed. No fonts or source pixels are exported.
bool loadTtf(const std::string& path, std::vector<unsigned char>& ttf, stbtt_fontinfo& font) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() <= 0 || input.tellg() > 32 * 1024 * 1024)
        return false;
    const size_t size = static_cast<size_t>(input.tellg());
    ttf.resize(size);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(ttf.data()), size) || size < 12)
        return false;
    const int offset = stbtt_GetFontOffsetForIndex(ttf.data(), 0);
    return offset >= 0 && static_cast<size_t>(offset) < size && stbtt_InitFont(&font, ttf.data(), offset);
}
} // namespace
bool font97AsciiCells(const uint32_t* source, uint32_t w, uint32_t h, std::vector<FontAtlasCell>& cells) {
    cells.clear();
    if (!source || w != kSize || h != kSize)
        return false;
    // These full signatures are unchanged by the keyboard prompt rewriter.
    const std::vector<uint32_t> first = {3,   9,   13,  21,  33,  44,  60,  73,  78,  85,  91,  99,  110, 114,
                                         122, 126, 133, 145, 154, 166, 176, 188, 198, 209, 221, 232, 243, 247};
    const std::vector<uint32_t> second = {3,   8,   19,  30,  41,  52,  71,  84,  96,  109, 122,
                                          134, 145, 159, 172, 177, 187, 200, 211, 226, 239, 251};
    if (markers(source, 255) != first || markers(source, 233) != second)
        return false;
    uint32_t code = 32;
    for (uint32_t y = 0; code <= 126 && y + kRow <= kSize; y += kRow) {
        const auto bounds = markers(source, kSize - 1 - y);
        if (bounds.size() < 2) {
            cells.clear();
            return false;
        }
        for (size_t i = 0; i + 1 < bounds.size() && code <= 126; ++i, ++code) {
            const uint32_t width = bounds[i + 1] - bounds[i] - 1;
            if (!width || width > 40) {
                cells.clear();
                return false;
            }
            cells.push_back({code, bounds[i], y, width, kRow});
        }
    }
    if (cells.size() != 95) {
        cells.clear();
        return false;
    }
    return true;
}
bool font96AsciiCells(const uint32_t* source, uint32_t w, uint32_t h, std::vector<FontAtlasCell>& cells) {
    cells.clear();
    if (!source || w != kSize || h != kSize)
        return false;
    const std::vector<uint32_t> first = {3,   16,  23,  32,  46,  59,  75,  92,  98,  106, 114, 125,
                                         138, 146, 156, 162, 174, 188, 201, 214, 227, 241, 250};
    const std::vector<uint32_t> second = {3,   16,  29,  42,  56,  62,  70,  82,  92,  103,
                                          114, 132, 148, 163, 178, 196, 211, 225, 240, 253};
    if (markers(source, 255) != first || markers(source, 234) != second)
        return false;
    uint32_t code = 32;
    for (uint32_t y = 0; code <= 126 && y + 21 <= kSize; y += 21) {
        const auto bounds = markers(source, kSize - 1 - y);
        if (bounds.size() < 2) {
            cells.clear();
            return false;
        }
        for (size_t j = 0; j + 1 < bounds.size() && code <= 126; ++j, ++code) {
            const uint32_t width = bounds[j + 1] - bounds[j] - 1;
            if (!width || width > 40) {
                cells.clear();
                return false;
            }
            cells.push_back({code, bounds[j], y, width, 21});
        }
    }
    return cells.size() == 95;
}
static bool buildAsciiAtlas(const uint32_t* source, uint32_t w, uint32_t h, const std::string& path, FontAtlas& out,
                            bool decorative) {

    out = {};
    std::vector<FontAtlasCell> cells;
    if (!(decorative ? font96AsciiCells(source, w, h, cells) : font97AsciiCells(source, w, h, cells)))
        return false;
    std::vector<unsigned char> ttf;
    stbtt_fontinfo font{};
    if (!loadTtf(path, ttf, font))
        return false;
    FontAtlas result;
    result.width = w * kScale;
    result.height = h * kScale;
    result.cells = cells;
    result.rgba.resize(static_cast<size_t>(result.width) * result.height);
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x)
            result.rgba[static_cast<size_t>(y) * result.width + x] = source[(y / kScale) * w + x / kScale];
    for (const auto& c : cells) {
        if (c.codepoint == 32 || !stbtt_FindGlyphIndex(&font, c.codepoint))
            continue;
        uint32_t left = c.width, top = c.height, right = 0, bottom = 0, maxAlpha = 0, rgb = 0;
        for (uint32_t y = 1; y < c.height; ++y)
            for (uint32_t x = 0; x < c.width; ++x) {
                const uint32_t p = source[(h - 1 - (c.y + y)) * w + c.x + x], a = p >> 24;
                if (a > maxAlpha) {
                    maxAlpha = a;
                    rgb = p & 0xffffff;
                }
                if (a > 8) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x + 1);
                    bottom = std::max(bottom, y + 1);
                }
            }
        // Unknown/blank cells retain the exact source copy.
        if (left >= right || top >= bottom || maxAlpha > 128)
            continue;
        int gx0, gy0, gx1, gy1;
        if (!stbtt_GetCodepointBox(&font, c.codepoint, &gx0, &gy0, &gx1, &gy1) || gx1 <= gx0 || gy1 <= gy0)
            continue;
        const uint32_t targetW = (right - left) * kScale, targetH = (bottom - top) * kScale;
        const float sx = static_cast<float>(targetW) / (gx1 - gx0), sy = static_cast<float>(targetH) / (gy1 - gy0);
        int bw = 0, bh = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmap(&font, sx, sy, c.codepoint, &bw, &bh, nullptr, nullptr);
        if (!bitmap || bw <= 0 || bh <= 0) {
            if (bitmap)
                stbtt_FreeBitmap(bitmap, nullptr);
            continue;
        }
        // Clear only this ASCII sprite. Row-zero markers and every other cell stay copied.
        for (uint32_t y = kScale; y < c.height * kScale; ++y)
            for (uint32_t x = 0; x < c.width * kScale; ++x)
                result.rgba[static_cast<size_t>(result.height - 1 - (c.y * kScale + y)) * result.width + c.x * kScale +
                            x] = rgb;
        for (uint32_t y = 0; y < targetH; ++y)
            for (uint32_t x = 0; x < targetW; ++x) {
                const unsigned bx = std::min<unsigned>(bw - 1, static_cast<uint64_t>(x) * bw / targetW);
                const unsigned by = std::min<unsigned>(bh - 1, static_cast<uint64_t>(y) * bh / targetH);
                const uint32_t alpha = (static_cast<uint32_t>(bitmap[by * bw + bx]) * maxAlpha + 127) / 255;
                const uint32_t yy = (c.y + top) * kScale + y, xx = (c.x + left) * kScale + x;
                result.rgba[static_cast<size_t>(result.height - 1 - yy) * result.width + xx] = rgb | (alpha << 24);
            }
        stbtt_FreeBitmap(bitmap, nullptr);
        ++result.glyphsReplaced;
    }
    if (!result.glyphsReplaced)
        return false;
    out = std::move(result);
    return true;
}
bool buildFont97Atlas(const uint32_t* source, uint32_t w, uint32_t h, const std::string& path, FontAtlas& out) {
    return buildAsciiAtlas(source, w, h, path, out, false);
}
bool buildFont96Atlas(const uint32_t* source, uint32_t w, uint32_t h, const std::string& path, FontAtlas& out) {
    return buildAsciiAtlas(source, w, h, path, out, true);
}

namespace {
struct Rgb {
    float r, g, b;
};
uint32_t channel(uint32_t p, int c) {
    return (p >> (8 * c)) & 255;
}
// Upright HD canvas over the stored-row overlay image.
struct HdCanvas {
    TileOverlay& o;
    uint32_t& at(uint32_t x, uint32_t y) { return o.rgba[static_cast<size_t>(o.height - 1 - y) * o.width + x]; }
    // Paints colour c with coverage a (0..1) over what is there; alpha stays opaque GS 0x80.
    void blend(uint32_t x, uint32_t y, Rgb c, float a) {
        if (x >= o.width || y >= o.height || a <= 0)
            return;
        uint32_t& p = at(x, y);
        const float k = std::min(1.0f, a);
        const auto mix = [&](int ch, float v) {
            return static_cast<uint32_t>(channel(p, ch) * (1 - k) + v * k + 0.5f);
        };
        p = mix(0, c.r) | (mix(1, c.g) << 8) | (mix(2, c.b) << 16) | (0x80u << 24);
    }
};

// Key cap in the style of drakan_ui::drawLabelCap (KeyStyle defaults), anti-aliased at HD resolution.
void drawHdCap(HdCanvas& cv, const stbtt_fontinfo& font, float x0, float y0, float x1, float y1, float unit,
               const std::string& text) {
    const Rgb fill{44, 46, 60}, border{214, 218, 232}, shade{22, 22, 32}, ink{255, 255, 255};
    const float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f, hw = (x1 - x0) * .5f, hh = (y1 - y0) * .5f;
    const float radius = 1.5f * unit, line = .75f * unit, shadeTop = y1 - 2.5f * unit;
    for (uint32_t y = static_cast<uint32_t>(std::max(0.0f, y0 - 1)); y < static_cast<uint32_t>(y1 + 1); ++y)
        for (uint32_t x = static_cast<uint32_t>(std::max(0.0f, x0 - 1)); x < static_cast<uint32_t>(x1 + 1); ++x) {
            // signed distance to the rounded rectangle, pixel centres
            const float qx = std::fabs(x + .5f - cx) - (hw - radius), qy = std::fabs(y + .5f - cy) - (hh - radius);
            const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            const float d = std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
            const float a = std::clamp(.5f - d, 0.0f, 1.0f);
            if (a <= 0)
                continue;
            const bool bottom = y + .5f >= y1 - line;
            Rgb c = d > -line ? (bottom ? shade : border) : (y + .5f >= shadeTop ? shade : fill);
            cv.blend(x, y, c, a);
        }
    if (text.empty())
        return;
    // Text: fitted to the space above the shade band, at most ~45 % of the cap height. The buttons show their
    // windows about 1.4 times taller than wide on screen, so the glyphs are drawn that much flatter.
    constexpr float kScreenStretch = 1.4f;
    int hx0, hy0, hx1, hy1;
    const float capH = stbtt_GetCodepointBox(&font, 'H', &hx0, &hy0, &hx1, &hy1) ? float(hy1 - hy0) : 0.7f;
    const float faceH = shadeTop - y0 - 2 * line, faceW = x1 - x0 - 4 * line;
    float scale = std::min(faceH * .62f, (y1 - y0) * .45f) * kScreenStretch / (capH > 1 ? capH : 1);
    const auto advance = [&](float s) {
        float wsum = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            int adv, lsb;
            stbtt_GetCodepointHMetrics(&font, static_cast<unsigned char>(text[i]), &adv, &lsb);
            wsum += adv * s;
            if (i + 1 < text.size())
                wsum += s * stbtt_GetCodepointKernAdvance(&font, static_cast<unsigned char>(text[i]),
                                                          static_cast<unsigned char>(text[i + 1]));
        }
        return wsum;
    };
    if (const float wsum = advance(scale); wsum > faceW)
        scale *= faceW / wsum;
    const float scaleY = scale / kScreenStretch;
    float penX = cx - advance(scale) * .5f;
    const float baseline = y0 + line + (faceH + capH * scaleY) * .5f;
    for (size_t i = 0; i < text.size(); ++i) {
        const int ch = static_cast<unsigned char>(text[i]);
        const float shiftX = penX - std::floor(penX);
        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBoxSubpixel(&font, ch, scale, scaleY, shiftX, 0, &bx0, &by0, &bx1, &by1);
        const int bw = bx1 - bx0, bh = by1 - by0;
        if (bw > 0 && bh > 0) {
            std::vector<unsigned char> glyph(static_cast<size_t>(bw) * bh);
            stbtt_MakeCodepointBitmapSubpixel(&font, glyph.data(), bw, bh, bw, scale, scaleY, shiftX, 0, ch);
            const int gx = static_cast<int>(std::floor(penX)) + bx0, gy = static_cast<int>(std::floor(baseline)) + by0;
            for (int j = 0; j < bh; ++j)
                for (int k = 0; k < bw; ++k)
                    if (gx + k >= 0 && gy + j >= 0)
                        cv.blend(static_cast<uint32_t>(gx + k), static_cast<uint32_t>(gy + j), ink,
                                 glyph[static_cast<size_t>(j) * bw + k] / 255.0f);
        }
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&font, ch, &adv, &lsb);
        penX += adv * scale;
        if (i + 1 < text.size())
            penX += scale * stbtt_GetCodepointKernAdvance(&font, ch, static_cast<unsigned char>(text[i + 1]));
    }
}
} // namespace

bool buildPageTileOverlay(const uint32_t* source, uint32_t w, uint32_t h, uint32_t factor, const std::string& nextText,
                          const std::string& prevText, const std::string& ttfPath, TileOverlay& out) {
    out = {};
    if (!source || w != 128 || h != 128 || (factor != 2 && factor != 4 && factor != 8))
        return false;
    std::vector<unsigned char> ttf;
    stbtt_fontinfo font{};
    if (!loadTtf(ttfPath, ttf, font))
        return false;
    TileOverlay o;
    o.width = w * factor;
    o.height = h * factor;
    o.rgba.assign(static_cast<size_t>(o.width) * o.height, 0);
    o.coverage.assign(o.rgba.size(), 0);
    HdCanvas cv{o};
    // Stone background: bilinear upscale of the native tile, sampled inside the tile only.
    const int tx0 = int(kPageTileRect.x0), ty0 = int(kPageTileRect.y0), tx1 = int(kPageTileRect.x1) - 1,
              ty1 = int(kPageTileRect.y1) - 1;
    const auto src = [&](int x, int y) {
        x = std::clamp(x, tx0, tx1);
        y = std::clamp(y, ty0, ty1);
        return source[static_cast<size_t>(h - 1 - y) * w + x];
    };
    for (uint32_t y = ty0 * factor; y < (ty1 + 1) * factor; ++y)
        for (uint32_t x = tx0 * factor; x < (tx1 + 1) * factor; ++x) {
            const float fx = (x + .5f) / factor - .5f, fy = (y + .5f) / factor - .5f;
            const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
            const float ax = fx - ix, ay = fy - iy;
            uint32_t p = 0;
            for (int c = 0; c < 4; ++c) {
                const float v = (channel(src(ix, iy), c) * (1 - ax) + channel(src(ix + 1, iy), c) * ax) * (1 - ay) +
                                (channel(src(ix, iy + 1), c) * (1 - ax) + channel(src(ix + 1, iy + 1), c) * ax) * ay;
                p |= static_cast<uint32_t>(v + .5f) << (8 * c);
            }
            cv.at(x, y) = p;
            o.coverage[static_cast<size_t>(o.height - 1 - y) * o.width + x] = 255;
        }
    // Caps fill each window but for half a texel, so their border shows inside the button.
    const float f = float(factor), inset = .45f;
    for (const auto& [win, text] : {std::pair{kPageTileNext, &nextText}, std::pair{kPageTilePrev, &prevText}})
        drawHdCap(cv, font, (win.x0 + inset) * f, (win.y0 + inset) * f, (win.x1 - inset) * f, (win.y1 - inset) * f, f,
                  *text);
    out = std::move(o);
    return true;
}
} // namespace drakan_ui
