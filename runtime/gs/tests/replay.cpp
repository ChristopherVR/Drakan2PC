// Replay a DRKGSCAP capture (see docs/reference/gs-capture.md / ps2recomp .../gs_capture.cpp) through gsr and
// save one screenshot per recorded vsync frame.
//   gsr_replay <capture.bin> [--out DIR] [--scale N] [--warp] [--max N] [--every N] [--first N]
// Record layout: 32 byte header ("DRKGSCAP", u32 version, u64 time) then records {u32 type, u32 size, payload}:
//   1 FRAME_START {u64 frame, u64 vsyncTick}
//   2 GIF_PACKET  {u8 path(1..3), u8 flags(bit0 = PATH2 DIRECTHL, bit1 = PATH3 image), u16 pad, u32 size, data[size]}
//   3 PRIV_REGS   {u64 x20: pmode smode1 smode2 srfsh synch1 synch2 syncv dispfb1 display1 dispfb2 display2 extbuf
//                  extdata extwrite bgcolor csr vsyncTick imr busdir siglblid}
//   4 VSYNC       {u64 vsyncTick}
// A capture does not contain an initial GS local memory image: textures uploaded before the capture started are
// missing, so start captures at boot (or accept missing textures).
#include "gsr/gsr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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
    long upDbp = -1; // --upload <hex dbp>: write the raw bytes of every host->local transfer to that block base (frames
                     // >= --first) as up_<dbp>_f<frame>.bin
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            cfg.scale = (uint32_t)std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--warp"))
            cfg.forceWarp = true;
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

    static const uint32_t privOff[20] = {0x00, 0x10, 0x20, 0x30, 0x40, 0x50,       0x60,       0x70,   0x80,   0x90,
                                         0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xFFFFFFFF, 0xFFFFFFFF, 0x1010, 0x1040, 0x1080};
    int frame = 0, saved = 0;
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
            gs->submitGifPacket(path == 1   ? GifPath::Path1
                                : path == 2 ? GifPath::Path2
                                            : GifPath::Path3,
                                buf.data() + 8, std::min<uint32_t>(psize, size - 8));
        } else if (type == 3 && size >= 160) {
            uint64_t regs[20];
            std::memcpy(regs, buf.data(), sizeof(regs));
            for (int i = 0; i < 20; ++i)
                if (privOff[i] != 0xFFFFFFFFu)
                    gs->writePriv(privOff[i], regs[i]);
        } else if (type == 4) {
            gs->vsync(false);
            if (frame >= first && ((frame - first) % every) == 0) {
                Image img;
                if (gs->captureDisplay(img)) {
                    char name[64];
                    std::snprintf(name, sizeof(name), "\\replay_%04d.png", frame);
                    writePng((outDir + name).c_str(), img);
                    ++saved;
                    Image fb;
                    if (fbArgs[0] >= 0 &&
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
    return 0;
}
