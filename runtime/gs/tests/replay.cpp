// Replay a DRKGSCAP capture (see docs/reference/gs-capture.md / ps2recomp .../gs_capture.cpp) through gsr and
// save one screenshot per recorded vsync frame.
//   gsr_replay <capture.bin> [--out DIR] [--scale N] [--warp] [--max N] [--every N] [--first N]
//              [--bench A-B] [--bench-csv FILE] [--hashes FILE] [--no-png]
// --bench A-B times the renderer calls of the vsync frames A..B (inclusive) on the calling thread and prints the frame
// time distribution, the thread's CPU time and the renderer counters of that range; the capture is read outside the
// timed sections, so the numbers do not depend on the disk. --bench-csv FILE also writes one line per timed frame
// (frame, ms, vertices, draws, target row refreshes, readbacks). --hashes FILE writes "frame fnv1a64 width height" for
// every captured display image (compare two runs with a text diff); --no-png skips the PNG files.
//
// Record layout: 32 byte header ("DRKGSCAP", u32 version, u64 time) then records {u32 type, u32 size, payload}:
//   1 FRAME_START {u64 frame, u64 vsyncTick}
//   2 GIF_PACKET  {u8 path(1..3), u8 flags(bit0 = PATH2 DIRECTHL, bit1 = PATH3 image), u16 pad, u32 size, data[size]}
//   3 PRIV_REGS   {u64 x20: pmode smode1 smode2 srfsh synch1 synch2 syncv dispfb1 display1 dispfb2 display2 extbuf
//                  extdata extwrite bgcolor csr vsyncTick imr busdir siglblid}
//   4 VSYNC       {u64 vsyncTick}
// A capture does not contain an initial GS local memory image: textures uploaded before the capture started are
// missing, so start captures at boot (or accept missing textures).
#include "gsr/gsr.h"
#include "gsr/renderer_choice.h"
#include "gsr/vertex_path.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
// CPU time (user + kernel) of the calling thread in milliseconds; 0 where the platform call is missing.
double threadCpuMs() {
#ifdef _WIN32
    FILETIME c, e, k, u;
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
        return 0;
    const auto v = [](const FILETIME& f) { return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime; };
    return (double)(v(k) + v(u)) / 10000.0;
#else
    return 0;
#endif
}
uint64_t fnv1a64(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
} // namespace

using namespace gsr;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf(
            "usage: gsr_replay <capture.bin> [--out DIR] [--scale N] [--warp] [--max N] [--every N] [--first N]\n");
        return 1;
    }
    std::string in = argv[1];
    std::string outDir = GSR_OUT_DIR;
    Config cfg;
    cfg.vsync = false;
    cfg.textureUpscaleSync =
        true; // replays run far faster than real time: wait for upscale jobs so every screenshot is final
    int maxFrames = 1 << 30, every = 1, first = 0;
    int curFrame = 0;
    long fbArgs[5] = {-1, 0, 0, 0, 0}; // --fb <fbp hex> <fbw> <psm> <w> <h>: also save that frame buffer
                                       // (captureFramebuffer) with every screenshot as fb_<frame>.png
    int benchFirst = -1, benchLast = -1;
    std::string hashPath, benchCsvPath;
    bool writePngs = true;
    long upDbp = -1; // --upload <hex dbp>: write the raw bytes of every host->local transfer to that block base (frames
                     // >= --first) as up_<dbp>_f<frame>.bin
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            cfg.scale = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--warp"))
            cfg.forceWarp = true;
        else if (!std::strcmp(argv[i], "--gpu-api") && i + 1 < argc) // gsr_replay_webgpu: vulkan | d3d12 | opengl
            cfg.gpuApi = gsr::parseRendererChoice(argv[++i]).gpuApi;
        else if (!std::strcmp(argv[i], "--texup") && i + 1 < argc) {
            const char* v = argv[++i];
            cfg.textureUpscale = !std::strcmp(v, "auto") ? 0u : (uint32_t)std::atoi(v);
        } else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc) {
            const char* v = argv[++i];
            cfg.textureUpscaleFilter = !std::strcmp(v, "xbrz") ? 1u : !std::strcmp(v, "smooth") ? 2u : 0u;
        } else if (!std::strcmp(argv[i], "--aniso") && i + 1 < argc)
            cfg.anisotropic = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--mipbias") && i + 1 < argc) {
            const char* v = argv[++i];
            if (!std::strcmp(v, "auto"))
                cfg.mipBiasAuto = true;
            else {
                cfg.mipBiasAuto = false;
                cfg.mipBias = (float)std::atof(v);
            }
        } else if (!std::strcmp(argv[i], "--texdir") && i + 1 < argc)
            cfg.textureDir = argv[++i];
        else if (!std::strcmp(argv[i], "--dump"))
            cfg.textureDump = true;
        else if (!std::strcmp(argv[i], "--async"))
            cfg.textureUpscaleSync = false;
        else if (!std::strcmp(argv[i], "--fb") && i + 5 < argc) {
            fbArgs[0] = std::strtol(argv[++i], nullptr, 16);
            for (int k = 1; k < 5; ++k)
                fbArgs[k] = std::atol(argv[++i]);
        } else if (!std::strcmp(argv[i], "--upload") && i + 1 < argc)
            upDbp = std::strtol(argv[++i], nullptr, 16);
        else if (!std::strcmp(argv[i], "--bench") && i + 1 < argc) {
            const char* v = argv[++i];
            benchFirst = std::atoi(v);
            const char* dash = std::strchr(v, '-');
            benchLast = dash ? std::atoi(dash + 1) : benchFirst;
        } else if (!std::strcmp(argv[i], "--bench-csv") && i + 1 < argc)
            benchCsvPath = argv[++i];
        else if (!std::strcmp(argv[i], "--hashes") && i + 1 < argc)
            hashPath = argv[++i];
        else if (!std::strcmp(argv[i], "--no-png"))
            writePngs = false;
        else if (!std::strcmp(argv[i], "--max") && i + 1 < argc)
            maxFrames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--every") && i + 1 < argc)
            every = std::max(1, std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--first") && i + 1 < argc)
            first = std::atoi(argv[++i]);
    }
    FILE* f = std::fopen(in.c_str(), "rb");
    if (!f) {
        std::printf("cannot open %s\n", in.c_str());
        return 1;
    }
    char hdr[32];
    if (std::fread(hdr, 1, 32, f) != 32 || std::memcmp(hdr, "DRKGSCAP", 8) != 0) {
        std::printf("not a DRKGSCAP capture\n");
        return 1;
    }
    if (upDbp >= 0)
        cfg.onUpload = [&](const UploadEvent& ev) {
            if (ev.dbp != (uint32_t)upDbp || curFrame < first)
                return;
            char name[96];
            std::snprintf(name, sizeof(name), "/up_%x_f%d_psm%x_%ux%u.bin", ev.dbp, curFrame, ev.dpsm, ev.w, ev.h);
            if (FILE* o = std::fopen((outDir + name).c_str(), "wb")) {
                std::fwrite(ev.data, 1, ev.bytes, o);
                std::fclose(o);
            }
        };
    auto gs = Renderer::create(cfg);
    if (!gs) {
        std::printf("renderer creation failed\n");
        return 2;
    }
    std::printf("replaying %s on %s (scale %u)\n", in.c_str(), gs->adapterName().c_str(), cfg.scale);
    {
        // The replay has no host: it applies the vertex path switches itself (gsr/vertex_path.h). Records of a capture
        // are honoured under every setting; these decide whether a backend's vertex shader draws them
        // (DRAKAN_VERTEX_EVAL=gpu) and whether the path alternates inside the run (DRAKAN_VERTEX_TOGGLE=<frames>).
        vpath::Choice choice = vpath::Choice::Cpu;
        vpath::parseChoice(std::getenv("DRAKAN_VERTEX_PROCESSING"), choice);
        const char* toggle = std::getenv("DRAKAN_VERTEX_TOGGLE");
        vpath::configure(vpath::configuredWord(choice, gs->vertexPathCapability(), std::getenv("DRAKAN_VERTEX_EVAL"),
                                               std::getenv("DRAKAN_VERTEX_STAGES")),
                         toggle ? (uint32_t)std::strtoul(toggle, nullptr, 10) : 0u);
    }

    static const uint32_t privOff[20] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50,       0x60,       0x70,   0x80,   0x90,
                                         0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xFFFFFFFF, 0xFFFFFFFF, 0x1010, 0x1040, 0x1080};
    int frame = 0, saved = 0;
    FILE* hashFile = hashPath.empty() ? nullptr : std::fopen(hashPath.c_str(), "wb");
    using BenchClock = std::chrono::steady_clock;
    std::vector<double> benchFrameMs;
    double benchAcc = 0, benchCpu0 = 0, benchCpu = 0, benchDrainMs = 0;
    bool benchStarted = false;
    Stats benchStart{}, benchEnd{}, benchPrev{};
    FILE* benchCsv = benchCsvPath.empty() ? nullptr : std::fopen(benchCsvPath.c_str(), "wb");
    if (benchCsv)
        std::fprintf(benchCsv, "frame,ms,vertices,draws,rt_uploads,readbacks\n");
    const auto inBench = [&] { return benchFirst >= 0 && frame >= benchFirst && frame <= benchLast; };
    bool benchFinished = false;
    const auto finishBench = [&] {
        if (benchFinished || !benchStarted)
            return;
        benchFinished = true;
        benchCpu = threadCpuMs() - benchCpu0;
        benchEnd = gs->stats();
        // The GPU may still be working through the queued commands: a display capture maps the result, so its
        // duration is the backlog the timed frames left behind.
        const auto d0 = BenchClock::now();
        Image drain;
        gs->captureDisplay(drain);
        benchDrainMs = std::chrono::duration<double, std::milli>(BenchClock::now() - d0).count();
    };
    std::vector<uint8_t> buf;
    uint32_t type = 0, size = 0;
    while (std::fread(&type, 4, 1, f) == 1 && std::fread(&size, 4, 1, f) == 1) {
        buf.resize(size);
        if (size && std::fread(buf.data(), 1, size, f) != size)
            break;
        if (type == 2 && size >= 8) {
            uint32_t psize;
            std::memcpy(&psize, buf.data() + 4, 4);
            const uint8_t path = buf[0];
            const bool timed = inBench();
            if (timed && !benchStarted) {
                benchStarted = true;
                benchStart = benchPrev = gs->stats();
                benchCpu0 = threadCpuMs();
            }
            const auto t0 = timed ? BenchClock::now() : BenchClock::time_point{};
            gs->submitGifPacket(path == 1   ? GifPath::Path1
                                : path == 2 ? GifPath::Path2
                                            : GifPath::Path3,
                                buf.data() + 8, std::min<uint32_t>(psize, size - 8));
            if (timed)
                benchAcc += std::chrono::duration<double, std::milli>(BenchClock::now() - t0).count();
        } else if (type == 3 && size >= 160) {
            uint64_t regs[20];
            std::memcpy(regs, buf.data(), sizeof(regs));
            for (int i = 0; i < 20; ++i)
                if (privOff[i] != 0xFFFFFFFFu)
                    gs->writePriv(privOff[i], regs[i]);
        } else if (type == 4) {
            const bool timed = inBench();
            const auto t0 = timed ? BenchClock::now() : BenchClock::time_point{};
            gs->vsync(false);
            if (timed) {
                benchFrameMs.push_back(benchAcc +
                                       std::chrono::duration<double, std::milli>(BenchClock::now() - t0).count());
                benchAcc = 0;
                if (benchCsv) {
                    const Stats now = gs->stats();
                    std::fprintf(benchCsv, "%d,%.4f,%llu,%llu,%llu,%llu\n", frame, benchFrameMs.back(),
                                 (unsigned long long)(now.vertices - benchPrev.vertices),
                                 (unsigned long long)(now.drawCalls - benchPrev.drawCalls),
                                 (unsigned long long)(now.rtUploads - benchPrev.rtUploads),
                                 (unsigned long long)(now.rtReadbacks - benchPrev.rtReadbacks));
                    benchPrev = now;
                }
                if (frame == benchLast)
                    finishBench();
            }
            if (frame >= first && ((frame - first) % every) == 0) {
                Image img;
                if (gs->captureDisplay(img)) {
                    char name[64];
                    if (hashFile)
                        std::fprintf(hashFile, "%d %016llx %u %u\n", frame,
                                     (unsigned long long)fnv1a64(img.rgba.data(), img.rgba.size()), img.width,
                                     img.height);
                    std::snprintf(name, sizeof(name), "\\replay_%04d.png", frame);
                    if (writePngs)
                        writePng((outDir + name).c_str(), img);
                    ++saved;
                    Image fb;
                    if (writePngs && fbArgs[0] >= 0 &&
                        gs->captureFramebuffer((uint32_t)fbArgs[0], (uint32_t)fbArgs[1], (uint32_t)fbArgs[2],
                                               (uint32_t)fbArgs[3], (uint32_t)fbArgs[4], fb)) {
                        std::snprintf(name, sizeof(name), "/fb_%04d.png", frame);
                        writePng((outDir + name).c_str(), fb);
                    }
                }
            }
            ++frame;
            curFrame = frame;
            if (frame >= maxFrames)
                break;
        }
    }
    std::fclose(f);
    finishBench(); // a range that ends past the last recorded frame
    if (hashFile)
        std::fclose(hashFile);
    if (benchCsv)
        std::fclose(benchCsv);
    if (!benchFrameMs.empty()) {
        std::vector<double> sorted = benchFrameMs;
        std::sort(sorted.begin(), sorted.end());
        const auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, (size_t)(p * sorted.size()))]; };
        double sum = 0;
        for (double v : sorted)
            sum += v;
        const double n = (double)sorted.size();
        const auto per = [&](uint64_t Stats::* m) { return (double)(benchEnd.*m - benchStart.*m) / n; };
        std::printf("[bench] frames %d-%d (%zu): renderer thread wall ms/frame mean %.3f median %.3f p95 %.3f p99 %.3f "
                    "max %.3f; thread cpu ms/frame %.3f; total %.1f ms; gpu drain after last frame %.1f ms\n",
                    benchFirst, benchLast, sorted.size(), sum / n, pct(0.5), pct(0.95), pct(0.99), sorted.back(),
                    benchCpu / n, sum, benchDrainMs);
        std::printf("[bench] per frame: gif packets %.1f, vertices %.1f, draws %.2f, triangles %.1f, tex decodes %.3f, "
                    "tex hits %.2f, revalidated %.3f, rt samples %.3f, readbacks %.4f, readbacks skipped %.3f, "
                    "rt uploads %.3f, target uploads %.3f, gpu copies %.3f, h2l %.3f, l2l %.3f, l2h %.3f, clut loads "
                    "%.2f, shader blend draws %.2f, target refresh KiB %.1f\n",
                    per(&Stats::gifPackets), per(&Stats::vertices), per(&Stats::drawCalls), per(&Stats::triangles),
                    per(&Stats::textureDecodes), per(&Stats::textureCacheHits), per(&Stats::textureRevalidated),
                    per(&Stats::rtSamples), per(&Stats::rtReadbacks), per(&Stats::rtReadbacksSkipped),
                    per(&Stats::rtUploads), per(&Stats::targetUploads), per(&Stats::rtGpuCopies),
                    per(&Stats::transfersH2L), per(&Stats::transfersL2L), per(&Stats::transfersL2H),
                    per(&Stats::clutLoads), per(&Stats::shaderBlendDraws), per(&Stats::targetRefreshBytes) / 1024.0);
    }
    const Stats& s = gs->stats();
    std::printf("%d frames replayed, %d screenshots in %s\n", frame, saved, outDir.c_str());
    std::printf(
        "gif tags %llu, vertices %llu, draws %llu, tex decodes %llu (hits %llu), rt samples %llu, readbacks %llu\n",
        (unsigned long long)s.gifTags, (unsigned long long)s.vertices, (unsigned long long)s.drawCalls,
        (unsigned long long)s.textureDecodes, (unsigned long long)s.textureCacheHits, (unsigned long long)s.rtSamples,
        (unsigned long long)s.rtReadbacks);
    std::printf("texture upscale: %llu upscaled (%.1f ms total, %.2f ms each, %.1f MB), %llu replaced, %llu dumped, "
                "cache %.1f MB\n",
                (unsigned long long)s.texUpscaled, s.texUpscaleUs / 1000.0,
                s.texUpscaled ? s.texUpscaleUs / 1000.0 / s.texUpscaled : 0.0, s.texUpscaleBytes / 1048576.0,
                (unsigned long long)s.texReplaced, (unsigned long long)s.texDumped, s.texCacheBytes / 1048576.0);
    {
        const vpath::Counters& c = vpath::state().counters;
        std::printf("vertex path %s: records %llu, program draws %llu, vertices by shader %llu, by CPU evaluation "
                    "%llu, switches %llu; fallbacks:",
                    vpath::evalName(vpath::evalOf(vpath::state().baseWord.load())),
                    (unsigned long long)s.programRecords, (unsigned long long)s.programDraws,
                    (unsigned long long)c.gpuVertices.load(), (unsigned long long)c.cpuVertices.load(),
                    (unsigned long long)c.switches.load());
        bool any = false;
        for (uint32_t i = 0; i < vpath::kFallbackCount; ++i)
            if (const uint64_t n = c.fallback[i].load()) {
                std::printf(" %s %llu", vpath::kFallbackNames[i], (unsigned long long)n);
                any = true;
            }
        std::printf(any ? "\n" : " none\n");
    }
    return 0;
}
