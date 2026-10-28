// Synthetic GIF packet scenes rendered through gsr, saved to decomp/build/gs/ for visual inspection, with a few
// automatic pixel checks.
//   gsr_test_synthetic [--warp] [--scale N] [--out DIR]
#include "gsr/gsr.h"
#include "gsr/scene_guide_camera.h"

#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

using namespace gsr;

// ------------------------------------------------------------------------------------------------ GIF builder
struct Gif {
    std::vector<uint8_t> d;
    void qw(uint64_t lo, uint64_t hi) {
        const uint8_t* a = reinterpret_cast<const uint8_t*>(&lo);
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&hi);
        d.insert(d.end(), a, a + 8);
        d.insert(d.end(), b, b + 8);
    }
    void tag(uint32_t nloop, bool eop, bool pre, uint64_t prim, uint32_t flg, uint32_t nreg, uint64_t regs) {
        uint64_t lo = nloop | ((uint64_t)eop << 15) | ((uint64_t)pre << 46) | (prim << 47) | ((uint64_t)flg << 58) |
                      ((uint64_t)(nreg & 15) << 60);
        qw(lo, regs);
    }
    // A+D register writes
    void ad(const std::vector<std::pair<uint8_t, uint64_t>>& w) {
        tag((uint32_t)w.size(), true, false, 0, 0, 1, 0xE);
        for (auto& p : w)
            qw(p.second, p.first);
    }
    void image(const std::vector<uint8_t>& bytes) {
        const uint32_t n = (uint32_t)((bytes.size() + 15) / 16);
        tag(n, true, false, 0, 2, 0, 0);
        const size_t at = d.size();
        d.resize(at + (size_t)n * 16, 0);
        std::memcpy(d.data() + at, bytes.data(), bytes.size());
    }
};

static uint64_t fbits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// register ids
enum : uint8_t {
    PRIM = 0x00,
    TEX0_1 = 0x06,
    CLAMP_1 = 0x08,
    TEX1_1 = 0x14,
    XYOFFSET_1 = 0x18,
    PRMODECONT = 0x1A,
    TEXA = 0x3B,
    FOGCOL = 0x3D,
    SCISSOR_1 = 0x40,
    ALPHA_1 = 0x42,
    COLCLAMP = 0x46,
    TEST_1 = 0x47,
    FRAME_1 = 0x4C,
    ZBUF_1 = 0x4E,
    BITBLTBUF = 0x50,
    TRXPOS = 0x51,
    TRXREG = 0x52,
    TRXDIR = 0x53,
    TEXFLUSH = 0x3F,
};

static uint64_t mkPrim(int type, int iip, int tme, int fge, int abe, int fst) {
    return type | (iip << 3) | (tme << 4) | (fge << 5) | (abe << 6) | (fst << 8);
}
static uint64_t mkTex0(uint32_t tbp, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th, uint32_t tcc, uint32_t tfx,
                       uint32_t cbp, uint32_t cpsm, uint32_t csm, uint32_t csa, uint32_t cld) {
    return tbp | ((uint64_t)tbw << 14) | ((uint64_t)psm << 20) | ((uint64_t)tw << 26) | ((uint64_t)th << 30) |
           ((uint64_t)tcc << 34) | ((uint64_t)tfx << 35) | ((uint64_t)cbp << 37) | ((uint64_t)cpsm << 51) |
           ((uint64_t)csm << 55) | ((uint64_t)csa << 56) | ((uint64_t)cld << 61);
}
static uint64_t mkTest(int ate, int atst, int aref, int afail, int date, int datm, int zte, int ztst) {
    return ate | (atst << 1) | (aref << 4) | (afail << 12) | (date << 14) | (datm << 15) | (zte << 16) |
           ((uint64_t)ztst << 17);
}
static uint64_t mkAlpha(int a, int b, int c, int d, int fix) {
    return a | (b << 2) | (c << 4) | (d << 6) | ((uint64_t)fix << 32);
}
static const uint64_t kOfs = (2048ull << 4) | ((2048ull << 4) << 32);

static uint64_t xy(float x, float y) {
    return (uint64_t)(uint16_t)(int)((x + 2048.0f) * 16.0f) | ((uint64_t)(uint16_t)(int)((y + 2048.0f) * 16.0f) << 32);
}

// untextured / textured sprite, optional UV
static void sprite(Gif& g, float x0, float y0, float x1, float y1, uint8_t r, uint8_t gg, uint8_t b, uint8_t a,
                   uint32_t z, int prim, float u0 = 0, float v0 = 0, float u1 = 0, float v1 = 0) {
    // PRIM + (UV, RGBAQ, XYZ2) x2 ; NREG=3: UV(3) RGBAQ(1) XYZ2(5)
    g.tag(2, true, true, (uint64_t)prim, 0, 3, 0x513);
    g.qw((uint64_t)(uint32_t)(u0 * 16) | ((uint64_t)(uint32_t)(v0 * 16) << 32), 0);
    g.qw(r | ((uint64_t)gg << 32), b | ((uint64_t)a << 32));
    g.qw(xy(x0, y0), z);
    g.qw((uint64_t)(uint32_t)(u1 * 16) | ((uint64_t)(uint32_t)(v1 * 16) << 32), 0);
    g.qw(r | ((uint64_t)gg << 32), b | ((uint64_t)a << 32));
    g.qw(xy(x1, y1), z);
}

static uint32_t swap34(uint32_t i) {
    return (i & ~0x18u) | ((i & 8u) << 1) | ((i & 0x10u) >> 1);
}

static void uploadImage(Gif& g, uint32_t dbp, uint32_t dbw, uint32_t dpsm, uint32_t x, uint32_t y, uint32_t w,
                        uint32_t h, const std::vector<uint8_t>& bytes) {
    g.ad({{BITBLTBUF, ((uint64_t)dbp << 32) | ((uint64_t)dbw << 48) | ((uint64_t)dpsm << 56)},
          {TRXPOS, ((uint64_t)x << 32) | ((uint64_t)y << 48)},
          {TRXREG, w | ((uint64_t)h << 32)},
          {TRXDIR, 0}});
    g.image(bytes);
}

// ------------------------------------------------------------------------------------------------
static int g_fail = 0, g_checks = 0;
static void expectPixel(const Image& img, int x, int y, int r, int g, int b, int tol, const char* what) {
    ++g_checks;
    const uint8_t* p = &img.rgba[((size_t)y * img.width + x) * 4];
    const bool ok = std::abs(p[0] - r) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - b) <= tol;
    std::printf("  [%s] %-34s at (%3d,%3d) got (%3d,%3d,%3d) want (%3d,%3d,%3d)\n", ok ? "ok" : "FAIL", what, x, y,
                p[0], p[1], p[2], r, g, b);
    if (!ok)
        ++g_fail;
}
static void expectTrue(bool v, const char* what) {
    ++g_checks;
    std::printf("  [%s] %s\n", v ? "ok" : "FAIL", what);
    if (!v)
        ++g_fail;
}

// ------------------------------------------------------------------------------------------------
// Regression: two textured sprites that tile a surface but leave a <2 px gap (Drakan menu background: 170..340.625 and
// 342..512). Real GS leaves the gap pixels untouched; with Config::fixSpriteSeams (default) the second sprite is
// extended. Also checks the plain GS coverage rules (pixel centres inside [x0,x1)) for exactly adjacent sprites.
static void seamTest(Config cfg, bool fix, const std::string& outDir) {
    cfg.fixSpriteSeams = fix;
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        std::printf("seam test: renderer creation failed\n");
        return;
    }
    std::printf("seam test (fixSpriteSeams=%d, scale %u)\n", fix ? 1 : 0, cfg.scale);
    const uint32_t FBW = 10, TA = 0x2000, TB = 0x2100;
    Gif g;
    g.ad({{FRAME_1, ((uint64_t)FBW << 16)},
          {ZBUF_1, 140ull | (1ull << 32)}, // ZMSK: no depth writes
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 639ull << 16 | (447ull << 48)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
          {COLCLAMP, 1},
          {PRMODECONT, 1},
          {TEXA, 0x80 | (0x80ull << 32)}});
    g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
    g.tag(2, true, false, 0, 0, 2, 0x51);
    g.qw(0, 0x80ull << 32);
    g.qw(xy(0, 0), 0);
    g.qw(0, 0x80ull << 32);
    g.qw(xy(640, 448), 0);
    auto solid = [&](uint32_t tbp, uint32_t rgba) {
        std::vector<uint8_t> t(64 * 64 * 4);
        for (size_t i = 0; i < t.size(); i += 4)
            std::memcpy(&t[i], &rgba, 4);
        uploadImage(g, tbp, 1, 0, 0, 0, 64, 64, t);
    };
    solid(TA, 0x800000FFu); // red
    solid(TB, 0x80FF0000u); // blue
    g.ad({{TEX1_1, 0}, {CLAMP_1, 1}, {PRIM, mkPrim(6, 0, 1, 0, 0, 1)}});
    auto spr = [&](uint32_t tbp, float x0, float x1, float y0, float y1) {
        g.ad({{TEX0_1, mkTex0(tbp, 1, 0, 6, 6, 1, 1, 0, 0, 0, 0, 0)}});
        sprite(g, x0, y0, x1, y1, 0x80, 0x80, 0x80, 0x80, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 64, 64);
    };
    spr(TA, 8, 40.625f, 8, 72); // covers pixels 8..40
    spr(TB, 42, 80, 8, 72);     // covers pixels 42..79 (gap: pixel 41)
    spr(TA, 100, 132, 8, 72);   // exactly adjacent pair: 100..131 | 132..163
    spr(TB, 132, 164, 8, 72);
    spr(TA, 8, 40, 100, 132); // vertical pair with a 1.5 px gap: A rows 100..131, B rows 133.5..
    spr(TB, 8, 40, 133.5f, 168);
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    gs->writePriv(0x12000000, 1);
    gs->writePriv(0x12000070, ((uint64_t)FBW << 9));
    gs->writePriv(0x12000080, (639ull * 4 + 3) << 32 | (447ull << 44) | (3ull << 23));
    gs->vsync(true);
    Image img;
    if (!gs->captureDisplay(img)) {
        ++g_fail;
        std::printf("  capture failed\n");
        return;
    }
    const int S = (int)cfg.scale;
    auto at = [&](float x, float y) { return std::pair<int, int>((int)(x * S), (int)(y * S)); };
    auto E = [&](float x, float y, int r, int gg, int b, const char* what) {
        auto p = at(x, y);
        expectPixel(img, p.first, p.second, r, gg, b, 3, what);
    };
    E(40.25f, 40, 255, 0, 0, "sprite A last covered pixel (40)");
    E(42.1f, 40, 0, 0, 255, "sprite B first pixel (42)");
    if (fix)
        E(41.25f, 40, 0, 0, 255, "gap pixel 41 closed by extending B");
    else
        E(41.25f, 40, 0, 0, 0, "gap pixel 41 untouched (GS behaviour)");
    if (fix)
        E(41.0f + 0.05f, 40, 0, 0, 255, "gap closed at its left edge");
    E(131.25f, 40, 255, 0, 0, "adjacent pair: last pixel of left sprite");
    E(132.25f, 40, 0, 0, 255, "adjacent pair: first pixel of right sprite");
    E(20, 131.25f, 255, 0, 0, "vertical pair: last row of upper sprite");
    if (fix)
        E(20, 132.25f, 0, 0, 255, "vertical gap row closed");
    else
        E(20, 132.25f, 0, 0, 0, "vertical gap row untouched (GS behaviour)");
    E(20, 134.25f, 0, 0, 255, "vertical pair: first row of lower sprite");
    writePng((outDir + "\\seam_test_fix" + (fix ? "1" : "0") + "_s" + std::to_string(cfg.scale) + ".png").c_str(), img);
}

// ------------------------------------------------------------------------------------------------
// Regression: Drakan loading screen fade (full-screen textured sprite, T8 + CT32 CLUT with palette alpha 0x7F, vertex
// colour 0, alpha 0x80, ALPHA = (Cs-Cd)*As+Cd, COLCLAMP = 0 (wrap)). Expect the destination to be darkened to ~0, not
// wrapped around.
static void fadeTest(Config cfg, const std::string& outDir) {
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    std::printf("fade test (scale %u)\n", cfg.scale);
    const uint32_t FBW = 8, CBP = 0x2600, TBP = 0x2700;
    Gif g;
    g.ad({{FRAME_1, ((uint64_t)FBW << 16)},
          {ZBUF_1, 140ull | (1ull << 32)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 511ull << 16 | (511ull << 48)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
          {COLCLAMP, 0},
          {PRMODECONT, 1},
          {TEXA, 0x80 | (0x80ull << 32)}});
    g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
    // clear with (100,110,130)
    g.tag(2, true, false, 0, 0, 2, 0x51);
    g.qw(100 | (110ull << 32), 130 | (0x80ull << 32));
    g.qw(xy(0, 0), 0);
    g.qw(100 | (110ull << 32), 130 | (0x80ull << 32));
    g.qw(xy(512, 512), 0);
    // palette (CT32 16x16 raster) all (255,254,255,0x7F), indices 0
    std::vector<uint8_t> pal(1024), idx(32 * 32, 0);
    for (int i = 0; i < 256; ++i) {
        pal[i * 4] = 255;
        pal[i * 4 + 1] = 254;
        pal[i * 4 + 2] = 255;
        pal[i * 4 + 3] = 0x7F;
    }
    uploadImage(g, CBP, 1, 0, 0, 0, 16, 16, pal);
    uploadImage(g, TBP, 1, 0x13, 0, 0, 32, 32, idx);
    g.ad({{TEX1_1, 0x60 | (0xED0ull << 32)},
          {CLAMP_1, 5},
          {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)},
          {TEST_1, mkTest(1, 6, 2, 0, 0, 0, 0, 1)},
          {TEX0_1, mkTex0(TBP, 1, 0x13, 5, 5, 1, 0, CBP, 0, 0, 0, 1)},
          {PRIM, mkPrim(6, 1, 1, 1, 1, 0)}});
    // STQ sprite: ST, RGBAQ, XYZ2 per vertex
    for (int rep = 0; rep < 3; ++rep) // the game draws the same full-screen sprite three times in one batch
    {
        g.tag(2, true, false, 0, 0, 3, 0x512);
        g.qw(fbits(0.0156f) | (fbits(0.9844f) << 32), fbits(1.0f));
        g.qw(0 | (0ull << 32), 0 | (0x80ull << 32));
        g.qw(xy(0, 0), 0);
        g.qw(fbits(1.0156f) | (fbits(-0.0156f) << 32), fbits(1.0f));
        g.qw(0 | (0ull << 32), 0 | (0x80ull << 32));
        g.qw(xy(512, 512), 0);
    }
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    gs->writePriv(0x12000000, 1);
    gs->writePriv(0x12000070, ((uint64_t)FBW << 9));
    gs->writePriv(0x12000080, (511ull * 4 + 3) << 32 | (511ull << 44) | (3ull << 23));
    gs->vsync(true);
    Image img;
    if (!gs->captureDisplay(img)) {
        ++g_fail;
        std::printf("  capture failed\n");
        return;
    }
    const int S = (int)cfg.scale;
    expectPixel(img, 100 * S, 100 * S, 0, 0, 0, 2, "faded to black (palette alpha 0x7F, wrap blend)");
    expectPixel(img, 400 * S, 300 * S, 0, 0, 0, 2, "faded to black (2)");
    writePng((outDir + "\fade_test.png").c_str(), img);
}

// ------------------------------------------------------------------------------------------------
// Texture decode regression for every texture / CLUT format (formats the game itself does not use are only covered
// here): data is uploaded with an IMAGE transfer, sampled once, and the decoded level 0 (Config::onTexture) is compared
// with an independent reference computed from the raster data.
static void texFormatTests(Config cfg) {
    std::vector<std::vector<uint32_t>> dec;
    std::vector<TextureEvent> evs;
    cfg.onTexture = [&](const TextureEvent& e) {
        evs.push_back(e);
        std::vector<uint32_t> all;
        for (uint32_t l = 0; l < e.mipLevels && l < 7; ++l)
            all.insert(all.end(), e.level[l], e.level[l] + (size_t)e.levelW[l] * e.levelH[l]);
        dec.push_back(std::move(all));
    };
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    std::printf("texture format tests\n");
    auto ex16 = [](uint32_t v, uint64_t texa) {
        const uint32_t ta0 = texa & 0xFF, ta1 = (texa >> 32) & 0xFF;
        uint32_t a = (v & 0x8000) ? ta1 : ta0;
        if (((texa >> 15) & 1) && (v & 0x7FFF) == 0)
            a = 0;
        return ((v & 31) << 3) | (((v >> 5) & 31) << 11) | (((v >> 10) & 31) << 19) | (a << 24);
    };
    uint32_t nextBase = 0x1000;
    auto alloc = [&](uint32_t blocks) {
        const uint32_t r = nextBase;
        nextBase += (blocks + 31) & ~31u;
        return r;
    };
    uint32_t rs = 12345;
    auto rnd = [&]() {
        rs = rs * 1664525u + 1013904223u;
        return rs >> 8;
    };

    auto setup = [&]() {
        Gif g;
        g.ad({{FRAME_1, (1ull << 16)},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 63ull << 16 | (63ull << 48)},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {COLCLAMP, 1},
              {PRMODECONT, 1}});
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    };
    setup();

    // draws one sprite sampling `tex0` (texture w x h); `pre` runs first (uploads); returns the concatenated decoded
    // levels
    auto run = [&](const std::string& name, const std::function<void(Gif&)>& pre, uint64_t tex0, uint32_t w, uint32_t h,
                   uint64_t tex1, uint64_t miptbp1, const std::vector<uint32_t>& expect, uint64_t texa) {
        evs.clear();
        dec.clear();
        Gif gg;
        gg.ad({{TEXA, texa}});
        pre(gg);
        gg.ad({{TEX1_1, tex1},
               {CLAMP_1, 0},
               {0x34 /* MIPTBP1_1 */, miptbp1},
               {TEX0_1, tex0},
               {PRIM, mkPrim(6, 0, 1, 0, 0, 1)}});
        sprite(gg, 0, 0, 8, 8, 0x80, 0x80, 0x80, 0x80, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, (float)w, (float)h);
        gs->submitGifPacket(GifPath::Path3, gg.d.data(), (uint32_t)gg.d.size());
        gs->vsync(false);
        bool ok = !dec.empty() && dec.back().size() == expect.size();
        size_t bad = 0, first = 0;
        if (ok)
            for (size_t i = 0; i < expect.size(); ++i)
                if (dec.back()[i] != expect[i]) {
                    if (!bad++)
                        first = i;
                }
        ok = ok && bad == 0;
        ++g_checks;
        if (!ok) {
            ++g_fail;
            std::printf("  [FAIL] %-44s %zu texels differ (first %zu got %08x want %08x)%s\n", name.c_str(), bad, first,
                        (dec.empty() || dec.back().size() <= first) ? 0u : dec.back()[first],
                        first < expect.size() ? expect[first] : 0u, dec.empty() ? " [no decode event]" : "");
        } else
            std::printf("  [ok] %s\n", name.c_str());
    };

    const uint32_t W = 32, H = 16;
    std::vector<uint32_t> pal32(256);
    for (auto& c : pal32)
        c = rnd() | (rnd() << 24);
    std::vector<uint8_t> palRaster(1024); // CT32 16x16 raster, entry j at raster swap34(j) (what the game uploads)
    for (uint32_t j = 0; j < 256; ++j)
        std::memcpy(&palRaster[swap34(j) * 4], &pal32[j], 4);

    { // T8 + CT32 CLUT, CSM1
        const uint32_t tbp = alloc(64), cbp = alloc(8);
        std::vector<uint8_t> idx(W * H);
        std::vector<uint32_t> ex(W * H);
        for (size_t i = 0; i < idx.size(); ++i) {
            idx[i] = (uint8_t)rnd();
            ex[i] = pal32[idx[i]];
        }
        run(
            "T8 + CT32 CLUT, CSM1 (bit3/4 swap)",
            [&](Gif& q) {
                uploadImage(q, cbp, 1, 0, 0, 0, 16, 16, palRaster);
                uploadImage(q, tbp, 1, 0x13, 0, 0, W, H, idx);
            },
            mkTex0(tbp, 1, 0x13, 5, 4, 1, 1, cbp, 0, 0, 0, 1), W, H, 0, 0, ex, 0);
    }
    { // T8 + CT16 CLUT with TEXA
        const uint32_t tbp = alloc(64), cbp = alloc(8);
        std::vector<uint16_t> pal16(256);
        for (auto& c : pal16)
            c = (uint16_t)rnd();
        pal16[7] = 0;
        std::vector<uint8_t> praster(512);
        for (uint32_t j = 0; j < 256; ++j)
            std::memcpy(&praster[swap34(j) * 2], &pal16[j], 2);
        std::vector<uint8_t> idx(W * H);
        std::vector<uint32_t> ex(W * H);
        const uint64_t texa = 0x20 | (1ull << 15) | (0x70ull << 32);
        for (size_t i = 0; i < idx.size(); ++i) {
            idx[i] = (uint8_t)(i < 8 ? 7 : rnd());
            ex[i] = ex16(pal16[idx[i]], texa);
        }
        run(
            "T8 + CT16 CLUT, TEXA TA0/TA1/AEM",
            [&](Gif& q) {
                uploadImage(q, cbp, 1, 2, 0, 0, 16, 16, praster);
                uploadImage(q, tbp, 1, 0x13, 0, 0, W, H, idx);
            },
            mkTex0(tbp, 1, 0x13, 5, 4, 1, 1, cbp, 2, 0, 0, 1), W, H, 0, 0, ex, texa);
    }
    { // T4 + CT32 CLUT, CSA 0 and CSA 5
        const uint32_t tbp = alloc(64), cbp0 = alloc(8), cbp5 = alloc(8);
        std::vector<uint32_t> p0(16), p5(16);
        for (auto& c : p0)
            c = rnd() | (rnd() << 24);
        for (auto& c : p5)
            c = rnd() | (rnd() << 24);
        std::vector<uint8_t> d0(64), d5(64);
        std::memcpy(d0.data(), p0.data(), 64);
        std::memcpy(d5.data(), p5.data(), 64);
        std::vector<uint8_t> idx(W * H / 2, 0);
        std::vector<uint32_t> ex0(W * H), ex5(W * H);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint32_t v = rnd() & 15;
            idx[i / 2] = (uint8_t)((i & 1) ? (idx[i / 2] | (v << 4)) : v);
            ex0[i] = p0[v];
            ex5[i] = p5[v];
        }
        run(
            "T4 + CT32 CLUT, CSA 0",
            [&](Gif& q) {
                uploadImage(q, cbp0, 1, 0, 0, 0, 8, 2, d0);
                uploadImage(q, cbp5, 1, 0, 0, 0, 8, 2, d5);
                uploadImage(q, tbp, 1, 0x14, 0, 0, W, H, idx);
            },
            mkTex0(tbp, 1, 0x14, 5, 4, 1, 1, cbp0, 0, 0, 0, 1), W, H, 0, 0, ex0, 0);
        run(
            "T4 + CT32 CLUT, CSA 5 (CLD=1 reload)", [&](Gif&) {}, mkTex0(tbp, 1, 0x14, 5, 4, 1, 1, cbp5, 0, 0, 5, 1), W,
            H, 0, 0, ex5, 0);
    }
    { // T4 + CT16 CLUT
        const uint32_t tbp = alloc(64), cbp = alloc(8);
        std::vector<uint16_t> p(16);
        for (auto& c : p)
            c = (uint16_t)rnd();
        std::vector<uint8_t> d(32);
        std::memcpy(d.data(), p.data(), 32);
        std::vector<uint8_t> idx(W * H / 2, 0);
        std::vector<uint32_t> ex(W * H);
        const uint64_t texa = 0x40 | (0x80ull << 32);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint32_t v = rnd() & 15;
            idx[i / 2] = (uint8_t)((i & 1) ? (idx[i / 2] | (v << 4)) : v);
            ex[i] = ex16(p[v], texa);
        }
        run(
            "T4 + CT16 CLUT (16x1), TEXA",
            [&](Gif& q) {
                uploadImage(q, cbp, 1, 2, 0, 0, 16, 1, d);
                uploadImage(q, tbp, 1, 0x14, 0, 0, W, H, idx);
            },
            mkTex0(tbp, 1, 0x14, 5, 4, 1, 1, cbp, 2, 0, 0, 1), W, H, 0, 0, ex, texa);
    }
    { // T8H / T4HL / T4HH read the upper byte / nibbles of CT32 pixels
        const uint32_t tbp = alloc(64), cbp = alloc(8);
        std::vector<uint8_t> pix(W * H * 4);
        std::vector<uint32_t> ex8(W * H), exl(W * H), exh(W * H);
        std::vector<uint32_t> p4(16);
        for (auto& c : p4)
            c = rnd() | (rnd() << 24);
        std::vector<uint8_t> d4(64);
        std::memcpy(d4.data(), p4.data(), 64);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint32_t hi = rnd() & 0xFF, lo = rnd() & 0xFFFFFF;
            const uint32_t v = (hi << 24) | lo;
            std::memcpy(&pix[i * 4], &v, 4);
            ex8[i] = pal32[hi];
            exl[i] = p4[hi & 15];
            exh[i] = p4[(hi >> 4) & 15];
        }
        run(
            "T8H (bits 24-31 of CT32) + CT32 CLUT",
            [&](Gif& q) {
                uploadImage(q, cbp, 1, 0, 0, 0, 16, 16, palRaster);
                uploadImage(q, tbp, 1, 0, 0, 0, W, H, pix);
            },
            mkTex0(tbp, 1, 0x1B, 5, 4, 1, 1, cbp, 0, 0, 0, 1), W, H, 0, 0, ex8, 0);
        run(
            "T4HL (bits 24-27) + CT32 CLUT", [&](Gif& q) { uploadImage(q, cbp, 1, 0, 0, 0, 8, 2, d4); },
            mkTex0(tbp, 1, 0x24, 5, 4, 1, 1, cbp, 0, 0, 0, 1), W, H, 0, 0, exl, 0);
        run(
            "T4HH (bits 28-31) + CT32 CLUT", [&](Gif&) {}, mkTex0(tbp, 1, 0x2C, 5, 4, 1, 1, cbp, 0, 0, 0, 0), W, H, 0,
            0, exh, 0);
    }
    { // CT24 with TEXA
        const uint32_t tbp = alloc(64);
        std::vector<uint8_t> d(W * H * 3);
        std::vector<uint32_t> ex(W * H);
        const uint64_t texa = 0x55 | (1ull << 15);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint32_t v = (i < 5) ? 0 : (rnd() & 0xFFFFFF);
            d[i * 3] = (uint8_t)v;
            d[i * 3 + 1] = (uint8_t)(v >> 8);
            d[i * 3 + 2] = (uint8_t)(v >> 16);
            ex[i] = v | ((v == 0 ? 0u : 0x55u) << 24);
        }
        run(
            "CT24, TEXA TA0 + AEM", [&](Gif& q) { uploadImage(q, tbp, 1, 1, 0, 0, W, H, d); },
            mkTex0(tbp, 1, 1, 5, 4, 1, 1, 0, 0, 0, 0, 0), W, H, 0, 0, ex, texa);
    }
    for (uint32_t psm : {2u, 10u}) { // CT16 / CT16S
        const uint32_t tbp = alloc(64);
        std::vector<uint8_t> d(W * H * 2);
        std::vector<uint32_t> ex(W * H);
        const uint64_t texa = 0x10 | (0xF0ull << 32);
        for (size_t i = 0; i < (size_t)W * H; ++i) {
            const uint16_t v = (uint16_t)rnd();
            std::memcpy(&d[i * 2], &v, 2);
            ex[i] = ex16(v, texa);
        }
        run(
            psm == 2 ? "CT16, TEXA TA0/TA1" : "CT16S, TEXA TA0/TA1",
            [&](Gif& q) { uploadImage(q, tbp, 1, psm, 0, 0, W, H, d); }, mkTex0(tbp, 1, psm, 5, 4, 1, 1, 0, 0, 0, 0, 0),
            W, H, 0, 0, ex, texa);
    }
    { // T8 + CT32 CLUT in CSM2: linear 256 x 1 row, CBW/COU/COV from TEXCLUT, no bit3/4 swap
        const uint32_t tbp = alloc(64), cbp = alloc(16);
        std::vector<uint8_t> row(1024);
        std::memcpy(row.data(), pal32.data(), 1024);
        std::vector<uint8_t> idx(W * H);
        std::vector<uint32_t> ex(W * H);
        for (size_t i = 0; i < idx.size(); ++i) {
            idx[i] = (uint8_t)rnd();
            ex[i] = pal32[idx[i]];
        }
        run(
            "T8 + CT32 CLUT, CSM2 (256x1 row)",
            [&](Gif& q) {
                uploadImage(q, cbp, 4, 0, 0, 0, 256, 1, row);
                uploadImage(q, tbp, 1, 0x13, 0, 0, W, H, idx);
                q.ad({{0x1C /* TEXCLUT */, 4ull}});
            },
            mkTex0(tbp, 1, 0x13, 5, 4, 1, 1, cbp, 0, 1, 0, 1), W, H, 0, 0, ex, 0);
    }
    { // mip chain: level 1 from MIPTBP1 (level 0 + level 1 compared)
        const uint32_t tbp = alloc(64), t1 = alloc(64), cbp = alloc(8);
        std::vector<uint8_t> i0(W * H), i1((W / 2) * (H / 2));
        std::vector<uint32_t> ex;
        for (auto& v : i0)
            v = (uint8_t)rnd();
        for (auto& v : i1)
            v = (uint8_t)rnd();
        for (auto v : i0)
            ex.push_back(pal32[v]);
        for (auto v : i1)
            ex.push_back(pal32[v]);
        run(
            "T8 mip level 1 via MIPTBP1",
            [&](Gif& q) {
                uploadImage(q, cbp, 1, 0, 0, 0, 16, 16, palRaster);
                uploadImage(q, tbp, 1, 0x13, 0, 0, W, H, i0);
                uploadImage(q, t1, 1, 0x13, 0, 0, W / 2, H / 2, i1);
            },
            mkTex0(tbp, 1, 0x13, 5, 4, 1, 1, cbp, 0, 0, 0, 1), W, H, 0x60 | (1ull << 2) | (4ull << 6),
            (uint64_t)t1 | (1ull << 14), ex, 0);
    }
}

// ------------------------------------------------------------------------------------------------
// Render target sampled as a texture in another format: a CT32 target read as CT24 gets alpha = TEXA.TA0 (not the
// stored alpha).
static void rtAlphaTest(Config cfg) {
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    std::printf("render-target-as-texture alpha test (scale %u)\n", cfg.scale);
    Gif g;
    g.ad({{FRAME_1, (1ull << 16)},
          {ZBUF_1, 140ull | (1ull << 32)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 63ull << 16 | (63ull << 48)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
          {COLCLAMP, 1},
          {PRMODECONT, 1},
          {TEXA, 0x40}});
    g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
    g.tag(2, true, false, 0, 0, 2, 0x51); // red, alpha 0x80, into FBP 0
    g.qw(255 | (0ull << 32), 0 | (0x80ull << 32));
    g.qw(xy(0, 0), 0);
    g.qw(255 | (0ull << 32), 0 | (0x80ull << 32));
    g.qw(xy(64, 64), 0);
    g.ad({{FRAME_1, 0x20ull | (1ull << 16)}}); // second target at page 0x20, cleared to black
    g.tag(2, true, false, 0, 0, 2, 0x51);
    g.qw(0, 0x80ull << 32);
    g.qw(xy(0, 0), 0);
    g.qw(0, 0x80ull << 32);
    g.qw(xy(64, 64), 0);
    g.ad({{TEX1_1, 0},
          {CLAMP_1, 0},
          {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)},
          {TEX0_1, mkTex0(0, 1, 1, 6, 6, 1, 1, 0, 0, 0, 0, 0)},
          {PRIM, mkPrim(6, 0, 1, 0, 1, 1)}}); // CT24, TCC, DECAL, ABE, FST
    sprite(g, 0, 0, 64, 64, 0x80, 0x80, 0x80, 0x80, 0, (int)mkPrim(6, 0, 1, 0, 1, 1), 0, 0, 64, 64);
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    gs->writePriv(0x12000000, 1);
    gs->writePriv(0x12000070, 0x20ull | (1ull << 9));
    gs->writePriv(0x12000080, (63ull * 4 + 3) << 32 | (63ull << 44) | (3ull << 23));
    gs->vsync(true);
    Image img;
    if (!gs->captureDisplay(img)) {
        ++g_fail;
        std::printf("  capture failed\n");
        return;
    }
    expectPixel(img, 20 * (int)cfg.scale, 20 * (int)cfg.scale, 127, 0, 0, 3,
                "CT32 RT sampled as CT24: alpha = TA0 (0x40 -> half)");
}

// Authored font atlases must replace native texels on the very first draw,
// even with automatic texture upscaling disabled or after a cache recreation.
static void savePreviewTest(Config cfg, const std::string& outDir) {
    cfg.textureUpscale = 1;
    cfg.textureUpscaleSync = true;
    cfg.textureReplace = false;
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    Image last;
    float aspect = 0;
    expectTrue(!gs->captureLastGameplay(last, aspect), "no save preview before a completed gameplay frame");
    std::vector<uint32_t> native(128 * 128, 0x800000dcu);
    const auto hash = textureContentHash(native.data(), 128, 128);
    auto draw = [&] {
        Gif g;
        g.ad({{FRAME_1, 1ull << 16},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 63ull << 16 | 63ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
        std::vector<uint8_t> raw(native.size() * 4);
        std::memcpy(raw.data(), native.data(), raw.size());
        uploadImage(g, 0x2000, 2, 0, 0, 0, 128, 128, raw);
        g.ad({{TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 2, 0, 7, 7, 1, 1, 0, 0, 0, 0, 1)}});
        sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 128, 128);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        expectTrue(gs->captureFramebuffer(0, 1, 0, 64, 64, image), "capture save preview fixture");
        return image;
    };
    auto image = draw();
    expectPixel(image, 20 * cfg.scale, 10 * cfg.scale, 220, 0, 0, 1, "legacy save keeps native thumbnail");
    Image companion;
    companion.width = companion.height = 1024;
    companion.rgba.resize(1024 * 1024 * 4);
    for (unsigned y = 0; y < 1024; ++y)
        for (unsigned x = 0; x < 1024; ++x) {
            auto* p = companion.rgba.data() + (size_t(y) * 1024 + x) * 4;
            p[y < 512 ? 1 : 2] = 255;
            p[3] = 255;
        }
    const auto path = outDir + "/save-companion.png";
    expectTrue(writePng(path.c_str(), companion), "write high resolution companion fixture");
    registerSavePreview(hash, path, false);
    image = draw();
    expectPixel(image, 20 * cfg.scale, 10 * cfg.scale, 0, 255, 0, 1,
                "late preview registration replaces cached native image");
    expectPixel(image, 20 * cfg.scale, 50 * cfg.scale, 0, 0, 255, 1,
                "high resolution preview preserves vertical orientation");
    registerSavePreview(hash, path, true);
    image = draw();
    expectPixel(image, 20 * cfg.scale, 10 * cfg.scale, 0, 0, 255, 1, "preview orientation updates cached replacement");
    registerSavePreview(hash, outDir + "/absent-companion.png", false);
    image = draw();
    expectPixel(image, 20 * cfg.scale, 10 * cfg.scale, 220, 0, 0, 1,
                "missing companion falls back to native thumbnail");
}
static void streamedReplacementTest(Config cfg, const std::string& outDir) {
    cfg.textureUpscale = 1;
    cfg.textureUpscaleSync = true;
    cfg.textureReplace = true;
    cfg.textureDir = outDir + "/streamed-pack";
    std::filesystem::create_directories(cfg.textureDir);
    std::vector<uint32_t> native(16, 0x800000ddu);
    const auto hash = textureContentHash(native.data(), 4, 4);
    char name[32];
    std::snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)hash);
    Image replacement;
    replacement.width = replacement.height = 16;
    replacement.rgba.resize(16 * 16 * 4);
    for (size_t i = 0; i < replacement.rgba.size(); i += 4) {
        replacement.rgba[i + 1] = 255;
        replacement.rgba[i + 3] = 255;
    }
    expectTrue(writePng((cfg.textureDir + name).c_str(), replacement), "write exact fixture replacement");
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    uint32_t format = 0x13;
    auto draw = [&](uint32_t color) {
        Gif g;
        g.ad({{FRAME_1, 1ull << 16},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 63ull << 16 | 63ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
        std::vector<uint32_t> pixels(16, color);
        std::vector<uint8_t> raw(pixels.size() * 4);
        std::memcpy(raw.data(), pixels.data(), raw.size());
        if (format == 0x13) {
            std::vector<uint32_t> palette(256, color);
            std::vector<uint8_t> paletteBytes(1024);
            std::memcpy(paletteBytes.data(), palette.data(), 1024);
            uploadImage(g, 0x2100, 1, 0, 0, 0, 16, 16, paletteBytes);
            uploadImage(g, 0x2000, 1, 0x13, 0, 0, 4, 4, std::vector<uint8_t>(16, 0));
        } else
            uploadImage(g, 0x2000, 1, 0, 0, 0, 4, 4, raw);
        g.ad({{TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 1, format, 2, 2, 1, 1, 0x2100, 0, 0, 0, 1)}});
        sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 4, 4);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        expectTrue(gs->captureFramebuffer(0, 1, 0, 64, 64, image), "capture streamed texture");
        return image;
    };
    for (unsigned color = 1; color <= 4; ++color)
        draw(0x80000000u | color);
    expectTrue(gs->stats().texVolatile > 0, "reused slot became volatile");
    auto image = draw(native[0]);
    expectPixel(image, 20 * cfg.scale, 20 * cfg.scale, 0, 255, 0, 1, "exact HD replacement survives recycled slot");
    image = draw(0x80000077u);
    expectPixel(image, 20 * cfg.scale, 20 * cfg.scale, 119, 0, 0, 1, "unknown animated content keeps native pixels");
    gs->setTextureReplace(false);
    gs->vsync(false); // queued live settings apply at the frame boundary
    image = draw(native[0]);
    expectPixel(image, 20 * cfg.scale, 20 * cfg.scale, 221, 0, 0, 1, "HD toggle still disables exact replacement");
    gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    format = 0;
    for (unsigned color = 1; color <= 4; ++color)
        draw(0x80000000u | color);
    expectTrue(gs->stats().texVolatile > 0, "full-color effect control uses a volatile slot");
    image = draw(native[0]);
    expectPixel(image, 20 * cfg.scale, 20 * cfg.scale, 221, 0, 0, 1,
                "volatile full-color orb/effect masks retain native pixels");
}

static void inventoryFontShadowTest(Config cfg) {
    cfg.textureUpscale = 1;
    for (unsigned mode = 0; mode < 4; ++mode) {
        std::vector<uint32_t> native(16, 0x800000a0u + mode), atlas(64, 0x80ffffffu);
        const auto hash = textureContentHash(native.data(), 4, 4);
        registerRuntimeTexture(hash, 8, 8, atlas.data(), mode != 3);
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            continue;
        }
        gs->setHudScaleX(1);
        gs->setHudSize(1);
        gs->vsync(false);
        Gif g;
        g.ad({{FRAME_1, 8ull << 16},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 511ull << 16 | 511ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)}});
        sprite(g, 0, 0, 512, 512, 200, 200, 200, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        SceneGuideCamera camera{};
        camera.guiScreen = 21;
        gs->noteSceneCamera(camera);
        if (mode == 2) {
            camera.sourcePc = 0x139010u;
            gs->noteSceneCamera(camera);
        }
        g.d.clear();
        std::vector<uint8_t> raw(native.size() * 4);
        std::memcpy(raw.data(), native.data(), raw.size());
        uploadImage(g, 0x2000, 1, 0, 0, 0, 4, 4, raw);
        g.ad({{TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 1, 0, 2, 2, 1, 0, 0, 0, 0, 0, 0)}});
        sprite(g, 100, 100, 110, 110, mode == 1 ? 127 : 0, 0, 0, 128, 0, (int)mkPrim(6, 0, 1, 0, 1, 1), 0, 2, 4, 4);
        // Real text batches share draw state across black shadows and white
        // foregrounds. A whole-draw black test misses these shadows entirely.
        sprite(g, 120, 100, 130, 110, 127, 127, 127, 128, 0, (int)mkPrim(6, 0, 1, 0, 1, 1), 0, 0, 4, 4);
        sprite(g, 140, 100, 150, 110, 0, 0, 0, 128, 0, (int)mkPrim(6, 0, 1, 0, 1, 1), 0, 0, 4, 1);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 8, 0, 512, 512, image)) {
            ++g_fail;
            continue;
        }
        const int edge = mode == 0 ? 100 : 200;
        expectPixel(image, 99 * cfg.scale, 104 * cfg.scale, edge, edge, edge, 1,
                    "only inventory font shadow tightens at left edge");
        expectPixel(image, 104 * cfg.scale, 104 * cfg.scale,
                    mode == 0   ? 100
                    : mode == 1 ? 253
                                : 0,
                    mode == 0 ? 100 : 0, mode == 0 ? 100 : 0, 1, "shadow alpha and colored/world/nonfont controls");
        expectPixel(image, 124 * cfg.scale, 104 * cfg.scale, 253, 253, 253, 1,
                    "batched foreground keeps color and opacity");
        expectPixel(image, 144 * cfg.scale, 104 * cfg.scale, 0, 0, 0, 1,
                    "retained prompt pictograms keep native shadow opacity");
    }
}

static void runtimeAtlasTest(Config cfg) {
    for (unsigned factor : {1u, 4u}) {
        cfg.textureUpscale = factor;
        std::vector<uint32_t> native(16, 0x800000ffu), authored(256, 0x8000ff00u);
        const auto hash = textureContentHash(native.data(), 4, 4);
        expectTrue(registerRuntimeTexture(hash, 16, 16, authored.data()), "register authored atlas");
        for (int pass = 0; pass < 3; ++pass) {
            auto gs = Renderer::create(cfg);
            if (!gs) {
                ++g_fail;
                continue;
            }
            Gif g;
            g.ad({{FRAME_1, 1ull << 16},
                  {ZBUF_1, 140ull | (1ull << 32)},
                  {XYOFFSET_1, kOfs},
                  {SCISSOR_1, 63ull << 16 | 63ull << 48},
                  {PRMODECONT, 1},
                  {COLCLAMP, 1},
                  {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
            if (pass == 2) {
                // Streaming reuses the same VRAM slot for unrelated images.
                // Make it volatile before putting the selected font back.
                for (unsigned color = 1; color <= 4; ++color) {
                    Gif warm = g;
                    std::vector<uint32_t> pixels(16, 0x80000000u | color);
                    std::vector<uint8_t> raw(pixels.size() * 4);
                    std::memcpy(raw.data(), pixels.data(), raw.size());
                    uploadImage(warm, 0x2000, 1, 0, 0, 0, 4, 4, raw);
                    warm.ad({{TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 1, 0, 2, 2, 1, 1, 0, 0, 0, 0, 0)}});
                    sprite(warm, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 4, 4);
                    gs->submitGifPacket(GifPath::Path3, warm.d.data(), (uint32_t)warm.d.size());
                    Image scratch;
                    gs->captureFramebuffer(0, 1, 0, 64, 64, scratch);
                }
                expectTrue(gs->stats().texVolatile > 0, "font regression exercised a recycled volatile slot");
            }
            std::vector<uint8_t> bytes(native.size() * 4);
            std::memcpy(bytes.data(), native.data(), bytes.size());
            uploadImage(g, 0x2000, 1, 0, 0, 0, 4, 4, bytes);
            g.ad({{TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 1, 0, 2, 2, 1, 1, 0, 0, 0, 0, 0)}});
            sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 4, 4);
            gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
            Image image;
            if (!gs->captureFramebuffer(0, 1, 0, 64, 64, image)) {
                ++g_fail;
                continue;
            }
            expectPixel(image, 20 * (int)cfg.scale, 20 * (int)cfg.scale, 0, 255, 0, 1,
                        factor == 1 ? "authored atlas first draw with upscale off"
                                    : "authored atlas first draw with async upscale");
        }
    }
}

// Regression (Drakan blob shadows drawn as huge dark polygons / stripes): render targets are allocated with >= 512
// rows, so a 128 wide target created earlier overlaps the 128 wide shadow-map target that was created later at a higher
// base address. A texture whose TBP is exactly the base of the later target has to be sampled from that target, not
// from the older one at an offset (which holds stale memory).
static void alphaPipelineTest(Config cfg) {
    cfg.textureUpscale = 1;
    for (unsigned mode = 0; mode < 8; ++mode) {
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            return;
        }
        Gif g;
        g.ad({{FRAME_1, 1ull << 16},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 63ull << 16 | 63ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {0x4a, 0},
              {0x49, 0}});
        sprite(g, 0, 0, 64, 64, 0, 0, 200, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        const bool fail = mode == 1 || mode == 2 || mode == 4;
        const bool fba = mode == 0 || mode == 3 || mode == 5;
        g.ad({{ALPHA_1, mkAlpha(0, 1, 0, 1, 0)},
              {0x4a, fba ? 1ull : 0ull},
              {TEST_1, mkTest(fail, 6, 100, mode == 2 ? 3 : 1, 0, 0, 0, 1)}});
        if (mode == 3 || mode == 4)
            g.ad({{FRAME_1, (1ull << 16) | (0x01000000ull << 32)}}); // force SW blend through partial alpha mask
        if (mode == 5)
            g.ad({{0x49, 1}}); // PABE must see source 64, not FBA-forced 192
        if (mode >= 6) {
            std::vector<uint8_t> texels(8, 0);
            if (mode == 6)
                for (unsigned i = 1; i < 8; i += 2)
                    texels[i] = 0x80;
            uploadImage(g, 0x2000, 1, 2, 0, 0, 2, 2, texels);
            g.ad({{TEXA, (128ull << 32) | (1ull << 15) | 128},
                  {TEX1_1, 0},
                  {CLAMP_1, 5},
                  {TEX0_1, mkTex0(0x2000, 1, 2, 1, 1, 1, 1, 0, 0, 0, 0, 0)},
                  {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
            sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, 1, 1), 0, 0, 2, 2);
        } else
            sprite(g, 0, 0, 64, 64, 100, 0, 0, 64, 0, (int)mkPrim(6, 0, 0, 0, 1, 0));
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 1, 0, 64, 64, image)) {
            ++g_fail;
            continue;
        }
        const char* names[] = {"FBA HW source alpha unchanged",        "AFAIL FB_ONLY retains HW blending",
                               "AFAIL RGB_ONLY retains HW blending",   "FBA shader source alpha unchanged",
                               "AFAIL FB_ONLY shader parity",          "FBA does not enable PABE blend",
                               "AEM opaque CT16 black remains opaque", "AEM zero CT16 black remains transparent"};
        const int r = mode == 5 ? 100 : mode >= 6 ? 0 : 50;
        const int b = mode == 5 || mode == 6 ? 0 : mode == 7 ? 200 : 100;
        expectPixel(image, 20 * (int)cfg.scale, 20 * (int)cfg.scale, r, 0, b, 2, names[mode]);
        const unsigned stored = gs->readVram(0, 0, 1, 20, 20) >> 24;
        const unsigned expected = mode == 2 || mode == 6 ? 128 : mode == 7 ? 0 : fba ? 192 : 64;
        expectTrue(stored == expected, "stored framebuffer alpha matches FBA/AFAIL policy");
    }
}

// Diagnostic fixtures for two pending RT sampler defects. Opt-in only until
// validated and corrected: --rt-filter-only. These are NOT game-image proof.
static void rtFilterDiagnostic(Config cfg) {
    cfg.textureUpscale = 1;
    cfg.textureFilter = 0;
    for (unsigned mode = 0; mode < 2; ++mode) {
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            continue;
        }
        Gif g;
        g.ad({{FRAME_1, 0x20ull | (1ull << 16)},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 63ull << 16 | 63ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {0x49, 0},
              {0x4a, 0}});
        sprite(g, 0, 0, 64, 64, 0, 0, 0, 0, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        sprite(g, 0, 0, 1, 2, 255, 0, 0, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        g.ad({{FRAME_1, 1ull << 16}});
        sprite(g, 0, 0, 64, 64, 0, 0, 200, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        g.ad({{TEX0_1, mkTex0(0x20u * 32, 1, mode ? 1 : 0, 1, 1, mode ? 1 : 0, 1, 0, 0, 0, 0, 0)},
              {TEX1_1, 0x60},
              {CLAMP_1, mode ? 5ull : 0ull},
              {TEXA, 128ull | (1ull << 15)},
              {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)}});
        const float u = mode ? 1.0f : 0.0f;
        sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, (int)mkPrim(6, 0, 1, 0, mode ? 1 : 0, 1), u, 0.5f, u, 0.5f);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 1, 0, 64, 64, image)) {
            ++g_fail;
            continue;
        }
        expectPixel(image, 20 * (int)cfg.scale, 20 * (int)cfg.scale, mode ? 64 : 128, 0, mode ? 100 : 0, 2,
                    mode ? "CT24 AEM expands each tap before bilinear blend"
                         : "RT repeat wraps each bilinear tap at logical edge");
    }
}

// Native alpha-only masks must follow their inventory RGB page and scissor.
// Depth-only clears and live world draws retain their original coordinates.
static void inventoryAlphaMaskTest(Config cfg) {
    cfg.textureUpscale = 1;
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            continue;
        }
        gs->setHudScaleX(mode == 1 ? 1.0f : .75f);
        gs->setHudSize(1.0f);
        gs->vsync(false); // public live settings take effect at a frame boundary
        const bool depthOnly = mode == 3, world = mode == 2, inverse = mode == 4;
        const uint64_t fullScissor = 511ull << 16 | 511ull << 48;
        Gif init;
        init.ad({{FRAME_1, 8ull << 16},
                 {ZBUF_1, 140ull},
                 {XYOFFSET_1, kOfs},
                 {SCISSOR_1, fullScissor},
                 {PRMODECONT, 1},
                 {COLCLAMP, 1},
                 {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)},
                 {0x49, 0},
                 {0x4a, 0}});
        sprite(init, 0, 0, 512, 512, 0, 0, 200, depthOnly ? 128 : 0, 0x80000000u, (int)mkPrim(6, 0, 0, 0, 0, 0));
        gs->submitGifPacket(GifPath::Path3, init.d.data(), (uint32_t)init.d.size());
        SceneGuideCamera camera{};
        camera.guiScreen = 21;
        gs->noteSceneCamera(camera);
        if (world) {
            SceneGuideCamera phase{};
            phase.sourcePc = 0x139010u;
            gs->noteSceneCamera(phase);
        }
        Gif mask;
        mask.ad({{FRAME_1, (8ull << 16) | ((depthOnly ? 0xffffffffull : 0x00ffffffull) << 32)},
                 {ZBUF_1, 140ull | (depthOnly ? 0ull : 1ull << 32)},
                 {SCISSOR_1, 48ull | (79ull << 16) | (400ull << 32) | (463ull << 48)},
                 {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)}});
        sprite(mask, 32, 400, 96, 464, 255, 255, 255, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        gs->submitGifPacket(GifPath::Path3, mask.d.data(), (uint32_t)mask.d.size());
        if (!depthOnly) {
            // Inventory uses the established left origin: native x=64 maps to 48.
            const unsigned inside = (mode == 1 || world) ? 64 : 48;
            const unsigned outside = (mode == 1 || world) ? 112 : 72;
            expectTrue((gs->readVram(0, 0, 8, inside, 432) >> 24) == 128,
                       "inventory alpha-only mask maps native geometry AND scissor");
            expectTrue((gs->readVram(0, 0, 8, outside, 432) >> 24) == 0,
                       "alpha mask leaves expected exterior untouched");
        } else {
            SceneGuideCamera phase{};
            phase.sourcePc = 0x139010u;
            gs->noteSceneCamera(phase); // test native depth clear using a native world read
        }
        Gif colour;
        colour.ad({{FRAME_1, 8ull << 16},
                   {SCISSOR_1, fullScissor},
                   {ZBUF_1, 140ull | (1ull << 32)},
                   {TEST_1, mkTest(0, 1, 0, 0, !depthOnly, inverse ? 0 : 1, 1, depthOnly ? 2 : 1)}});
        sprite(colour, 0, 384, 128, 480, 200, 0, 0, 128, 0x40000000u, (int)mkPrim(6, 0, 0, 0, 0, 0));
        gs->submitGifPacket(GifPath::Path3, colour.d.data(), (uint32_t)colour.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 8, 0, 512, 512, image)) {
            ++g_fail;
            continue;
        }
        const int in = (mode == 1 || world || depthOnly) ? 64 : 48;
        const int out = (mode == 1 || world || depthOnly) ? 112 : 72;
        const char* names[] = {"mapped alpha-only DATE mask", "identity page alpha mask",
                               "world ignores inventory metadata", "depth-only clear stays native",
                               "mapped alpha-only inverse DATE mask"};
        expectPixel(image, in * (int)cfg.scale, 432 * (int)cfg.scale, inverse ? 0 : 200, 0, inverse ? 200 : 0, 1,
                    names[mode]);
        expectPixel(image, out * (int)cfg.scale, 432 * (int)cfg.scale, inverse ? 200 : 0, 0, inverse ? 0 : 200, 1,
                    "no mask/RGB transform spill");
        if (mode == 0 || inverse) {
            expectPixel(image, 32 * (int)cfg.scale, 432 * (int)cfg.scale, inverse ? 200 : 0, 0, inverse ? 0 : 200, 1,
                        "mapped left scissor clips mask geometry");
            expectPixel(image, 64 * (int)cfg.scale, 432 * (int)cfg.scale, inverse ? 200 : 0, 0, inverse ? 0 : 200, 1,
                        "mapped right scissor clips mask geometry");
        }
    }
}

// Equipment health fill: a 128x128 texture starts on the right-hand page of
// a 128-wide RT. GS pages carry into the next row; a linear SRV offset cannot.
static void rtShiftedRowTest(Config cfg) {
    cfg.textureUpscale = 1;
    cfg.textureFilter = 1;
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            continue;
        }
        const uint32_t parent = 0x100;
        const uint32_t tbp = parent * 32 + (mode == 0 ? 9 : mode == 1 ? 8 : 0) * 32;
        Gif g;
        g.ad({{FRAME_1, parent | (2ull << 16)},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 127ull << 16 | 511ull << 48},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {0x49, 0},
              {0x4a, 0}});
        sprite(g, 0, 0, 128, 128, 40, 40, 40, 140, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        std::vector<uint8_t> atlas(128 * 128 * 4);
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                const float dx = (float)x + .5f - 58.5f, dy = (float)y + .5f - 97.5f;
                const size_t p = ((size_t)y * 128 + x) * 4;
                atlas[p] = atlas[p + 1] = atlas[p + 2] = 128;
                atlas[p + 3] = dx * dx + dy * dy < 30 * 30 ? 128 : 0;
            }
        uploadImage(g, tbp, 2, 0, 0, 0, 128, 128, atlas);
        g.ad({{FRAME_1, 8ull << 16}, {SCISSOR_1, 511ull << 16 | 511ull << 48}});
        sprite(g, 0, 0, 512, 512, 0, 0, 200, 0, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
        g.ad({{TEX0_1, mkTex0(tbp, 2, 0, mode == 2 ? 8 : 7, 7, 1, 0, 0, 0, 0, 0, 0)},
              {TEX1_1, 0},
              {CLAMP_1, 5},
              {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)},
              {TEST_1, mkTest(1, 6, 2, 0, 0, 0, 1, 1)}});
        sprite(g, 14, 433, 74, 493, 72, 0, 0, 128, 0x7fffffffu, (int)mkPrim(6, 0, 1, 0, 1, 1), 28.5f, 127.5f, 88.5f,
               67.5f);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 8, 0, 512, 512, image)) {
            ++g_fail;
            continue;
        }
        const char* names[] = {"shifted-row orb uses GS page addressing", "aligned-row RT subview remains valid",
                               "zero-origin larger logical texture keeps RT path"};
        expectPixel(image, 44 * (int)cfg.scale, 463 * (int)cfg.scale, 72, 0, 0, 1, names[mode]);
        expectPixel(image, 68 * (int)cfg.scale, 438 * (int)cfg.scale, 0, 0, 200, 1,
                    "orb top-right exterior is transparent, not stretched red");
        expectPixel(image, 20 * (int)cfg.scale, 438 * (int)cfg.scale, 0, 0, 200, 1,
                    "orb top-left exterior stays transparent");
        expectTrue((gs->readVram(0, 0, 8, 68, 438) >> 24) == 0, "orb exterior stored alpha stays zero");
        expectTrue(mode == 0 ? gs->stats().rtSamples == 0 : gs->stats().rtSamples == 1,
                   "unsafe shifted row decodes VRAM; valid RT views stay direct");
    }
}

// Regression (first inventory open showed native art for a while): the HD pack image of a GUI page texture is on
// screen on its first draw even when the worker pool is busy with background upscales of world textures.
static void guiPagePackFirstFrameTest(Config cfg, const std::string& outDir) {
    cfg.textureUpscale = 8;
    cfg.textureUpscaleSync = false;
    cfg.textureReplace = true;
    cfg.textureDir = outDir + "/gui-page-pack";
    std::filesystem::create_directories(cfg.textureDir);
    std::vector<uint32_t> native(16, 0x800000ddu);
    const auto hash = textureContentHash(native.data(), 4, 4);
    char name[32];
    std::snprintf(name, sizeof(name), "/%016llx.png", (unsigned long long)hash);
    Image replacement;
    replacement.width = replacement.height = 16;
    replacement.rgba.resize(16 * 16 * 4);
    for (size_t i = 0; i < replacement.rgba.size(); i += 4) {
        replacement.rgba[i + 1] = 255;
        replacement.rgba[i + 3] = 255;
    }
    expectTrue(writePng((cfg.textureDir + name).c_str(), replacement), "write GUI page pack fixture");
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    const int textured = (int)mkPrim(6, 0, 1, 0, 0, 1);
    Gif g;
    g.ad({{FRAME_1, 1ull << 16},
          {ZBUF_1, 140ull | (1ull << 32)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 63ull << 16 | 63ull << 48},
          {PRMODECONT, 1},
          {COLCLAMP, 1},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
    // Gameplay: a burst of new world textures (smooth art, distinct per texture) fills the pool with 8x upscales.
    for (uint32_t k = 0; k < 48; ++k) {
        std::vector<uint8_t> raw(64 * 64 * 4);
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x) {
                uint8_t* p = &raw[(y * 64 + x) * 4];
                p[0] = (uint8_t)(x * 3 + k);
                p[1] = (uint8_t)(y * 3 + ((x / 8 + y / 8) % 2) * 20);
                p[2] = (uint8_t)(k * 5);
                p[3] = 0x80;
            }
        const uint32_t tbp = 0x2400 + k * 0x20;
        uploadImage(g, tbp, 1, 0, 0, 0, 64, 64, raw);
        g.ad({{TEXFLUSH, 0}, {TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(tbp, 1, 0, 6, 6, 1, 1, 0, 0, 0, 0, 0)}});
        sprite(g, 0, 0, 32, 32, 128, 128, 128, 128, 0, textured, 0, 0, 64, 64);
    }
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    g.d.clear();
    expectTrue(gs->stats().texUpPending > 4, "world upscales are still queued");
    // The inventory opens: its page texture (exact pack image) is drawn for the first time.
    SceneGuideCamera camera{};
    camera.guiScreen = 21;
    gs->noteSceneCamera(camera);
    std::vector<uint8_t> raw(native.size() * 4);
    std::memcpy(raw.data(), native.data(), raw.size());
    uploadImage(g, 0x2000, 1, 0, 0, 0, 4, 4, raw);
    g.ad({{TEXFLUSH, 0}, {TEX1_1, 0}, {CLAMP_1, 5}, {TEX0_1, mkTex0(0x2000, 1, 0, 2, 2, 1, 1, 0, 0, 0, 0, 0)}});
    sprite(g, 0, 0, 64, 64, 128, 128, 128, 128, 0, textured, 0, 0, 4, 4);
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    Image image;
    expectTrue(gs->captureFramebuffer(0, 1, 0, 64, 64, image), "capture GUI page");
    expectPixel(image, 40 * cfg.scale, 40 * cfg.scale, 0, 255, 0, 1,
                "GUI page pack image on its first draw despite queued world upscales");
}

// Regression: a rejected shifted-row alias is decoded and cached, then
// overlapping GPU writes occur without any RAM write-clock change. TEXFLUSH
// must make the next sample observe those writes. No capture/readVram/vsync
// may intervene between the two samples: a readback would mask the defect.
// The checkedClock-only cache guard failed this fixture; not game-image proof.
static void rtCacheCoherenceDiagnostic(Config cfg) {
    cfg.textureUpscale = 1;
    cfg.textureFilter = 1;
    cfg.smoothMenuHighlight = false;
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto gs = Renderer::create(cfg);
        if (!gs) {
            ++g_fail;
            continue;
        }
        const uint32_t parent = 0x100, tbp = parent * 32 + 9 * 32;
        const uint64_t dst = 8ull << 16, src = parent | (2ull << 16);
        const uint64_t fullScissor = 511ull << 16 | 511ull << 48;
        const int plain = (int)mkPrim(6, 0, 0, 0, 0, 0);
        const int textured = (int)mkPrim(6, 0, 1, 0, 0, 1);
        const bool nonoverlap = mode == 2;
        Gif g;
        g.ad({{FRAME_1, dst},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, fullScissor},
              {PRMODECONT, 1},
              {COLCLAMP, 1},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
              {0x49, 0},
              {0x4a, 0}});
        sprite(g, 0, 0, 128, 64, 0, 0, 0, 128, 0, plain);
        g.ad({{FRAME_1, src}, {SCISSOR_1, 127ull << 16 | 511ull << 48}});
        sprite(g, 0, 0, 128, 512, 200, 0, 0, 128, 0, plain);
        auto sample = [&](float x) {
            g.ad({{FRAME_1, dst},
                  {SCISSOR_1, fullScissor},
                  {TEXFLUSH, 0},
                  {TEX0_1, mkTex0(tbp, 2, 0, 7, 7, 1, 1, 0, 0, 0, 0, 0)},
                  {TEX1_1, 0},
                  {CLAMP_1, 5}});
            // Exercise both the right page and the page carrying into row160.
            sprite(g, x, 8, x + 16, 24, 128, 128, 128, 128, 0, textured, 0, 0, 16, 16);
            sprite(g, x, 32, x + 16, 48, 128, 128, 128, 128, 0, textured, 64, 0, 80, 16);
        };
        sample(8); // first resolve flushes the source and warms the decoded cache
        g.ad({{FRAME_1, src}, {SCISSOR_1, 127ull << 16 | 511ull << 48}});
        sprite(g, 0, nonoverlap ? 384 : 128, 128, nonoverlap ? 416 : 288, 0, 0, 200, 128, 0, plain);
        if (mode == 1) {
            // An unrelated host upload advances only the RAM clock. It must
            // not be necessary for overlapping GPU writes to become visible.
            uploadImage(g, 0x3f00, 1, 0, 0, 0, 1, 1, std::vector<uint8_t>{0, 0, 0, 128});
        }
        sample(40);
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        Image image;
        if (!gs->captureFramebuffer(0, 8, 0, 128, 64, image)) {
            ++g_fail;
            continue;
        }
        std::printf("shifted alias cache coherence: %s (scale %u)\n",
                    mode == 0   ? "overlapping GPU write, no RAM upload"
                    : mode == 1 ? "unrelated RAM-clock control"
                                : "nonoverlapping GPU-write control",
                    cfg.scale);
        const int scale = (int)cfg.scale;
        expectPixel(image, 16 * scale, 16 * scale, 200, 0, 0, 1, "first sample warms cache from red GPU data");
        expectPixel(image, 16 * scale, 40 * scale, 200, 0, 0, 1, "first sample follows GS page-row carry");
        expectPixel(image, 48 * scale, 16 * scale, nonoverlap ? 200 : 0, 0, nonoverlap ? 0 : 200, 1,
                    "cached sample observes overlapping GPU write");
        expectPixel(image, 48 * scale, 40 * scale, nonoverlap ? 200 : 0, 0, nonoverlap ? 0 : 200, 1,
                    "cached carried page observes GPU write");
        expectTrue(gs->stats().rtSamples == 0, "both shifted alias samples use decoded fallback");
        // The texture aliases a dirty target, so it is assembled from the target's GPU copy (no readback) and
        // rebuilt after any later write into that target, including the nonoverlapping control.
        expectTrue(gs->stats().aliasedTextures == 2u, "aliased texture assembled on the GPU, rebuilt after the write");
        expectTrue(gs->stats().textureDecodes == 2u, "rebuild after a write into the aliased target");
    }
}

// Per-frame uploads must not force GPU readbacks or redundant texture rebuilds:
// (a) re-uploading identical texture bytes keeps the decoded texture; changed bytes decode again;
// (b) an image upload into a freshly drawn frame buffer is applied to the GPU target, keeping the rest of it;
// (c) a CLUT load reads back only the CLUT's own blocks, not a dirty target that follows it in memory.
static void uploadCoherenceTest(Config cfg) {
    cfg.textureUpscale = 1;
    cfg.textureFilter = 1;
    cfg.smoothMenuHighlight = false;
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    const int plain = (int)mkPrim(6, 0, 0, 0, 0, 0), textured = (int)mkPrim(6, 0, 1, 0, 0, 1);
    const uint64_t fullScissor = 511ull << 16 | 511ull << 48;
    auto solid = [](uint32_t w, uint32_t h, uint8_t r, uint8_t gg, uint8_t b) {
        std::vector<uint8_t> v((size_t)w * h * 4);
        for (size_t i = 0; i < v.size(); i += 4) {
            v[i] = r;
            v[i + 1] = gg;
            v[i + 2] = b;
            v[i + 3] = 128;
        }
        return v;
    };
    Gif g;
    g.ad({{FRAME_1, 8ull << 16},
          {ZBUF_1, 140ull | (1ull << 32)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, fullScissor},
          {PRMODECONT, 1},
          {COLCLAMP, 1},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
          {0x49, 0},
          {0x4a, 0}});
    sprite(g, 0, 0, 512, 512, 200, 0, 0, 128, 0, plain);
    // (b) 16x16 patch into the drawn frame buffer, then a later draw elsewhere.
    uploadImage(g, 0, 8, 0, 0, 0, 16, 16, solid(16, 16, 0, 200, 0));
    sprite(g, 100, 100, 120, 120, 0, 0, 200, 128, 0, plain);
    // (a) the same texture bytes uploaded twice, then different bytes.
    const uint32_t tbp = 0x2800;
    const auto texDraw = [&](float x) {
        g.ad({{TEXFLUSH, 0}, {TEX0_1, mkTex0(tbp, 1, 0, 4, 4, 1, 1, 0, 0, 0, 0, 0)}, {TEX1_1, 0}, {CLAMP_1, 5}});
        sprite(g, x, 200, x + 16, 216, 128, 128, 128, 128, 0, textured, 0, 0, 16, 16);
    };
    uploadImage(g, tbp, 1, 0, 0, 0, 16, 16, solid(16, 16, 200, 200, 0));
    texDraw(200);
    uploadImage(g, tbp, 1, 0, 0, 0, 16, 16, solid(16, 16, 200, 200, 0));
    texDraw(232);
    uploadImage(g, tbp, 1, 0, 0, 0, 16, 16, solid(16, 16, 0, 200, 200));
    texDraw(264);
    // (c) a dirty target at page 0x100 and a 256-entry CT32 CLUT in the four blocks just before it.
    const uint32_t parent = 0x100, cbp = parent * 32 - 4, t8 = 0x3000;
    g.ad({{FRAME_1, parent | (2ull << 16)}, {SCISSOR_1, 127ull << 16 | 127ull << 48}});
    sprite(g, 0, 0, 128, 128, 40, 40, 40, 128, 0, plain);
    uploadImage(g, cbp, 1, 0, 0, 0, 16, 16, solid(16, 16, 200, 0, 200));
    uploadImage(g, t8, 2, 0x13, 0, 0, 16, 16, std::vector<uint8_t>(16 * 16, 1));
    g.ad({{FRAME_1, 8ull << 16},
          {SCISSOR_1, fullScissor},
          {TEXFLUSH, 0},
          {TEX0_1, mkTex0(t8, 2, 0x13, 4, 4, 1, 1, cbp, 0, 0, 0, 1)},
          {TEX1_1, 0},
          {CLAMP_1, 5}});
    sprite(g, 300, 300, 316, 316, 128, 128, 128, 128, 0, textured, 0, 0, 16, 16);
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    const auto& st = gs->stats();
    std::printf("  upload coherence: decodes %llu revalidated %llu target uploads %llu readbacks %llu\n",
                (unsigned long long)st.textureDecodes, (unsigned long long)st.textureRevalidated,
                (unsigned long long)st.targetUploads, (unsigned long long)st.rtReadbacks);
    expectTrue(st.rtReadbacks == 0, "frame-buffer patch and CLUT load cause no target readback");
    expectTrue(st.targetUploads >= 1, "frame-buffer patch applied to the GPU target");
    expectTrue(st.textureRevalidated == 1, "identical re-upload keeps the decoded texture");
    Image image;
    if (!gs->captureFramebuffer(0, 8, 0, 512, 512, image)) {
        ++g_fail;
        return;
    }
    const int s = (int)cfg.scale;
    expectPixel(image, 8 * s, 8 * s, 0, 200, 0, 1, "uploaded patch visible in the frame buffer");
    expectPixel(image, 40 * s, 8 * s, 200, 0, 0, 1, "frame buffer around the patch keeps the GPU drawing");
    expectPixel(image, 110 * s, 110 * s, 0, 0, 200, 1, "later draw over the target after the patch");
    expectPixel(image, 208 * s, 208 * s, 200, 200, 0, 1, "first texture upload");
    expectPixel(image, 240 * s, 208 * s, 200, 200, 0, 1, "identical re-upload samples the same texture");
    expectPixel(image, 272 * s, 208 * s, 0, 200, 200, 1, "changed texture bytes are visible");
    expectPixel(image, 308 * s, 308 * s, 200, 0, 200, 1, "CLUT next to a dirty target is read correctly");
}

static void rtOverlapTest(Config cfg) {
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    std::printf("overlapping render targets sampled as texture (scale %u)\n", cfg.scale);
    Gif g;
    g.ad({{FRAME_1, 0x20ull | (2ull << 16)},
          {ZBUF_1, 140ull | (1ull << 32)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 127ull << 16 | (127ull << 48)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)},
          {COLCLAMP, 1},
          {PRMODECONT, 1},
          {TEXA, 0x80 | (0x80ull << 32)}});
    g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
    auto fill = [&](uint32_t fbp, uint32_t fbw, float size, uint8_t r, uint8_t gg, uint8_t b) {
        g.ad({{FRAME_1, (uint64_t)fbp | ((uint64_t)fbw << 16)}});
        g.tag(2, true, false, 0, 0, 2, 0x51);
        g.qw(r | ((uint64_t)gg << 32), b | (0x80ull << 32));
        g.qw(xy(0, 0), 0);
        g.qw(r | ((uint64_t)gg << 32), b | (0x80ull << 32));
        g.qw(xy(size, size), 0);
    };
    fill(0x20, 2, 128, 0, 0, 255); // older 128 wide target: blue
    fill(0x28, 2, 128, 255, 0,
         0); // shadow-map like target 8 pages (= 128 rows) further: red, lies inside the older one's memory
    fill(0, 1, 64, 0, 0, 0); // 64x64 destination, black
    g.ad({{TEX1_1, 0},
          {CLAMP_1, 0},
          {TEX0_1, mkTex0(0x28u * 32u, 2, 0, 7, 7, 0, 1, 0, 0, 0, 0, 0)},
          {PRIM, mkPrim(6, 0, 1, 0, 0, 1)}});
    sprite(g, 0, 0, 64, 64, 0x80, 0x80, 0x80, 0x80, 0, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 128, 128);
    gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
    gs->writePriv(0x12000000, 1);
    gs->writePriv(0x12000070, 0ull | (1ull << 9));
    gs->writePriv(0x12000080, (63ull * 4 + 3) << 32 | (63ull << 44) | (3ull << 23));
    gs->vsync(true);
    Image img;
    if (!gs->captureDisplay(img)) {
        ++g_fail;
        std::printf("  capture failed\n");
        return;
    }
    expectPixel(img, 20 * (int)cfg.scale, 20 * (int)cfg.scale, 255, 0, 0, 3,
                "texture comes from the target at its exact base");
}

// HUD clusters (hud_scale / hud_aspect_correct): sprites that overlap form one cluster (found from the previous frame)
// and share one anchor, so an orb's fill and its ring stay together. The ring is centre-anchored (its box straddles the
// middle zone), the fill alone would be left-anchored by its own centre.
static void hudClusterTest(Config cfg) {
    auto gs = Renderer::create(cfg);
    if (!gs) {
        ++g_fail;
        return;
    }
    std::printf("HUD clusters share one anchor (scale %u)\n", cfg.scale);
    gs->setHudScaleX(0.5f);
    gs->setHudSize(1.0f);
    auto frame = [&]() {
        Gif g;
        g.ad({{FRAME_1, 0ull | (8ull << 16)},
              {ZBUF_1, 140ull | (1ull << 32)},
              {XYOFFSET_1, kOfs},
              {SCISSOR_1, 511ull << 16 | (511ull << 48)},
              {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)},
              {COLCLAMP, 1},
              {PRMODECONT, 1},
              {TEXA, 0x80 | (0x80ull << 32)}});
        g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
        sprite(g, 0, 0, 512, 512, 0, 0, 0, 0x80, 0, (int)mkPrim(6, 0, 0, 0, 0, 0)); // full-frame clear: never touched
        sprite(g, 180, 200, 260, 280, 255, 0, 0, 0x80, 0, (int)mkPrim(6, 0, 0, 0, 0, 0)); // ring (red)
        sprite(g, 190, 220, 200, 240, 0, 0, 255, 0x80, 0,
               (int)mkPrim(6, 0, 0, 0, 0, 0)); // fill (blue), inside the ring
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        gs->writePriv(0x12000000, 1);
        gs->writePriv(0x12000070, 0ull | (8ull << 9));
        gs->writePriv(0x12000080, (2559ull << 32) | (511ull << 44) | (4ull << 23));
        gs->vsync(true);
    };
    frame(); // frame 1: the clusters are learned
    frame(); // frame 2: drawn with the clusters
    Image img;
    if (!gs->captureDisplay(img)) {
        ++g_fail;
        std::printf("  capture failed\n");
        return;
    }
    const int s = (int)cfg.scale;
    if (std::getenv("GSR_HUDTEST_DUMP")) {
        std::string row;
        for (int x = 150; x < 270; x += 4) {
            const uint8_t* px = &img.rgba[((size_t)(230 * s) * img.width + (size_t)(x * s)) * 4];
            row += px[0] > 128 ? 'R' : (px[2] > 128 ? 'B' : '.');
        }
        std::printf("  row y=230, x=150..270 step 4: %s  (hudSprites %llu, draws %llu)\n", row.c_str(),
                    (unsigned long long)gs->stats().hudSprites, (unsigned long long)gs->stats().drawCalls);
    }
    // centre anchored, x' = 256 + (x - 256) * 0.5: ring x 218..258, fill x 223..228 (y unchanged)
    expectPixel(img, 225 * s, 230 * s, 0, 0, 255, 3, "fill moved with its ring (centre anchor)");
    expectPixel(img, 220 * s, 230 * s, 255, 0, 0, 3, "ring is squeezed about the same anchor");
    expectPixel(img, 195 * s, 230 * s, 0, 0, 0, 3, "original x range is empty");
    expectPixel(img, 100 * s, 230 * s, 0, 0, 0, 3, "no left-anchored copy of the fill (would sit at x 95..100)");
    expectPixel(img, 10 * s, 10 * s, 0, 0, 0, 3, "full-frame clear untouched");
}

// Replay a world-to-loading boundary before host menu state catches up.
// Loading foreground keeps one centre canvas regardless of gameplay HUD size.
static void loadingCanvasTest(Config cfg) {
    cfg.textureUpscale = 1;
    for (float aspect : {1.0f, 0.75f, 4.0f / 7.0f})
        for (float size : {0.65f, 1.0f, 1.5f})
            for (float offset : {0.0f, 110.0f}) {
                auto gs = Renderer::create(cfg);
                if (!gs) {
                    ++g_fail;
                    continue;
                }
                gs->setHudScaleX(aspect);
                gs->setHudSize(size);
                gs->setNativeMenu(false);
                gs->vsync(false); // setters take effect at the presentation boundary
                SceneGuideCamera world;
                world.flags = SceneGuideCamera::World;
                world.guiScreen = -1;
                gs->noteSceneCamera(world);
                SceneGuideCamera loading;
                loading.sourcePc = 0x43d340u;
                loading.guiScreen = 14;
                loading.flags = 0;
                std::memcpy(&loading.reserved, &offset, sizeof(offset));
                gs->noteSceneCamera(loading);
                Gif g;
                g.ad({{FRAME_1, 8ull << 16},
                      {ZBUF_1, 140ull | (1ull << 32)},
                      {XYOFFSET_1, kOfs},
                      {SCISSOR_1, 511ull << 16 | 511ull << 48},
                      {PRMODECONT, 1},
                      {COLCLAMP, 1},
                      {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 0, 1)}});
                sprite(g, 0, 0, 512, 512, 0, 0, 0, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
                sprite(g, 300, 460, 460, 470, 255, 0, 0, 128, 0, (int)mkPrim(6, 0, 0, 0, 0, 0));
                gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
                Image image;
                if (!gs->captureFramebuffer(0, 8, 0, 512, 512, image)) {
                    ++g_fail;
                    continue;
                }
                const int left = int(256 + (300 - offset - 256) * aspect),
                          right = int(256 + (460 - offset - 256) * aspect), sc = int(cfg.scale);
                expectPixel(image, (left + 2) * sc, 465 * sc, 255, 0, 0, 1, "loading bar fixed left edge");
                expectPixel(image, (right - 2) * sc, 465 * sc, 255, 0, 0, 1, "loading bar fixed right edge");
                expectPixel(image, (left - 2) * sc, 465 * sc, 0, 0, 0, 1, "loading bar no gameplay anchor");
                expectPixel(image, (right + 2) * sc, 465 * sc, 0, 0, 0, 1, "loading bar no gameplay scaling");
            }
}

int main(int argc, char** argv) {
    Config cfg;
    bool alphaOnly = false, rtFilterOnly = false, inventoryAlphaOnly = false, rtShiftedRowOnly = false,
         rtCacheCoherenceOnly = false;
    std::string outDir = GSR_OUT_DIR;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--warp"))
            cfg.forceWarp = true;
        else if (!std::strcmp(argv[i], "--alpha-only"))
            alphaOnly = true;
        else if (!std::strcmp(argv[i], "--rt-filter-only"))
            rtFilterOnly = true;
        else if (!std::strcmp(argv[i], "--inventory-alpha-only"))
            inventoryAlphaOnly = true;
        else if (!std::strcmp(argv[i], "--rt-shifted-row-only"))
            rtShiftedRowOnly = true;
        else if (!std::strcmp(argv[i], "--rt-cache-coherence-only"))
            rtCacheCoherenceOnly = true;
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            cfg.scale = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--log"))
            cfg.logUnsupported = true;
    }
    cfg.vsync = false;
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--save-preview-only")) {
            std::filesystem::create_directories(outDir);
            savePreviewTest(cfg, outDir);
            std::printf("%d checks, %d failed\n", g_checks, g_fail);
            return g_fail ? 1 : 0;
        }
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--inventory-textures-only")) {
            streamedReplacementTest(cfg, outDir);
            guiPagePackFirstFrameTest(cfg, outDir);
            inventoryFontShadowTest(cfg);
            runtimeAtlasTest(cfg);
            std::printf("%d checks, %d failed\n", g_checks, g_fail);
            return g_fail ? 1 : 0;
        }
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--loading-only")) {
            runtimeAtlasTest(cfg);
            loadingCanvasTest(cfg);
            std::printf("%d checks, %d failed\n", g_checks, g_fail);
            return g_fail ? 1 : 0;
        }
    if (alphaOnly) {
        alphaPipelineTest(cfg);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    if (rtFilterOnly) {
        rtFilterDiagnostic(cfg);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    if (inventoryAlphaOnly) {
        inventoryAlphaMaskTest(cfg);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    if (rtShiftedRowOnly) {
        rtShiftedRowTest(cfg);
        rtOverlapTest(cfg);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    if (rtCacheCoherenceOnly) {
        rtCacheCoherenceDiagnostic(cfg);
        std::printf("%d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }
    auto gs = Renderer::create(cfg);
    if (!gs) {
        std::printf("renderer creation failed\n");
        return 2;
    }
    std::printf("adapter: %s  scale %u\n", gs->adapterName().c_str(), cfg.scale);

    const uint32_t FBW = 10, ZBP = 140, CBP = 0x2600, TBP = 0x2700;
    Gif g;
    // --- setup
    g.ad({{FRAME_1, 0ull | ((uint64_t)FBW << 16) | (0ull << 24)},
          {ZBUF_1, (uint64_t)ZBP | (0ull << 24)},
          {XYOFFSET_1, kOfs},
          {SCISSOR_1, 0ull | (639ull << 16) | (0ull << 32) | (447ull << 48)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)},
          {COLCLAMP, 1},
          {PRMODECONT, 1},
          {TEXA, 0x80 | (0x80ull << 32)}});
    // clear (sprite, flat colour) with z = 0
    g.ad({{PRIM, mkPrim(6, 0, 0, 0, 0, 0)}});
    g.tag(2, true, false, 0, 0, 2, 0x51);
    g.qw(32 | (32ull << 32), 64 | (0x80ull << 32));
    g.qw(xy(0, 0), 0);
    g.qw(32 | (32ull << 32), 64 | (0x80ull << 32));
    g.qw(xy(640, 448), 0);

    // --- CLUT (256 x CT32, hardware index quirk: u[swap34(j)] = pal[j]) and an 8-bit texture
    {
        uint32_t pal[256];
        for (int i = 0; i < 256; ++i) {
            const uint32_t r = i, gg = 255 - i, b = (i * 3) & 255, a = (i == 0) ? 0 : 0x80;
            pal[i] = r | (gg << 8) | (b << 16) | (a << 24);
        }
        uint32_t up[256];
        for (int j = 0; j < 256; ++j)
            up[swap34(j)] = pal[j];
        std::vector<uint8_t> bytes(1024);
        std::memcpy(bytes.data(), up, 1024);
        uploadImage(g, CBP, 1, 0, 0, 0, 16, 16, bytes);

        std::vector<uint8_t> tex(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                tex[y * 64 + x] = (((x >> 3) ^ (y >> 3)) & 1) ? 0 : (uint8_t)(((x * 2 + y * 2) & 255) | 1);
        uploadImage(g, TBP, 1, 0x13, 0, 0, 64, 64, tex);
    }

    // --- perspective-correct textured tristrip (T8 + CLUT, bilinear)
    g.ad({{TEX0_1, mkTex0(TBP, 1, 0x13, 6, 6, 1, 0, CBP, 0, 0, 0, 1)},
          {TEX1_1, (1ull << 5) | (1ull << 6)},
          {CLAMP_1, 0},
          {TEST_1, mkTest(1, 6, 0x20, 0, 0, 0, 1, 2)}, // alpha test GREATER 0x20 (kills transparent texels), z GEQUAL
          {PRIM, mkPrim(4, 1, 1, 0, 0, 0)}});
    {
        const float qt = 0.4167f;
        struct V {
            float s, t, q, x, y;
        };
        const V v[4] = {{0, 0, qt, 130, 260}, {1 * qt, 0, qt, 230, 260}, {0, 1, 1, 60, 420}, {1, 1, 1, 300, 420}};
        g.tag(4, true, false, 0, 0, 3, 0x512);
        for (auto& p : v) {
            g.qw(fbits(p.s * (p.q == 1 ? 1 : 1)) | (fbits(p.t * p.q) << 32), fbits(p.q));
            g.qw(0x80 | (0x80ull << 32), 0x80 | (0x80ull << 32));
            g.qw(xy(p.x, p.y), 0x100000);
        }
    }

    // --- DECAL point-sampled sprite with FST uv
    g.ad({{TEX0_1, mkTex0(TBP, 1, 0x13, 6, 6, 1, 1, CBP, 0, 0, 0, 0)}, {TEX1_1, 0}});
    sprite(g, 340, 40, 580, 200, 0x80, 0x80, 0x80, 0x80, 0x200000, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 64, 64);

    // --- alpha blended sprite and additive sprite
    g.ad({{TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)}, {ALPHA_1, mkAlpha(0, 1, 0, 1, 0)}});
    sprite(g, 200, 300, 360, 420, 255, 0, 0, 0x40, 0x300000, (int)mkPrim(6, 0, 0, 0, 1, 0));
    g.ad({{ALPHA_1, mkAlpha(0, 2, 0, 1, 0)}});
    sprite(g, 250, 330, 450, 430, 0, 200, 0, 0x60, 0x310000, (int)mkPrim(6, 0, 0, 0, 1, 0));
    // subtractive: Cd - Cs*As
    g.ad({{ALPHA_1, mkAlpha(2, 0, 0, 1, 0)}});
    sprite(g, 470, 350, 600, 430, 100, 100, 100, 0x80, 0x320000, (int)mkPrim(6, 0, 0, 0, 1, 0));
    // shader-path blend (Cd as A: (Cd - Cs)*FIX + Cs) with FIX 0x40
    g.ad({{ALPHA_1, mkAlpha(1, 0, 2, 0, 0x40)}});
    sprite(g, 480, 220, 600, 260, 255, 255, 0, 0x80, 0x330000, (int)mkPrim(6, 0, 0, 0, 1, 0));

    // --- fog: gouraud triangle
    g.ad({{FOGCOL, 255 | (255ull << 8) | (0ull << 16)}, {PRIM, mkPrim(3, 1, 0, 1, 0, 0)}});
    g.tag(3, true, false, 0, 0, 3, 0x5A1);
    {
        struct V {
            uint8_t r, g, b, f;
            float x, y;
        };
        const V v[3] = {{255, 0, 0, 255, 470, 260}, {0, 255, 0, 0, 630, 260}, {0, 0, 255, 128, 550, 340}};
        for (auto& p : v) {
            g.qw(p.r | ((uint64_t)p.g << 32), p.b | (0x80ull << 32));
            g.qw(0, (uint64_t)p.f << 36); // FOG packed: F at bits 100..107
            g.qw(xy(p.x, p.y), 0x340000);
        }
    }

    // --- framebuffer as a texture (feedback): shrink the screen into the top-left corner
    g.ad({{TEX0_1, mkTex0(0, FBW, 0, 10, 9, 0, 1, 0, 0, 0, 0, 0)},
          {TEX1_1, (1ull << 5) | (1ull << 6)},
          {TEST_1, mkTest(0, 1, 0, 0, 0, 0, 1, 1)}});
    sprite(g, 8, 8, 168, 120, 0x80, 0x80, 0x80, 0x80, 0x400000, (int)mkPrim(6, 0, 1, 0, 0, 1), 0, 0, 640, 448);

    // --- transfers: host->local, local->local, local->host round trip
    {
        std::vector<uint8_t> pat(32 * 16 * 4);
        for (size_t i = 0; i < pat.size(); ++i)
            pat[i] = (uint8_t)(i * 7 + 3);
        uploadImage(g, 0x3000, 1, 0, 3, 5, 32, 16, pat);
        g.ad({{BITBLTBUF, (0x3000ull) | (1ull << 16) | (0ull << 24) | (0x3200ull << 32) | (1ull << 48) | (0ull << 56)},
              {TRXPOS, (3ull) | (5ull << 16) | (10ull << 32) | (2ull << 48)},
              {TRXREG, 32ull | (16ull << 32)},
              {TRXDIR, 2}});
        g.ad({{BITBLTBUF, (0x3200ull) | (1ull << 16)},
              {TRXPOS, (10ull) | (2ull << 16)},
              {TRXREG, 32ull | (16ull << 32)},
              {TRXDIR, 1}});
        gs->submitGifPacket(GifPath::Path3, g.d.data(), (uint32_t)g.d.size());
        std::vector<uint8_t> back(pat.size() + 64);
        const uint32_t n = gs->consumeLocalToHostBytes(back.data(), (uint32_t)back.size());
        expectTrue(n == pat.size() && std::memcmp(back.data(), pat.data(), pat.size()) == 0,
                   "host->local->local->host transfer round trip");
    }

    // --- display
    gs->writePriv(0x12000000, 1);                                          // PMODE EN1
    gs->writePriv(0x12000070, 0ull | ((uint64_t)FBW << 9) | (0ull << 15)); // DISPFB1
    gs->writePriv(0x12000080, 0ull | (639ull * 4 + 3) << 32 | (447ull << 44) | (3ull << 23));
    gs->vsync(true);

    Image img;
    if (!gs->captureDisplay(img)) {
        std::printf("capture failed\n");
        return 2;
    }
    const uint32_t S = cfg.scale;
    auto px = [&](int x, int y) { return std::pair<int, int>(x * S + S / 2, y * S + S / 2); };
    auto E = [&](int x, int y, int r, int gg, int b, int tol, const char* what) {
        auto p = px(x, y);
        expectPixel(img, p.first, p.second, r, gg, b, tol, what);
    };
    E(620, 20, 32, 32, 64, 1, "clear colour");
    E(340, 310, 143, 16, 32, 3, "alpha blend (red a=0x40)");
    E(400, 380, 32, 182, 64, 3, "additive blend (green a=0x60)");
    E(540, 400, 0, 0, 0, 3, "subtractive blend clamps to 0");
    E(540, 240, 255 * 1 / 1, 255 - 0, 0, 255, "shader blend (just must not crash)");
    E(620, 263, 245, 247, 0, 30, "fog towards FOGCOL");
    std::printf("  stats: draws %llu, tris %llu, tex decodes %llu, cache hits %llu, rt samples %llu, readbacks %llu, "
                "hwblend %llu, sbblend %llu\n",
                (unsigned long long)gs->stats().drawCalls, (unsigned long long)gs->stats().triangles,
                (unsigned long long)gs->stats().textureDecodes, (unsigned long long)gs->stats().textureCacheHits,
                (unsigned long long)gs->stats().rtSamples, (unsigned long long)gs->stats().rtReadbacks,
                (unsigned long long)gs->stats().hwBlendDraws, (unsigned long long)gs->stats().shaderBlendDraws);

    const std::string base = outDir + "\\synthetic_scene1";
    writePng((base + ".png").c_str(), img);
    writeBmp((base + ".bmp").c_str(), img);
    std::printf("wrote %s.png\n", base.c_str());

    // --- GS memory coherence: force the GPU render target back into local memory and read a pixel
    gs->flushToMemory();
    const uint32_t mem00 = gs->readVram(0, 0, FBW, 620, 20);
    expectTrue((mem00 & 0xFFFFFF) == (32u | (32u << 8) | (64u << 16)),
               "render target read back into local memory (CT32 swizzle)");

    seamTest(cfg, true, outDir);
    seamTest(cfg, false, outDir);
    fadeTest(cfg, outDir);
    texFormatTests(cfg);
    runtimeAtlasTest(cfg);
    streamedReplacementTest(cfg, outDir);
    guiPagePackFirstFrameTest(cfg, outDir);
    savePreviewTest(cfg, outDir);
    inventoryFontShadowTest(cfg);
    rtAlphaTest(cfg);
    alphaPipelineTest(cfg);
    inventoryAlphaMaskTest(cfg);
    rtShiftedRowTest(cfg);
    rtCacheCoherenceDiagnostic(cfg);
    uploadCoherenceTest(cfg);
    rtOverlapTest(cfg);
    hudClusterTest(cfg);
    loadingCanvasTest(cfg);

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
