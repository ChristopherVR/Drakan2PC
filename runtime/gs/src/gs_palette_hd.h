// HD versions of palette-animated textures (water, lava, glow cycles): the index plane stays fixed while the CLUT
// changes every few frames, so every palette step has a new decoded content hash that no HD pack file or cached upscale
// can match. Instead the index plane is upscaled once ("HD index map", guided by an upscaled or HD-pack image of one
// palette step) and each new palette is applied to it with a table lookup. Pure CPU code, no D3D11 (portable core).
#pragma once
#include "gs_texdecode.h"

#include <cstdint>
#include <vector>

namespace gsr::palhd {

// Raw palette indices of an indexed (T8 / T4 family) texture level, decoded through an identity CLUT.
inline void decodeIndexPlane(const LocalMemory& mem, uint32_t psm, uint32_t tbp, uint32_t tbw, uint32_t w, uint32_t h,
                             std::vector<uint8_t>& out) {
    ClutBuf identity;
    identity.cpsm = PSM_CT32;
    for (uint32_t i = 0; i < 512; ++i)
        identity.raw[i] = i & 0xFFu;
    std::vector<uint32_t> decoded;
    decodeTextureLevel(mem, psm, tbp, tbw, w, h, 0, identity, 0, decoded);
    out.resize(decoded.size());
    for (size_t i = 0; i < decoded.size(); ++i)
        out[i] = static_cast<uint8_t>(decoded[i]);
}

inline uint64_t hashIndexPlane(const std::vector<uint8_t>& idx, uint32_t w, uint32_t h, uint32_t psm) {
    uint64_t v = 1469598103934665603ull ^ (uint64_t(w) << 32) ^ (uint64_t(h) << 16) ^ psm;
    for (uint8_t b : idx) {
        v ^= b;
        v *= 1099511628211ull;
    }
    return v;
}

// Palette of the current decode: colour of every index used by the level (unused entries stay 0).
inline void paletteFromDecode(const std::vector<uint8_t>& idx, const std::vector<uint32_t>& rgba,
                              uint32_t (&lut)[256]) {
    for (uint32_t& c : lut)
        c = 0;
    for (size_t i = 0; i < idx.size() && i < rgba.size(); ++i)
        lut[idx[i]] = rgba[i];
}

// HD index map: every HD pixel takes the index of one of the 3x3 source texels around it, the one whose colour (in
// `guide`, the source decoded with some palette) is closest to the HD image `hdGuide` (the same palette step upscaled
// or replaced by an HD pack file), with a small distance term so equal colours keep the nearest texel. Edges follow
// the HD image, values stay real palette indices, so any later palette applies exactly.
inline void buildIndexMap(const std::vector<uint8_t>& idx, const std::vector<uint32_t>& guide, uint32_t w, uint32_t h,
                          const std::vector<uint32_t>& hdGuide, uint32_t n, bool wrapX, bool wrapY,
                          std::vector<uint8_t>& out) {
    const uint32_t W = w * n, H = h * n;
    out.assign(size_t(W) * H, 0);
    auto wrap = [](int v, uint32_t size, bool rep) -> uint32_t {
        if (rep)
            return uint32_t(((v % int(size)) + int(size)) % int(size));
        return uint32_t(v < 0 ? 0 : (v >= int(size) ? int(size) - 1 : v));
    };
    auto channelDistance = [](uint32_t a, uint32_t b) {
        int d = 0;
        for (int c = 0; c < 32; c += 8) {
            const int x = int((a >> c) & 255u) - int((b >> c) & 255u);
            d += x * x;
        }
        return d;
    };
    for (uint32_t Y = 0; Y < H; ++Y)
        for (uint32_t X = 0; X < W; ++X) {
            // HD pixel centre in source texel units
            const float fx = (float(X) + 0.5f) / float(n) - 0.5f, fy = (float(Y) + 0.5f) / float(n) - 0.5f;
            const int cx = int(fx + 0.5f), cy = int(fy + 0.5f);
            const uint32_t target = hdGuide[size_t(Y) * W + X];
            int best = -1;
            float bestCost = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const uint32_t sx = wrap(cx + dx, w, wrapX), sy = wrap(cy + dy, h, wrapY);
                    const size_t s = size_t(sy) * w + sx;
                    const float ox = float(cx + dx) - fx, oy = float(cy + dy) - fy;
                    const float cost = float(channelDistance(guide[s], target)) + 64.0f * (ox * ox + oy * oy);
                    if (best < 0 || cost < bestCost) {
                        best = int(s);
                        bestCost = cost;
                    }
                }
            out[size_t(Y) * W + X] = idx[size_t(best)];
        }
}

inline void applyPalette(const std::vector<uint8_t>& map, const uint32_t (&lut)[256], std::vector<uint32_t>& out) {
    out.resize(map.size());
    for (size_t i = 0; i < map.size(); ++i)
        out[i] = lut[map[i]];
}

} // namespace gsr::palhd
