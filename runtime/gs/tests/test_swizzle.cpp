// Unit tests for the GS local memory address functions.
#include "gs_mem.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>

#ifdef GSR_HAVE_PS2RECOMP_TABLES
#include "runtime/gs/ps2_gs_psmct16.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include "runtime/gs/ps2_gs_psmt4.h"
#include "runtime/gs/ps2_gs_psmt8.h"
#endif

using namespace gsr;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

static uint32_t A(uint32_t psm, uint32_t bp, uint32_t bw, uint32_t x, uint32_t y, uint32_t* nib = nullptr) {
    return pixelAddr(psm, bp, bw, x, y, nib);
}

static void testKnown() {
    // PSMCT32: 64x32 page, 8x8 blocks, block table 8x4, columns of 8 words
    CHECK(A(PSM_CT32, 0, 1, 0, 0) == 0);
    CHECK(A(PSM_CT32, 0, 1, 1, 0) == 4);
    CHECK(A(PSM_CT32, 0, 1, 0, 1) == 8);
    CHECK(A(PSM_CT32, 0, 1, 1, 1) == 12);
    CHECK(A(PSM_CT32, 0, 1, 2, 0) == 16);
    CHECK(A(PSM_CT32, 0, 1, 0, 2) == 64);
    CHECK(A(PSM_CT32, 0, 1, 7, 7) == 63 * 4);
    CHECK(A(PSM_CT32, 0, 1, 8, 0) == 256);      // block 1
    CHECK(A(PSM_CT32, 0, 1, 0, 8) == 512);      // block 2
    CHECK(A(PSM_CT32, 0, 1, 16, 0) == 4 * 256); // block 4
    CHECK(A(PSM_CT32, 0, 1, 32, 0) == 16 * 256);
    CHECK(A(PSM_CT32, 0, 1, 63, 31) == 8192 - 4);
    CHECK(A(PSM_CT32, 0, 1, 64, 0) == 8192); // next page
    CHECK(A(PSM_CT32, 0, 2, 0, 32) == 2 * 8192);
    CHECK(A(PSM_CT32, 0, 10, 0, 32) == 10 * 8192);
    CHECK(A(PSM_CT32, 32, 10, 0, 0) == 8192); // bp is in 64-word blocks (32 blocks per page)
    CHECK(A(PSM_CT32, 5, 1, 0, 0) == 5 * 256);
    CHECK(A(PSM_CT24, 0, 1, 9, 9) == A(PSM_CT32, 0, 1, 9, 9));
    CHECK(A(PSM_T8H, 3, 2, 17, 5) == A(PSM_CT32, 3, 2, 17, 5));
    // PSMZ32 uses the Z block table
    CHECK(A(PSM_Z32, 0, 1, 0, 0) == 24 * 256);
    CHECK(A(PSM_Z32, 0, 1, 32, 0) == 8 * 256);
    CHECK(A(PSM_Z32, 0, 1, 0, 16) == 16 * 256);
    // PSMCT16: 64x64 page, 16x8 blocks
    CHECK(A(PSM_CT16, 0, 1, 0, 0) == 0);
    CHECK(A(PSM_CT16, 0, 1, 1, 0) == 4);
    CHECK(A(PSM_CT16, 0, 1, 0, 1) == 8);
    CHECK(A(PSM_CT16, 0, 1, 8, 0) == 2);
    CHECK(A(PSM_CT16, 0, 1, 0, 2) == 64);
    CHECK(A(PSM_CT16, 0, 1, 16, 0) == 2 * 256);
    CHECK(A(PSM_CT16, 0, 1, 0, 8) == 1 * 256);
    CHECK(A(PSM_CT16S, 0, 1, 16, 0) == 2 * 256);
    CHECK(A(PSM_CT16S, 0, 1, 32, 0) == 16 * 256);
    CHECK(A(PSM_CT16, 0, 1, 32, 0) == 8 * 256);
    // PSMT8: 128x64 page, 16x16 blocks
    CHECK(A(PSM_T8, 0, 2, 0, 0) == 0);
    CHECK(A(PSM_T8, 0, 2, 1, 0) == 4);
    CHECK(A(PSM_T8, 0, 2, 0, 1) == 8);
    CHECK(A(PSM_T8, 0, 2, 8, 0) == 2);
    CHECK(A(PSM_T8, 0, 2, 16, 0) == 256);
    CHECK(A(PSM_T8, 0, 2, 0, 16) == 2 * 256);
    CHECK(A(PSM_T8, 0, 2, 128, 0) == 8192);
    CHECK(A(PSM_T8, 0, 2, 0, 64) == 8192 && A(PSM_T8, 0, 4, 0, 64) == 2 * 8192);
    // PSMT4: 128x128 page, 32x16 blocks, nibble addressing
    uint32_t n = 9;
    CHECK(A(PSM_T4, 0, 2, 0, 0, &n) == 0 && n == 0);
    CHECK(A(PSM_T4, 0, 2, 1, 0, &n) == 4 && n == 0);
    CHECK(A(PSM_T4, 0, 2, 2, 0, &n) == 16 && n == 0);
    CHECK(A(PSM_T4, 0, 2, 0, 1, &n) == 8 && n == 0);
    CHECK(A(PSM_T4, 0, 2, 4, 2, &n) == 0 && n == 1);
    CHECK(A(PSM_T4, 0, 2, 8, 0, &n) == 1 && n == 0);
    CHECK(A(PSM_T4, 0, 2, 32, 0, &n) == 2 * 256);
    CHECK(A(PSM_T4, 0, 2, 0, 16, &n) == 1 * 256);
    CHECK(A(PSM_T4, 0, 2, 128, 0, &n) == 8192);
}

// every pixel of a region must map to a distinct storage cell, all inside the region's pages, covering them fully
static void testBijection(uint32_t psm, uint32_t bw, uint32_t pagesTall) {
    const PsmGeom g = geomOf(psm);
    const uint32_t pagesWide = (g.pageW == 128) ? bw / 2 : bw; // bw counts 64-pixel units
    const uint32_t W = pagesWide * g.pageW, H = pagesTall * g.pageH;
    std::set<std::pair<uint32_t, uint32_t>> seen;
    bool inRange = true;
    const uint32_t bytes = pagesWide * pagesTall * 8192;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            uint32_t nib = 0;
            const uint32_t a = pixelAddr(psm, 0, bw, x, y, &nib);
            if (a >= bytes)
                inRange = false;
            seen.insert({a, nib});
        }
    CHECK(inRange);
    CHECK(seen.size() == (size_t)W * H);
    CHECK((uint64_t)W * H * g.bits == (uint64_t)bytes * 8); // pixel count exactly fills the pages
}

static void testRoundTrip() {
    LocalMemory m;
    uint32_t seed = 1234;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    const uint32_t psms[] = {PSM_CT32, PSM_CT24, PSM_CT16, PSM_CT16S, PSM_T8,  PSM_T4,  PSM_T8H,
                             PSM_T4HL, PSM_T4HH, PSM_Z32,  PSM_Z24,   PSM_Z16, PSM_Z16S};
    for (uint32_t psm : psms) {
        const uint32_t mask = (psm == PSM_CT32 || psm == PSM_Z32)                                          ? 0xFFFFFFFFu
                              : (psm == PSM_CT24 || psm == PSM_Z24)                                        ? 0xFFFFFFu
                              : (psm == PSM_CT16 || psm == PSM_CT16S || psm == PSM_Z16 || psm == PSM_Z16S) ? 0xFFFFu
                              : (psm == PSM_T8 || psm == PSM_T8H)                                          ? 0xFFu
                                                                                                           : 0xFu;
        std::vector<uint32_t> ref(128 * 128, 0xFFFFFFFFu);
        for (int i = 0; i < 20000; ++i) {
            const uint32_t x = rnd() % 128, y = rnd() % 128, v = rnd() & mask;
            m.write(psm, 64, 2, x, y, v);
            ref[y * 128 + x] = v;
        }
        bool ok = true;
        for (uint32_t y = 0; y < 128; ++y)
            for (uint32_t x = 0; x < 128; ++x)
                if (ref[y * 128 + x] != 0xFFFFFFFFu && m.read(psm, 64, 2, x, y) != ref[y * 128 + x])
                    ok = false;
        CHECK(ok);
        std::memset(m.data(), 0, kVramSize);
    }
}

#ifdef GSR_HAVE_PS2RECOMP_TABLES
static void testAgainstPs2Recomp() {
    uint32_t seed = 99;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    bool ok32 = true, ok16 = true, ok16s = true, okz16 = true, ok8 = true, ok4 = true;
    for (int i = 0; i < 200000; ++i) {
        const uint32_t bp = (rnd() % 40) * 32u + (rnd() % 3 ? 0 : rnd() % 32);
        const uint32_t bw = 1 + rnd() % 10;
        const uint32_t x = rnd() % (bw * 64), y = rnd() % 256;
        if (GSPSMCT32::addrPSMCT32(bp, bw, x, y) != pixelAddr(PSM_CT32, bp, bw, x, y))
            ok32 = false;
        if (GSPSMCT16::addrPSMCT16(bp, bw, x, y) != pixelAddr(PSM_CT16, bp, bw, x, y))
            ok16 = false;
        if (GSPSMCT16::addrPSMCT16S(bp, bw, x, y) != pixelAddr(PSM_CT16S, bp, bw, x, y))
            ok16s = false;
        if (GSPSMCT16::addrPSMZ16(bp, bw, x, y) != pixelAddr(PSM_Z16, bp, bw, x, y))
            okz16 = false;
        if (bw >= 2) {
            const uint32_t x8 = rnd() % (bw / 2 * 128);
            if (GSPSMT8::addrPSMT8(bp, bw, x8, y) != pixelAddr(PSM_T8, bp, bw, x8, y))
                ok8 = false;
            uint32_t nib = 0;
            const uint32_t nibIdx = GSPSMT4::addrPSMT4(bp, bw, x8, y);
            const uint32_t a = pixelAddr(PSM_T4, bp, bw, x8, y, &nib);
            if (nibIdx != a * 2 + nib)
                ok4 = false;
        }
    }
    CHECK(ok32);
    CHECK(ok16);
    CHECK(ok16s);
    CHECK(okz16);
    CHECK(ok8);
    CHECK(ok4);
    std::printf("cross-checked against PS2Recomp tables\n");
}
#endif

int main() {
    testKnown();
    const uint32_t psms[] = {PSM_CT32, PSM_CT16, PSM_CT16S, PSM_T8, PSM_T4, PSM_Z32, PSM_Z16, PSM_Z16S};
    for (uint32_t p : psms)
        testBijection(p, 2, 2);
    testRoundTrip();
#ifdef GSR_HAVE_PS2RECOMP_TABLES
    testAgainstPs2Recomp();
#endif
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
