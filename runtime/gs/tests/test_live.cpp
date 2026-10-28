// gsr_test_live: toggles the live settings (Renderer::set*) while a DRKGSCAP capture is replayed, from the replay
// thread and from a second "UI" thread, and checks that nothing crashes, the settings take effect at the next frame
// boundary, the display stays valid and memory does not grow when the settings are cycled (render targets / texture
// caches are released).
//   gsr_test_live [capture.bin] [--frames N] [--warp]
#include "gsr/gsr.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#ifndef GSR_DEFAULT_CAPTURE
#define GSR_DEFAULT_CAPTURE "gscap.bin"
#endif
#ifndef GSR_OUT_DIR
#define GSR_OUT_DIR "."
#endif

using namespace gsr;

static int g_pass = 0, g_fail = 0;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            std::printf("  FAIL %s:%d  %s  ", __FILE__, __LINE__, #c);                                                 \
            std::printf(__VA_ARGS__);                                                                                  \
            std::printf("\n");                                                                                         \
        }                                                                                                              \
    } while (0)

static size_t workingSet() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    return (size_t)pmc.PrivateUsage;
}

struct Capture {
    struct Rec {
        uint32_t type;
        std::vector<uint8_t> data;
    };
    std::vector<Rec> recs;
    size_t vsyncs = 0;
    bool load(const std::string& path, size_t maxVsync) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return false;
        char hdr[32];
        if (std::fread(hdr, 1, 32, f) != 32 || std::memcmp(hdr, "DRKGSCAP", 8) != 0) {
            std::fclose(f);
            return false;
        }
        uint32_t type = 0, size = 0;
        while (vsyncs < maxVsync && std::fread(&type, 4, 1, f) == 1 && std::fread(&size, 4, 1, f) == 1) {
            Rec r;
            r.type = type;
            r.data.resize(size);
            if (size && std::fread(r.data.data(), 1, size, f) != size)
                break;
            if (type == 4)
                ++vsyncs;
            recs.push_back(std::move(r));
        }
        std::fclose(f);
        return vsyncs > 0;
    }
};

// feeds records until the next VSYNC (inclusive); returns false at the end of the capture
static bool playFrame(Renderer& gs, const Capture& cap, size_t& pos) {
    static const uint32_t privOff[20] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50,       0x60,       0x70,   0x80,   0x90,
                                         0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xFFFFFFFF, 0xFFFFFFFF, 0x1010, 0x1040, 0x1080};
    while (pos < cap.recs.size()) {
        const Capture::Rec& r = cap.recs[pos++];
        if (r.type == 2 && r.data.size() >= 8) {
            uint32_t psize;
            std::memcpy(&psize, r.data.data() + 4, 4);
            const uint8_t path = r.data[0];
            gs.submitGifPacket(path == 1   ? GifPath::Path1
                               : path == 2 ? GifPath::Path2
                                           : GifPath::Path3,
                               r.data.data() + 8, std::min<uint32_t>(psize, (uint32_t)r.data.size() - 8));
        } else if (r.type == 3 && r.data.size() >= 160) {
            uint64_t regs[20];
            std::memcpy(regs, r.data.data(), sizeof(regs));
            for (int i = 0; i < 20; ++i)
                if (privOff[i] != 0xFFFFFFFFu)
                    gs.writePriv(privOff[i], regs[i]);
        } else if (r.type == 4) {
            gs.vsync(false);
            return true;
        }
    }
    return false;
}

static double meanLuma(const Image& im) {
    double s = 0;
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4)
        s += im.rgba[i] + im.rgba[i + 1] + im.rgba[i + 2];
    return im.rgba.empty() ? 0 : s / (double)(im.rgba.size() / 4 * 3);
}

static void runPass(const char* name, const Capture& cap, bool sync, bool warp, const std::string& texDir) {
    std::printf("[%s]\n", name);
    Config cfg;
    cfg.vsync = false;
    cfg.forceWarp = warp;
    cfg.textureUpscaleSync = sync;
    cfg.textureDir = texDir;
    cfg.textureUpscale = 1; // start with upscaling off so the toggles below have something to do
    auto gs = Renderer::create(cfg);
    if (!gs) {
        CHECK(false, "renderer creation failed");
        return;
    }
    std::printf("  adapter %s\n", gs->adapterName().c_str());

    size_t pos = 0;
    int frame = 0;
    CHECK(gs->internalScale() == 1, "initial scale");
    // play on until the capture shows something (the first frames are black boot screens)
    for (int i = 0; i < 640; ++i) {
        if (!playFrame(*gs, cap, pos))
            break;
        ++frame;
        Image t;
        if (frame >= 10 && (frame % 8) == 0 && gs->captureDisplay(t) && meanLuma(t) > 5.0)
            break;
    }
    std::printf("  content from frame %d of %zu\n", frame, cap.vsyncs);
    const size_t wrapPos =
        pos; // wrapping the replay restarts here (the HUD is on screen) instead of at the black boot frames
    Image base;
    CHECK(gs->captureDisplay(base) && base, "baseline capture");
    const double baseLuma = meanLuma(base);
    CHECK(baseLuma > 2.0, "baseline frame is black (%.2f)", baseLuma);
    const uint32_t nativeW = base.width, nativeH = base.height;
    const size_t mem0 = workingSet();

    // ---- (a) deterministic checks: setters take effect at the next frame boundary, not before
    gs->setInternalScale(2);
    CHECK(gs->internalScale() == 1, "scale must not change before the frame boundary");
    playFrame(*gs, cap, pos);
    ++frame;
    CHECK(gs->internalScale() == 2, "scale after vsync: %u", gs->internalScale());
    Image im2;
    CHECK(gs->captureDisplay(im2) && im2.width == nativeW * 2 && im2.height == nativeH * 2,
          "capture at x2: %ux%u (native %ux%u)", im2.width, im2.height, nativeW, nativeH);
    CHECK(meanLuma(im2) > baseLuma * 0.5, "frame after the scale change is black / lost (%.2f vs %.2f)", meanLuma(im2),
          baseLuma);
    for (int i = 0; i < 5; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }
    Image im2b;
    CHECK(gs->captureDisplay(im2b) && meanLuma(im2b) > baseLuma * 0.5, "frames after the change recovered");
    gs->setInternalScale(1);
    playFrame(*gs, cap, pos);
    ++frame;
    Image im1;
    CHECK(gs->captureDisplay(im1) && im1.width == nativeW && im1.height == nativeH, "back to x1");

    gs->setVsync(true);
    CHECK(gs->vsyncEnabled(), "vsyncEnabled reports the requested value");
    gs->setVsync(false);
    const uint64_t applied0 = gs->stats().liveApplied;
    gs->setAspect(16.0f / 9.0f, false);
    gs->setTextureFilter(2);
    gs->setDeinterlace(2);
    gs->setFlickerFilter(true);
    gs->setSharpPresent(false);
    gs->setAnisotropic(8);
    gs->setMipBias(-1.0f, false);
    gs->setFrameLatency(2);
    playFrame(*gs, cap, pos);
    ++frame;
    CHECK(gs->stats().liveApplied == applied0 + 1, "all queued settings are applied as one batch (%llu)",
          (unsigned long long)(gs->stats().liveApplied - applied0));
    Image imf;
    CHECK(gs->captureDisplay(imf) && imf.width == nativeW, "capture after option changes");

    // ---- (b) texture settings: upscale factor / filter, HD replace, rescan, dump
    gs->setTextureUpscale(2, 1);
    for (int i = 0; i < 4; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }
    gs->setTextureUpscale(2, 2); // filter change flushes
    for (int i = 0; i < 3; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }
    gs->setTextureReplace(false);
    gs->setTextureReplace(true); // folder rescanned
    gs->rescanTextures();
    for (int i = 0; i < 3; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }
    gs->setTextureDump(true);
    for (int i = 0; i < 3; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }
    gs->setTextureDump(false);
    gs->setTextureUpscale(1, 0);
    for (int i = 0; i < 3; ++i) {
        playFrame(*gs, cap, pos);
        ++frame;
    }

    // ---- (c) cycling: memory must not grow (render targets / texture caches are released on each change)
    for (int round = 0; round < 2; ++round) // warm-up: driver pools, texture cache high-water mark
        for (uint32_t s : {2u, 3u, 1u}) {
            gs->setInternalScale(s);
            for (int i = 0; i < 2; ++i) {
                if (!playFrame(*gs, cap, pos))
                    pos = wrapPos;
                ++frame;
            }
        }
    const size_t memCycleStart = workingSet();
    for (int round = 0; round < 8; ++round) {
        for (uint32_t s : {2u, 3u, 1u}) {
            gs->setInternalScale(s);
            gs->setTextureUpscale(round % 2 ? 2 : 1, round % 3);
            for (int i = 0; i < 2; ++i) {
                if (!playFrame(*gs, cap, pos))
                    pos = wrapPos;
                ++frame;
            }
        }
        std::printf("    round %d: %.0f MB\n", round, workingSet() / 1048576.0);
    }
    const size_t memCycleEnd = workingSet();
    std::printf("  private bytes: start %.0f MB, before cycles %.0f MB, after %.0f MB\n", mem0 / 1048576.0,
                memCycleStart / 1048576.0, memCycleEnd / 1048576.0);
    CHECK(memCycleEnd < memCycleStart + 150ull * 1048576, "memory grew by %.0f MB while cycling settings",
          (memCycleEnd - memCycleStart) / 1048576.0);

    // ---- (d) hammer the setters from another thread while the replay thread keeps submitting and presenting
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> calls{0};
    std::thread ui([&] {
        uint32_t x = 12345;
        auto rnd = [&] {
            x = x * 1664525u + 1013904223u;
            return x >> 8;
        };
        while (!stop) {
            switch (rnd() % 14) {
                case 0:
                    gs->setAspect((rnd() & 1) ? 0.0f : (rnd() & 1) ? 4.0f / 3.0f : 16.0f / 9.0f, (rnd() & 1) != 0);
                    break;
                case 1:
                    gs->setTextureFilter(rnd() % 3);
                    break;
                case 2:
                    gs->setDeinterlace(rnd() % 3);
                    break;
                case 3:
                    gs->setFlickerFilter((rnd() & 1) != 0);
                    break;
                case 4:
                    gs->setSharpPresent((rnd() & 1) != 0);
                    break;
                case 5:
                    gs->setAnisotropic(1u << (rnd() % 5));
                    break;
                case 6:
                    gs->setMipBias((float)(int)(rnd() % 5) - 2.0f, (rnd() & 1) != 0);
                    break;
                case 7:
                    gs->setTextureUpscale(rnd() % 4 == 0 ? 0 : 1u << (rnd() % 3), rnd() % 3);
                    break;
                case 8:
                    gs->setTextureReplace((rnd() & 1) != 0);
                    break;
                case 9:
                    gs->rescanTextures();
                    break;
                case 10:
                    gs->setInternalScale(1 + rnd() % 4);
                    break;
                case 11:
                    gs->setVsync(false);
                    break;
                case 12:
                    gs->setFrameLatency(1 + rnd() % 3);
                    break;
                default:
                    gs->setTextureDump(false);
                    break;
            }
            ++calls;
            std::this_thread::yield();
        }
    });
    int hammered = 0;
    for (int i = 0; i < 200; ++i) {
        if (!playFrame(*gs, cap, pos))
            pos = wrapPos;
        ++hammered;
        if ((i % 10) == 0) {
            Image t;
            gs->captureDisplay(t);
        }
        gs->presentLatched();
    }
    stop = true;
    ui.join();
    std::printf("  %llu setter calls from the UI thread during %d frames, %llu batches applied, %llu scale changes\n",
                (unsigned long long)calls.load(), hammered, (unsigned long long)gs->stats().liveApplied,
                (unsigned long long)gs->stats().liveScaleChanges);
    CHECK(calls > 20, "UI thread barely ran");
    // settle: back to defaults, the display must still render something sensible
    gs->setInternalScale(1);
    gs->setAspect(0, false);
    gs->setTextureFilter(0);
    gs->setDeinterlace(0);
    gs->setFlickerFilter(false);
    gs->setSharpPresent(true);
    gs->setAnisotropic(1);
    gs->setMipBias(0, true);
    gs->setTextureUpscale(1, 0);
    for (int i = 0; i < 4; ++i) {
        if (!playFrame(*gs, cap, pos))
            pos = wrapPos;
    }
    Image fin;
    CHECK(gs->captureDisplay(fin) && fin.width == nativeW && fin.height == nativeH && meanLuma(fin) > 2.0,
          "final frame valid");
    if (fin)
        writePng((std::string(GSR_OUT_DIR) + "\\live_" + name + "_final.png").c_str(), fin);
    CHECK(gs->valid(), "renderer still valid");
    gs.reset(); // destroy with settings in flight / worker jobs possibly pending
    CHECK(true, "destroyed cleanly");
}

int main(int argc, char** argv) {
    std::string capPath = GSR_DEFAULT_CAPTURE;
    size_t frames = 700;
    bool warp = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = (size_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--warp"))
            warp = true;
        else
            capPath = argv[i];
    }
    if (!std::filesystem::exists(capPath)) {
        std::printf("SKIP: capture %s not found (captures are local game data)\n", capPath.c_str());
        return 77; // ctest SKIP_RETURN_CODE
    }
    Capture cap;
    if (!cap.load(capPath, frames)) {
        std::printf("cannot load capture %s\n", capPath.c_str());
        return 2;
    }
    std::printf("capture %s: %zu records, %zu vsyncs\n", capPath.c_str(), cap.recs.size(), cap.vsyncs);
    const std::string texDir = std::string(GSR_OUT_DIR) + "\\live_textures";
    std::filesystem::create_directories(texDir);
    runPass("sync", cap, true, warp, texDir);
    runPass("async", cap, false, warp, texDir);
    std::error_code ec;
    std::filesystem::remove_all(texDir, ec);
    std::printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
