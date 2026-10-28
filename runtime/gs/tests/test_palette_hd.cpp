// HD index maps for palette-animated textures (gs_palette_hd.h): index planes decode exactly, the map keeps real
// indices that follow the HD guide's edges, and a new palette applies without rebuilding the map.
#include "gs_palette_hd.h"
#include <cstdio>
#include <vector>

using namespace gsr;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                 \
        }                                                                                                              \
    } while (0)

int main() {
    constexpr uint32_t w = 16, h = 16;
    // 1. index plane of a T8 and a T4 texture, independent of the CLUT contents and of CSA
    {
        LocalMemory mem;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                mem.write(PSM_T8, 1000, 1, x, y, (x * 7 + y * 13) & 255u);
                mem.write(PSM_T4, 2000, 1, x, y, (x + y) & 15u);
            }
        std::vector<uint8_t> i8, i4;
        palhd::decodeIndexPlane(mem, PSM_T8, 1000, 1, w, h, i8);
        palhd::decodeIndexPlane(mem, PSM_T4, 2000, 1, w, h, i4);
        bool ok8 = i8.size() == w * h, ok4 = i4.size() == w * h;
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                ok8 = ok8 && i8[y * w + x] == ((x * 7 + y * 13) & 255u);
                ok4 = ok4 && i4[y * w + x] == ((x + y) & 15u);
            }
        CHECK(ok8);
        CHECK(ok4);
        CHECK(palhd::hashIndexPlane(i8, w, h, PSM_T8) != palhd::hashIndexPlane(i4, w, h, PSM_T4));
        CHECK(palhd::hashIndexPlane(i8, w, h, PSM_T8) == palhd::hashIndexPlane(i8, w, h, PSM_T8));
    }

    // 2. map: left half index 1, right half index 2, both with the SAME colour in the guide palette (a cycling
    // gradient often starts that way); a diagonal edge in the HD guide must still be followed where colours differ
    {
        std::vector<uint8_t> idx(w * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                idx[y * w + x] = x < w / 2 ? 1 : (x < w - 2 ? 2 : 3);
        uint32_t guidePal[256] = {};
        guidePal[1] = 0xFF0000FFu; // red
        guidePal[2] = 0xFF0000FFu; // red as well
        guidePal[3] = 0xFFFF0000u; // blue
        std::vector<uint32_t> guide(w * h);
        for (size_t i = 0; i < guide.size(); ++i)
            guide[i] = guidePal[idx[i]];
        constexpr uint32_t n = 4;
        // HD guide: nearest upscale of the guide (what an edge-preserving upscaler gives on flat regions)
        std::vector<uint32_t> hd(w * n * h * n);
        for (uint32_t Y = 0; Y < h * n; ++Y)
            for (uint32_t X = 0; X < w * n; ++X)
                hd[Y * w * n + X] = guide[(Y / n) * w + X / n];
        std::vector<uint8_t> map;
        palhd::buildIndexMap(idx, guide, w, h, hd, n, false, false, map);
        CHECK(map.size() == hd.size());
        bool nearest = true;
        for (uint32_t Y = 0; Y < h * n; ++Y)
            for (uint32_t X = 0; X < w * n; ++X)
                nearest = nearest && map[Y * w * n + X] == idx[(Y / n) * w + X / n];
        CHECK(nearest); // equal colours keep the nearest texel's index: the 1 | 2 seam stays where it was

        // 3. a palette step: index 2 turns green; the HD texture changes exactly in the index-2 region
        uint32_t step[256] = {};
        step[1] = 0xFF0000FFu;
        step[2] = 0xFF00FF00u;
        step[3] = 0xFFFF0000u;
        std::vector<uint32_t> out;
        palhd::applyPalette(map, step, out);
        CHECK(out.size() == map.size());
        CHECK(out[0] == 0xFF0000FFu);           // left: still red
        CHECK(out[10 * n] == 0xFF00FF00u);      // texel x = 10: index 2, green
        CHECK(out[(w - 1) * n] == 0xFFFF0000u); // last column: blue
        uint32_t lut[256];
        std::vector<uint32_t> decoded(w * h);
        for (size_t i = 0; i < decoded.size(); ++i)
            decoded[i] = step[idx[i]];
        palhd::paletteFromDecode(idx, decoded, lut);
        CHECK(lut[1] == step[1] && lut[2] == step[2] && lut[3] == step[3] && lut[0] == 0);
    }

    // 4. the map follows a sharper HD edge than the source grid where colours differ
    {
        std::vector<uint8_t> idx = {5, 6, 5, 6}; // 2x2: left 5 (black), right 6 (white)
        std::vector<uint32_t> guide = {0xFF000000u, 0xFFFFFFFFu, 0xFF000000u, 0xFFFFFFFFu};
        constexpr uint32_t n = 4;
        std::vector<uint32_t> hd(16 * 4, 0xFF000000u);
        for (uint32_t Y = 0; Y < 8; ++Y)
            for (uint32_t X = 0; X < 8; ++X)
                hd[Y * 8 + X] = X >= 3 ? 0xFFFFFFFFu : 0xFF000000u; // edge one HD pixel left of the texel seam
        std::vector<uint8_t> map;
        palhd::buildIndexMap(idx, guide, 2, 2, hd, n, false, false, map);
        CHECK(map[0 * 8 + 3] == 6 && map[0 * 8 + 2] == 5 && map[7 * 8 + 3] == 6);
    }

    std::printf("palette_hd: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
