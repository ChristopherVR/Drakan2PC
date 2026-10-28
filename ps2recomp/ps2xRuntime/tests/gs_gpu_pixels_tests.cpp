#include "gs/gs_gpu_raster.h"
#include "portable_gpu.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <fstream>
#include <filesystem>
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
#include <xmmintrin.h>
#endif

class RecordingCompute final : public gsr::gpu::Compute {
    std::unique_ptr<gsr::gpu::Compute> wrapped_;
    std::filesystem::path output_;
    unsigned index_ = 0;
    std::vector<uint32_t> textureMirror_;
    size_t lastTextureWords_ = 0;

public:
    RecordingCompute(std::unique_ptr<gsr::gpu::Compute> device, const char* output)
        : wrapped_(std::move(device)), output_(output ? output : "") {
        if (!output_.empty())
            std::filesystem::create_directories(output_);
    }
    const std::string& adapter() const override { return wrapped_->adapter(); }
    size_t lastTextureWords() const { return lastTextureWords_; }
    bool run(gsr::gpu::Job& job) override {
        lastTextureWords_ = job.textures.size();
        if (output_.empty())
            return wrapped_->run(job);
        if (job.textureWords) {
            size_t source = 0;
            for (size_t w = 0; w < job.textureWrites.size(); w += 2) {
                const size_t offset = job.textureWrites[w], count = job.textureWrites[w + 1];
                textureMirror_.resize(std::max(textureMirror_.size(), offset + count));
                std::copy_n(job.textures.begin() + source, count, textureMirror_.begin() + offset);
                source += count;
            }
        } else
            textureMirror_ = job.textures;
        std::ofstream file(output_ / ("job-" + std::to_string(index_++) + ".json"));
        const auto array = [&](const std::vector<uint32_t>& data) {
            file << '[';
            bool first = true;
            for (auto v : data) {
                if (!first)
                    file << ',';
                first = false;
                file << v;
            }
            file << ']';
        };
        file << "{\"width\":" << job.width << ",\"height\":" << job.height << ",\"inputs\":[";
        array(job.draws);
        file << ',';
        array(job.bins);
        file << ',';
        array(textureMirror_);
        file << ',';
        array(job.pixels);
        if (!wrapped_->run(job))
            return false;
        file << "],\"expected\":";
        array(job.pixels);
        file << "}\n";
        return bool(file);
    }
};

class FailedCompute final : public gsr::gpu::Compute {
    std::string name_ = "simulated device loss";

public:
    const std::string& adapter() const override { return name_; }
    bool run(gsr::gpu::Job& job) override {
        std::fill(job.pixels.begin(), job.pixels.end(), 0);
        return false;
    }
};
int main(int argc, char** argv) {
#if defined(_WIN32)
    _putenv_s("PS2X_CPU_RASTER_THREADS", "1");
#else
    setenv("PS2X_CPU_RASTER_THREADS", "1", 1);
#endif
    GSMem::InitLookupTables();
    auto device = gsr::gpu::createCompute();
    if (!device)
        return 77;
    RecordingCompute recording(std::move(device), argc > 1 ? argv[1] : nullptr);
    uint32_t seed = 0x81731;
    const auto random = [&] {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    };
    for (unsigned test = 0; test < 192; ++test) {
        std::vector<uint8_t> expected(GSMem::MEMORY_SIZE);
        for (auto& byte : expected)
            byte = uint8_t(random());
        auto actual = expected;
        GSCpuBackend cpu;
        cpu.Initialize(expected.data(), unsigned(expected.size()));
        std::vector<uint32_t> texture(16 * 16);
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x) {
                texture[y * 16 + x] = random();
                GSMem::WriteCT32(expected.data(), 32, 1, x, y, texture[y * 16 + x]);
                GSMem::WriteCT32(actual.data(), 32, 1, x, y, texture[y * 16 + x]);
            }
        auto integrated = actual;
        std::array<GSPrimitiveBatch, 4> primitives;
        std::array<gs_gpu::Draw, 4> draws;
        for (unsigned i = 0; i < 4; ++i) {
            auto& b = primitives[i];
            auto& s = b.state;
            const bool sprite = test & 1;
            b.vertexCount = sprite ? 2 : 3;
            s.prim.type = sprite ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE;
            s.prim.iip = test & 2;
            s.prim.tme = test & 4;
            s.prim.fge = test & 8;
            s.prim.abe = test & 16;
            s.prim.aa1 = (test & 16) != 0; // AA1 is a no-op in the current CPU rasterizer.
            s.prim.fst = true;
            s.dthe = test & 1; // Dithering does not alter RGBA32/RGB24 destinations.
            s.pabe = test & 32;
            s.context.frame = {128, 1, test % 5 == 0 ? GS_PSM_CT24 : GS_PSM_CT32, test % 3 ? 0u : 0xff000000u};
            s.context.zbuf = {320, GS_PSM_Z32, test % 3 == 0};
            s.context.scissor = {0, 63, 0, 63};
            s.context.test = (1u << 17) | (test % 4 ? (1 | (6u << 1) | (70u << 4) | ((test % 4) << 12)) : 0);
            if (test % 7 == 0)
                s.context.test |= 1u << 14;
            s.context.alpha = 0x44 | (128ull << 32);
            s.context.fba = i & 1;
            s.context.tex0.tbp0 = 32;
            s.context.tex0.tbw = 1;
            s.context.tex0.psm = GS_PSM_CT32;
            s.context.tex0.tfx = test % 4;
            s.context.tex0.tcc = test & 2;
            s.context.clamp = test & 2 ? 5 : 0;
            s.textureWidth = s.textureHeight = 16;
            s.linearFilter = (test & 8) || test >= 48;
            if (test >= 48 && test < 96)
                s.prim.fst = false;
            if (test >= 96) {
                s.context.test = (s.context.test & ~(3u << 17)) | ((2u + (test & 1)) << 17);
                s.context.zbuf.psm = test & 2 ? GS_PSM_Z24 : GS_PSM_Z32;
                // The first group does not touch depth. A later group enables
                // it without changing storage; one dispatch must retain both.
                if (i < 2) {
                    s.context.zbuf.zmask = true;
                    s.context.zbuf.zbp = 0; // Ignored storage differs from the later real depth target.
                    s.context.zbuf.psm = GS_PSM_Z16;
                    s.context.test = (s.context.test & ~(3u << 17)) | (1u << 17);
                }
            }
            if (test >= 144) {
                s.prim.fst = false;
                s.linearFilter = false;
                // Exercise negative ST rounding and bounded region clamp/repeat.
                s.context.clamp =
                    test & 2 ? (3ull | (3ull << 2) | (7ull << 4) | (8ull << 14) | (7ull << 24) | (8ull << 34))
                             : (2ull | (2ull << 2) | (2ull << 4) | (12ull << 14) | (2ull << 24) | (12ull << 34));
            }
            s.fogR = 47;
            s.fogG = 91;
            s.fogB = 150;
            for (unsigned v = 0; v < 3; ++v) {
                auto& vert = b.vertices[v];
                vert.x = (v == 1 ? 62.f : 1.f) - float(i);
                vert.y = (v == 2 || (sprite && v == 1)) ? 62.f : 1.f;
                vert.z = test >= 96 ? double(random()) : double(1000 + i * 100 + v * 3);
                if (test >= 96) {
                    vert.x += float(v) * 0.125f;
                    vert.y += float(i) * 0.25f;
                }
                const auto rgba = random();
                vert.r = rgba;
                vert.g = rgba >> 8;
                vert.b = rgba >> 16;
                vert.a = rgba >> 24;
                vert.u = v == 1 ? 255 : 0;
                vert.v = v == 2 || (sprite && v == 1) ? 255 : 0;
                vert.fog = uint8_t(v * 97);
                vert.q = 0.5f + float(v) * 0.75f;
                vert.s = (float(vert.u) / 256.f) * vert.q;
                vert.t = (float(vert.v) / 256.f) * vert.q;
                if (test >= 144) {
                    vert.s -= 0.75f * vert.q;
                    vert.t -= 0.75f * vert.q;
                }
            }
            draws[i] = {&b, {0, 63, 0, 63}, texture, 16, 16};
            cpu.Submit(b);
        }
        cpu.Flush();
#if !defined(__EMSCRIPTEN__) && (defined(__SSE__) || defined(_M_X64))
        if (test == 6) {
            const unsigned saved = _mm_getcsr();
            for (unsigned mode = 0; mode < 4; ++mode) {
                const unsigned control = (saved & ~0x6000u) | (mode << 13);
                _mm_setcsr(control);
                auto referenceMemory = integrated, queuedMemory = integrated;
                GSCpuBackend reference, queued;
                reference.Initialize(referenceMemory.data(), unsigned(referenceMemory.size()));
                queued.Initialize(queuedMemory.data(), unsigned(queuedMemory.size()));
                for (const auto& b : primitives)
                    reference.Submit(b);
                reference.Flush();
                assert(referenceMemory == expected && (_mm_getcsr() & 0xffc0u) == (control & 0xffc0u));
#if defined(_WIN32)
                _putenv_s("DRAKAN_GPU_RASTER", "1");
                _putenv_s("DRAKAN_GPU_SCALE", "3");
#else
                setenv("DRAKAN_GPU_RASTER", "1", 1);
                setenv("DRAKAN_GPU_SCALE", "3", 1);
#endif
                const auto before = gs_gpu::completedJobs();
                for (const auto& b : primitives)
                    queued.Submit(b);
                queued.Flush();
                if (gs_gpu::completedJobs() <= before || queuedMemory != expected) {
                    const auto diff = std::mismatch(queuedMemory.begin(), queuedMemory.end(), expected.begin());
                    std::fprintf(stderr, "EE rounding case %u: jobs=%llu delta=%llu firstDifference=%zu control=%x\n",
                                 mode, (unsigned long long)gs_gpu::completedJobs(),
                                 (unsigned long long)(gs_gpu::completedJobs() - before),
                                 size_t(diff.first - queuedMemory.begin()), _mm_getcsr());
                    return 1;
                }
                assert((_mm_getcsr() & 0xffc0u) == (control & 0xffc0u));
#if defined(_WIN32)
                _putenv_s("DRAKAN_GPU_RASTER", "");
                _putenv_s("DRAKAN_GPU_SCALE", "");
#else
                unsetenv("DRAKAN_GPU_RASTER");
                unsetenv("DRAKAN_GPU_SCALE");
#endif
            }
            _mm_setcsr(saved);
            std::puts("GS rendering ignores EE rounding mode and restores host control across CPU/GPU submission");
        }
#endif
        if (!gs_gpu::rasterize(draws, {0, 63, 0, 63}, actual.data(), &recording)) {
            std::fprintf(stderr, "GPU dispatch failed in fixture %u\n", test);
            return 1;
        }
        // Extra presentation samples must never change guest-visible color,
        // depth, masks or unrelated VRAM, including HD samplers and sprites.
        auto highResolution = integrated;
        gs_gpu::HighResolutionSurface surface;
        if (!gs_gpu::rasterize(draws, {0, 63, 0, 63}, highResolution.data(), &recording, &surface) ||
            highResolution != expected || surface.pixels.size() != 192 * 192 * 2) {
            std::fprintf(stderr, "High-resolution centre-sample mismatch in fixture %u\n", test);
            return 1;
        }
        if (test < 16) {
            auto splitMemory = integrated;
            gs_gpu::HighResolutionSurface split;
            assert(
                gs_gpu::rasterize(std::span(draws).first(2), {0, 63, 0, 63}, splitMemory.data(), &recording, &split));
            assert(gs_gpu::rasterize(std::span(draws).last(2), {0, 63, 0, 63}, splitMemory.data(), &recording, &split));
            assert(splitMemory == expected);
            assert(split.pixels == surface.pixels);
        }
        // Keep two command groups across recycling of the guest texture slot.
        // Both successful compute and failed-compute replay must use the owned
        // old source image, and preserve every unrelated byte of VRAM.
        for (const bool loseDevice : {false, true}) {
            auto stagedMemory = integrated;
            auto stagedExpected = expected;
            auto image = std::make_shared<gsr::PackImage>();
            image->width = image->height = 16;
            image->pixels = texture;
            auto ownedDraws = draws;
            for (auto& draw : ownedDraws)
                if (draw.primitive->state.prim.tme) {
                    draw.image = image;
                    draw.texture = image->pixels;
                }
            if (!loseDevice && primitives.front().state.prim.tme) {
                auto resident = integrated;
                const auto leases = image.use_count();
                assert(gs_gpu::rasterize(ownedDraws, {0, 63, 0, 63}, resident.data(), &recording));
                assert(image.use_count() == leases); // GPU residency must not pin the CPU pack cache.
                const auto firstUpload = recording.lastTextureWords();
                assert(resident == expected);
                resident = integrated;
                assert(gs_gpu::rasterize(ownedDraws, {0, 63, 0, 63}, resident.data(), &recording));
                assert(recording.lastTextureWords() + texture.size() == firstUpload);
                assert(resident == expected);
            }
            GSCpuBackend recovered;
            recovered.Initialize(stagedMemory.data(), unsigned(stagedMemory.size()));
            gs_gpu::Deferred deferred;
            assert(deferred.append(std::span(ownedDraws).first(2), 0));
            if (test >= 96)
                assert(!deferred.touches({320u * 8192, 320u * 8192 + 8192}));
            assert(deferred.append(std::span(ownedDraws).last(2), 0));
            if (test >= 96)
                assert(deferred.touches({320u * 8192, 320u * 8192 + 8192}));
            assert(deferred.size() == 4);
            assert(!deferred.touchesSource(primitives.front().state));
            auto feedback = primitives.front().state;
            feedback.prim.tme = true;
            feedback.context.tex0.tbp0 = feedback.context.frame.fbp * 32;
            assert(deferred.touchesSource(feedback));
            auto incompatible = primitives.front();
            incompatible.state.context.frame.fbp += 1;
            auto invalidDraw = ownedDraws.front();
            invalidDraw.primitive = &incompatible;
            assert(!deferred.append(std::span(&invalidDraw, 1), 0));
            assert(deferred.size() == 4);
            if (test >= 96) {
                incompatible = primitives.back();
                incompatible.state.context.zbuf.zbp += 16;
                invalidDraw = ownedDraws.back();
                invalidDraw.primitive = &incompatible;
                assert(!deferred.append(std::span(&invalidDraw, 1), 0));
                assert(deferred.size() == 4); // Real depth storage changes still require a fence.
            }
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    const uint32_t next = 0x807f3412;
                    GSMem::WriteCT32(stagedMemory.data(), 32, 1, x, y, next);
                    GSMem::WriteCT32(stagedExpected.data(), 32, 1, x, y, next);
                }
            FailedCompute lost;
            assert(deferred.flush(recovered, loseDevice ? &lost : nullptr) == !loseDevice);
            assert(deferred.empty());
            const auto mismatch = std::mismatch(stagedExpected.begin(), stagedExpected.end(), stagedMemory.begin());
            if (mismatch.first != stagedExpected.end()) {
                std::fprintf(stderr, "Deferred fixture %u loss=%d differs at byte %zu: CPU=%02x actual=%02x\n", test,
                             loseDevice, size_t(mismatch.first - stagedExpected.begin()), *mismatch.first,
                             *mismatch.second);
                return 1;
            }
        }
        if (test % 8 == 4) {
            auto uploadedCpu = integrated, uploadedGpu = integrated;
            std::vector<uint32_t> nextTexture(16 * 16, 0x807f3412);
            for (const bool useGpu : {false, true}) {
                setenv("PS2X_CPU_RASTER_THREADS", useGpu ? "8" : "1", 1);
                setenv("DRAKAN_GPU_RASTER", useGpu ? "1" : "0", 1);
                auto& memory = useGpu ? uploadedGpu : uploadedCpu;
                GSCpuBackend backend;
                backend.Initialize(memory.data(), unsigned(memory.size()));
                const auto jobsBefore = gs_gpu::completedJobs();
                backend.Submit(primitives[0]);
                backend.Submit(primitives[1]);
                GSTransferCommand upload{};
                upload.direction = 0;
                upload.bitbltbuf.dbp = 32;
                upload.bitbltbuf.dbw = 1;
                upload.bitbltbuf.dpsm = GS_PSM_CT32;
                upload.trxreg.rrw = upload.trxreg.rrh = 16;
                backend.BeginTransfer(upload);
                backend.UploadImage(reinterpret_cast<const uint8_t*>(nextTexture.data()),
                                    unsigned(nextTexture.size() * 4));
                // A disjoint upload must not force a GPU submit/readback.
                if (useGpu)
                    assert(gs_gpu::completedJobs() == jobsBefore);
                backend.TextureFlush();
                backend.Submit(primitives[2]);
                backend.Submit(primitives[3]);
                backend.Flush();
                if (useGpu)
                    assert(gs_gpu::completedJobs() == jobsBefore + 1);
            }
            setenv("PS2X_CPU_RASTER_THREADS", "1", 1);
            unsetenv("DRAKAN_GPU_RASTER");
            const auto mismatch = std::mismatch(uploadedCpu.begin(), uploadedCpu.end(), uploadedGpu.begin());
            if (mismatch.first != uploadedCpu.end()) {
                std::fprintf(stderr, "Upload/recycle fixture %u differs at byte %zu\n", test,
                             size_t(mismatch.first - uploadedCpu.begin()));
                return 1;
            }
        }
        if (test == 4) {
            // Switching destination and sampling the previous destination must
            // resolve queued writes before capturing the new source image.
            for (unsigned boundary = 0; boundary < 4; ++boundary) {
                auto boundaryCpu = integrated, boundaryGpu = integrated;
                for (const bool useGpu : {false, true}) {
                    setenv("PS2X_CPU_RASTER_THREADS", useGpu ? "8" : "1", 1);
                    setenv("DRAKAN_GPU_RASTER", useGpu ? "1" : "0", 1);
                    auto& memory = useGpu ? boundaryGpu : boundaryCpu;
                    GSCpuBackend backend;
                    backend.Initialize(memory.data(), unsigned(memory.size()));
                    const auto before = gs_gpu::completedJobs();
                    backend.Submit(primitives[0]);
                    backend.Submit(primitives[1]);
                    if (boundary == 0) {
                        auto feedback = primitives[2];
                        feedback.state.context.frame.fbp += 64;
                        feedback.state.context.tex0.tbp0 = primitives[0].state.context.frame.fbp * 32;
                        backend.Submit(feedback);
                        backend.Submit(feedback);
                    } else if (boundary == 1) {
                        auto packed = primitives[2];
                        packed.state.context.frame.psm = GS_PSM_CT16;
                        backend.Submit(packed); // CPU fallback must see the prior GPU writes.
                    } else if (boundary == 2) {
                        GSTransferCommand read{};
                        read.direction = 1;
                        read.bitbltbuf.sbp = primitives[0].state.context.frame.fbp * 32;
                        read.bitbltbuf.sbw = 1;
                        read.bitbltbuf.spsm = GS_PSM_CT32;
                        read.trxreg.rrw = read.trxreg.rrh = 16;
                        backend.BeginTransfer(read);
                        if (useGpu)
                            assert(gs_gpu::completedJobs() == before + 1);
                        std::vector<uint8_t> output(16 * 16 * 4);
                        assert(backend.ConsumeLocalToHostBytes(output.data(), unsigned(output.size())) ==
                               output.size());
                        for (unsigned y = 0; y < 16; ++y)
                            for (unsigned x = 0; x < 16; ++x) {
                                uint32_t pixel = 0;
                                std::memcpy(&pixel, output.data() + (y * 16 + x) * 4, 4);
                                assert(pixel == GSMem::ReadCT32(memory.data(), read.bitbltbuf.sbp, 1, x, y));
                            }
                    }
                    if (boundary == 3) {
                        auto indexed = primitives[2];
                        indexed.state.context.frame.fbp += 64;
                        auto& tex = indexed.state.context.tex0;
                        tex.psm = GS_PSM_T8;
                        tex.cpsm = GS_PSM_CT32;
                        tex.csm = tex.csa = 0;
                        tex.cld = 1;
                        tex.cbp = primitives[0].state.context.frame.fbp * 32;
                        backend.LoadClut(tex, {});
                        if (useGpu)
                            assert(gs_gpu::completedJobs() == before + 1);
                        backend.Submit(indexed);
                        backend.Submit(indexed);
                    }
                    backend.Flush();
                    if (useGpu)
                        assert(gs_gpu::completedJobs() > before);
                }
                setenv("PS2X_CPU_RASTER_THREADS", "1", 1);
                unsetenv("DRAKAN_GPU_RASTER");
                const auto mismatch = std::mismatch(boundaryCpu.begin(), boundaryCpu.end(), boundaryGpu.begin());
                if (mismatch.first != boundaryCpu.end()) {
                    std::fprintf(stderr, "GPU boundary %u differs at byte %zu\n", boundary,
                                 size_t(mismatch.first - boundaryCpu.begin()));
                    return 1;
                }
            }
        }
        if (test < 48 || test >= 96) {
            setenv("PS2X_CPU_RASTER_THREADS", "8", 1);
            setenv("DRAKAN_GPU_RASTER", "1", 1);
            GSCpuBackend queued;
            queued.Initialize(integrated.data(), unsigned(integrated.size()));
            const auto before = gs_gpu::completedJobs();
            for (const auto& primitive : primitives)
                queued.Submit(primitive);
            queued.Flush();
            if (gs_gpu::completedJobs() == before) {
                std::fprintf(stderr, "Fixture %u never dispatched GPU work\n", test);
                return 1;
            }
            if (test >= 96)
                assert(gs_gpu::completedJobs() == before + 1);
            const auto mismatch = std::mismatch(expected.begin(), expected.end(), integrated.begin());
            if (mismatch.first != expected.end()) {
                std::fprintf(stderr, "Integrated GPU fixture %u differs at byte %zu: CPU=%02x GPU=%02x\n", test,
                             size_t(mismatch.first - expected.begin()), *mismatch.first, *mismatch.second);
                return 1;
            }
            setenv("PS2X_CPU_RASTER_THREADS", "1", 1);
            unsetenv("DRAKAN_GPU_RASTER");
        }
        const auto difference = std::mismatch(expected.begin(), expected.end(), actual.begin());
        if (difference.first != expected.end()) {
            std::fprintf(stderr, "GPU fixture %u differs at byte %zu: CPU=%02x GPU=%02x\n", test,
                         size_t(difference.first - expected.begin()), *difference.first, *difference.second);
            return 1;
        }
    }
    {
        // A replacement has four texels inside each native pixel. Shading
        // fractional positions must reveal them, while the centre stays exact.
#if defined(_WIN32)
        _putenv_s("DRAKAN_GPU_SCALE", "3");
#else
        setenv("DRAKAN_GPU_SCALE", "3", 1);
#endif
        std::vector<uint8_t> memory(GSMem::MEMORY_SIZE), native = memory;
        GSPrimitiveBatch sprite{};
        sprite.vertexCount = 2;
        auto& s = sprite.state;
        s.prim.type = GS_PRIM_SPRITE;
        s.prim.tme = s.prim.fst = true;
        s.context.frame = {128, 1, GS_PSM_CT32, 0};
        s.context.zbuf = {320, GS_PSM_Z32, true};
        s.context.test = 1u << 17;
        s.context.scissor = {0, 15, 0, 15};
        s.context.tex0.tfx = s.context.tex0.tcc = 1;
        s.textureWidth = s.textureHeight = 16;
        sprite.vertices[1].x = sprite.vertices[1].y = 16;
        sprite.vertices[1].u = sprite.vertices[1].v = 256;
        auto image = std::make_shared<gsr::PackImage>();
        image->width = image->height = 64;
        image->scaleX = image->scaleY = 4;
        image->pixels.resize(64 * 64);
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 64; ++x)
                image->pixels[y * 64 + x] = 0x80000000u | x | (y << 8);
        const gs_gpu::Draw draw{&sprite, {0, 15, 0, 15}, image->pixels, 64, 64, 4, 4, image, true};
        assert(gs_gpu::rasterize(std::span(&draw, 1), {0, 15, 0, 15}, native.data(), &recording));
        gs_gpu::HighResolutionCache cache;
        auto* surface = cache.acquire(s.context);
        assert(surface);
        assert(gs_gpu::rasterize(std::span(&draw, 1), {0, 15, 0, 15}, memory.data(), &recording, surface));
        assert(memory == native);
        auto sparseMemory = std::vector<uint8_t>(GSMem::MEMORY_SIZE);
        gs_gpu::HighResolutionSurface sparse;
        auto half = draw;
        half.bounds = {0, 7, 0, 15};
        assert(gs_gpu::rasterize(std::span(&half, 1), half.bounds, sparseMemory.data(), &recording, &sparse));
        half.bounds = {8, 15, 0, 15};
        assert(gs_gpu::rasterize(std::span(&half, 1), half.bounds, sparseMemory.data(), &recording, &sparse));
        assert(sparseMemory == native && sparse.pixels == surface->pixels);
        const auto frame = [&] {
            PresentationFrame result;
            result.width = result.height = 16;
            result.pixels.resize(640 * 16 * 4);
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x) {
                    const auto color = GSMem::ReadCT32(memory.data(), 128 * 32, 1, x, y) | 0xff000000u;
                    std::memcpy(result.pixels.data() + (y * 640 + x) * 4, &color, 4);
                }
            return result;
        };
        auto sharp = frame();
        assert(cache.present(sharp, 128 * 32, 1, 0, 0));
        assert(sharp.width == 48 && sharp.height == 48 && sharp.pixels.size() == 48 * 48 * 4);
        assert(sharp.pixels[(12 * 48 + 12) * 4] != sharp.pixels[(12 * 48 + 13) * 4]);
        const auto beforeEmpty = memory;
        const auto beforeEmptyPixels = surface->pixels;
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto invisibleSprite = sprite;
            invisibleSprite.vertices[0].x = invisibleSprite.vertices[0].y = 5;
            invisibleSprite.vertices[1].x = axis == 1 ? 12 : 5;
            invisibleSprite.vertices[1].y = axis == 0 ? 12 : 5;
            auto invisible = draw;
            invisible.primitive = &invisibleSprite;
            assert(gs_gpu::rasterize(std::span(&invisible, 1), {0, 15, 0, 15}, memory.data(), &recording, surface));
            assert(memory == beforeEmpty && surface->pixels == beforeEmptyPixels);
        }
        cache.invalidateMemory(gs_gpu::memoryRange(GS_PSM_CT32, 32, 1, 0, 0, 16, 16));
        auto disjoint = frame();
        assert(cache.present(disjoint, 128 * 32, 1, 0, 0));
        assert(disjoint.pixels == sharp.pixels);
        // Same-value CPU writes still replace every subpixel. Comparing only
        // native VRAM centres would miss this invalidation.
        const auto original = GSMem::ReadCT32(memory.data(), 128 * 32, 1, 4, 4) | 0xff000000u;
        auto cpu = sprite;
        cpu.state.prim.tme = false;
        cpu.vertices[0].x = cpu.vertices[0].y = 4;
        cpu.vertices[1].x = cpu.vertices[1].y = 5;
        cache.invalidateDraw(cpu);
        auto mixed = frame();
        assert(cache.present(mixed, 128 * 32, 1, 0, 0));
        for (unsigned y = 12; y < 15; ++y)
            for (unsigned x = 12; x < 15; ++x) {
                uint32_t color;
                std::memcpy(&color, mixed.pixels.data() + (y * 48 + x) * 4, 4);
                assert(color == original);
            }
        assert(mixed.pixels[0] == sharp.pixels[0]);
        cache.invalidateMemory(gs_gpu::memoryRange(GS_PSM_CT32, 128 * 32, 1, 0, 0, 16, 16));
        auto invalid = frame();
        assert(!cache.present(invalid, 128 * 32, 1, 0, 0));
        assert(invalid.width == 16);
        std::puts("High-resolution detail, native centres, disjoint uploads and CPU-write invalidation passed");
    }
    for (const unsigned psm : {unsigned(GS_PSM_CT32), unsigned(GS_PSM_CT24)}) {
#if defined(_WIN32)
        _putenv_s("PS2X_CPU_RASTER_THREADS", "2");
#else
        setenv("PS2X_CPU_RASTER_THREADS", "2", 1);
#endif
        std::vector<uint8_t> initial(GSMem::MEMORY_SIZE);
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x) {
                GSMem::WriteCT32(initial.data(), 32, 1, x, y, 0x40000000u | x * 11 | (y * 13 << 8));
                GSMem::WriteCT32(initial.data(), 64, 1, x, y, 0x80000000u | 0x25 | (x * 7 << 16));
            }
        auto cpuMemory = initial, gpuMemory = initial;
        GSCpuBackend cpu, gpu;
        cpu.Initialize(cpuMemory.data(), unsigned(cpuMemory.size()));
        gpu.Initialize(gpuMemory.data(), unsigned(gpuMemory.size()));
        std::array<GSPrimitiveBatch, 2> sprites{};
        for (unsigned i = 0; i < 2; ++i) {
            auto& b = sprites[i];
            auto& s = b.state;
            b.vertexCount = 2;
            s.prim.type = GS_PRIM_SPRITE;
            s.prim.tme = s.prim.fst = true;
            s.context.frame = {128 + 64 * i, 2, i ? unsigned(GS_PSM_CT32) : psm, 0};
            s.context.zbuf = {320, GS_PSM_Z32, true};
            s.context.scissor = {0, 15, 0, 15};
            s.context.test = 1u << 17;
            s.context.tex0.tbp0 = 32 + 32 * i;
            s.context.tex0.tbw = 1;
            s.context.tex0.psm = GS_PSM_CT32;
            s.context.tex0.tfx = s.context.tex0.tcc = 1;
            s.textureWidth = s.textureHeight = 16;
            s.linearFilter = true;
            b.vertices[1].x = b.vertices[1].y = 16;
            b.vertices[1].u = b.vertices[1].v = 256;
        }
#if defined(_WIN32)
        _putenv_s("DRAKAN_GPU_RASTER", "");
#else
        unsetenv("DRAKAN_GPU_RASTER");
#endif
        for (const auto& b : sprites)
            cpu.Submit(b);
        cpu.Flush();
#if defined(_WIN32)
        _putenv_s("DRAKAN_GPU_RASTER", "1");
#else
        setenv("DRAKAN_GPU_RASTER", "1", 1);
#endif
        for (const auto& b : sprites)
            gpu.Submit(b);
        gpu.Flush();
        assert(cpuMemory == gpuMemory);
        for (const bool narrow : {false, true})
            for (const bool background : {false, true})
                for (const bool constant : {false, true})
                    for (const unsigned factor : {0u, 1u, 127u, 128u, 255u}) {
                        GSPresentationRequest request{};
                        request.pmode =
                            3 | (uint64_t(constant) << 5) | (uint64_t(background) << 7) | (uint64_t(factor) << 8);
                        request.bgcolor = 0x665544;
                        request.dispfb1 = 128 | (2ull << 9) | (uint64_t(psm) << 15);
                        request.dispfb2 = 192 | (2ull << 9);
                        request.display1 = (uint64_t(narrow ? 63 : 127) << 32) | (uint64_t(narrow ? 63 : 127) << 44);
                        request.display2 = (127ull << 32) | (127ull << 44);
                        if (narrow)
                            request.dispfb1 |= (4ull << 32) | (4ull << 43);
                        const auto native = cpu.Present(request), high = gpu.Present(request);
                        assert(native.width == 128 && native.height == 128 && high.width == 384 && high.height == 384);
                        for (unsigned y = 0; y < 128; ++y)
                            for (unsigned x = 0; x < 128; ++x) {
                                uint32_t a, b;
                                std::memcpy(&a, native.pixels.data() + (y * 640 + x) * 4, 4);
                                std::memcpy(&b, high.pixels.data() + ((y * 3 + 1) * 384 + x * 3 + 1) * 4, 4);
                                assert(a == b);
                            }
                    }
#if defined(_WIN32)
        _putenv_s("DRAKAN_GPU_RASTER", "");
#else
        unsetenv("DRAKAN_GPU_RASTER");
#endif
    }
    std::puts("High-resolution dual-CRT composition matches native centres for alpha, backgrounds and read origins");
    {
        // Cross the 128 MiB residency budget with distinct immutable HD images.
        // Revisit the first image after eviction; stale offsets must not survive.
        auto residentDevice = gsr::gpu::createCompute();
        assert(residentDevice);
        std::vector<uint8_t> memory(GSMem::MEMORY_SIZE);
        GSPrimitiveBatch primitive{};
        primitive.vertexCount = 2;
        auto& state = primitive.state;
        state.prim.type = GS_PRIM_SPRITE;
        state.prim.tme = state.prim.fst = true;
        state.context.frame = {128, 1, GS_PSM_CT32, 0};
        state.context.zbuf = {320, GS_PSM_Z32, true};
        state.context.test = 1u << 17;
        state.context.tex0.tfx = state.context.tex0.tcc = 1;
        state.textureWidth = state.textureHeight = 16;
        primitive.vertices[1].x = primitive.vertices[1].y = 16;
        primitive.vertices[1].u = primitive.vertices[1].v = 256;
        std::shared_ptr<gsr::PackImage> first;
        for (unsigned i = 0; i < 35; ++i) {
            auto image = i == 34 ? first : std::make_shared<gsr::PackImage>();
            const uint32_t color = 0x80000000u | (i == 34 ? 0u : i * 0x070301u);
            if (i != 34) {
                image->width = image->height = 1024;
                image->scaleX = image->scaleY = 64;
                image->pixels.assign(1024 * 1024, color);
            }
            if (i == 0)
                first = image;
            const gs_gpu::Draw draw{&primitive, {0, 15, 0, 15}, image->pixels, 1024, 1024, 64, 64, image, true};
            assert(gs_gpu::rasterize(std::span(&draw, 1), {0, 15, 0, 15}, memory.data(), residentDevice.get()));
            for (unsigned y = 0; y < 16; ++y)
                for (unsigned x = 0; x < 16; ++x)
                    assert(GSMem::ReadCT32(memory.data(), 128 * 32, 1, x, y) == color);
        }
        std::puts("Resident HD atlas reuse, capacity reset and evicted-source reload passed");
    }
    {
        std::vector<uint8_t> memory(GSMem::MEMORY_SIZE, 0x5a);
        const auto original = memory;
        GSPrimitiveBatch primitive{};
        primitive.vertexCount = 3;
        primitive.state.prim.type = GS_PRIM_TRIANGLE;
        primitive.state.context.frame = {128, 1, GS_PSM_CT32, 0};
        primitive.state.context.zbuf = {320, GS_PSM_Z32, false};
        primitive.state.context.test = 1u << 17;
        const gs_gpu::Draw draw{&primitive, {0, 63, 0, 63}, {}};
        FailedCompute lost;
        gs_gpu::HighResolutionSurface lostSurface;
        assert(!gs_gpu::rasterize(std::span(&draw, 1), {0, 63, 0, 63}, memory.data(), &lost, &lostSurface));
        assert(lostSurface.pixels.empty());
        assert(memory == original); // A partial GPU result cannot poison CPU fallback.
    }
    std::printf(
        "192 portable GPU fixtures match all 4 MiB of CPU VRAM; deferred/recycled/lost-device cases match; jobs=%llu\n",
        static_cast<unsigned long long>(gs_gpu::completedJobs()));
}
