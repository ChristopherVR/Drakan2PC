// Unit tests + CLI for the texture upscaler core (gs_texup.h; no GPU needed).
//   gsr_test_texup                              run the self tests
//   gsr_test_texup --file in.png --factor 4 --algo xbrz|smooth|auto --out out.png [--wrap]   upscale one image (timing
//   printed)
#include "gs_texup.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace gsr::texup;

static int g_fail = 0, g_n = 0;
#define CHECK(c, msg)                                                                                                  \
    do {                                                                                                               \
        ++g_n;                                                                                                         \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("  [FAIL] %s\n", msg);                                                                         \
        } else                                                                                                         \
            std::printf("  [ok] %s\n", msg);                                                                           \
    } while (0)

static uint32_t px(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 0x80) {
    return r | (g << 8) | (b << 16) | (a << 24);
}

int main(int argc, char** argv) {
    // Replacement packs depend on stable hashes, including odd pixel counts.
    bool rgbHashesMatch = true;
    uint32_t random = 0x17391u;
    for (uint32_t w : {0u, 1u, 3u, 16u, 127u})
        for (uint32_t h : {0u, 1u, 3u, 32u}) {
            std::vector<uint32_t> rgba((size_t)w * h), rgb(rgba.size());
            for (size_t i = 0; i < rgba.size(); ++i) {
                random = random * 1664525u + 1013904223u;
                rgba[i] = random;
                rgb[i] = random & 0x00FFFFFFu;
            }
            rgbHashesMatch &= hashContentRgb(rgba.data(), w, h) == hashContent(rgb.data(), w, h);
        }
    CHECK(rgbHashesMatch, "RGB hash matches legacy masked-image lookup, including empty and odd dimensions");
    CHECK(contentPolicy(false, false).automaticScale && contentPolicy(false, false).externalFiles,
          "ordinary textures allow scaling and external packs");
    CHECK(contentPolicy(true, false).automaticScale && !contentPolicy(true, false).externalFiles,
          "generated font keeps automatic scaling without dump or external replacement");
    CHECK(!contentPolicy(false, true).automaticScale && !contentPolicy(false, true).externalFiles,
          "explicit user exclusion still disables texture changes");
    CHECK(!contentPolicy(true, true).automaticScale && !contentPolicy(true, true).externalFiles,
          "user exclusion also wins for generated content");
    {
        // The intro sky ramp: a 64x64 CT24 texture with two written bands in random memory.
        std::vector<uint32_t> ramp(64 * 64);
        uint32_t random = 12345;
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                random = random * 1664525u + 1013904223u;
                const bool written = y <= 8 || (y >= 33 && y <= 38);
                ramp[y * 64 + x] = written ? 0x004a6274u + y : (random >> 8) & 0x00FFFFFFu;
            }
        CHECK(looksUninitialized(ramp.data(), 64, 64), "random rows around a sampled band are left native");
        std::vector<uint32_t> art(64 * 64);
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                random = random * 1664525u + 1013904223u;
                const uint32_t v = 96 + ((x / 4 + y / 4) % 2) * 40 + ((random >> 24) & 31);
                art[y * 64 + x] = 0x80000000u | v | (v << 8) | ((v / 2) << 16);
            }
        CHECK(!looksUninitialized(art.data(), 64, 64), "checkered grainy art still upscales");
    }
    {
        // The GUI page's jobs (urgent lane) start before queued background upscales; each lane keeps its order.
        std::vector<int> order;
        std::mutex orderMu;
        std::atomic<bool> release{false}, started{false};
        {
            Pool pool(1);
            pool.submit([&] {
                started = true;
                while (!release)
                    std::this_thread::yield();
            });
            while (!started)
                std::this_thread::yield();
            auto record = [&](int id) {
                return [&order, &orderMu, id] {
                    std::lock_guard<std::mutex> lk(orderMu);
                    order.push_back(id);
                };
            };
            pool.submit(record(1));
            pool.submit(record(2));
            pool.submit(record(10), true);
            pool.submit(record(11), true);
            release = true;
        } // joins after draining both lanes
        CHECK((order == std::vector<int>{10, 11, 1, 2}), "urgent pool jobs run before queued background jobs");
    }
    std::string in, out, algo = "auto";
    uint32_t factor = 2;
    bool wrap = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--file") && i + 1 < argc)
            in = argv[++i];
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!std::strcmp(argv[i], "--algo") && i + 1 < argc)
            algo = argv[++i];
        else if (!std::strcmp(argv[i], "--factor") && i + 1 < argc)
            factor = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--wrap"))
            wrap = true;
    }
    if (!in.empty()) {
        std::vector<uint32_t> img, o;
        uint32_t w, h;
        if (!readImageFile(in, img, w, h)) {
            std::printf("cannot read %s\n", in.c_str());
            return 1;
        }
        const Algo a = algo == "xbrz"     ? Algo::XBrz
                       : algo == "smooth" ? Algo::Smooth
                                          : classify(analyse(img.data(), w, h));
        const auto t0 = std::chrono::steady_clock::now();
        upscale(img.data(), w, h, factor, a, wrap, wrap, o);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const Stats st = analyse(img.data(), w, h);
        std::printf("%ux%u -> %ux%u with %s in %.2f ms (colours %u, equal-neighbour %.2f)\n", w, h, w * factor,
                    h * factor, a == Algo::XBrz ? "xbrz" : "smooth", ms, st.uniqueColors, st.equalNeighbourFrac);
        if (!out.empty())
            writePngRGBA(out, o.data(), w * factor, h * factor);
        return 0;
    }

    // flat colour stays flat (both algorithms, 2x 4x 8x)
    for (Algo a : {Algo::XBrz, Algo::Smooth})
        for (uint32_t N : {2u, 4u, 8u}) {
            std::vector<uint32_t> src(8 * 8, px(10, 20, 30)), o;
            upscale(src.data(), 8, 8, N, a, false, false, o);
            bool ok = o.size() == (size_t)64 * N * N;
            for (uint32_t c : o)
                ok &= (c == px(10, 20, 30));
            char m[96];
            std::snprintf(m, sizeof(m), "flat colour preserved (%s %ux)", a == Algo::XBrz ? "xbrz" : "smooth", N);
            CHECK(ok, m);
        }
    // An all-zero GS alpha plane must not erase RGB during smooth upscaling.
    // Compare with the identical opaque image: its RGB must upscale identically.
    for (uint32_t N : {2u, 4u, 8u}) {
        std::vector<uint32_t> zero(8 * 8), opaque(8 * 8), a, b;
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 8; ++x) {
                zero[y * 8 + x] = px(20 + x * 23, 30 + y * 19, 100 + x * 7, 0);
                opaque[y * 8 + x] = zero[y * 8 + x] | 0x80000000u;
            }
        upscale(zero.data(), 8, 8, N, Algo::Smooth, false, false, a);
        upscale(opaque.data(), 8, 8, N, Algo::Smooth, false, false, b);
        bool ok = a.size() == b.size();
        for (size_t i = 0; i < a.size(); ++i)
            ok &= (a[i] >> 24) == 0 && (a[i] & 0xffffffu) == (b[i] & 0xffffffu);
        CHECK(ok, "zero-alpha smooth upscale preserves RGB and keeps alpha zero");
    }
    // hard vertical edge (black | white): xBRZ keeps it exactly one hard edge, no new colours
    {
        std::vector<uint32_t> src(8 * 8), o;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                src[y * 8 + x] = x < 4 ? px(0, 0, 0) : px(255, 255, 255);
        upscale(src.data(), 8, 8, 4, Algo::XBrz, false, false, o);
        bool ok = true;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                ok &= o[y * 32 + x] == (x < 16 ? px(0, 0, 0) : px(255, 255, 255));
        CHECK(ok, "xbrz keeps a straight edge hard");
    }
    // diagonal staircase gets smoothed (new intermediate colours appear) but stays inside the range
    {
        std::vector<uint32_t> src(16 * 16), o;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                src[y * 16 + x] = x > y ? px(255, 255, 255) : px(0, 0, 0);
        upscale(src.data(), 16, 16, 2, Algo::XBrz, false, false, o);
        int mid = 0;
        for (uint32_t c : o) {
            const uint32_t r = c & 255;
            if (r > 0 && r < 255)
                ++mid;
        }
        CHECK(mid > 0, "xbrz smooths a diagonal edge");
    }
    // alpha: transparent texel stays transparent, opaque stays opaque away from the edge; range stays 0..0x80
    {
        std::vector<uint32_t> src(8 * 8, px(200, 100, 50, 0x80)), o;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 4; ++x)
                src[y * 8 + x] = px(0, 0, 0, 0);
        for (Algo a : {Algo::XBrz, Algo::Smooth}) {
            upscale(src.data(), 8, 8, 4, a, false, false, o);
            uint32_t maxA = 0;
            for (uint32_t c : o)
                maxA = std::max(maxA, c >> 24);
            CHECK((o[16 * 32 + 0] >> 24) == 0 && (o[16 * 32 + 31] >> 24) == 0x80 && maxA <= 0x80,
                  a == Algo::XBrz ? "alpha edge preserved (xbrz)" : "alpha edge preserved (smooth)");
            // no dark fringe: colour of partially transparent edge texels comes from the opaque side
            bool fringe = false;
            for (uint32_t c : o)
                if ((c >> 24) > 0x20 && (c & 255) < 150)
                    fringe = true;
            CHECK(!fringe, a == Algo::XBrz ? "no colour fringe at alpha edge (xbrz)"
                                           : "no colour fringe at alpha edge (smooth)");
        }
    }
    // wrap: a tiling pattern upscaled with wrap is periodic
    {
        std::vector<uint32_t> src(8 * 8), o;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                src[y * 8 + x] = ((x ^ y) & 1) ? px(255, 0, 0) : px(0, 0, 255);
        upscale(src.data(), 8, 8, 2, Algo::Smooth, true, true, o);
        bool ok = true;
        for (int y = 0; y < 16; ++y)
            ok &= o[y * 16 + 0] != 0; // just exercise
        CHECK(ok, "smooth upscale with wrap runs");
    }
    // hash: content + size sensitive, stable
    {
        std::vector<uint32_t> a(16, 5), b(16, 5);
        b[7] = 6;
        CHECK(hashContent(a.data(), 4, 4) == hashContent(a.data(), 4, 4) &&
                  hashContent(a.data(), 4, 4) != hashContent(b.data(), 4, 4) &&
                  hashContent(a.data(), 4, 4) != hashContent(a.data(), 2, 8),
              "content hash stable and sensitive");
    }
    // PNG roundtrip (alpha 0x80 <-> 255)
    {
        std::vector<uint32_t> src(16 * 16), back;
        for (int i = 0; i < 256; ++i)
            src[i] = px(i, 255 - i, (i * 7) & 255, (uint32_t)(i % 129));
        const std::string path = "texup_roundtrip.png";
        uint32_t w = 0, h = 0;
        CHECK(writePngRGBA(path, src.data(), 16, 16) && readImageFile(path, back, w, h) && w == 16 && h == 16 &&
                  back == src,
              "PNG write/read round trip incl. alpha 0..0x80");
        std::remove(path.c_str());
    }
    // compressed PNG: real deflate (dynamic Huffman), exact round trip, RGB under alpha 0 preserved, much smaller than
    // raw
    {
        const uint32_t W = 256, H = 200;
        std::vector<uint32_t> src(W * H), back;
        uint32_t seed = 12345;
        for (uint32_t y = 0; y < H; ++y)
            for (uint32_t x = 0; x < W; ++x) {
                seed = seed * 1664525u + 1013904223u;
                const bool flat = (x / 32 + y / 32) % 3 == 0;
                const uint32_t n = flat ? 0 : (seed >> 24) & 15;
                src[y * W + x] =
                    px((x + n) & 255, (y * 2 + n) & 255, ((x ^ y) + n) & 255,
                       (x < 64) ? 0u : (x < 128 ? 0x80u : (y & 0x7f))); // left quarter: alpha 0 with real colour
            }
        const std::string path = "texup_compressed.png";
        uint32_t w = 0, h = 0;
        const bool ok = writePngRGBA(path, src.data(), W, H) && readImageFile(path, back, w, h) && w == W && h == H;
        bool same = ok && back.size() == src.size();
        for (size_t i = 0; same && i < src.size(); ++i)
            same = (back[i] & 0x00FFFFFFu) == (src[i] & 0x00FFFFFFu) && (back[i] >> 24) == (src[i] >> 24);
        CHECK(same, "compressed PNG round trip is exact (RGB kept under alpha 0)");
        FILE* f = std::fopen(path.c_str(), "rb");
        long sz = 0;
        if (f) {
            std::fseek(f, 0, SEEK_END);
            sz = std::ftell(f);
            std::fclose(f);
        }
        std::printf("    %ux%u PNG: %ld bytes (raw %u)\n", W, H, sz, W * H * 4);
        CHECK(sz > 0 && sz < (long)(W * H * 4 * 7 / 10), "PNG dump is actually compressed");
        std::vector<uint32_t> flatImg(64 * 64, px(10, 20, 30, 0x80));
        CHECK(writePngRGBA(path, flatImg.data(), 64, 64) && readImageFile(path, back, w, h) && back == flatImg,
              "flat image (one symbol) round trip");
        std::remove(path.c_str());
    }
    // mips
    {
        std::vector<uint32_t> src(4 * 4, px(100, 100, 100)), o;
        downsample2x(src.data(), 4, 4, o);
        CHECK(o.size() == 4 && o[0] == px(100, 100, 100), "box mip");
    }
    std::printf("%d checks, %d failed\n", g_n, g_fail);
    return g_fail ? 1 : 0;
}
