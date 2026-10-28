#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "gs_texup.h"
#include "gsr/texture_pack.h"
#include "gs/gs_hd_source_cache.h"
#if defined(PS2X_TEST_PORTABLE_GPU)
#include "gs/gs_gpu_raster.h"
#endif

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
void environment(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}
std::shared_ptr<const gsr::PackImage> ready(gsr::TexturePack& pack, const std::vector<uint32_t>& pixels) {
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        if (auto image = pack.find(pixels, 4, 4))
            return image;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {};
}
} // namespace

int main() {
    {
        gs_cpu::HdSourceCache cache;
        gs_cpu::HdSourceCache::Key key{};
        key[3] = key[4] = 4;
        std::array<uint16_t, 512> palette{};
        std::array<uint8_t, 8192> source{};
        unsigned decodes = 0;
        const auto decode = [&](auto& pixels) {
            ++decodes;
            pixels.assign(16, source[64] + palette[0]);
            return std::array<uint64_t, 2>{};
        };
        assert(cache.get(key, source.data(), source.size(), palette, decode).pixels[0] == 0);
        source[64] = 1; // Outside the sampled fingerprint; full bytes must still differ.
        assert(cache.get(key, source.data(), source.size(), palette, decode).pixels[0] == 1);
        source[64] = 0;
        assert(cache.get(key, source.data(), source.size(), palette, decode).pixels[0] == 0);
        assert(decodes == 2); // Revisit earlier content in a recycled upload slot.
        palette[0] = 2;
        assert(cache.get(key, source.data(), source.size(), palette, decode).pixels[0] == 2);
        assert(decodes == 3);
        const auto lease = cache.getShared(key, source.data(), source.size(), palette, decode);
        // Evict the original entry while an in-flight GPU batch retains it.
        for (unsigned version = 0; version < 520; ++version) {
            key[11] = version + 1;
            source[64] = uint8_t(version);
            cache.get(key, source.data(), source.size(), palette, decode);
        }
        assert(lease->pixels.size() == 16 && lease->pixels[0] == 2);
    }
    const std::string directory = "hd-texture-test-pack";
    std::filesystem::create_directory(directory);
    const std::vector<uint32_t> native(16, 0x800000ffu);
    const auto hash = gsr::texup::hashContent(native.data(), 4, 4);
    const auto rgbHash = gsr::texup::hashContentRgb(native.data(), 4, 4);
    const std::string name = gsr::texup::hashName(hash) + ".png";
    std::vector<uint32_t> hd(64, 0x80ff0000u);
    for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 4; x < 8; ++x)
            hd[y * 8 + x] = 0x8000ff00u;
    for (unsigned y = 0; y < 8; ++y)
        hd[y * 8 + 1] = 0x80ff00ffu;
    for (unsigned x = 0; x < 8; ++x)
        hd[7 * 8 + x] = 0x80808080u;
    assert(gsr::texup::writePngRGBA(directory + "/" + name, hd.data(), 8, 8));
    {
        gsr::TexturePack pack(directory);
        auto image = ready(pack, native);
        assert(image && image->width == 8 && image->height == 8 && image->pixels == hd);
    }
    std::ofstream(directory + "/alias.txt")
        << gsr::texup::hashName(rgbHash) << ' ' << gsr::texup::hashName(hash) << '\n';
    {
        auto changedAlpha = native;
        for (auto& pixel : changedAlpha)
            pixel = (pixel & 0xffffffu) | 0x20000000u;
        gsr::TexturePack pack(directory);
        auto image = ready(pack, changedAlpha);
        assert(image);
        for (auto pixel : image->pixels)
            assert((pixel >> 24) == 0x20);
    }
    environment("DRAKAN_TEXTURE_DIR", directory.c_str());
    environment("DRAKAN_HD_TEXTURES", "1");
    environment("PS2X_CPU_RASTER_THREADS", "4");
    std::vector<uint8_t> vram(GSMem::MEMORY_SIZE);
    GSCpuBackend backend;
    backend.Initialize(vram.data(), uint32_t(vram.size()));
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            backend.WriteVram(GS_PSM_CT32, 64, 1, x, y, native[y * 4 + x]);
    GSPrimitiveBatch draw{};
    draw.vertexCount = 2;
    auto& state = draw.state;
    state.prim.type = GS_PRIM_SPRITE;
    state.prim.tme = state.prim.fst = true;
    state.context.frame = {128, 1, GS_PSM_CT32, 0};
    state.context.zbuf.zmask = true;
    state.context.test = (1ull << 16) | (1ull << 17);
    state.context.scissor = {0, 63, 0, 63};
    state.context.tex0.tbp0 = 64;
    state.context.tex0.tbw = 1;
    state.context.tex0.psm = GS_PSM_CT32;
    state.context.tex0.tw = state.context.tex0.th = 2;
    state.context.tex0.tfx = 1;
    state.context.tex0.tcc = true;
    state.textureWidth = state.textureHeight = 4;
    state.context.clamp = 5;
    draw.vertices[1].x = draw.vertices[1].y = 64;
    draw.vertices[1].u = draw.vertices[1].v = 64;
    bool replaced = false;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        backend.Submit(draw);
        backend.Submit(draw);
        backend.Flush();
        const auto left = backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 8, 8);
        const auto right = backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 56, 8);
        if (left == hd[1] && right == hd[7]) {
            replaced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(replaced);
    // Distinct HD texels within one original texel retain their detail.
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 4, 8) == hd[0]);
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 12, 8) == hd[1]);
    state.linearFilter = true;
    backend.Submit(draw);
    backend.Submit(draw);
    backend.Flush();
    const auto filtered = backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 8, 8);
    // Sprite FST rounds this position to U=9/16; HD bilinear weight is 5/8.
    assert(filtered == 0x80ff009fu);
    for (uint64_t clamp :
         {0ull, 1ull, 4ull, 5ull, 2ull | (2ull << 2) | (1ull << 4) | (2ull << 14) | (1ull << 24) | (2ull << 34),
          3ull | (3ull << 2) | (1ull << 4) | (2ull << 14) | (1ull << 24) | (2ull << 34)}) {
        state.context.clamp = clamp;
        backend.Submit(draw);
        backend.Submit(draw);
        backend.Flush();
        // At screen pixel zero, original sprite FST rounds U,V to 1/16.
        // In the 2x pack the two taps straddle -1 and 0, with weight 5/8.
        const auto coordinate = [](int value, unsigned mode) {
            if (mode == 0)
                return (value + 8) % 8;
            if (mode == 1)
                return std::clamp(value, 0, 7);
            if (mode == 2)
                return std::clamp(value, 2, 5);
            const int cell = int(std::floor(float(value) / 2));
            return ((cell & 1) | 2) * 2 + value - cell * 2;
        };
        uint32_t expected = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            double sum = 0;
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                    const auto pixel = hd[coordinate(y - 1, (clamp >> 2) & 3) * 8 + coordinate(x - 1, clamp & 3)];
                    sum += ((pixel >> shift) & 255) * (x ? .625 : .375) * (y ? .625 : .375);
                }
            expected |= uint32_t(std::lround(sum)) << shift;
        }
        assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 0, 0) == expected);
    }
    state.linearFilter = false;
    state.context.clamp = 0;
    state.prim.fst = false;
    draw.vertices[0].q = draw.vertices[1].q = 1;
    draw.vertices[0].s = draw.vertices[0].t = -.125f;
    draw.vertices[1].s = draw.vertices[1].t = .875f;
    backend.Submit(draw);
    backend.Submit(draw);
    backend.Flush();
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 0, 0) == hd[7 * 8 + 7]);
    state.prim.fst = true;
    state.context.clamp = 5;
    // Recycled texture slot must not keep showing the previous replacement.
    backend.WriteVram(GS_PSM_CT32, 64, 1, 0, 0, 0x8000ffffu);
    backend.TextureFlush();
    backend.Submit(draw);
    backend.Submit(draw);
    backend.Flush();
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 1, 8, 8) == 0x8000ffffu);
    // HD rendering never modifies the original source texture.
    assert(backend.ReadVram(GS_PSM_CT32, 64, 1, 1, 0) == native[1]);
    // The same index image with a different retained palette is different
    // content, including when both draws share a pending tile run.
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
            backend.WriteVram(GS_PSM_T8, 64, 1, x, y, 0);
    backend.WriteVram(GS_PSM_CT32, 640, 1, 0, 0, native[0]);
    state.context.tex0.psm = GS_PSM_T8;
    state.context.tex0.cbp = 640;
    state.context.tex0.cpsm = GS_PSM_CT32;
    state.context.tex0.cld = 1;
    state.context.frame.fbw = 2;
    state.context.scissor.x1 = 127;
    backend.TextureFlush();
    backend.LoadClut(state.context.tex0, state.texclut);
    backend.TextureFlush();
    backend.Submit(draw);
    backend.Submit(draw);
    backend.WriteVram(GS_PSM_CT32, 640, 1, 0, 0, 0x8000ffffu);
    backend.TextureFlush();
    backend.LoadClut(state.context.tex0, state.texclut);
    backend.TextureFlush();
    draw.vertices[0].x = 64;
    draw.vertices[1].x = 128;
    backend.Submit(draw);
    backend.Submit(draw);
    backend.Flush();
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 2, 8, 8) == hd[1]);
    assert(backend.ReadVram(GS_PSM_CT32, 128 * 32, 2, 72, 8) == 0x8000ffffu);
    std::ofstream(directory + "/exclude.txt") << gsr::texup::hashName(hash) << '\n';
    {
        gsr::TexturePack excluded(directory);
        assert(!excluded.find(native, 4, 4));
    }
    std::filesystem::remove(directory + "/" + name);
    std::filesystem::remove(directory + "/alias.txt");
    std::filesystem::remove(directory + "/exclude.txt");
    std::filesystem::remove(directory);
#if defined(PS2X_TEST_PORTABLE_GPU)
    if (const char* gpu = std::getenv("DRAKAN_GPU_RASTER"); gpu && gpu[0] == '1') {
        assert(gs_gpu::completedJobs() > 0);
        std::printf("HD GPU jobs=%llu\n", static_cast<unsigned long long>(gs_gpu::completedJobs()));
    }
#endif
    std::puts("HD replacement pixels, palette-alpha aliases, slot recycling and exclusions passed");
}
