#include "gs/gs_cpu_async_backend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
#include <xmmintrin.h>
#endif

namespace {
void threads(const char* value) {
#if defined(_WIN32)
    _putenv_s("PS2X_CPU_RASTER_THREADS", value);
#else
    setenv("PS2X_CPU_RASTER_THREADS", value, 1);
#endif
}
GSPrimitiveBatch draw(unsigned index) {
    GSPrimitiveBatch result{};
    auto& state = result.state;
    state.prim.type = index & 1 ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE;
    result.vertexCount = index & 1 ? 2 : 3;
    state.prim.tme = state.prim.iip = state.prim.abe = state.prim.fge = true;
    state.prim.fst = (index % 3) != 0;
    state.context.frame = {128, 4, GS_PSM_CT32, 0};
    state.context.zbuf = {256, GS_PSM_Z32, false};
    state.context.scissor = {0, 255, 0, 127};
    state.context.test = 1ull << 17;
    state.context.alpha = 0x44;
    auto& tex = state.context.tex0;
    tex.psm = GS_PSM_T8;
    tex.tbp0 = 64;
    tex.tbw = 2;
    tex.tw = tex.th = 7;
    tex.cbp = 640 + (index % 3) * 32;
    tex.cpsm = GS_PSM_CT32;
    tex.cld = 1;
    tex.tcc = true;
    state.textureWidth = state.textureHeight = 128;
    state.linearFilter = true;
    state.fogR = 20;
    state.fogG = 40;
    state.fogB = 60;
    for (unsigned i = 0; i < 3; ++i) {
        auto& v = result.vertices[i];
        v.x = float((index % 16) * 8 + (i == 1 ? 10 : 0));
        v.y = float((index / 16) * 8 + (i != 0 ? 10 : 0));
        v.z = 123456.0 + index * 100.0 + i;
        v.u = uint16_t((index + i * 7) * 16);
        v.v = uint16_t((index + i * 5) * 16);
        v.s = float(index + i * 7) / 128.0f;
        v.t = float(index + i * 5) / 128.0f;
        v.q = 0.7f + float(i) / 10;
        v.r = uint8_t(40 + i * 40);
        v.g = uint8_t(70 + i * 20);
        v.b = uint8_t(110 + i * 10);
        v.a = 128;
        v.fog = uint8_t(70 + i * 80);
    }
    return result;
}
void same(GSRasterBackend& a, GSRasterBackend& b) {
    std::vector<uint8_t> first, second;
    a.SnapshotVram(first);
    b.SnapshotVram(second);
    assert(first == second);
}
} // namespace

int main() {
    unsigned modes = 1;
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
    const unsigned saved = _mm_getcsr();
    modes = 4;
#endif
    for (unsigned mode = 0; mode < modes; ++mode) {
        std::vector<uint8_t> a(4 * 1024 * 1024);
        uint32_t random = 0xa19625e3;
        for (auto& byte : a) {
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            byte = uint8_t(random);
        }
        auto b = a;
        threads("1");
        GSCpuBackend serial;
        serial.Initialize(a.data(), static_cast<uint32_t>(a.size()));
        threads("8");
        GSAsyncCpuBackend queued;
        queued.Initialize(b.data(), static_cast<uint32_t>(b.size()));
        for (unsigned i = 0; i < 128; ++i) {
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
            _mm_setcsr((saved & ~0x6000u) | (((mode + i) % 4) << 13));
#endif
            auto batch = draw(i);
            if (i % 3 == 0) {
                serial.LoadClut(batch.state.context.tex0, batch.state.texclut);
                queued.LoadClut(batch.state.context.tex0, batch.state.texclut);
            }
            serial.Submit(batch);
            queued.Submit(batch);
            if (i % 5 == 0) {
                serial.TextureFlush();
                queued.TextureFlush();
            }
            if (i % 13 == 0) {
                serial.WriteVram(GS_PSM_CT32, 2048, 4, i, 0, i * 0x112233);
                queued.WriteVram(GS_PSM_CT32, 2048, 4, i, 0, i * 0x112233);
                assert(serial.ReadVram(GS_PSM_CT32, 2048, 4, i, 0) == queued.ReadVram(GS_PSM_CT32, 2048, 4, i, 0));
            }
        }
#if !defined(__EMSCRIPTEN__) && (defined(__SSE4_1__) || defined(_M_X64))
        _mm_setcsr(saved);
#endif
        queued.Flush();
        queued.Sync(GSSyncReason::Finish);
        same(serial, queued);
        GSTransferCommand transfer{};
        transfer.direction = 0;
        transfer.bitbltbuf.dbp = 3072;
        transfer.bitbltbuf.dbw = 1;
        transfer.bitbltbuf.dpsm = GS_PSM_CT32;
        transfer.trxreg.rrw = 16;
        transfer.trxreg.rrh = 1;
        serial.BeginTransfer(transfer);
        queued.BeginTransfer(transfer);
        for (unsigned i = 0; i < 4; ++i) {
            std::vector<uint8_t> bytes(16, uint8_t(19 + i * 27));
            serial.UploadImage(bytes.data(), static_cast<uint32_t>(bytes.size()));
            queued.UploadImage(bytes.data(), static_cast<uint32_t>(bytes.size()));
            std::fill(bytes.begin(), bytes.end(), 255); // queued uploads own their bytes
        }
        assert(serial.GetTransferSnapshot().copiedPixels == queued.GetTransferSnapshot().copiedPixels);
        same(serial, queued);
        transfer.direction = 1;
        transfer.bitbltbuf.sbp = transfer.bitbltbuf.dbp;
        transfer.bitbltbuf.sbw = 1;
        transfer.bitbltbuf.spsm = GS_PSM_CT32;
        serial.BeginTransfer(transfer);
        queued.BeginTransfer(transfer);
        std::vector<uint8_t> first(64), second(64);
        assert(serial.ConsumeLocalToHostBytes(first.data(), 64) == queued.ConsumeLocalToHostBytes(second.data(), 64));
        assert(first == second);
        const auto context = draw(0).state.context;
        assert(serial.ClearFramebuffer(context, 0xff193a58) == queued.ClearFramebuffer(context, 0xff193a58));
        same(serial, queued);
        GSPresentationRequest request{};
        request.pmode = 1;
        request.dispfb1 = 128 | (4ull << 9);
        request.display1 = (255ull << 32) | (127ull << 44);
        serial.Submit(draw(2));
        queued.Submit(draw(2));
        const auto imageA = serial.Present(request), imageB = queued.Present(request);
        assert(imageA.width == imageB.width && imageA.height == imageB.height && imageA.pixels == imageB.pixels);
        serial.Submit(draw(1));
        queued.Submit(draw(1));
        serial.Reset();
        queued.Reset();
        same(serial, queued);
        std::vector<uint8_t> nextA(a.size()), nextB(b.size());
        serial.Submit(draw(3));
        queued.Submit(draw(3));
        serial.Initialize(nextA.data(), static_cast<uint32_t>(nextA.size()));
        queued.Initialize(nextB.data(), static_cast<uint32_t>(nextB.size()));
        assert(a == b);
        bool rejected = false;
        try {
            queued.Initialize(nextB.data(), 1024);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
        queued.Initialize(nextB.data(), static_cast<uint32_t>(nextB.size()));
        // Concurrent disjoint writes also exercise bounded-queue backpressure.
        const auto writer = [&](unsigned column) {
            for (unsigned i = 0; i < 16000; ++i)
                queued.WriteVram(GS_PSM_CT32, 0, 1, column, i % 128, i);
        };
        std::thread one([&] { writer(0); }), two([&] { writer(1); });
        one.join();
        two.join();
        for (unsigned i = 0; i < 16000; ++i)
            for (unsigned column = 0; column < 2; ++column)
                serial.WriteVram(GS_PSM_CT32, 0, 1, column, i % 128, i);
        same(serial, queued);
    }
    std::vector<uint8_t> final(4 * 1024 * 1024);
    {
        GSAsyncCpuBackend pending;
        pending.Initialize(final.data(), static_cast<uint32_t>(final.size()));
        pending.WriteVram(GS_PSM_CT32, 0, 1, 0, 0, 0x87654321);
    }
    threads("1");
    GSCpuBackend reader;
    reader.Initialize(final.data(), static_cast<uint32_t>(final.size()));
    assert(reader.ReadVram(GS_PSM_CT32, 0, 1, 0, 0) == 0x87654321);
    std::puts("Ordered CPU rendering, owned uploads, fences, rounding, rebind, backpressure and shutdown passed");
}
