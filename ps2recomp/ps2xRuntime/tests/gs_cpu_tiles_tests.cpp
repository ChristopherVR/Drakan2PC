#include "gs/gs_host_scanout.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "gs/gs_cpu_tile_workers.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
#include <xmmintrin.h>
#endif

namespace {
void threads(const char* value) {
#if defined(DRAKAN_TEST_RASTER_THREADS)
    if (value[0] == '8')
        value = DRAKAN_TEST_RASTER_THREADS;
#else
    if (value[0] == '8')
        if (const char* requested = std::getenv("PS2X_CPU_TEST_THREADS"))
            value = requested;
#endif
#if defined(_WIN32)
    _putenv_s("PS2X_CPU_RASTER_THREADS", value);
#else
    setenv("PS2X_CPU_RASTER_THREADS", value, 1);
#endif
}
uint32_t randomWord() {
    static uint32_t state = 0x753ea91;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
void same(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, unsigned step) {
    const auto mismatch = std::mismatch(a.begin(), a.end(), b.begin());
    if (mismatch.first != a.end()) {
        std::printf("VRAM differs at step %u byte %zu: serial=%02x tiled=%02x\n", step,
                    static_cast<size_t>(mismatch.first - a.begin()), *mismatch.first, *mismatch.second);
        std::fflush(stdout);
        std::abort();
    }
}
GSPrimitiveBatch batch(unsigned step) {
    GSPrimitiveBatch batch{};
    const bool sprite = (step & 1) != 0;
    batch.vertexCount = sprite ? 2 : 3;
    auto& s = batch.state;
    s.prim.type = sprite ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE;
    s.prim.iip = s.prim.tme = s.prim.fge = s.prim.abe = true;
    s.prim.fst = (step % 3) != 0;
    s.prim.tme = (step % 7) != 0;
    s.context.frame = {128, 4, GS_PSM_CT32, (step % 4 == 0) ? 0xff000000u : 0u};
    s.context.zbuf = {320, GS_PSM_Z32, (step % 3) == 0};
    s.context.scissor.x0 = 0;
    s.context.scissor.x1 = 255;
    s.context.scissor.y0 = 0;
    s.context.scissor.y1 = 127;
    s.context.test = (1ull << 16) | (uint64_t(1 + step % 3) << 17);
    if (step % 3 == 1)
        s.context.test |= 1 | (6ull << 1) | (96ull << 4) | (uint64_t(step % 4) << 12);
    if (step % 5 == 1)
        s.context.test |= 1ull << 14;
    s.context.alpha = 0x44 | (128ull << 32);
    s.context.fba = step & 1;
    constexpr uint8_t formats[] = {GS_PSM_CT32, GS_PSM_CT24, GS_PSM_CT16, GS_PSM_CT16S, GS_PSM_T8,
                                   GS_PSM_T4,   GS_PSM_T8H,  GS_PSM_T4HL, GS_PSM_T4HH,  GS_PSM_Z32,
                                   GS_PSM_Z24,  GS_PSM_Z16,  GS_PSM_Z16S};
    auto& t = s.context.tex0;
    t.psm = formats[step % std::size(formats)];
    t.tbp0 = 64 + step % 4;
    t.tbw = 2;
    t.tw = t.th = 7;
    t.tfx = step % 4;
    t.tcc = step & 1;
    t.cbp = 640;
    t.cpsm = GS_PSM_CT32;
    t.cld = 1;
    s.context.clamp = step % 2 ? 5 : 0;
    s.textureWidth = s.textureHeight = 128;
    s.linearFilter = (step % 4) != 0;
    s.texa.ta0 = 32;
    s.texa.ta1 = 128;
    s.texa.aem = (step % 3) == 0;
    s.fogR = 30;
    s.fogG = 60;
    s.fogB = 90;
    for (unsigned i = 0; i < 3; ++i) {
        auto& v = batch.vertices[i];
        const uint32_t color = randomWord();
        v.r = color;
        v.g = color >> 8;
        v.b = color >> 16;
        v.a = color >> 24;
        v.z = double(randomWord());
        v.fog = static_cast<uint8_t>(i * 90);
        v.x = i == 1 ? 255.0f : 0.0f;
        v.y = (i == 2 || (sprite && i == 1)) ? 127.0f : 0.0f;
        v.u = i == 1 ? 2048 : 0;
        v.v = v.y > 0 ? 2048 : 0;
        v.s = i == 1 ? 1.0f : 0.0f;
        v.t = v.y > 0 ? 1.0f : 0.0f;
        v.q = 0.5f + i * 0.5f;
    }
    return batch;
}
void rejectedDepthCache(bool left) {
    std::vector<uint8_t> a(4 * 1024 * 1024);
    for (auto& value : a)
        value = static_cast<uint8_t>(randomWord());
    auto b = a;
    threads("1");
    GSCpuBackend serial;
    serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
    threads("8");
    GSCpuBackend tiled;
    tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
    auto draw = batch(2);
    auto& state = draw.state;
    state.prim.tme = state.prim.fst = true;
    state.prim.abe = state.prim.fge = state.prim.iip = false;
    state.context.frame.fbmsk = 0;
    state.context.test = (1ull << 16) | (3ull << 17);
    state.context.zbuf.zmask = true;
    state.context.tex0.psm = GS_PSM_CT32;
    state.context.tex0.tbp0 = 64;
    state.context.tex0.tbw = 4;
    state.context.tex0.tw = state.context.tex0.th = 8;
    state.textureWidth = state.textureHeight = 256;
    state.context.clamp = 0;
    state.linearFilter = false;
    state.context.tex0.tfx = 1;
    state.context.tex0.tcc = true;
    const unsigned xs[] = {0, 128, left ? 0u : 128u}, ys[] = {0, 0, 128};
    for (unsigned i = 0; i < 3; ++i) {
        draw.vertices[i].x = float(xs[i]);
        draw.vertices[i].y = float(ys[i]);
        draw.vertices[i].u = uint16_t(xs[i] * 16);
        draw.vertices[i].v = uint16_t(ys[i] * 16);
        draw.vertices[i].z = 0;
    }
    const auto& z = state.context.zbuf;
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 128; ++x) {
            serial.WriteVram(z.psm, z.zbp * 32, 4, x, y, UINT32_MAX);
            tiled.WriteVram(z.psm, z.zbp * 32, 4, x, y, UINT32_MAX);
        }
    serial.Submit(draw);
    tiled.Submit(draw);
    serial.Submit(draw);
    tiled.Submit(draw);
    serial.Flush();
    tiled.Flush();
    same(a, b, 500);
    const uint32_t retained = serial.ReadVram(GS_PSM_CT32, 64, 4, left ? 0 : 127, 127);
    // Every triangle pixel failed Z. Its final texture sample must still
    // leave the original page cached, even after writes change that page.
    for (unsigned y = 0; y < 256; ++y)
        for (unsigned x = 0; x < 256; ++x) {
            serial.WriteVram(GS_PSM_CT32, 64, 4, x, y, 0x76543210);
            tiled.WriteVram(GS_PSM_CT32, 64, 4, x, y, 0x76543210);
        }
    draw.state.prim.type = GS_PRIM_SPRITE;
    draw.state.context.test = 1ull << 17;
    draw.vertexCount = 2;
    for (unsigned i = 0; i < 2; ++i) {
        draw.vertices[i].x = float(i);
        draw.vertices[i].y = float(i);
        draw.vertices[i].u = left ? 0 : 127 * 16;
        draw.vertices[i].v = 127 * 16;
    }
    serial.Submit(draw);
    tiled.Submit(draw);
    serial.Flush();
    tiled.Flush();
    same(a, b, 501);
    assert(serial.ReadVram(GS_PSM_CT32, state.context.frame.fbp * 32, 4, 0, 0) == retained);
}
void paletteCacheOrdering(bool overlap, bool invalidate = false) {
    std::vector<uint8_t> a(4 * 1024 * 1024);
    for (auto& value : a)
        value = static_cast<uint8_t>(randomWord());
    auto b = a;
    threads("1");
    GSCpuBackend serial;
    serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
    threads("8");
    GSCpuBackend tiled;
    tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
    auto draw = batch(4);
    draw.state.prim.type = GS_PRIM_SPRITE;
    draw.vertexCount = 2;
    draw.state.prim.abe = draw.state.prim.fge = false;
    draw.state.context.test = 1ull << 17;
    draw.state.context.frame.fbmsk = 0;
    draw.state.context.zbuf.zmask = true;
    draw.vertices[0].x = draw.vertices[0].y = 0;
    draw.vertices[1].x = draw.vertices[1].y = 127;
    auto palette = draw.state.context.tex0;
    serial.LoadClut(palette, draw.state.texclut);
    tiled.LoadClut(palette, draw.state.texclut);
    const auto count = gs_cpu::tileDispatches.load();
    for (unsigned i = 0; i < 2; ++i) {
        serial.Submit(draw);
        tiled.Submit(draw);
    }
    palette.cbp = overlap ? draw.state.context.frame.fbp * 32 : palette.cbp + 32;
    serial.LoadClut(palette, draw.state.texclut);
    tiled.LoadClut(palette, draw.state.texclut);
    assert(gs_cpu::tileDispatches.load() == count + (overlap ? 1 : 0));
    if (invalidate) {
        serial.TextureFlush();
        tiled.TextureFlush();
    }
    // A later textured command with no covered pixels must not displace the
    // last real palette read from the final retained-cache observation.
    auto empty = draw;
    empty.state.prim.type = GS_PRIM_TRIANGLE;
    empty.vertexCount = 3;
    for (auto& vertex : empty.vertices)
        vertex.x = vertex.y = 0;
    serial.Submit(empty);
    tiled.Submit(empty);
    serial.Flush();
    tiled.Flush();
    same(a, b, 510);
    const auto retained = serial.ReadVram(GS_PSM_CT32, palette.cbp, 1, 15, 15);
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x) {
            serial.WriteVram(GS_PSM_CT32, palette.cbp, 1, x, y, 0x12345678);
            tiled.WriteVram(GS_PSM_CT32, palette.cbp, 1, x, y, 0x12345678);
        }
    auto& state = draw.state;
    state.context.tex0.psm = GS_PSM_CT32;
    state.context.tex0.tbp0 = palette.cbp;
    state.context.tex0.tbw = 1;
    state.context.tex0.tcc = true;
    state.context.tex0.tfx = 1;
    state.context.tex0.tw = state.context.tex0.th = 4;
    state.textureWidth = state.textureHeight = 16;
    state.linearFilter = false;
    for (unsigned i = 0; i < 2; ++i) {
        draw.vertices[i].x = draw.vertices[i].y = float(i);
        draw.vertices[i].u = draw.vertices[i].v = 15 * 16;
    }
    serial.Submit(draw);
    tiled.Submit(draw);
    serial.Flush();
    tiled.Flush();
    same(a, b, 511);
    assert(serial.ReadVram(GS_PSM_CT32, state.context.frame.fbp * 32, 4, 0, 0) == (invalidate ? 0x12345678 : retained));
}
void displayComposition() {
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    for (auto& byte : memory)
        byte = static_cast<uint8_t>(randomWord());
    threads("1");
    GSCpuBackend backend;
    backend.Initialize(memory.data(), static_cast<uint32_t>(memory.size()));
    for (unsigned i = 0; i < 32; ++i) {
        const bool fixed = i & 1, background = (i >> 2) & 1;
        const unsigned alpha = (i * 17) & 255;
        GSPresentationRequest request{};
        request.pmode = 3 | (uint64_t(i & 1) << 5) | (uint64_t((i >> 1) & 1) << 6) | (uint64_t(background) << 7) |
                        (uint64_t(alpha) << 8);
        request.bgcolor = 0xcc8844;
        request.dispfb1 = 4 | (4ull << 9);
        request.dispfb2 = 260 | (4ull << 9);
        request.display1 = (126ull << 32) | (94ull << 44);
        request.display2 = (190ull << 32) | (126ull << 44);
        const auto image = backend.Present(request);
        assert(image.width == 191 && image.height == 127);
        for (unsigned y = 0; y < image.height; ++y)
            for (unsigned x = 0; x < image.width; ++x) {
                const auto destination = background ? 0xcc8844u : GSMem::ReadCT32(memory.data(), 260 * 32, 4, x, y);
                uint32_t expected = destination | 0xff000000u;
                if (x < 127 && y < 95) {
                    const auto source = GSMem::ReadCT32(memory.data(), 4 * 32, 4, x, y);
                    const int factor = fixed ? alpha : std::min(255u, (source >> 24) * 2);
                    expected = 0xff000000u;
                    for (unsigned shift = 0; shift < 24; shift += 8) {
                        const int src = (source >> shift) & 255, dst = (destination >> shift) & 255;
                        expected |= uint32_t(std::clamp(dst + ((src - dst) * factor) / 255, 0, 255)) << shift;
                    }
                }
                uint32_t actual;
                std::memcpy(&actual, image.pixels.data() + (y * 640 + x) * 4, sizeof(actual));
                assert(actual == expected);
            }
    }
}

void completedFrameScanout() {
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    threads("1");
    GSCpuBackend backend;
    backend.Initialize(memory.data(), static_cast<uint32_t>(memory.size()));
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x)
            backend.WriteVram(GS_PSM_CT32, 4 * 32, 1, x, y, 0x80000000u | ((y + 1) * 3) | (x << 8));
    const auto originalMemory = memory;
    GSPresentationRequest request{};
    request.pmode = 1;
    request.smode2 = 1;
    request.dispfb1 = 4 | (1ull << 9);
    request.display1 = (63ull << 32) | (63ull << 44);
    const auto evenField = backend.Present(request);
    request.vsyncTick = 1;
    const auto oddField = backend.Present(request);
    assert(evenField && oddField && evenField.pixels != oddField.pixels);
    auto progressive = gs_cpu::completedFrameScanout(request);
    assert(request.smode2 == 1 && progressive.smode2 == 0);
    const auto fullOdd = backend.Present(progressive);
    progressive.vsyncTick = 0;
    const auto fullEven = backend.Present(progressive);
    assert(fullEven && fullEven.width == 64 && fullEven.height == 64);
    assert(fullEven.pixels == fullOdd.pixels);
    assert(fullEven.pixels != evenField.pixels && fullEven.pixels != oddField.pixels);
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            uint32_t color;
            std::memcpy(&color, fullEven.pixels.data() + (y * 640 + x) * 4, sizeof(color));
            assert((color & 0xffffffu) == ((y + 1) * 3 | (x << 8)));
        }
    assert(memory == originalMemory); // Presentation never changes guest VRAM.
}

void emptySprites() {
    for (const char* count : {"1", "8"}) {
        threads(count);
        std::vector<uint8_t> memory(GSMem::MEMORY_SIZE, 0x39);
        const auto initial = memory;
        GSCpuBackend backend;
        backend.Initialize(memory.data(), unsigned(memory.size()));
        auto draw = batch(1);
        draw.state.prim.tme = draw.state.prim.abe = draw.state.prim.fge = false;
        draw.state.context.test = 1u << 17;
        draw.state.context.frame.fbmsk = 0;
        draw.vertices[0].x = draw.vertices[0].y = 12;
        draw.vertices[1].x = 12;
        draw.vertices[1].y = 48;
        backend.Submit(draw);
        draw.vertices[1].x = 48;
        draw.vertices[1].y = 12;
        backend.Submit(draw);
        draw.vertices[1].x = 12;
        backend.Submit(draw);
        backend.Flush();
        assert(memory == initial);
        // A real half-pixel rectangle still contains a pixel centre.
        draw.vertices[0].x = 12.25f;
        draw.vertices[1].x = 12.75f;
        draw.vertices[1].y = 48;
        backend.Submit(draw);
        backend.Flush();
        assert(memory != initial);
    }
    std::puts("Empty sprite rectangles leave VRAM untouched; real subpixel rectangles retain coverage");
}
void progressiveFlickerBypass() {
    std::vector<uint8_t> memory(GSMem::MEMORY_SIZE);
    threads("1");
    GSCpuBackend backend;
    backend.Initialize(memory.data(), unsigned(memory.size()));
    for (unsigned y = 0; y < 72; ++y)
        for (unsigned x = 0; x < 64; ++x)
            backend.WriteVram(GS_PSM_CT32, 4 * 32, 1, x, y, 0x80000000u | ((y * 17) & 255) | (x << 8));
    const auto original = memory;
    for (const int offset : {-2, -1, 0, 1, 2}) {
        GSPresentationRequest request{};
        request.pmode = 3 | 32 | (128u << 8);
        request.smode2 = 1;
        request.dispfb1 = 4 | (1ull << 9) | (4ull << 43);
        request.dispfb2 = 4 | (1ull << 9) | (uint64_t(4 + offset) << 43);
        request.display1 = request.display2 = (63ull << 32) | (63ull << 44);
        request.hasPreferredSource = true;
        request.preferredDestFbp = 4;
        request.preferredSource = {64, 1, GS_PSM_CT32, 0};
        gs_cpu::setFlickerFilter(false);
        const auto sharpRequest = gs_cpu::completedFrameScanout(request);
        assert((sharpRequest.pmode & 3) == 1 && sharpRequest.smode2 == 0);
        assert((request.pmode & 3) == 3 && request.smode2 == 1);
        assert(request.hasPreferredSource && !sharpRequest.hasPreferredSource);
        auto reference = request;
        reference.hasPreferredSource = false;
        reference.smode2 = 0;
        reference.pmode &= ~2ull;
        const auto sharp = backend.Present(sharpRequest);
        assert(sharp.pixels == backend.Present(reference).pixels);
        gs_cpu::setFlickerFilter(true);
        const auto filteredRequest = gs_cpu::completedFrameScanout(request);
        assert(filteredRequest.pmode == request.pmode && filteredRequest.smode2 == 0);
        reference.pmode = request.pmode;
        const auto filtered = backend.Present(filteredRequest);
        assert(filtered.pixels == backend.Present(reference).pixels);
        if (offset)
            assert(filtered.pixels != sharp.pixels);
    }
    gs_cpu::setFlickerFilter(false);
    GSPresentationRequest request{};
    request.pmode = 3 | 32 | (128u << 8);
    request.dispfb1 = 4 | (1ull << 9);
    request.dispfb2 = request.dispfb1 | (1ull << 43);
    request.display1 = request.display2 = (63ull << 32) | (63ull << 44);
    for (unsigned mismatch = 0; mismatch < 7; ++mismatch) {
        auto different = request;
        switch (mismatch) {
            case 0:
                different.dispfb2 ^= 1;
                break; // framebuffer
            case 1:
                different.dispfb2 ^= 1ull << 9;
                break; // pitch
            case 2:
                different.dispfb2 ^= 1ull << 15;
                break; // format
            case 3:
                different.dispfb2 ^= 1ull << 32;
                break; // horizontal read origin
            case 4:
                different.dispfb2 = request.dispfb1 | (3ull << 43);
                break;
            case 5:
                different.display2 ^= 32ull << 32;
                break; // visible width
            case 6:
                different.pmode &= ~32ull;
                break; // per-pixel alpha composition
        }
        assert(gs_cpu::completedFrameScanout(different).pmode == different.pmode);
    }
    assert(memory == original);
    std::puts("Progressive anti-flicker bypass matches single-source output; opt-in blending and unrelated CRTs remain "
              "exact");
}
void identicalDisplaySources() {
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    for (unsigned i = 0; i < memory.size() / 2; ++i)
        memory[i] = memory[i + memory.size() / 2] = static_cast<uint8_t>(randomWord());
    threads("1");
    GSCpuBackend backend;
    backend.Initialize(memory.data(), static_cast<uint32_t>(memory.size()));
    constexpr uint8_t formats[] = {GS_PSM_CT32, GS_PSM_CT24, GS_PSM_CT16, GS_PSM_CT16S};
    for (unsigned i = 0; i < 64; ++i) {
        GSPresentationRequest request{};
        request.pmode = 3 | (uint64_t(i & 3) << 5) | (uint64_t(i * 37 & 255) << 8);
        request.smode2 = i % 3;
        request.vsyncTick = i & 1;
        const uint64_t source =
            4 | (4ull << 9) | (uint64_t(formats[i % 4]) << 15) | (uint64_t(i % 17) << 32) | (uint64_t(i % 31) << 43);
        request.dispfb1 = request.dispfb2 = source;
        request.display1 = request.display2 = (126ull << 32) | (62ull << 44);
        const auto sameSource = backend.Present(request);
        request.dispfb2 = source + 256; // identical bytes, distinct CRT address: full blend path
        const auto separateSources = backend.Present(request);
        assert(sameSource.width == separateSources.width && sameSource.height == separateSources.height);
        assert(!sameSource.pixels.empty() && sameSource.pixels == separateSources.pixels);
    }
}

void colorReadbacks() {
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    for (auto& byte : memory)
        byte = static_cast<uint8_t>(randomWord());
    for (unsigned i = 0; i < 512; ++i) {
        const unsigned bp = randomWord() & 16383, bw = 1 + i % 9;
        const unsigned x = randomWord() & 2047, y = randomWord() & 4095;
        const unsigned count = 1 + randomWord() % 1025;
        const bool preserve = i & 1;
        std::vector<uint8_t> result(count * 4 + 3, 0x7b);
        GSMem::ReadColor32Row(memory.data(), bp, bw, x, y, result.data() + 1, count, preserve);
        for (unsigned j = 0; j < count; ++j) {
            const uint32_t expected = GSMem::ReadCT32(memory.data(), bp, bw, x + j, y) | (preserve ? 0u : 0xff000000u);
            uint32_t actual;
            std::memcpy(&actual, result.data() + 1 + j * 4, sizeof(actual));
            assert(actual == expected);
        }
        assert(result.front() == 0x7b && result[result.size() - 2] == 0x7b && result.back() == 0x7b);
    }
}

void indexedUploads() {
    std::vector<uint8_t> expected(4 * 1024 * 1024, 0xa7), actual = expected;
    threads("1");
    GSCpuBackend backend;
    backend.Initialize(actual.data(), static_cast<uint32_t>(actual.size()));
    for (unsigned i = 0; i < 128; ++i) {
        GSTransferCommand transfer{};
        transfer.direction = 0;
        transfer.bitbltbuf.dbp = randomWord() & 16383;
        transfer.bitbltbuf.dbw = i % 9;
        transfer.bitbltbuf.dpsm = GS_PSM_T8;
        transfer.trxpos.dsax = randomWord() & 2047;
        transfer.trxpos.dsay = randomWord() & 2047;
        transfer.trxreg.rrw = 1 + randomWord() % 513;
        transfer.trxreg.rrh = 1 + i % 5;
        const unsigned total = transfer.trxreg.rrw * transfer.trxreg.rrh;
        std::vector<uint8_t> bytes(total + 9);
        for (auto& byte : bytes)
            byte = static_cast<uint8_t>(randomWord());
        for (unsigned j = 0; j < total; ++j)
            GSMem::WriteP8(expected.data(), transfer.bitbltbuf.dbp, std::max(1u, unsigned(transfer.bitbltbuf.dbw)),
                           transfer.trxpos.dsax + j % transfer.trxreg.rrw,
                           transfer.trxpos.dsay + j / transfer.trxreg.rrw, bytes[j]);
        backend.BeginTransfer(transfer);
        unsigned offset = 0;
        while (offset < bytes.size()) {
            const unsigned count = std::min<unsigned>(1 + randomWord() % 317, bytes.size() - offset);
            backend.UploadImage(bytes.data() + offset, count);
            offset += count;
        }
        const auto snapshot = backend.GetTransferSnapshot();
        assert(snapshot.direction == 3 && snapshot.copiedPixels == total);
        assert(snapshot.x == transfer.trxpos.dsax + transfer.trxreg.rrw - 1);
        assert(snapshot.y == transfer.trxpos.dsay + transfer.trxreg.rrh - 1);
        same(expected, actual, 700 + i);
    }
}

void unusedDepthSource() {
    std::vector<uint8_t> a(4 * 1024 * 1024);
    for (auto& byte : a)
        byte = static_cast<uint8_t>(randomWord());
    auto b = a;
    threads("1");
    GSCpuBackend serial;
    serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
    threads("8");
    GSCpuBackend tiled;
    tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
    auto draw = batch(2);
    draw.state.context.test = 0x3000d;
    draw.state.context.zbuf = {2, GS_PSM_Z32, true};
    draw.state.context.tex0.tbp0 = 64; // same pages as unused depth storage
    draw.state.context.tex0.psm = GS_PSM_CT32;
    draw.state.context.frame.fbmsk = 0;
    const auto before = gs_cpu::tileDispatches.load();
    for (unsigned i = 0; i < 2; ++i) {
        serial.Submit(draw);
        tiled.Submit(draw);
    }
    serial.Flush();
    tiled.Flush();
    same(a, b, 900);
    assert(gs_cpu::tileDispatches.load() > before);
}

void commonFragmentCases(bool feedback, bool specialized = false) {
    std::vector<uint8_t> a(4 * 1024 * 1024);
    for (auto& value : a)
        value = static_cast<uint8_t>(randomWord());
    auto b = a;
    threads("1");
    GSCpuBackend serial;
    serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
    threads("8");
    GSCpuBackend tiled;
    tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
    for (unsigned i = 0; i < 52; ++i) {
        auto draw = batch(i);
        auto& state = draw.state;
        state.prim.type = GS_PRIM_TRIANGLE;
        draw.vertexCount = 3;
        state.prim.tme = state.prim.abe = true;
        state.prim.iip = i % 5 != 0;
        state.prim.fge = i % 7 != 0;
        state.pabe = false;
        state.context.frame = {128, 4, GS_PSM_CT32, 0};
        state.context.zbuf = {320, GS_PSM_Z32, (i & 1) != 0};
        state.context.fba = 0;
        state.context.alpha = 0x44;
        state.context.test = (i % 3 ? 0x5000d : 0x3000d) | (uint64_t((i * 11) & 255) << 4);
        state.context.tex0.tfx = 0;
        state.context.tex0.tcc = i & 1;
        if (specialized) {
            state.prim.iip = state.prim.fge = true;
            state.prim.fst = false;
            state.linearFilter = true;
            state.context.tex0.psm = GS_PSM_T8;
            const bool alphaMode = (i / 2) & 1;
            state.context.clamp = alphaMode ? 5 : 0;
            state.context.zbuf.zmask = alphaMode;
            state.context.tex0.tcc = (i / 4) & 1;
            state.context.test = (alphaMode ? 0x3000d : 0x5000d) | (uint64_t((i * 11) & 255) << 4);
        }
        if (feedback)
            state.context.tex0.tbp0 = state.context.frame.fbp * 32;
        serial.LoadClut(state.context.tex0, state.texclut);
        tiled.LoadClut(state.context.tex0, state.texclut);
        serial.Submit(draw);
        tiled.Submit(draw);
        serial.Flush();
        tiled.Flush();
        same(a, b, 600 + i);
    }
}
void texturedPointsAndLines() {
    constexpr uint32_t background = 0x11223344;
    constexpr uint32_t texels[] = {0x80102030, 0x00405060, 0x80708090, 0x80a0b0c0};
    for (bool fixed : {false, true})
        for (bool alphaTest : {false, true})
            for (unsigned effect : {0u, 1u}) {
                std::vector<uint8_t> memory(GSMem::MEMORY_SIZE, 0);
                threads("1");
                GSCpuBackend backend;
                backend.Initialize(memory.data(), unsigned(memory.size()));
                GSPrimitiveBatch draw{};
                auto& state = draw.state;
                state.prim.type = GS_PRIM_LINE;
                state.prim.tme = true;
                state.prim.fst = fixed;
                state.context.frame = {128, 1, GS_PSM_CT32, 0};
                state.context.zbuf = {320, GS_PSM_Z32, true};
                state.context.scissor = {0, 63, 0, 63};
                state.context.test = (1u << 16) | (1u << 17);
                if (alphaTest)
                    state.context.test |= 1 | (6u << 1) | (1u << 4);
                state.context.tex0.tbp0 = 64;
                state.context.tex0.tbw = 1;
                state.context.tex0.psm = GS_PSM_CT32;
                state.context.tex0.tw = state.context.tex0.th = 2;
                state.context.tex0.tfx = effect;
                state.context.tex0.tcc = true;
                state.textureWidth = state.textureHeight = 4;
                state.context.clamp = 5;
                for (unsigned y = 0; y < 4; ++y)
                    for (unsigned x = 0; x < 4; ++x)
                        backend.WriteVram(GS_PSM_CT32, 64, 1, x, y, texels[x]);
                for (int y = 10; y <= 13; ++y)
                    for (int x = 10; x <= 13; ++x)
                        GSMem::WriteCT32(memory.data(), 128 * 32, 1, x, y, background);
                draw.vertexCount = 2;
                for (unsigned i = 0; i < 2; ++i) {
                    auto& v = draw.vertices[i];
                    v.x = float(10 + i * 3);
                    v.y = 10;
                    v.z = 1;
                    v.r = v.g = v.b = v.a = 128;
                    v.u = uint16_t(i * 3 * 16);
                    v.s = float(i) * 0.75f;
                    v.q = 1;
                }
                backend.Submit(draw);
                backend.Flush();
                for (unsigned x = 0; x < 4; ++x)
                    assert(GSMem::ReadCT32(memory.data(), 128 * 32, 1, 10 + x, 10) ==
                           (alphaTest && x == 1 ? background : texels[x]));
                state.prim.type = GS_PRIM_POINT;
                draw.vertexCount = 1;
                auto& v = draw.vertices[0];
                for (unsigned x = 1; x <= 2; ++x) {
                    v.x = 10 + float(x);
                    v.y = 12;
                    v.u = uint16_t(x * 16);
                    v.s = float(x) / 4;
                    backend.Submit(draw);
                    backend.Flush();
                    assert(GSMem::ReadCT32(memory.data(), 128 * 32, 1, 10 + x, 12) ==
                           (alphaTest && x == 1 ? background : texels[x]));
                }
                if (!fixed) {
                    state.prim.type = GS_PRIM_LINE;
                    draw.vertexCount = 2;
                    draw.vertices[0].x = 10;
                    draw.vertices[0].y = draw.vertices[1].y = 14;
                    draw.vertices[0].s = 0;
                    draw.vertices[1].x = 17;
                    draw.vertices[1].s = 1.5f;
                    draw.vertices[1].q = 2;
                    for (unsigned x = 10; x <= 17; ++x)
                        GSMem::WriteCT32(memory.data(), 128 * 32, 1, x, 14, background);
                    backend.Submit(draw);
                    backend.Flush();
                    constexpr unsigned indices[] = {0, 0, 1, 1, 2, 2, 2, 3};
                    for (unsigned x = 0; x < 8; ++x)
                        assert(GSMem::ReadCT32(memory.data(), 128 * 32, 1, 10 + x, 14) ==
                               (alphaTest && indices[x] == 1 ? background : texels[indices[x]]));
                }
            }
    std::puts("Textured points and lines preserve UV/STQ colors and transparent-texel rejection");
}
} // namespace
int main() {
    texturedPointsAndLines();
    displayComposition();
    completedFrameScanout();
    progressiveFlickerBypass();
    emptySprites();
    identicalDisplaySources();
    colorReadbacks();
    indexedUploads();
    paletteCacheOrdering(false);
    paletteCacheOrdering(true);
    paletteCacheOrdering(false, true);
    paletteCacheOrdering(true, true);
    rejectedDepthCache(false);
    rejectedDepthCache(true);
    {
        gs_cpu::PaletteMemo memo;
        uint32_t color = 0;
        memo.reset();
        memo.texture(100);
        auto& entry = memo.entry(3);
        assert(!memo.read(entry, color));
        memo.write(entry, 0x12345678);
        assert(memo.read(entry, color) && color == 0x12345678);
        memo.texture(100);
        assert(memo.read(entry, color));
        memo.texture(101);
        assert(!memo.read(entry, color));
        memo.write(entry, 0xabcdef01);
        memo.reset();
        memo.texture(101);
        assert(!memo.read(entry, color));
        memo.write(entry, 1);
        assert(!memo.read(memo.entry(4), color));
    }
    {
        std::vector<uint8_t> memory(32768);
        for (auto& byte : memory)
            byte = static_cast<uint8_t>(randomWord());
        GSMem::TexturePageCache serial, direct;
        direct.BeginImmutableRead();
        constexpr uint32_t addresses[] = {8191, 8192, 16383, 16384, 1, 24576, 8192, 16385};
        for (const auto address : addresses)
            assert(*direct.Resolve(memory.data(), address) == *serial.Resolve(memory.data(), address));
        direct.EndImmutableRead(memory.data());
        memory[16385] ^= 255;
        assert(!direct.MatchesVram(memory.data()));
        assert(*direct.Resolve(memory.data(), 16385) == *serial.Resolve(memory.data(), 16385));
        assert(*direct.Resolve(memory.data(), 1) == *serial.Resolve(memory.data(), 1));
        assert(*direct.Resolve(memory.data(), 16385) == *serial.Resolve(memory.data(), 16385));
    }

    unsigned modes = 1;
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
    const unsigned saved = _mm_getcsr();
    modes = 4;
#endif
    for (unsigned mode = 0; mode < modes; ++mode) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
        _mm_setcsr((saved & ~0x6000u) | (mode << 13));
#endif
        unusedDepthSource();
        commonFragmentCases(false);
        commonFragmentCases(false, true);
        commonFragmentCases(true);
        std::vector<uint8_t> a(4 * 1024 * 1024);
        for (auto& value : a)
            value = static_cast<uint8_t>(randomWord());
        auto b = a;
        GSCpuBackend serial, tiled;
        threads("1");
        serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
        threads("8");
        tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
        const auto before = gs_cpu::tileDispatches.load();
        for (unsigned step = 0; step < 52; ++step) {
            auto draw = batch(step);
            serial.LoadClut(draw.state.context.tex0, draw.state.texclut);
            tiled.LoadClut(draw.state.context.tex0, draw.state.texclut);
            serial.Submit(draw);
            tiled.Submit(draw);
            serial.Flush();
            tiled.Flush();
            same(a, b, step);
        }
        assert(gs_cpu::tileDispatches.load() > before);
        // Small primitives benefit from a shared dispatch, preserving original
        // order even when all of them blend into the same pixels.
        for (bool overlap : {false, true}) {
            serial.TextureFlush();
            tiled.TextureFlush();
            const auto dispatches = gs_cpu::tileDispatches.load();
            const auto primitives = gs_cpu::tilePrimitives.load();
            for (unsigned i = 0; i < 64; ++i) {
                auto draw = batch(1);
                const float x = overlap ? 16.0f : float(i % 16) * 16;
                const float y = overlap ? 16.0f : float(i / 16) * 16;
                draw.vertices[0].x = x;
                draw.vertices[0].y = y;
                draw.vertices[1].x = x + 15;
                draw.vertices[1].y = y + 15;
                draw.vertices[1].r = static_cast<uint8_t>(i * 3);
                serial.LoadClut(draw.state.context.tex0, draw.state.texclut);
                tiled.LoadClut(draw.state.context.tex0, draw.state.texclut); // non-indexed bind is not a fence
                serial.Submit(draw);
                tiled.Submit(draw);
            }
            assert(gs_cpu::tileDispatches.load() == dispatches);
            // A public readback is a fence, even when the caller has a different
            // floating-point mode from the submitting guest thread.
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
            const unsigned submitMode = _mm_getcsr();
            _mm_setcsr((submitMode & ~0x6000u) | (((mode + 1) % 4) << 13));
#endif
            const auto aPixel = serial.ReadVram(GS_PSM_CT32, 128 * 32, 4, 16, 16);
            const auto bPixel = tiled.ReadVram(GS_PSM_CT32, 128 * 32, 4, 16, 16);
            assert(aPixel == bPixel);
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
            _mm_setcsr(submitMode);
#endif
            assert(gs_cpu::tileDispatches.load() == dispatches + 1);
            assert(gs_cpu::tilePrimitives.load() == primitives + 64);
            same(a, b, 90);
        }
        const auto enqueueSmall = [&](unsigned salt) {
            for (unsigned i = 0; i < 24; ++i) {
                auto draw = batch(1);
                draw.state.prim.tme = false;
                draw.state.prim.abe = false;
                draw.state.context.test = 1ull << 17;
                draw.state.context.zbuf.zmask = true;
                const float x = float((i + salt) % 8) * 24, y = float((i / 8 + salt) % 4) * 24;
                draw.vertices[0].x = x;
                draw.vertices[0].y = y;
                draw.vertices[1].x = x + 23;
                draw.vertices[1].y = y + 23;
                serial.Submit(draw);
                tiled.Submit(draw);
            }
        };
        // Every externally observable or mutating operation drains prior draws.
        enqueueSmall(0);
        serial.WriteVram(GS_PSM_CT32, 128 * 32, 4, 0, 0, 0x12345678);
        tiled.WriteVram(GS_PSM_CT32, 128 * 32, 4, 0, 0, 0x12345678);
        same(a, b, 300);
        enqueueSmall(1);
        std::vector<uint8_t> sa, sb;
        serial.SnapshotVram(sa);
        tiled.SnapshotVram(sb);
        same(sa, sb, 301);
        same(a, b, 301);
        enqueueSmall(2);
        auto clear = batch(1).state.context;
        clear.scissor.x0 = clear.scissor.y0 = 0;
        clear.scissor.x1 = clear.scissor.y1 = 31;
        serial.ClearFramebuffer(clear, 0x80403020);
        tiled.ClearFramebuffer(clear, 0x80403020);
        same(a, b, 302);
        enqueueSmall(3);
        GSTransferCommand transfer{};
        transfer.bitbltbuf.dbp = 64;
        transfer.bitbltbuf.dbw = 2;
        transfer.bitbltbuf.dpsm = GS_PSM_CT32;
        transfer.trxreg.rrw = transfer.trxreg.rrh = 1;
        transfer.direction = 0;
        serial.BeginTransfer(transfer);
        tiled.BeginTransfer(transfer);
        const uint8_t pixel[] = {13, 26, 39, 128};
        serial.UploadImage(pixel, 4);
        tiled.UploadImage(pixel, 4);
        same(a, b, 303);
        enqueueSmall(4);
        serial.GetTransferSnapshot();
        tiled.GetTransferSnapshot();
        same(a, b, 304);
        enqueueSmall(5);
        serial.Reset();
        tiled.Reset();
        same(a, b, 305);
        enqueueSmall(6);
        std::vector<uint8_t> nextA(a.size()), nextB(a.size());
        serial.Initialize(nextA.data(), static_cast<uint32_t>(nextA.size()));
        tiled.Initialize(nextB.data(), static_cast<uint32_t>(nextB.size()));
        same(a, b, 306);
        same(nextA, nextB, 306);
        threads("1");
        serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
        threads("8");
        tiled.Initialize(b.data(), static_cast<uint32_t>(b.size()));
        // Independent palette reloads retain per-draw colors in one batch.
        auto indexed = batch(4);
        indexed.state.prim.type = GS_PRIM_SPRITE;
        indexed.vertexCount = 2;
        indexed.state.context.test = 1ull << 17;
        indexed.state.context.zbuf.zmask = true;
        indexed.state.prim.abe = false;
        indexed.vertices[1].y = 127;
        indexed.vertices[1].v = 2048;
        auto palette = indexed.state.context.tex0;
        serial.LoadClut(palette, indexed.state.texclut);
        tiled.LoadClut(palette, indexed.state.texclut);
        const auto paletteDispatches = gs_cpu::tileDispatches.load();
        for (unsigned i = 0; i < 16; ++i) {
            if (i == 8) {
                palette.cbp += 32;
                serial.LoadClut(palette, indexed.state.texclut);
                tiled.LoadClut(palette, indexed.state.texclut);
            }
            auto noLoad = palette;
            noLoad.cld = 0;
            serial.LoadClut(noLoad, indexed.state.texclut);
            tiled.LoadClut(noLoad, indexed.state.texclut);
            if (i % 3 == 0) {
                serial.TextureFlush();
                tiled.TextureFlush();
            }
            indexed.vertices[0].x = float(i * 16);
            indexed.vertices[1].x = float(i * 16 + 15);
            indexed.vertices[1].r = static_cast<uint8_t>(i * 15);
            serial.Submit(indexed);
            tiled.Submit(indexed);
        }
        serial.Flush();
        tiled.Flush();
        same(a, b, 307);
        assert(gs_cpu::tileDispatches.load() == paletteDispatches + 1);
        // Probe feedback, packed storage, region modes, wrapped textures and
        // color/depth overlap. All must preserve the serial ordering.
        for (unsigned step = 0; step < 6; ++step) {
            auto draw = batch(5);
            if (step == 0)
                draw.state.context.tex0.tbp0 = draw.state.context.frame.fbp * 32;
            if (step == 1)
                draw.state.context.frame.psm = GS_PSM_T4;
            if (step == 2) {
                draw.state.context.tex0.tbp0 = draw.state.context.frame.fbp * 32 - 32;
                draw.state.context.clamp = 10 | (1023ull << 14) | (1023ull << 34);
            }
            if (step == 3)
                draw.state.context.tex0.tbp0 = 16383;
            if (step == 4)
                draw.state.context.zbuf.zbp = draw.state.context.frame.fbp;
            if (step == 5) {
                draw.state.context.tex0.tbp0 = draw.state.context.zbuf.zbp * 32 - 32;
                draw.state.context.clamp = 15 | (1023ull << 4) | (1023ull << 24);
            }
            const auto count = gs_cpu::tileDispatches.load();
            serial.Submit(draw);
            tiled.Submit(draw);
            serial.Flush();
            tiled.Flush();
            assert(gs_cpu::tileDispatches.load() == count);
            same(a, b, 100 + step);
        }
        // Region bounds can exceed TW/TH. Their complete address footprint
        // must remain separate from the destination, including unaligned TBP.
        for (uint64_t clamp : {10ull | (1023ull << 14) | (1023ull << 34),
                               15ull | (511ull << 4) | (256ull << 14) | (511ull << 24) | (256ull << 34)}) {
            auto draw = batch(5);
            draw.state.context.clamp = clamp;
            serial.TextureFlush();
            tiled.TextureFlush();
            const auto count = gs_cpu::tileDispatches.load();
            serial.Submit(draw);
            tiled.Submit(draw);
            serial.Flush();
            tiled.Flush();
            assert(gs_cpu::tileDispatches.load() > count);
            same(a, b, 130);
        }
        // Packed true-color pixels still have independent 16-bit stores.
        for (uint8_t color : {uint8_t(GS_PSM_CT16), uint8_t(GS_PSM_CT16S)})
            for (uint8_t depth : {uint8_t(GS_PSM_Z16), uint8_t(GS_PSM_Z16S), uint8_t(GS_PSM_Z32)}) {
                auto draw = batch(5);
                draw.state.context.frame.psm = color;
                draw.state.context.zbuf.psm = depth;
                serial.TextureFlush();
                tiled.TextureFlush();
                const auto count = gs_cpu::tileDispatches.load();
                serial.Submit(draw);
                tiled.Submit(draw);
                serial.Flush();
                tiled.Flush();
                assert(gs_cpu::tileDispatches.load() > count);
                same(a, b, 140);
            }
        // Movie/GUI sprites can leave an unused depth buffer at the color
        // address. With Z writes masked and an always-pass test it is unused.
        auto noDepth = batch(3);
        noDepth.state.context.zbuf.zbp = noDepth.state.context.frame.fbp;
        noDepth.state.context.zbuf.psm = GS_PSM_Z16;
        noDepth.state.context.zbuf.zmask = true;
        noDepth.state.context.test = 1ull << 17;
        serial.TextureFlush();
        tiled.TextureFlush();
        const auto noDepthCount = gs_cpu::tileDispatches.load();
        serial.Submit(noDepth);
        tiled.Submit(noDepth);
        serial.Flush();
        tiled.Flush();
        assert(gs_cpu::tileDispatches.load() > noDepthCount);
        same(a, b, 150);
        // Retain stale texture bytes until their original cache eviction/flush.
        auto draw = batch(1);
        draw.state.prim.tme = true;
        serial.Submit(draw);
        tiled.Submit(draw);
        serial.Flush();
        tiled.Flush();
        const auto& t = draw.state.context.tex0;
        for (unsigned y = 0; y < 128; ++y)
            for (unsigned x = 0; x < 128; ++x) {
                serial.WriteVram(t.psm, t.tbp0, t.tbw, x, y, 37);
                tiled.WriteVram(t.psm, t.tbp0, t.tbw, x, y, 37);
            }
        const auto count = gs_cpu::tileDispatches.load();
        serial.Submit(draw);
        tiled.Submit(draw);
        serial.Flush();
        tiled.Flush();
        assert(gs_cpu::tileDispatches.load() == count);
        same(a, b, 200);
        serial.TextureFlush();
        tiled.TextureFlush();
        serial.Submit(draw);
        tiled.Submit(draw);
        serial.Flush();
        tiled.Flush();
        same(a, b, 201);
        std::vector<GSPrimitiveBatch> draws;
        for (unsigned i = 0; i < 16; ++i)
            draws.push_back(batch(1));
        auto expected = a;
        {
            threads("1");
            GSCpuBackend reference;
            reference.Initialize(expected.data(), static_cast<uint32_t>(expected.size()));
            for (const auto& draw : draws)
                reference.Submit(draw);
        }
        auto first = a, second = a;
        threads("8");
        const unsigned submitRounding =
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
            _mm_getcsr();
#else
            0;
#endif
        const auto client = [&](std::vector<uint8_t>& image) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
            _mm_setcsr(submitRounding);
#else
            (void)submitRounding;
#endif
            GSCpuBackend backend;
            backend.Initialize(image.data(), static_cast<uint32_t>(image.size()));
            for (const auto& draw : draws)
                backend.Submit(draw);
            // Destructor also fences while owning a reference to the shared pool.
        };
        std::thread one([&] { client(first); }), two([&] { client(second); });
        one.join();
        two.join();
        same(expected, first, 400);
        same(expected, second, 401);
    }
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
    _mm_setcsr(saved);
#endif
    std::printf("Serial/tiled VRAM parity passed across %u rounding modes; dispatches=%llu\n", modes,
                static_cast<unsigned long long>(gs_cpu::tileDispatches.load()));
}
