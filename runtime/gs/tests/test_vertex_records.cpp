// A vertex-program record draws exactly what the packets of the microprogram would draw (gsr/vertex_program.h,
// gsr/vertex_path.h), proven on whole frames through the public renderer API.
//
// Every scene is a GS stream the way the world pass has it (frame and depth buffer, scissor, offset, depth test,
// blending, alpha test, fog colour, uploaded textures) with many strip runs. A run is sent two ways:
//   stream A  the packets the strip stage sends: per strip one tag (PACKED, PRIM in the tag, four registers per
//             vertex: ST, RGBAQ, FOG, XYZ2) filled from vp::evaluate
//   stream B  the record of the run's inputs, wrapped as the recogniser wraps it (one tag with FLG = 3)
// Two renderers of the same backend and scale run side by side, one fed A and one fed B, and after every frame the
// complete picture of both is compared; a second frame makes the depth buffer visible (a ladder of depth-tested
// sprites, and the runs drawn again as packets under "greater or equal" and under "greater") and is compared too.
// Runs of the batch path travel the same two ways: as the packets vp::batchToGif makes of a batch record
// (gsr/vertex_batch.h), or as that record. Scenes "batch" and "batchmix" hold every pass kind, chains of up to 24
// passes, records with one vertex per index and with shared vertices, the closing empty tag, and batch records
// between strip records and ordinary packets; malformed batch records must be ignored.
// B runs under every evaluation mode of the control word and with the mode flipping at frame boundaries; the
// counters of the path are checked for every frame. Negative controls prove that a difference of one unit in one
// constant is seen. When a backend gains the shader path, the `gpu` runs here become its proof without a change.
//
//   gsr_test_vertex_records [--scale N] [--frames N] [--gpu-api vulkan|d3d12] [--warp] [--out DIR [--pictures]]
// Exit 77: no hardware adapter (or not the graphics API asked for).
#include "gsr/gsr.h"
#include "gsr/renderer_choice.h"
#include "gsr/vertex_batch.h"
#include "gsr/vertex_path.h"
#include "gsr/vertex_program.h"
#include "vertex_program_cases.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace gsr;

namespace {

// ------------------------------------------------------------------------------------------------ GIF builder
struct Gif {
    std::vector<uint8_t> d;
    void qw(uint64_t lo, uint64_t hi) {
        const size_t at = d.size();
        d.resize(at + 16);
        std::memcpy(d.data() + at, &lo, 8);
        std::memcpy(d.data() + at + 8, &hi, 8);
    }
    void tag(uint32_t nloop, bool pre, uint64_t prim, uint32_t flg, uint32_t nreg, uint64_t regs) {
        qw(nloop | (1ull << 15) | (static_cast<uint64_t>(pre) << 46) | (prim << 47) |
               (static_cast<uint64_t>(flg) << 58) | (static_cast<uint64_t>(nreg & 15u) << 60),
           regs);
    }
    void ad(std::initializer_list<std::pair<uint8_t, uint64_t>> writes) {
        tag(static_cast<uint32_t>(writes.size()), false, 0, 0, 1, 0xE);
        for (const auto& w : writes)
            qw(w.second, w.first);
    }
    void image(const std::vector<uint8_t>& bytes) {
        const uint32_t n = static_cast<uint32_t>((bytes.size() + 15) / 16);
        tag(n, false, 0, 2, 0, 0);
        const size_t at = d.size();
        d.resize(at + size_t(n) * 16, 0);
        std::memcpy(d.data() + at, bytes.data(), bytes.size());
    }
};

enum : uint8_t {
    kRegTex0 = 0x06,
    kRegClamp = 0x08,
    kRegTex1 = 0x14,
    kRegXyOffset = 0x18,
    kRegPrModeCont = 0x1A,
    kRegTexA = 0x3B,
    kRegFogCol = 0x3D,
    kRegScissor = 0x40,
    kRegAlpha = 0x42,
    kRegDthe = 0x45,
    kRegColClamp = 0x46,
    kRegTest = 0x47,
    kRegFba = 0x4A,
    kRegFrame = 0x4C,
    kRegZbuf = 0x4E,
    kRegBitBltBuf = 0x50,
    kRegTrxPos = 0x51,
    kRegTrxReg = 0x52,
    kRegTrxDir = 0x53,
};

// PRIM bits
constexpr uint32_t kStrip = 4, kTriangle = 3, kSprite = 6;
constexpr uint32_t kIip = 1u << 3, kTme = 1u << 4, kFge = 1u << 5, kAbe = 1u << 6;

constexpr uint32_t kWidth = 640, kHeight = 448, kFbw = 10;
constexpr uint32_t kZbp = 140; // behind the frame, in pages
constexpr uint32_t kTexA = 0x2800, kTexB = 0x2900, kTexC = 0x2A00;
constexpr uint32_t kOffsetX = 2048 - kWidth / 2, kOffsetY = 2048 - kHeight / 2;
constexpr uint32_t kTagRegisters = 0x5A12; // ST, RGBAQ, FOG, XYZ2: the strip tag template the recogniser accepts

uint64_t tex0(uint32_t tbp, uint32_t tfx) {
    // 64 x 64, 32 bit, texture alpha used
    return tbp | (1ull << 14) | (6ull << 26) | (6ull << 30) | (1ull << 34) | (static_cast<uint64_t>(tfx) << 35);
}
uint64_t testReg(uint32_t ate, uint32_t atst, uint32_t aref, uint32_t afail, uint32_t ztst) {
    return ate | (atst << 1) | (aref << 4) | (afail << 12) | (1ull << 16) | (static_cast<uint64_t>(ztst) << 17);
}
uint64_t alphaReg(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fix = 0) {
    return a | (b << 2) | (c << 4) | (d << 6) | (static_cast<uint64_t>(fix) << 32);
}
constexpr uint32_t kZAlways = 1, kZGreaterEqual = 2, kZGreater = 3;
constexpr uint32_t kAlphaGreaterEqual = 5;

uint64_t screenXy(float x, float y) {
    const uint32_t fx = static_cast<uint32_t>((x + static_cast<float>(kOffsetX)) * 16.0f) & 0xFFFFu;
    const uint32_t fy = static_cast<uint32_t>((y + static_cast<float>(kOffsetY)) * 16.0f) & 0xFFFFu;
    return fx | (static_cast<uint64_t>(fy) << 32);
}
uint64_t rgLo(uint32_t r, uint32_t g) {
    return r | (static_cast<uint64_t>(g) << 32);
}
uint64_t baHi(uint32_t b, uint32_t a) {
    return b | (static_cast<uint64_t>(a) << 32);
}

void upload(Gif& g, uint32_t dbp, const std::vector<uint8_t>& rgba64) {
    g.ad({{kRegBitBltBuf, (static_cast<uint64_t>(dbp) << 32) | (1ull << 48)},
          {kRegTrxPos, 0},
          {kRegTrxReg, 64ull | (64ull << 32)},
          {kRegTrxDir, 0}});
    g.image(rgba64);
}

// 64 x 64 test textures. kind 0: coloured checks; 1: smooth gradients with noise; 2: alpha in bands (alpha test);
// 3: stripes that move with `step` (the texture uploaded between two records).
std::vector<uint8_t> texture(uint32_t kind, uint32_t step) {
    std::vector<uint8_t> t(64 * 64 * 4);
    uint32_t noise = 77u + kind;
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x) {
            noise = noise * 1664525u + 1013904223u;
            uint8_t* p = &t[(y * 64 + x) * 4];
            const bool check = (((x >> 3) ^ (y >> 3)) & 1u) != 0u;
            switch (kind) {
                case 0:
                    p[0] = check ? 0xF0 : 0x40;
                    p[1] = static_cast<uint8_t>(0x30 + x * 3);
                    p[2] = static_cast<uint8_t>(0x30 + y * 3);
                    p[3] = 0x80;
                    break;
                case 1:
                    p[0] = static_cast<uint8_t>(x * 4);
                    p[1] = static_cast<uint8_t>(y * 4);
                    p[2] = static_cast<uint8_t>(0x60 + ((noise >> 24) & 0x3F));
                    p[3] = 0x80;
                    break;
                case 2:
                    p[0] = check ? 0xC0 : 0x90;
                    p[1] = 0xA0;
                    p[2] = static_cast<uint8_t>(0x50 + x * 2);
                    p[3] = static_cast<uint8_t>(((x + y) & 15u) * 8u);
                    break;
                default:
                    p[0] = (((x + step * 5u) >> 2) & 1u) ? 0xE0 : 0x20;
                    p[1] = (((y + step * 3u) >> 2) & 1u) ? 0xD0 : 0x30;
                    p[2] = static_cast<uint8_t>(0x40 + step * 7u);
                    p[3] = 0x80;
                    break;
            }
        }
    return t;
}

// ------------------------------------------------------------------------------------------------ runs and scenes
struct Run {
    uint32_t program = vp::StripsLit;
    uint32_t prim = kStrip;
    vp::Constants k{};
    std::vector<uint32_t> strips;
    std::vector<vp::Vertex> vertices;
};

// One index-list run of the batch path (gsr/vertex_batch.h).
struct BatchRun {
    uint32_t prim = kStrip;
    std::vector<vp::Pass> passes;
    std::vector<uint32_t> strips;  // lengths in indices
    std::vector<uint16_t> indices; // into the vertices
    std::vector<vp::BatchVertex> vertices;
    bool emptyTag = false;
};

struct Item {
    bool isRun = false;
    GifPath path = GifPath::Path3;
    std::vector<uint8_t> raw;
    Run run;
    bool isBatch = false;
    BatchRun batch;
};

struct Scene {
    std::vector<Item> items;
    uint64_t zbuf = kZbp;       // the ZBUF value of the scene (the depth picture needs it)
    uint32_t zLimit = 1u << 28; // depth values lie below this
    void raw(GifPath path, Gif&& g) { items.push_back(Item{false, path, std::move(g.d), {}}); }
    void run(Run&& r) { items.push_back(Item{true, GifPath::Path1, {}, std::move(r)}); }
    void batch(BatchRun&& b) { items.push_back(Item{false, GifPath::Path1, {}, {}, true, std::move(b)}); }
};
void sendBatch(Renderer& gs, const BatchRun& r, bool asRecord);
void batchDepthPackets(Gif& g, const BatchRun& r, const uint8_t* colour);

// Stream B: the wrapped record, exactly as vu1gpu::buildStrips writes it (payload quadwords, end of packet, FLG = 3,
// the magic in the upper half; header, constants, strip lengths padded to four words, vertex rows).
std::vector<uint8_t> wrapRecord(const Run& r) {
    const uint32_t stripCount = static_cast<uint32_t>(r.strips.size());
    const uint32_t vertexCount = static_cast<uint32_t>(r.vertices.size());
    const uint32_t payload = vp::recordBytes(stripCount, vertexCount);
    std::vector<uint8_t> packet(16u + payload, 0);
    const uint64_t wrapLo = uint64_t(payload / 16u) | (1ull << 15) | (3ull << 58);
    std::memcpy(packet.data(), &wrapLo, 8);
    std::memcpy(packet.data() + 8, &vp::kTagMagic, 8);
    const vp::Header header{r.program, r.prim, stripCount, vertexCount};
    uint8_t* out = packet.data() + 16;
    std::memcpy(out, &header, 16);
    out += 16;
    std::memcpy(out, &r.k, sizeof(r.k));
    out += sizeof(r.k);
    std::memcpy(out, r.strips.data(), size_t(stripCount) * 4u);
    out += vp::stripTableBytes(stripCount);
    std::memcpy(out, r.vertices.data(), size_t(vertexCount) * sizeof(vp::Vertex));
    return packet;
}

// Stream A: one strip as the packet the stage kicks. The tag carries PRIM; per vertex ST (s, t, q), RGBAQ (one
// component per word), FOG and XYZ2. The stage stores one four-lane vector for the place (x, y, z, fog lane) and
// both FOG and XYZ2 take it: the fog value is bits 4..11 of the fourth word and bit 15 of that word is the "do not
// draw" bit of XYZ2. `colour` replaces the evaluated colour (the depth picture draws the runs in one colour).
void stripPacket(Gif& g, uint32_t prim, uint32_t program, const vp::Constants& k, const vp::Vertex* v, uint32_t n,
                 const uint8_t* colour = nullptr) {
    g.tag(n, true, prim, 0, 4, kTagRegisters);
    for (uint32_t i = 0; i < n; ++i) {
        const vp::Output o = vp::evaluate(program, k, v[i]);
        const uint64_t lane = (static_cast<uint64_t>(o.fog) << 4) | (o.adc ? 1ull << 15 : 0ull);
        g.qw(o.s | (static_cast<uint64_t>(o.t) << 32), o.q);
        if (colour)
            g.qw(rgLo(colour[0], colour[1]), baHi(colour[2], colour[3]));
        else
            g.qw(rgLo(o.r, o.g), baHi(o.b, o.a));
        g.qw(o.x | (static_cast<uint64_t>(o.y) << 32), o.z | (lane << 32));
        g.qw(o.x | (static_cast<uint64_t>(o.y) << 32), o.z | (lane << 32));
    }
}

void sendRun(Renderer& gs, const Run& r, bool asRecord) {
    if (asRecord) {
        const std::vector<uint8_t> packet = wrapRecord(r);
        gs.submitGifPacket(GifPath::Path1, packet.data(), static_cast<uint32_t>(packet.size()));
        return;
    }
    const vp::Vertex* v = r.vertices.data();
    for (const uint32_t n : r.strips) { // one kick per strip
        Gif g;
        stripPacket(g, r.prim, r.program, r.k, v, n);
        gs.submitGifPacket(GifPath::Path1, g.d.data(), static_cast<uint32_t>(g.d.size()));
        v += n;
    }
}

void sendScene(Renderer& gs, const Scene& s, bool asRecords) {
    for (const Item& it : s.items) {
        if (it.isRun)
            sendRun(gs, it.run, asRecords);
        else if (it.isBatch)
            sendBatch(gs, it.batch, asRecords);
        else
            gs.submitGifPacket(it.path, it.raw.data(), static_cast<uint32_t>(it.raw.size()));
    }
}

// The depth picture of a scene: red and green from a ladder of depth-tested sprites (each pixel ends with the lowest
// rung at or above its depth), blue from the runs drawn again as packets, first where "greater or equal" passes and
// then where "greater" passes. Nothing writes depth. Both renderers get the same packets.
std::vector<uint8_t> depthPicture(const Scene& s, uint32_t rungs) {
    Gif g;
    const uint64_t zmsk = 1ull << 32;
    g.ad({{kRegFrame, static_cast<uint64_t>(kFbw) << 16},
          {kRegZbuf, s.zbuf | zmsk},
          {kRegScissor, (static_cast<uint64_t>(kWidth - 1) << 16) | (static_cast<uint64_t>(kHeight - 1) << 48)},
          {kRegTest, testReg(0, 0, 0, 0, kZAlways)}});
    g.tag(2, true, kSprite, 0, 2, 0x51);
    g.qw(0, baHi(0, 0x80));
    g.qw(screenXy(0, 0), 0);
    g.qw(0, baHi(0, 0x80));
    g.qw(screenXy(kWidth, kHeight), 0);
    g.ad({{kRegFrame, (static_cast<uint64_t>(kFbw) << 16) | (0xFFFF0000ull << 32)},
          {kRegTest, testReg(0, 0, 0, 0, kZGreaterEqual)}});
    for (uint32_t i = rungs; i-- > 0;) {
        const uint64_t z = (static_cast<uint64_t>(s.zLimit) * (i + 1u)) / rungs;
        g.tag(2, true, kSprite, 0, 2, 0x51);
        g.qw(rgLo(i & 0xFFu, (i >> 8) & 0xFFu), baHi(0, 0x80));
        g.qw(screenXy(0, 0), z);
        g.qw(rgLo(i & 0xFFu, (i >> 8) & 0xFFu), baHi(0, 0x80));
        g.qw(screenXy(kWidth, kHeight), z);
    }
    for (uint32_t pass = 0; pass < 2; ++pass) {
        const uint8_t colour[4] = {0, 0, static_cast<uint8_t>(pass ? 0xFF : 0x80), 0x80};
        g.ad({{kRegFrame, (static_cast<uint64_t>(kFbw) << 16) | (0xFF00FFFFull << 32)},
              {kRegTest, testReg(0, 0, 0, 0, pass ? kZGreater : kZGreaterEqual)}});
        for (const Item& it : s.items) {
            if (it.isBatch)
                batchDepthPackets(g, it.batch, colour);
            if (!it.isRun)
                continue;
            const vp::Vertex* v = it.run.vertices.data();
            for (const uint32_t n : it.run.strips) {
                stripPacket(g, kStrip, it.run.program, it.run.k, v, n, colour);
                v += n;
            }
        }
    }
    return std::move(g.d);
}

// ---- game-like constants: a perspective view of an object, the viewport and fog constants of the generator of
// vertex_program_cases.h, four directional lights ----
uint32_t bitsOf(float f) {
    return vpcases::bitsOf(f);
}

struct Place {
    float yaw = 0, pitch = 0, x = 0, y = 0, z = 600;
};

vp::Constants worldConstants(std::mt19937& rng, const Place& at, float zScale, uint32_t alpha) {
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const float fx = 1.6f, fy = 2.1f, zn = 120.0f, zf = 4000.0f;
    const float a = (zf + zn) / (zf - zn), b = -2.0f * zf * zn / (zf - zn);
    const float cy = std::cos(at.yaw), sy = std::sin(at.yaw), cp = std::cos(at.pitch), sp = std::sin(at.pitch);
    const float axes[3][3] = {{cy, sy * sp, -sy * cp}, {0.0f, cp, sp}, {sy, -cy * sp, cy * cp}};
    vp::Constants k{};
    for (int c = 0; c < 3; ++c) {
        k.mvp[c][0] = bitsOf(fx * axes[c][0]);
        k.mvp[c][1] = bitsOf(fy * axes[c][1]);
        k.mvp[c][2] = bitsOf(a * axes[c][2]);
        k.mvp[c][3] = bitsOf(axes[c][2]);
    }
    k.mvp[3][0] = bitsOf(fx * at.x);
    k.mvp[3][1] = bitsOf(fy * at.y);
    k.mvp[3][2] = bitsOf(a * at.z + b);
    k.mvp[3][3] = bitsOf(at.z);
    const float scale[4] = {320.0f + unit(rng), -240.0f + unit(rng), -zScale, 0.0f};
    const float offset[4] = {2048.0f, 2048.0f, zScale, 255.0f + unit(rng)};
    const float fog[4] = {0.0f, 100.0f + 50.0f * unit(rng), 3600.0f + 300.0f * unit(rng), 0.06f + 0.004f * unit(rng)};
    for (int l = 0; l < 4; ++l) {
        k.scale[l] = bitsOf(scale[l]);
        k.offset[l] = bitsOf(offset[l]);
        k.fog[l] = bitsOf(fog[l]);
    }
    for (int l = 0; l < 3; ++l)
        k.colour[l] = static_cast<int32_t>(20u + rng() % 90u);
    k.colour[3] = static_cast<int32_t>(alpha);
    for (int light = 0; light < 4; ++light) {
        float d[3] = {unit(rng), 0.3f + std::fabs(unit(rng)), unit(rng)};
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int r = 0; r < 3; ++r)
            k.lightDir[r][light] = bitsOf(d[r] / len / 127.0f);
        for (int l = 0; l < 3; ++l)
            k.lightColour[light][l] = static_cast<int32_t>(rng() % 110u);
    }
    return k;
}

// A wavy patch of rows x cols quads as `rows` strips of 2 * cols vertices.
void addPatch(Run& run, uint32_t rows, uint32_t cols, float size, float amp, float repeat, float phase) {
    for (uint32_t j = 0; j < rows; ++j) {
        run.strips.push_back(cols * 2u);
        for (uint32_t i = 0; i < cols; ++i)
            for (uint32_t d = 0; d < 2; ++d) {
                const float u = static_cast<float>(i) / static_cast<float>(cols - 1u);
                const float v = static_cast<float>(j + d) / static_cast<float>(rows);
                const float sx = std::sin(6.0f * u + phase), cx = std::cos(6.0f * u + phase);
                const float sz = std::sin(5.0f * v), cz = std::cos(5.0f * v);
                const float normal[3] = {-amp * 6.0f * cx * cz / size, 1.0f, amp * 5.0f * sx * sz / size};
                const float len = std::sqrt(normal[0] * normal[0] + 1.0f + normal[2] * normal[2]);
                vp::Vertex vx{};
                vx.pos[0] = bitsOf((u - 0.5f) * size);
                vx.pos[1] = bitsOf(amp * sx * cz);
                vx.pos[2] = bitsOf((v - 0.5f) * size);
                vx.pos[3] = bitsOf(1.0f);
                vx.st[0] = bitsOf(u * repeat);
                vx.st[1] = bitsOf(v * repeat);
                vx.st[2] = bitsOf(1.0f);
                for (int l = 0; l < 3; ++l)
                    vx.normal[l] = static_cast<int32_t>(normal[l] / len * 127.0f);
                run.vertices.push_back(vx);
            }
    }
}

// A screen-facing quad (one strip of four) at view depth `z`, `half` units wide and high around (x, y).
Run facingQuad(std::mt19937& rng, float x, float y, float z, float half, uint32_t prim, uint32_t program) {
    Run r;
    r.program = program;
    r.prim = prim;
    r.k = worldConstants(rng, Place{0, 0, x, y, z}, 8388608.0f, 0x80);
    r.strips.push_back(4);
    for (uint32_t i = 0; i < 4; ++i) {
        vp::Vertex v{};
        v.pos[0] = bitsOf((i & 1u) ? half : -half);
        v.pos[1] = bitsOf((i & 2u) ? half : -half);
        v.pos[2] = bitsOf(0.0f);
        v.pos[3] = bitsOf(1.0f);
        v.st[0] = bitsOf((i & 1u) ? 1.0f : 0.0f);
        v.st[1] = bitsOf((i & 2u) ? 1.0f : 0.0f);
        v.st[2] = bitsOf(1.0f);
        v.normal[2] = -127;
        r.vertices.push_back(v);
    }
    return r;
}

// The start of every frame: the world pass's registers, a clear of colour and depth, then the depth test on.
void beginFrame(Scene& s, uint32_t frame, bool uploads) {
    Gif g;
    g.ad({{kRegFrame, static_cast<uint64_t>(kFbw) << 16},
          {kRegZbuf, s.zbuf},
          {kRegXyOffset, (static_cast<uint64_t>(kOffsetX) << 4) | (static_cast<uint64_t>(kOffsetY) << 36)},
          {kRegScissor, (static_cast<uint64_t>(kWidth - 1) << 16) | (static_cast<uint64_t>(kHeight - 1) << 48)},
          {kRegPrModeCont, 1},
          {kRegColClamp, 1},
          {kRegDthe, 0},
          {kRegFba, 0},
          {kRegTexA, 0x80ull | (0x80ull << 32)},
          {kRegFogCol, 0x00A08060ull},
          {kRegClamp, 0},
          {kRegTex1, 0x60},
          {kRegAlpha, alphaReg(0, 1, 0, 1)},
          {kRegTest, testReg(0, 0, 0, 0, kZAlways)}});
    g.tag(2, true, kSprite, 0, 2, 0x51);
    g.qw(rgLo(24, 32), baHi(48 + (frame & 7u), 0x80));
    g.qw(screenXy(0, 0), 0);
    g.qw(rgLo(24, 32), baHi(48 + (frame & 7u), 0x80));
    g.qw(screenXy(kWidth, kHeight), 0);
    if (uploads) {
        upload(g, kTexA, texture(0, 0));
        upload(g, kTexB, texture(1, 0));
        upload(g, kTexC, texture(2, 0));
    }
    g.ad({{kRegTest, testReg(0, 0, 0, 0, kZGreaterEqual)}});
    s.raw(GifPath::Path3, std::move(g));
}

// The draw states the runs of a scene cycle through: registers (sent on PATH2 before the run) and the PRIM and
// program of the run.
struct Variant {
    uint32_t prim, program, alpha;
};
Variant setVariant(Scene& s, uint32_t which) {
    Gif g;
    Variant v{kStrip | kIip, vp::StripsLit, 0x80};
    uint64_t test = testReg(0, 0, 0, 0, kZGreaterEqual), alpha = alphaReg(0, 1, 0, 1), tex = tex0(kTexA, 0);
    uint64_t tex1 = 0x60;
    switch (which % 8u) {
        case 0: // lit, textured, fogged
            v.prim |= kTme | kFge;
            break;
        case 1: // constant colour, textured, fogged, nearest texels
            v.prim |= kTme | kFge;
            v.program = vp::StripsFlat;
            tex = tex0(kTexB, 0);
            tex1 = 0;
            break;
        case 2: // lit, no texture, no fog
            break;
        case 3: // constant colour, no texture, fogged
            v.prim |= kFge;
            v.program = vp::StripsFlat;
            break;
        case 4: // lit, textured, blended over the frame
            v.prim |= kTme | kAbe;
            v.alpha = 0x50;
            break;
        case 5: // constant colour, textured with an alpha pattern, alpha test
            v.prim |= kTme | kFge;
            v.program = vp::StripsFlat;
            tex = tex0(kTexC, 0);
            test = testReg(1, kAlphaGreaterEqual, 0x40, 0, kZGreaterEqual);
            break;
        case 6: // lit, textured, fogged, added to the frame
            v.prim |= kTme | kFge | kAbe;
            v.alpha = 0x38;
            alpha = alphaReg(0, 2, 0, 1);
            break;
        default: // lit, alpha pattern, alpha test that keeps the colour but not the depth, blended
            v.prim |= kTme | kAbe;
            v.alpha = 0x70;
            tex = tex0(kTexC, 0);
            test = testReg(1, kAlphaGreaterEqual, 0x30, 1, kZGreaterEqual);
            break;
    }
    g.ad({{kRegTex0, tex}, {kRegTex1, tex1}, {kRegAlpha, alpha}, {kRegTest, test}});
    s.raw(GifPath::Path2, std::move(g));
    return v;
}

// Scene "world": 64 objects through all draw states, then a constant-colour quad in front (the run the colour
// control changes).
Scene worldScene(uint32_t frame) {
    Scene s;
    beginFrame(s, frame, frame == 0);
    std::mt19937 rng(1001);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const float turn = 0.04f * static_cast<float>(frame);
    for (uint32_t i = 0; i < 64; ++i) {
        const Variant v = setVariant(s, i);
        Run r;
        r.prim = v.prim;
        r.program = v.program;
        const Place at{unit(rng) * 3.0f + turn, -0.9f + 0.5f * unit(rng), unit(rng) * 420.0f, unit(rng) * 260.0f,
                       350.0f + 1400.0f * std::fabs(unit(rng))};
        r.k = worldConstants(rng, at, 8388608.0f, v.alpha);
        addPatch(r, 3u + rng() % 4u, 4u + rng() % 9u, 180.0f + 220.0f * std::fabs(unit(rng)), 25.0f * unit(rng),
                 1.0f + 3.0f * std::fabs(unit(rng)), unit(rng) * 3.0f);
        s.run(std::move(r));
    }
    Gif g;
    g.ad({{kRegTest, testReg(0, 0, 0, 0, kZAlways)}});
    s.raw(GifPath::Path2, std::move(g));
    Run quad = facingQuad(rng, -180.0f, 90.0f, 300.0f, 40.0f, kStrip | kIip, vp::StripsFlat);
    quad.k.colour[0] = 90;
    quad.k.colour[1] = 140;
    quad.k.colour[2] = 60;
    s.run(std::move(quad));
    return s;
}
constexpr size_t kWorldQuadFromEnd = 1; // items.size() - 1 is the constant-colour quad

// Scene "many": 240 small runs under one state (what a backend batches across records), with strips of one and
// two vertices among them, into a 24 bit depth buffer.
Scene manyScene(uint32_t frame) {
    Scene s;
    s.zbuf = kZbp | (1ull << 24);
    s.zLimit = 1u << 24;
    beginFrame(s, frame, frame == 0);
    Gif g;
    g.ad({{kRegTex0, tex0(kTexA, 0)}});
    s.raw(GifPath::Path2, std::move(g));
    std::mt19937 rng(2002);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    for (uint32_t i = 0; i < 240; ++i) {
        Run r;
        r.prim = kStrip | kIip | kTme | kFge;
        r.program = (i % 3u) ? vp::StripsLit : vp::StripsFlat;
        const Place at{unit(rng) * 3.0f + 0.03f * static_cast<float>(frame), -1.2f + 0.4f * unit(rng),
                       unit(rng) * 600.0f, unit(rng) * 380.0f, 500.0f + 1800.0f * std::fabs(unit(rng))};
        r.k = worldConstants(rng, at, 524288.0f, 0x80);
        if (i % 5u == 1u) { // a strip of one and a strip of two before the patch: no triangle
            r.strips.push_back(1);
            r.strips.push_back(2);
            Run filler;
            addPatch(filler, 1, 2, 50.0f, 0.0f, 1.0f, 0.0f);
            r.vertices.assign(filler.vertices.begin(), filler.vertices.begin() + 3);
        }
        addPatch(r, 1u + rng() % 2u, 2u + rng() % 3u, 60.0f + 80.0f * std::fabs(unit(rng)), 8.0f * unit(rng), 2.0f,
                 unit(rng));
        if (i % 7u == 3u) { // and one after it
            r.strips.push_back(2);
            r.vertices.push_back(r.vertices[0]);
            r.vertices.push_back(r.vertices[1]);
        }
        s.run(std::move(r));
    }
    return s;
}

// Where the checks of the registers after a record look (frame pixels).
constexpr float kInheritX0 = 24, kInheritY0 = 24, kInheritX1 = 72, kInheritY1 = 72;
constexpr uint8_t kInheritColour[3] = {211, 37, 149};

// Scene "interleaved": records between ordinary packets and state changes, so that the stream order and the
// registers a record leaves behind decide the picture.
Scene interleavedScene(uint32_t frame) {
    Scene s;
    beginFrame(s, frame, frame == 0);
    std::mt19937 rng(3003);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const float turn = 0.05f * static_cast<float>(frame);
    const auto patchRun = [&](const Variant& v, const Place& at, uint32_t rows, uint32_t cols, float size) {
        Run r;
        r.prim = v.prim;
        r.program = v.program;
        r.k = worldConstants(rng, at, 8388608.0f, v.alpha);
        addPatch(r, rows, cols, size, 20.0f, 3.0f, turn);
        return r;
    };
    // a record, a blended sprite over it, a record under a different ALPHA over both
    s.run(patchRun(setVariant(s, 0), Place{0.3f + turn, -1.1f, -60, 20, 700}, 6, 12, 420));
    {
        Gif g;
        g.ad({{kRegTest, testReg(0, 0, 0, 0, kZAlways)}, {kRegAlpha, alphaReg(0, 1, 0, 1)}});
        g.tag(2, true, kSprite | kAbe, 0, 2, 0x51);
        g.qw(rgLo(250, 40), baHi(30, 0x48));
        g.qw(screenXy(200, 120), 0);
        g.qw(rgLo(250, 40), baHi(30, 0x48));
        g.qw(screenXy(470, 330), 0);
        s.raw(GifPath::Path2, std::move(g));
    }
    s.run(patchRun(setVariant(s, 6), Place{-0.4f - turn, -1.3f, 40, -10, 620}, 5, 10, 380));
    // the same texture address uploaded again between two records that sample it
    s.run(patchRun(setVariant(s, 0), Place{1.2f, -0.7f, -330, -150, 900}, 4, 8, 300));
    {
        Gif g;
        upload(g, kTexA, texture(3, frame + 1u));
        s.raw(GifPath::Path3, std::move(g));
    }
    s.run(patchRun(setVariant(s, 0), Place{1.2f, -0.7f, 330, -150, 900}, 4, 8, 300));
    {
        Gif g;
        upload(g, kTexA, texture(0, 0));
        s.raw(GifPath::Path3, std::move(g));
    }
    // the same state but another fog colour between two records
    s.run(patchRun(setVariant(s, 1), Place{2.0f + turn, -1.0f, -380, 190, 1500}, 4, 6, 340));
    {
        Gif g;
        g.ad({{kRegFogCol, 0x002040C0ull}});
        s.raw(GifPath::Path2, std::move(g));
    }
    s.run(patchRun(Variant{kStrip | kIip | kTme | kFge, vp::StripsFlat, 0x80},
                   Place{2.0f + turn, -1.0f, 380, 190, 1500}, 4, 6, 340));
    // a run whose far vertices carry the "do not draw" bit (the fog lane goes below zero beyond w = 510)
    {
        Run r = patchRun(setVariant(s, 0), Place{0.2f, -1.35f, 150, 120, 520}, 7, 10, 560);
        r.k.fog[3] = bitsOf(0.5f);
        r.k.offset[3] = bitsOf(255.0f);
        s.run(std::move(r));
    }
    // objects around the eye: vertices behind it (w <= 0) and coordinates that wrap; inside a smaller scissor
    // and without depth writes, so that the rest of the picture stays
    {
        Gif g;
        g.ad({{kRegZbuf, s.zbuf | (1ull << 32)},
              {kRegScissor, 330ull | (629ull << 16) | (150ull << 32) | (370ull << 48)}});
        s.raw(GifPath::Path2, std::move(g));
        s.run(patchRun(setVariant(s, 4), Place{turn, -1.45f, 0, -70, 60}, 7, 12, 900));
        s.run(patchRun(setVariant(s, 0), Place{0.9f, -0.2f, 30, -40, -120}, 5, 9, 700));
        Gif h;
        h.ad({{kRegZbuf, s.zbuf},
              {kRegScissor, (static_cast<uint64_t>(kWidth - 1) << 16) | (static_cast<uint64_t>(kHeight - 1) << 48)}});
        s.raw(GifPath::Path2, std::move(h));
    }
    // flat shading (IIP off): each triangle takes its last vertex's colour
    s.run(patchRun(Variant{kStrip | kTme, vp::StripsLit, 0x80}, Place{-1.0f + turn, -0.8f, 240, 30, 800}, 5, 9, 260));
    // strips of one and two vertices between real ones
    {
        Run r = patchRun(setVariant(s, 3), Place{0.5f, -1.0f, -250, 60, 760}, 2, 5, 200);
        const std::vector<vp::Vertex> body = r.vertices;
        r.strips = {1, 10, 2, 10, 1, 2};
        r.vertices.clear();
        const auto take = [&](size_t first, size_t n) {
            r.vertices.insert(r.vertices.end(), body.begin() + first, body.begin() + first + n);
        };
        take(3, 1);
        take(0, 10);
        take(5, 2);
        take(10, 10);
        take(19, 1);
        take(7, 2);
        s.run(std::move(r));
    }
    // ---- the registers after a record ----
    // 1. the vertex queue: a tag without PRIM continues the last strip with two more vertices
    s.run(patchRun(setVariant(s, 2), Place{0.1f, -1.2f, -230, -120, 640}, 3, 6, 220));
    {
        Gif g;
        g.tag(2, false, 0, 0, 4, kTagRegisters);
        for (uint32_t i = 0; i < 2; ++i) {
            g.qw(bitsOf(0.25f) | (static_cast<uint64_t>(bitsOf(0.5f)) << 32), bitsOf(1.0f));
            g.qw(rgLo(40 + 150 * i, 220), baHi(90, 0x80));
            g.qw(0, 200ull << 36);
            g.qw(screenXy(8.0f + 30.0f * static_cast<float>(i), 440.0f - 60.0f * static_cast<float>(i)), 0x0F000000ull);
        }
        s.raw(GifPath::Path1, std::move(g));
    }
    // 2. colour, texture coordinate, Q and fog: a textured, fogged triangle that sends only places
    s.run(patchRun(setVariant(s, 0), Place{-0.6f, -1.0f, 250, -130, 680}, 3, 6, 200));
    {
        Gif g;
        g.tag(3, true, kTriangle | kTme | kFge, 0, 1, 0x5);
        g.qw(screenXy(560, 380), 0x0F000000ull);
        g.qw(screenXy(630, 384), 0x0F000000ull);
        g.qw(screenXy(600, 440), 0x0F000000ull);
        s.raw(GifPath::Path1, std::move(g));
    }
    // 3. a sprite that sends only places takes the colour of the record's last vertex: a known colour here
    {
        Gif g;
        g.ad({{kRegTest, testReg(0, 0, 0, 0, kZAlways)}});
        s.raw(GifPath::Path2, std::move(g));
        Run quad = facingQuad(rng, 200.0f, 110.0f, 420.0f, 20.0f, kStrip | kIip, vp::StripsFlat);
        for (int l = 0; l < 3; ++l)
            quad.k.colour[l] = kInheritColour[l];
        s.run(std::move(quad));
        Gif sprite;
        sprite.tag(2, true, kSprite, 0, 1, 0x5);
        sprite.qw(screenXy(kInheritX0, kInheritY0), 0);
        sprite.qw(screenXy(kInheritX1, kInheritY1), 0);
        s.raw(GifPath::Path1, std::move(sprite));
    }
    return s;
}

// Scene "generated": the strip vertices of the shared generator (vertex_program_cases.h: random objects around the
// eye, every fourth vertex with operands that hit the arithmetic rules: denormals, infinities, NaN, the largest
// values), sixteen to a run, through all draw states.
Scene generatedScene(uint32_t frame) {
    static const vpcases::Cases cases = [] {
        vpcases::Cases c;
        vpcases::addGenerated(c, 96u * 16u, 20261112u);
        return c;
    }();
    Scene s;
    beginFrame(s, frame, frame == 0);
    const uint32_t runs = 48, total = static_cast<uint32_t>(cases.vertices.size()) / 16u;
    for (uint32_t i = 0; i < runs; ++i) {
        const uint32_t first = ((i + frame * 5u) % total) * 16u;
        const Variant v = setVariant(s, i);
        Run r;
        r.prim = v.prim;
        r.program = v.program;
        r.k = cases.constants[cases.vertices[first].constants];
        r.k.colour[3] = static_cast<int32_t>(v.alpha);
        if (i % 12u == 7u) // constants outside the float rules as well
            r.k.mvp[i % 4u][(i / 4u) % 4u] = (i & 1u) ? 0x7F800000u : 0x00000003u;
        r.strips = {3, 5, 4, 4};
        for (uint32_t n = 0; n < 16; ++n)
            r.vertices.push_back(cases.vertices[first + n].in);
        s.run(std::move(r));
    }
    return s;
}

// ------------------------------------------------------------------------------------------------ batch records
// A batch record (gsr/vertex_batch.h) is one index-list run of the batch path: raw vertex rows, the passes they still
// need and strips as indices into the vertices. Stream A sends what the microprogram sends for the run (the packets
// vp::batchToGif makes, one kick per strip, and the closing empty tag of a run with clip codes); stream B sends the
// record as vu1gpu::batch::Tracker wraps it.
std::vector<uint8_t> wrapBatch(const BatchRun& r) {
    vp::BatchHeader h{};
    h.program = vp::Batch;
    h.prim = r.prim;
    h.stripCount = static_cast<uint32_t>(r.strips.size());
    h.indexCount = static_cast<uint32_t>(r.indices.size());
    h.vertexCount = static_cast<uint32_t>(r.vertices.size());
    h.passCount = static_cast<uint32_t>(r.passes.size());
    h.flags = r.emptyTag ? vp::kBatchEmptyTag : 0u;
    const uint32_t payload = vp::batchRecordBytes(h.passCount, h.stripCount, h.indexCount, h.vertexCount);
    std::vector<uint8_t> packet(16u + payload, 0);
    const uint64_t wrapLo = uint64_t(payload / 16u) | (1ull << 15) | (3ull << 58);
    std::memcpy(packet.data(), &wrapLo, 8);
    std::memcpy(packet.data() + 8, &vp::kTagMagic, 8);
    uint8_t* out = packet.data() + 16;
    std::memcpy(out, &h, sizeof(h));
    out += sizeof(h);
    if (h.passCount)
        std::memcpy(out, r.passes.data(), size_t(h.passCount) * sizeof(vp::Pass));
    out += size_t(h.passCount) * sizeof(vp::Pass);
    if (h.stripCount)
        std::memcpy(out, r.strips.data(), size_t(h.stripCount) * 4u);
    out += vp::batchStripBytes(h.stripCount);
    if (h.indexCount)
        std::memcpy(out, r.indices.data(), size_t(h.indexCount) * 2u);
    out += vp::batchIndexBytes(h.indexCount);
    if (h.vertexCount)
        std::memcpy(out, r.vertices.data(), size_t(h.vertexCount) * sizeof(vp::BatchVertex));
    return packet;
}

// The packets of the run (stream A). False: the record does not parse (a test bug).
bool batchPackets(const BatchRun& r, std::vector<std::vector<uint8_t>>& packets) {
    static std::vector<uint64_t> aligned; // parseBatch hands out pointers into the payload
    const std::vector<uint8_t> wrapped = wrapBatch(r);
    aligned.assign((wrapped.size() + 7u) / 8u, 0);
    std::memcpy(aligned.data(), wrapped.data(), wrapped.size());
    const uint8_t* payload = reinterpret_cast<const uint8_t*>(aligned.data()) + 16;
    vp::BatchView view;
    if (!vp::parseBatch(payload, static_cast<uint32_t>(wrapped.size() - 16u), view))
        return false;
    vp::batchToGif(view, packets);
    return true;
}

void sendBatch(Renderer& gs, const BatchRun& r, bool asRecord) {
    if (asRecord) {
        const std::vector<uint8_t> packet = wrapBatch(r);
        gs.submitGifPacket(GifPath::Path1, packet.data(), static_cast<uint32_t>(packet.size()));
        return;
    }
    std::vector<std::vector<uint8_t>> packets;
    if (!batchPackets(r, packets))
        return;
    for (const auto& pk : packets) // one kick per strip, then the empty tag
        gs.submitGifPacket(GifPath::Path1, pk.data(), static_cast<uint32_t>(pk.size()));
}

// The strips of a batch run once more as plain strips in one colour (the depth picture).
void batchDepthPackets(Gif& g, const BatchRun& r, const uint8_t* colour) {
    std::vector<std::vector<uint8_t>> packets;
    if (!batchPackets(r, packets))
        return;
    for (const auto& pk : packets) {
        const uint32_t n = static_cast<uint32_t>((pk.size() - 16u) / 64u);
        if (!n)
            continue;
        g.tag(n, true, kStrip, 0, 4, kTagRegisters);
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t q[8];
            std::memcpy(q, pk.data() + 16u + size_t(i) * 64u, 64);
            g.qw(q[0], q[1]);
            g.qw(rgLo(colour[0], colour[1]), baHi(colour[2], colour[3]));
            g.qw(q[4], q[5]);
            g.qw(q[6], q[7]);
        }
    }
}

// ---- passes from the constants of a strip run ----
vp::Pass transformPass(const vp::Constants& k, uint32_t kind = vp::PassTransform) {
    vp::Pass p{};
    p.kind = kind;
    std::memcpy(p.k[0], k.mvp, 64);
    std::memcpy(p.k[4], k.fog, 16);
    std::memcpy(p.k[5], k.offset, 16);
    std::memcpy(p.k[6], k.scale, 16);
    return p;
}
vp::Pass lightPass(const vp::Constants& k) {
    vp::Pass p{};
    p.kind = vp::PassLight;
    std::memcpy(p.k[0], k.colour, 16);
    std::memcpy(p.k[1], k.lightDir, 64);
    std::memcpy(p.k[5], k.lightColour, 64);
    return p;
}
vp::Pass fadePass(float slope) {
    vp::Pass p{};
    p.kind = vp::PassFade;
    p.k[0][3] = bitsOf(slope);
    return p;
}
vp::Pass texMatrixPass(float a, float b, float c, float d, float tx, float ty) {
    vp::Pass p{};
    p.kind = vp::PassTexMatrix;
    p.k[0][0] = bitsOf(a);
    p.k[0][1] = bitsOf(b);
    p.k[1][0] = bitsOf(c);
    p.k[1][1] = bitsOf(d);
    p.k[2][0] = bitsOf(tx);
    p.k[2][1] = bitsOf(ty);
    return p;
}
// Texture coordinates as a plane over two components of a vector (the position, or the integers of the colour row).
vp::Pass texGenPass(uint32_t kind, float sx, float sz, float tx, float tz, float s0, float t0) {
    vp::Pass p{};
    p.kind = kind;
    p.k[0][0] = bitsOf(sx);
    p.k[0][1] = bitsOf(tx);
    p.k[2][0] = bitsOf(sz);
    p.k[2][1] = bitsOf(tz);
    p.k[3][0] = bitsOf(s0);
    p.k[3][1] = bitsOf(t0);
    return p;
}

// One strip run as a batch record with one vertex per index (world geometry: a texture coordinate per corner).
// lit: the colour row holds the integer normal and a Light pass comes before the Transform; else it holds `colour`.
BatchRun cornerBatch(const Run& r, bool lit, const uint32_t colour[4]) {
    BatchRun b;
    b.prim = r.prim;
    if (lit)
        b.passes.push_back(lightPass(r.k));
    b.passes.push_back(transformPass(r.k));
    b.strips = r.strips;
    for (size_t i = 0; i < r.vertices.size(); ++i) {
        vp::BatchVertex v{};
        std::memcpy(v.row.pos, r.vertices[i].pos, 16);
        std::memcpy(v.row.st, r.vertices[i].st, 16);
        if (lit)
            std::memcpy(v.row.col, r.vertices[i].normal, 16);
        else
            std::memcpy(v.row.col, colour, 16);
        v.passMask = (1u << b.passes.size()) - 1u;
        v.slot = static_cast<uint32_t>(i);
        b.indices.push_back(static_cast<uint16_t>(i));
        b.vertices.push_back(v);
    }
    return b;
}

// The same run with every pass already applied: the "processed rows" form (a record without passes).
BatchRun processedBatch(const BatchRun& from) {
    BatchRun b = from;
    for (vp::BatchVertex& v : b.vertices) {
        for (size_t p = 0; p < from.passes.size(); ++p)
            if ((v.passMask >> p) & 1u)
                vp::applyPass(from.passes[p], v.row, v.gridX, v.gridZ);
        v.passMask = 0;
    }
    b.passes.clear();
    return b;
}

// The same run with clip-space positions and the Project pass, ending with the empty tag (a run of a batch with clip
// codes whose vertices are all inside).
BatchRun projectedBatch(const Run& r, const uint32_t colour[4]) {
    BatchRun b = cornerBatch(r, false, colour);
    for (vp::BatchVertex& v : b.vertices) {
        uint32_t p[4], clip[4];
        for (int l = 0; l < 4; ++l)
            p[l] = vp::vu::operand(v.row.pos[l]);
        for (int l = 0; l < 4; ++l) {
            uint32_t acc = vp::vu::mul(vp::vu::operand(r.k.mvp[0][l]), p[0]);
            acc = vp::vu::madd(acc, vp::vu::operand(r.k.mvp[1][l]), p[1]);
            acc = vp::vu::madd(acc, vp::vu::operand(r.k.mvp[2][l]), p[2]);
            clip[l] = vp::vu::madd(acc, vp::vu::operand(r.k.mvp[3][l]), p[3]);
        }
        std::memcpy(v.row.pos, clip, 16);
    }
    b.passes.clear();
    b.passes.push_back(transformPass(r.k, vp::PassProject));
    b.emptyTag = true;
    return b;
}

// A terrain grid of cols x rows cells in object space as records of at most 64 indices: per record up to four strips
// along the columns over neighbouring rows, with shared vertices. Passes: the grid generator (flat: no heights),
// optionally the point light, texture coordinates from the positions, the transform, then `layers` texture matrix
// passes and optionally a fade that leaves the first `unfaded` vertices of each record out.
struct GridSpec {
    uint32_t cols = 8, rows = 9;
    float size = 420.0f, height = 30.0f;
    bool flat = false, pointLight = false, fade = false;
    uint32_t layers = 0, exponent = 3, unfaded = 0;
    uint32_t colour[4] = {96, 104, 88, 0x80};
};
void addGrid(Scene& s, const GridSpec& g, const vp::Constants& k, uint32_t prim, float phase) {
    vp::Pass grid{};
    grid.kind = g.flat ? vp::PassGridFlat : vp::PassGrid;
    grid.param = g.cols;
    const float origin[4] = {-0.5f * g.size, 0.0f, -0.5f * g.size, 1.0f};
    const float step[4] = {g.size / static_cast<float>(g.cols - 1u), 0.0f, g.size / static_cast<float>(g.rows - 1u),
                           0.0f};
    for (int l = 0; l < 4; ++l) {
        grid.k[0][l] = bitsOf(origin[l]);
        grid.k[1][l] = bitsOf(step[l]);
    }
    std::vector<uint32_t> axisX(g.cols), axisZ(g.rows);
    vp::gridAxis(grid.k[0][0], grid.k[1][0], g.cols, axisX.data());
    vp::gridAxis(grid.k[0][2], grid.k[1][2], g.rows, axisZ.data());
    std::vector<vp::Pass> passes;
    passes.push_back(grid);
    if (g.pointLight) {
        vp::Pass p{};
        p.kind = vp::PassPointLight;
        p.param = g.exponent;
        const float at[4] = {0.15f * g.size, 90.0f, -0.1f * g.size, 0.0f}, axis[4] = {0.1f, 1.0f, -0.2f, 0.0f};
        const float limits[4] = {0.0f, 10.0f, 120.0f, 30.0f};
        for (int l = 0; l < 4; ++l) {
            p.k[0][l] = bitsOf(at[l]);
            p.k[1][l] = bitsOf(axis[l]);
            p.k[3][l] = bitsOf(limits[l]);
        }
        p.k[2][0] = 70;
        p.k[2][1] = 60;
        p.k[2][2] = 25;
        p.k[4][3] = bitsOf(0.4f);
        passes.push_back(p);
    }
    passes.push_back(texGenPass(vp::PassTexGenPos, 2.0f / g.size, 0.0f, 0.0f, 2.0f / g.size, 1.0f, 1.0f));
    passes.push_back(transformPass(k));
    for (uint32_t layer = 0; layer < g.layers; ++layer)
        passes.push_back(texMatrixPass(1.3f, 0.2f * static_cast<float>(layer), -0.15f, 1.2f,
                                       0.1f + 0.05f * static_cast<float>(layer), 0.07f));
    const uint32_t fadeBit = g.fade ? 1u << passes.size() : 0u;
    if (g.fade)
        passes.push_back(fadePass(0.00045f));
    const uint32_t all = (1u << passes.size()) - 1u;
    for (uint32_t row0 = 0; row0 + 1u < g.rows;) {
        BatchRun b;
        b.prim = prim;
        b.passes = passes;
        // as many strips (rows of quads) as fit 64 indices
        const uint32_t perStrip = g.cols * 2u;
        const uint32_t stripsHere = std::min<uint32_t>(vp::kBatchMaxIndices / perStrip, g.rows - 1u - row0);
        for (uint32_t r = row0; r <= row0 + stripsHere; ++r)
            for (uint32_t c = 0; c < g.cols; ++c) {
                vp::BatchVertex v{};
                const float wave =
                    std::sin(0.9f * static_cast<float>(c) + phase) * std::cos(0.7f * static_cast<float>(r));
                v.row.pos[0] = 0x7FC00000u; // the generator overwrites x, z and w: anything may stand here
                v.row.pos[1] = static_cast<uint32_t>(32768 + static_cast<int32_t>(0.5f * g.height * wave));
                v.row.pos[2] = 0x00000001u;
                v.row.pos[3] = 0xFFFFFFFFu;
                std::memcpy(v.row.col, g.colour, 16);
                v.row.st[2] = bitsOf(1.0f);
                v.gridX = axisX[c];
                v.gridZ = axisZ[r];
                v.slot = r * g.cols + c;
                const uint32_t index = static_cast<uint32_t>(b.vertices.size());
                v.passMask = index < g.unfaded ? all & ~fadeBit : all;
                b.vertices.push_back(v);
            }
        for (uint32_t st = 0; st < stripsHere; ++st) {
            b.strips.push_back(perStrip);
            for (uint32_t c = 0; c < g.cols; ++c) {
                b.indices.push_back(static_cast<uint16_t>(st * g.cols + c));
                b.indices.push_back(static_cast<uint16_t>((st + 1u) * g.cols + c));
            }
        }
        row0 += stripsHere;
        s.batch(std::move(b));
    }
}

// A patch as a run for the batch builders (addPatch splits it into strips of at most 64 vertices in all).
Run patchForBatch(std::mt19937& rng, const Variant& v, const Place& at, uint32_t rows, uint32_t cols, float size,
                  float phase) {
    Run r;
    r.prim = v.prim;
    r.program = v.program;
    r.k = worldConstants(rng, at, 8388608.0f, v.alpha);
    addPatch(r, rows, cols, size, 22.0f, 2.5f, phase);
    return r;
}

// Scene "batch": every pass kind and chains of several passes, through all draw states.
Scene batchScene(uint32_t frame) {
    Scene s;
    beginFrame(s, frame, frame == 0);
    std::mt19937 rng(5005);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const float turn = 0.04f * static_cast<float>(frame);
    const uint32_t plain[4] = {150, 120, 90, 0x80};
    for (uint32_t i = 0; i < 40; ++i) {
        const Variant v = setVariant(s, i);
        const Place at{unit(rng) * 3.0f + turn, -0.9f + 0.5f * unit(rng), unit(rng) * 420.0f, unit(rng) * 260.0f,
                       350.0f + 1400.0f * std::fabs(unit(rng))};
        const float phase = unit(rng) * 3.0f + turn;
        switch (i % 10u) {
            case 0: // the transform alone, a vertex per index
                s.batch(cornerBatch(patchForBatch(rng, v, at, 3, 8, 260.0f, phase), false, plain));
                break;
            case 1: // directional lights and the transform
                s.batch(cornerBatch(patchForBatch(rng, v, at, 4, 8, 300.0f, phase), true, plain));
                break;
            case 2: // processed rows: no pass at all
                s.batch(processedBatch(cornerBatch(patchForBatch(rng, v, at, 2, 10, 240.0f, phase), true, plain)));
                break;
            case 3: { // texture coordinates from the integers of the colour row, then lights and the transform
                BatchRun b = cornerBatch(patchForBatch(rng, v, at, 3, 9, 280.0f, phase), true, plain);
                b.passes.insert(b.passes.begin(),
                                texGenPass(vp::PassTexGenInt, 1.0f / 90.0f, 0.0f, 0.0f, 1.0f / 90.0f, 0.5f, 0.5f));
                for (vp::BatchVertex& vx : b.vertices) {
                    vx.row.col[3] = 1; // the generator multiplies its fourth row by the fourth integer
                    vx.passMask = 7u;
                }
                s.batch(std::move(b));
                break;
            }
            case 4: { // terrain: grid, texture coordinates from positions, transform
                GridSpec g;
                g.cols = 6u + rng() % 5u;
                g.rows = 7u + rng() % 6u;
                addGrid(s, g, worldConstants(rng, at, 8388608.0f, v.alpha), v.prim, phase);
                break;
            }
            case 5: { // terrain layers: three texture matrix passes on top of each other and the depth fade
                GridSpec g;
                g.layers = 3;
                g.fade = true;
                g.unfaded = 5;
                addGrid(s, g, worldConstants(rng, at, 8388608.0f, v.alpha), v.prim, phase);
                break;
            }
            case 6: { // water: flat grid, point light, texture matrix
                GridSpec g;
                g.flat = true;
                g.pointLight = true;
                g.exponent = 1u + i % 5u;
                g.layers = 1;
                addGrid(s, g, worldConstants(rng, at, 8388608.0f, v.alpha), v.prim, phase);
                break;
            }
            case 7: { // the longest chain a record holds: 24 passes
                GridSpec g;
                g.cols = 8;
                g.rows = 5;
                g.pointLight = true;
                g.exponent = 0; // no light term
                g.layers = 19;
                g.fade = true;
                addGrid(s, g, worldConstants(rng, at, 8388608.0f, v.alpha), v.prim, phase);
                break;
            }
            case 8: // clip-space positions projected by the run itself, with the closing empty tag
                s.batch(projectedBatch(patchForBatch(rng, v, at, 3, 8, 260.0f, phase), plain));
                break;
            default: { // a faded, lit run: lights, transform, fade (the fade reads the transform's 1 / w)
                BatchRun b = cornerBatch(patchForBatch(rng, v, at, 4, 7, 320.0f, phase), true, plain);
                b.passes.push_back(fadePass(0.0006f));
                for (size_t n = 0; n < b.vertices.size(); ++n)
                    b.vertices[n].passMask = (n % 5u) ? 7u : 3u; // one vertex in five is outside the fade's range
                s.batch(std::move(b));
                break;
            }
        }
    }
    return s;
}

// Scene "batchmix": batch records between strip records and ordinary packets, the registers and the vertex queue a
// batch record leaves behind, records that send nothing, and strips of one and two vertices.
Scene batchMixScene(uint32_t frame) {
    Scene s;
    beginFrame(s, frame, frame == 0);
    std::mt19937 rng(6006);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const float turn = 0.05f * static_cast<float>(frame);
    const uint32_t plain[4] = {130, 150, 110, 0x80};
    const auto patch = [&](const Variant& v, const Place& at, uint32_t rows, uint32_t cols, float size) {
        return patchForBatch(rng, v, at, rows, cols, size, turn);
    };
    // a strip record, a batch record over it, an ordinary blended sprite, a strip record again
    s.run(patch(setVariant(s, 0), Place{0.3f + turn, -1.1f, -60, 20, 700}, 6, 12, 420));
    s.batch(cornerBatch(patch(setVariant(s, 6), Place{-0.4f - turn, -1.3f, 40, -10, 620}, 4, 8, 380), true, plain));
    {
        Gif g;
        g.ad({{kRegTest, testReg(0, 0, 0, 0, kZAlways)}, {kRegAlpha, alphaReg(0, 1, 0, 1)}});
        g.tag(2, true, kSprite | kAbe, 0, 2, 0x51);
        g.qw(rgLo(40, 250), baHi(30, 0x48));
        g.qw(screenXy(220, 140), 0);
        g.qw(rgLo(40, 250), baHi(30, 0x48));
        g.qw(screenXy(450, 310), 0);
        s.raw(GifPath::Path2, std::move(g));
    }
    s.run(patch(setVariant(s, 4), Place{1.2f, -0.7f, -330, -150, 900}, 4, 8, 300));
    {
        GridSpec g;
        g.layers = 2;
        g.fade = true;
        addGrid(s, g, worldConstants(rng, Place{0.8f + turn, -1.0f, 300, 120, 800}, 8388608.0f, 0x80),
                setVariant(s, 0).prim, turn);
    }
    // the texture uploaded again between two batch records that sample it
    s.batch(cornerBatch(patch(setVariant(s, 0), Place{1.2f, -0.7f, 330, -150, 900}, 4, 8, 300), false, plain));
    {
        Gif g;
        upload(g, kTexA, texture(3, frame + 1u));
        s.raw(GifPath::Path3, std::move(g));
    }
    s.batch(cornerBatch(patch(setVariant(s, 0), Place{1.2f, -0.7f, -330, 150, 900}, 4, 8, 300), false, plain));
    {
        Gif g;
        upload(g, kTexA, texture(0, 0));
        s.raw(GifPath::Path3, std::move(g));
    }
    // strips of one and two vertices between real ones, with shared vertices
    {
        BatchRun b = cornerBatch(patch(setVariant(s, 3), Place{0.5f, -1.0f, -250, 60, 760}, 2, 5, 200), true, plain);
        b.strips = {1, 10, 2, 10, 1, 2};
        b.indices = {3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 5, 6, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 19, 7, 8};
        s.batch(std::move(b));
    }
    // a run whose far vertices carry the "do not draw" bit (the fog lane goes below zero)
    {
        Run r = patch(setVariant(s, 0), Place{0.2f, -1.35f, 150, 120, 520}, 4, 8, 560);
        r.k.fog[3] = bitsOf(0.5f);
        r.k.offset[3] = bitsOf(255.0f);
        s.batch(cornerBatch(r, true, plain));
    }
    // vertices behind the eye and coordinates that wrap, inside a smaller scissor and without depth writes
    {
        Gif g;
        g.ad({{kRegZbuf, s.zbuf | (1ull << 32)},
              {kRegScissor, 330ull | (629ull << 16) | (150ull << 32) | (370ull << 48)}});
        s.raw(GifPath::Path2, std::move(g));
        s.batch(cornerBatch(patch(setVariant(s, 4), Place{turn, -1.45f, 0, -70, 60}, 4, 8, 900), true, plain));
        s.batch(cornerBatch(patch(setVariant(s, 0), Place{0.9f, -0.2f, 30, -40, -120}, 4, 8, 700), false, plain));
        Gif h;
        h.ad({{kRegZbuf, s.zbuf},
              {kRegScissor, (static_cast<uint64_t>(kWidth - 1) << 16) | (static_cast<uint64_t>(kHeight - 1) << 48)}});
        s.raw(GifPath::Path2, std::move(h));
    }
    // ---- the registers after a batch record ----
    const auto twoMore = [&s](float x) { // a tag without PRIM: two more vertices for whatever strip is open
        Gif g;
        g.tag(2, false, 0, 0, 4, kTagRegisters);
        for (uint32_t i = 0; i < 2; ++i) {
            g.qw(bitsOf(0.25f) | (static_cast<uint64_t>(bitsOf(0.5f)) << 32), bitsOf(1.0f));
            g.qw(rgLo(40 + 150 * i, 220), baHi(90, 0x80));
            g.qw(0, 200ull << 36);
            g.qw(screenXy(x + 30.0f * static_cast<float>(i), 440.0f - 60.0f * static_cast<float>(i)), 0x0F000000ull);
        }
        s.raw(GifPath::Path1, std::move(g));
    };
    // 1. the vertex queue: after a run without the empty tag the last strip stays open ...
    s.batch(cornerBatch(patch(setVariant(s, 2), Place{0.1f, -1.2f, -230, -120, 640}, 3, 6, 220), true, plain));
    twoMore(8.0f);
    // ... and the closing empty tag writes PRIM again, which starts a new primitive: two vertices draw nothing
    s.batch(projectedBatch(patch(setVariant(s, 2), Place{0.1f, -1.2f, 230, -120, 640}, 3, 6, 220), plain));
    twoMore(300.0f);
    // 2. a record that sends no vertex but the empty tag still sets PRIM: the triangle below has no PRIM of its own
    {
        Gif g;
        g.tag(1, true, kSprite, 0, 1, 0x5); // leave another primitive type behind first
        g.qw(screenXy(0, 0), 0);
        s.raw(GifPath::Path1, std::move(g));
        BatchRun empty;
        empty.prim = kTriangle | kIip;
        empty.emptyTag = true;
        s.batch(std::move(empty));
        Gif t;
        t.tag(3, false, 0, 0, 2, 0x51);
        const float xs[3] = {420, 500, 455}, ys[3] = {20, 30, 90};
        for (uint32_t i = 0; i < 3; ++i) {
            t.qw(rgLo(240 - 90 * i, 60 + 80 * i), baHi(40 + 100 * i, 0x80));
            t.qw(screenXy(xs[i], ys[i]), 0x0F000000ull);
        }
        s.raw(GifPath::Path1, std::move(t));
    }
    // 3. colour, texture coordinate, Q and fog of the last vertex: a textured, fogged triangle that sends only places
    s.batch(cornerBatch(patch(setVariant(s, 0), Place{-0.6f, -1.0f, 250, -130, 680}, 3, 6, 200), true, plain));
    {
        Gif g;
        g.tag(3, true, kTriangle | kTme | kFge, 0, 1, 0x5);
        g.qw(screenXy(560, 380), 0x0F000000ull);
        g.qw(screenXy(630, 384), 0x0F000000ull);
        g.qw(screenXy(600, 440), 0x0F000000ull);
        s.raw(GifPath::Path1, std::move(g));
    }
    // 4. a sprite that sends only places takes the colour of the record's last vertex: a known colour here
    {
        Gif g;
        g.ad({{kRegTest, testReg(0, 0, 0, 0, kZAlways)}});
        s.raw(GifPath::Path2, std::move(g));
        const uint32_t known[4] = {kInheritColour[0], kInheritColour[1], kInheritColour[2], 0x80};
        Run quad = facingQuad(rng, 200.0f, 110.0f, 420.0f, 20.0f, kStrip | kIip, vp::StripsFlat);
        s.batch(cornerBatch(quad, false, known));
        Gif sprite;
        sprite.tag(2, true, kSprite, 0, 1, 0x5);
        sprite.qw(screenXy(kInheritX0, kInheritY0), 0);
        sprite.qw(screenXy(kInheritX1, kInheritY1), 0);
        s.raw(GifPath::Path1, std::move(sprite));
    }
    return s;
}

// Records a renderer has to ignore without reading outside them: each is a batch record with one field broken.
std::vector<std::vector<uint8_t>> malformedBatches() {
    std::mt19937 rng(7007);
    const uint32_t plain[4] = {200, 40, 40, 0x80};
    const Variant v{kStrip | kIip, vp::StripsLit, 0x80};
    const BatchRun good =
        cornerBatch(patchForBatch(rng, v, Place{0, -1.0f, 0, 0, 500}, 2, 6, 300.0f, 0.0f), true, plain);
    std::vector<std::vector<uint8_t>> out;
    const auto broken = [&](const std::function<void(BatchRun&, std::vector<uint8_t>&)>& change) {
        BatchRun b = good;
        std::vector<uint8_t> none;
        change(b, none);
        std::vector<uint8_t> packet = wrapBatch(b);
        change(b, packet);
        out.push_back(std::move(packet));
    };
    const auto header = [](std::vector<uint8_t>& packet, uint32_t field, uint32_t value) {
        if (!packet.empty())
            std::memcpy(packet.data() + 16 + field * 4u, &value, 4);
    };
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.indices[5] = static_cast<uint16_t>(b.vertices.size()); });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.indices[0] = 0xFFFF; });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.strips[0] += 1; });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.strips[1] = 0; });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.passes[0].kind = 11; });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.passes[1].kind = 0; });
    broken([&](BatchRun& b, std::vector<uint8_t>&) { b.vertices[3].passMask = 4; }); // a pass the record has not
    broken([&](BatchRun& b, std::vector<uint8_t>&) {
        b.passes[0].kind = vp::PassPointLight;
        b.passes[0].param = vp::kBatchMaxExponent + 1u;
    });
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 2, 0x7FFFFFFFu); }); // strip count
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 3, 65u); });         // index count
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 3, 0xFFFFFFF0u); }); // index count that wraps sizes
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 4, 0xFFFFFFFFu); }); // vertex count
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 5, 25u); });         // pass count
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 5, 0x10000000u); }); // pass count that wraps sizes
    broken([&](BatchRun&, std::vector<uint8_t>& p) { header(p, 4, 3u); }); // fewer vertices than the size holds
    {
        // a record cut short: the wrapping tag announces only the header
        std::vector<uint8_t> packet = wrapBatch(good);
        packet.resize(16u + sizeof(vp::BatchHeader));
        const uint64_t wrapLo = uint64_t(sizeof(vp::BatchHeader) / 16u) | (1ull << 15) | (3ull << 58);
        std::memcpy(packet.data(), &wrapLo, 8);
        out.push_back(std::move(packet));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------ comparing
int g_failures = 0;
uint64_t g_checks = 0;
void expect(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what);
    }
}
void expectEqual(uint64_t got, uint64_t want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("  [FAIL] %s: %llu, expected %llu\n", what, static_cast<unsigned long long>(got),
                    static_cast<unsigned long long>(want));
    }
}

struct Tally {
    uint64_t frames = 0, framesDiffering = 0, pixels = 0, pixelsDiffering = 0;
    void add(const Tally& o) {
        frames += o.frames;
        framesDiffering += o.framesDiffering;
        pixels += o.pixels;
        pixelsDiffering += o.pixelsDiffering;
    }
};

uint64_t differingPixels(const Image& a, const Image& b) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
        return static_cast<uint64_t>(a.width) * a.height;
    if (std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size()) == 0)
        return 0;
    uint64_t n = 0;
    for (size_t i = 0; i < a.rgba.size(); i += 4)
        n += std::memcmp(&a.rgba[i], &b.rgba[i], 4) != 0;
    return n;
}

struct CounterShot {
    uint64_t cpu, gpu, taken, switches, fallback[vpath::kFallbackCount];
};
CounterShot counters() {
    const vpath::Counters& c = vpath::state().counters;
    CounterShot s{};
    s.cpu = c.cpuVertices.load();
    s.gpu = c.gpuVertices.load();
    s.taken = c.takenVertices.load();
    s.switches = c.switches.load();
    for (uint32_t i = 0; i < vpath::kFallbackCount; ++i)
        s.fallback[i] = c.fallback[i].load();
    return s;
}

struct Totals {
    uint64_t scenes = 0, sequences = 0, runsAsRecords = 0, runsAsPackets = 0, verticesAsRecords = 0;
    uint64_t verticesAsPackets = 0, backendFallbacks = 0, cpuVertices = 0, gpuVertices = 0;
    uint64_t batchRecords = 0, batchVertices = 0;
    Tally colour, depth;
    Tally controlColour, controlDepth; // the negative controls: these are meant to differ
} g_totals;

std::string g_outDir;
bool g_dumpPictures = false; // --pictures: frame 0 of every sequence as PNG files into --out

enum class Stream { Packets, Records, FollowWord };

struct Sequence {
    const char* name = "";
    std::function<Scene(uint32_t)> scene;
    std::function<void(Scene&)> changeB; // a negative control: applied to the second renderer's scene only
    uint32_t frames = 1;
    uint32_t depthEvery = 1; // a depth picture after every n-th frame (0: never)
    uint32_t rungs = 256;
    Stream stream = Stream::Records;
    bool control = false;
};
struct Result {
    Tally colour, depth;
    uint64_t recordFrames = 0, packetFrames = 0, records = 0, vertices = 0;
    Image lastA, lastB;
    bool ran = false;
};

void display(Renderer& gs) {
    gs.writePriv(0x12000000, 1);
    gs.writePriv(0x12000070, static_cast<uint64_t>(kFbw) << 9);
    gs.writePriv(0x12000080, (static_cast<uint64_t>(kWidth * 4u - 1u) << 32) |
                                 (static_cast<uint64_t>(kHeight - 1u) << 44) | (3ull << 23));
}

Tally compareFrames(Renderer& a, Renderer& b, const char* name, const char* what, uint32_t frame, Image* keepA,
                    Image* keepB, bool quiet) {
    Tally t;
    Image ia, ib;
    if (!a.captureDisplay(ia) || !b.captureDisplay(ib)) {
        expect(false, "capture of the display");
        return t;
    }
    t.frames = 1;
    t.pixels = static_cast<uint64_t>(ia.width) * ia.height;
    t.pixelsDiffering = differingPixels(ia, ib);
    t.framesDiffering = t.pixelsDiffering ? 1 : 0;
    if (t.pixelsDiffering && !quiet) {
        std::printf("  [DIFF] %s frame %u (%s): %llu of %llu pixels differ\n", name, frame, what,
                    static_cast<unsigned long long>(t.pixelsDiffering), static_cast<unsigned long long>(t.pixels));
        if (!g_outDir.empty()) {
            const std::string base = g_outDir + "/" + name + "_" + what + "_" + std::to_string(frame);
            writePng((base + "_packets.png").c_str(), ia);
            writePng((base + "_records.png").c_str(), ib);
        }
    }
    if (!g_outDir.empty() && g_dumpPictures && frame == 0) {
        writePng((g_outDir + "/" + name + "_" + what + "_packets.png").c_str(), ia);
        writePng((g_outDir + "/" + name + "_" + what + "_second.png").c_str(), ib);
    }
    if (keepA)
        *keepA = std::move(ia);
    if (keepB)
        *keepB = std::move(ib);
    return t;
}

// Runs one scene for some frames in two fresh renderers side by side: the first gets every run as packets, the
// second as `stream` says (FollowWord: as the VU1 hook decides, from the control word at the start of the frame).
Result runSequence(const Config& cfg, const Sequence& q) {
    Result res;
    const auto a = Renderer::create(cfg), b = Renderer::create(cfg);
    if (!a || !b) {
        expect(false, "renderer creation");
        return res;
    }
    display(*a);
    display(*b);
    const vpath::Capability cap = b->vertexPathCapability();
    for (uint32_t f = 0; f < q.frames; ++f) {
        const Scene sceneA = q.scene(f);
        Scene sceneB = sceneA;
        if (q.changeB)
            q.changeB(sceneB);
        const vpath::Eval eval = vpath::evalOf(vpath::controlWord());
        const bool records =
            q.stream == Stream::Records || (q.stream == Stream::FollowWord && eval != vpath::Eval::Microprogram);
        uint64_t runCount = 0, vertexCount = 0, batchCount = 0;
        for (const Item& it : sceneB.items)
            if (it.isRun) {
                ++runCount;
                vertexCount += it.run.vertices.size();
            } else if (it.isBatch) { // a batch record counts the vertices it sends: its indices
                ++runCount;
                ++batchCount;
                vertexCount += it.batch.indices.size();
            }

        const CounterShot before = counters();
        sendScene(*a, sceneA, false);
        const CounterShot afterPackets = counters();
        sendScene(*b, sceneB, records);
        const CounterShot after = counters();
        // plain packets count nothing
        expect(std::memcmp(&before, &afterPackets, sizeof(before)) == 0, "packets leave the path counters alone");
        const uint64_t cpu = after.cpu - afterPackets.cpu, gpu = after.gpu - afterPackets.gpu;
        const uint64_t noPath = after.fallback[static_cast<uint32_t>(vpath::Fallback::BackendNoPath)] -
                                afterPackets.fallback[static_cast<uint32_t>(vpath::Fallback::BackendNoPath)];
        uint64_t otherFallbacks = 0;
        for (uint32_t i = 0; i < vpath::kFallbackCount; ++i)
            if (i != static_cast<uint32_t>(vpath::Fallback::BackendNoPath))
                otherFallbacks += after.fallback[i] - afterPackets.fallback[i];
        expectEqual(cpu + gpu, records ? vertexCount : 0, "vertices evaluated on the CPU plus drawn by a shader");
        expectEqual(after.taken - before.taken, 0, "the renderer does not count runs taken (the VU1 hook does)");
        if (!records || eval != vpath::Eval::Gpu) {
            // records under `microprogram` or `records` are evaluated in the core and no reason is counted
            expectEqual(gpu, 0, "vertices drawn by a shader outside the gpu mode");
            expectEqual(noPath + otherFallbacks, 0, "fallbacks counted outside the gpu mode");
        } else if (!cap.backendHasPath) {
            expectEqual(noPath, runCount, "records counted as \"backend has no path\"");
            expectEqual(gpu, 0, "vertices drawn by a shader on a backend without the path");
            expectEqual(otherFallbacks, 0, "other fallback reasons on a backend without the path");
        } else {
            // every strip record either drawn by the shader or declined with exactly one reason; no backend has
            // a shader for batch records yet, so each of those counts as "backend has no path"
            expectEqual(noPath, batchCount, "\"backend has no path\" counted once per batch record");
            expect(noPath + otherFallbacks <= runCount, "at most one fallback reason per record");
        }

        a->vsync(true);
        b->vsync(true);
        const bool last = f + 1u == q.frames;
        res.colour.add(compareFrames(*a, *b, q.name, "colour", f, last ? &res.lastA : nullptr,
                                     last ? &res.lastB : nullptr, q.control));
        if (q.depthEvery && f % q.depthEvery == 0u) {
            const std::vector<uint8_t> picture = depthPicture(sceneA, q.rungs);
            a->submitGifPacket(GifPath::Path3, picture.data(), static_cast<uint32_t>(picture.size()));
            b->submitGifPacket(GifPath::Path3, picture.data(), static_cast<uint32_t>(picture.size()));
            a->vsync(true);
            b->vsync(true);
            res.depth.add(compareFrames(*a, *b, q.name, "depth", f, nullptr, nullptr, q.control));
        }
        (records ? res.recordFrames : res.packetFrames)++;
        if (records) {
            res.records += runCount;
            res.vertices += vertexCount;
            g_totals.runsAsRecords += runCount;
            g_totals.verticesAsRecords += vertexCount;
            g_totals.batchRecords += batchCount;
            for (const Item& it : sceneB.items)
                if (it.isBatch)
                    g_totals.batchVertices += it.batch.indices.size();
        } else {
            g_totals.runsAsPackets += runCount;
            g_totals.verticesAsPackets += vertexCount;
        }
        g_totals.backendFallbacks += noPath;
        g_totals.cpuVertices += cpu;
        g_totals.gpuVertices += gpu;
    }
    expect(!a->deviceLost() && !b->deviceLost(), "the device survived the sequence");
    g_totals.sequences++;
    (q.control ? g_totals.controlColour : g_totals.colour).add(res.colour);
    (q.control ? g_totals.controlDepth : g_totals.depth).add(res.depth);
    res.ran = true;
    return res;
}

void report(const char* what, const Sequence& q, const Result& r) {
    std::printf("  %-22s %-34s %3u frames (%llu as records, %llu as packets), %llu records, %llu vertices: colour "
                "%llu of %llu pixels differ, depth %llu of %llu\n",
                q.name, what, q.frames, static_cast<unsigned long long>(r.recordFrames),
                static_cast<unsigned long long>(r.packetFrames), static_cast<unsigned long long>(r.records),
                static_cast<unsigned long long>(r.vertices), static_cast<unsigned long long>(r.colour.pixelsDiffering),
                static_cast<unsigned long long>(r.colour.pixels),
                static_cast<unsigned long long>(r.depth.pixelsDiffering),
                static_cast<unsigned long long>(r.depth.pixels));
    std::fflush(stdout);
}
void expectIdentical(const Result& r, const char* what) {
    expect(r.ran && r.colour.frames != 0, what);
    expectEqual(r.colour.pixelsDiffering, 0, "colour pixels differing");
    expectEqual(r.depth.pixelsDiffering, 0, "depth picture pixels differing");
}

struct NamedScene {
    const char* name;
    Scene (*build)(uint32_t);
};
const NamedScene kScenes[] = {{"world", worldScene},         {"many", manyScene},   {"interleaved", interleavedScene},
                              {"generated", generatedScene}, {"batch", batchScene}, {"batchmix", batchMixScene}};

void resetPath() {
    vpath::configure(vpath::pack(vpath::Eval::Microprogram, vpath::kAllStages), 0);
}

// The pixel in the middle of the sprite that sends only places must have the colour of the record's last vertex.
void expectInheritedColour(const Image& img, uint32_t scale, const char* what) {
    const uint32_t x = static_cast<uint32_t>((kInheritX0 + kInheritX1) * 0.5f) * scale;
    const uint32_t y = static_cast<uint32_t>((kInheritY0 + kInheritY1) * 0.5f) * scale;
    if (x >= img.width || y >= img.height) {
        expect(false, what);
        return;
    }
    const uint8_t* p = &img.rgba[(size_t(y) * img.width + x) * 4];
    const bool ok = p[0] == kInheritColour[0] && p[1] == kInheritColour[1] && p[2] == kInheritColour[2];
    if (!ok)
        std::printf("  got (%u, %u, %u), expected (%u, %u, %u)\n", p[0], p[1], p[2], kInheritColour[0],
                    kInheritColour[1], kInheritColour[2]);
    expect(ok, what);
}

void runScale(Config cfg, uint32_t scale, uint32_t toggleFrames) {
    cfg.scale = scale;
    std::printf("internal scale %u\n", scale);
    const uint32_t rungs = scale >= 8 ? 64u : 256u;

    // The renderer itself is repeatable: packets against packets, nothing counted.
    {
        resetPath();
        Sequence q{"world", worldScene, nullptr, 2, 1, rungs, Stream::Packets, false};
        const Result r = runSequence(cfg, q);
        report("packets against packets", q, r);
        expectIdentical(r, "packets against packets");
    }

    // Every scene, records against packets, under each evaluation mode. Under `microprogram` the VU1 hook builds
    // no record, but one that arrives (a capture taken under another setting) is honoured all the same.
    const vpath::Eval kModes[] = {vpath::Eval::Records, vpath::Eval::Gpu, vpath::Eval::Microprogram};
    for (const NamedScene& ns : kScenes)
        for (const vpath::Eval mode : kModes) {
            vpath::configure(vpath::pack(mode, vpath::kAllStages), 0);
            Sequence q{ns.name, ns.build, nullptr, 2, 1, rungs, Stream::Records, false};
            const Result r = runSequence(cfg, q);
            const std::string what = std::string("records, mode ") + vpath::evalName(mode);
            report(what.c_str(), q, r);
            expectIdentical(r, what.c_str());
            if ((ns.build == interleavedScene || ns.build == batchMixScene) && r.ran) {
                expectInheritedColour(r.lastA, scale,
                                      "packets: a sprite without a colour takes the last strip vertex's");
                expectInheritedColour(r.lastB, scale,
                                      "records: a sprite without a colour takes the last record vertex's");
            }
            ++g_totals.scenes;
        }

    // The mode flipping at frame boundaries (DRAKAN_VERTEX_TOGGLE): the second renderer gets records in the frames
    // the control word asks for them and packets in the others, like the game.
    struct Toggle {
        uint32_t scene;
        vpath::Eval mode;
        uint32_t every;
    };
    const Toggle kToggles[] = {{2, vpath::Eval::Records, 1}, {0, vpath::Eval::Gpu, 3},     {1, vpath::Eval::Records, 5},
                               {3, vpath::Eval::Gpu, 2},     {4, vpath::Eval::Records, 2}, {5, vpath::Eval::Gpu, 1},
                               {5, vpath::Eval::Records, 3}};
    for (const Toggle& t : kToggles) {
        vpath::configure(vpath::pack(t.mode, vpath::kAllStages), t.every);
        const uint64_t switchesBefore = vpath::state().counters.switches.load();
        Sequence q{
            kScenes[t.scene].name, kScenes[t.scene].build, nullptr, toggleFrames, 8, rungs, Stream::FollowWord, false};
        const Result r = runSequence(cfg, q);
        const std::string what =
            std::string("toggle every ") + std::to_string(t.every) + ", mode " + vpath::evalName(t.mode);
        report(what.c_str(), q, r);
        expectIdentical(r, what.c_str());
        expect(r.recordFrames >= toggleFrames / 4u && r.packetFrames >= toggleFrames / 4u,
               "the toggle gave frames of both kinds");
        // the two renderers reach each boundary one after the other, so each flip counts once
        const uint64_t frameCount = r.colour.frames + r.depth.frames;
        // (the word starts as `microprogram`; with n = 1 the first boundary keeps it, otherwise it sets the mode)
        expectEqual(vpath::state().counters.switches.load() - switchesBefore,
                    t.every == 1u ? frameCount - 1u : 1u + frameCount / t.every, "changes of the control word");
        ++g_totals.scenes;
    }
    resetPath();

    // ---- negative controls: one constant of one record changed in the second renderer only ----
    vpath::configure(vpath::pack(vpath::Eval::Records, vpath::kAllStages), 0);
    {
        // one unit in the red component of the constant-colour quad
        Sequence q{"world",
                   worldScene,
                   [](Scene& s) { s.items[s.items.size() - kWorldQuadFromEnd].run.k.colour[0] += 1; },
                   1,
                   1,
                   rungs,
                   Stream::Records,
                   true};
        const Result r = runSequence(cfg, q);
        report("CONTROL colour + 1", q, r);
        expect(r.colour.pixelsDiffering != 0, "control: a colour integer changed by one changes pixels");
        expectEqual(r.depth.pixelsDiffering, 0, "control: a colour change leaves the depth picture alone");
    }
    {
        // the depth offset of the quad one float step up (one unit of depth before the fixed-point shift), then
        // 64 steps: its colour does not change, so only the depth picture can tell
        for (const uint32_t steps : {1u, 64u}) {
            Sequence q{"world",
                       worldScene,
                       [steps](Scene& s) { s.items[s.items.size() - kWorldQuadFromEnd].run.k.offset[2] += steps; },
                       1,
                       1,
                       rungs,
                       Stream::Records,
                       true};
            const Result r = runSequence(cfg, q);
            const std::string what = "CONTROL depth offset + " + std::to_string(steps) + " ulp";
            report(what.c_str(), q, r);
            expectEqual(r.colour.pixelsDiffering, 0, "control: a depth change of the quad leaves the colours alone");
            if (steps == 64u)
                expect(r.depth.pixelsDiffering != 0,
                       "control: a depth offset changed by 64 steps changes the depth picture");
        }
    }
    {
        // one unit in the last place of a matrix element: reported, not required (it may move no pixel)
        Sequence q{"world",
                   worldScene,
                   [](Scene& s) {
                       for (Item& it : s.items)
                           if (it.isRun) {
                               it.run.k.mvp[3][0] += 1;
                               break;
                           }
                   },
                   1,
                   1,
                   rungs,
                   Stream::Records,
                   true};
        const Result r = runSequence(cfg, q);
        report("CONTROL matrix + 1 ulp (info)", q, r);
    }
    // ---- batch records: malformed ones are ignored, and the controls ----
    {
        // every malformed record in front of every batch record of the second renderer: nothing may change
        Sequence q{"batchmix",
                   batchMixScene,
                   [](Scene& s) {
                       static const std::vector<std::vector<uint8_t>> bad = malformedBatches();
                       std::vector<Item> items;
                       for (Item& it : s.items) {
                           if (it.isBatch)
                               for (const auto& record : bad)
                                   items.push_back(Item{false, GifPath::Path1, record, {}});
                           items.push_back(std::move(it));
                       }
                       s.items = std::move(items);
                   },
                   2,
                   1,
                   rungs,
                   Stream::Records,
                   false};
        const Result r = runSequence(cfg, q);
        report("malformed batch records", q, r);
        expectIdentical(r, "malformed batch records are ignored");
        ++g_totals.scenes;
    }
    const auto firstBatchWith = [](Scene& s, uint32_t kind) -> BatchRun* {
        for (Item& it : s.items)
            if (it.isBatch)
                for (const vp::Pass& p : it.batch.passes)
                    if (p.kind == kind)
                        return &it.batch;
        return nullptr;
    };
    {
        // the viewport offset of every batch record one pixel down
        Sequence q{"batch",
                   batchScene,
                   [](Scene& s) {
                       for (Item& it : s.items)
                           if (it.isBatch)
                               for (vp::Pass& p : it.batch.passes)
                                   if (p.kind == vp::PassTransform || p.kind == vp::PassProject)
                                       p.k[5][1] = bitsOf(2049.0f);
                   },
                   1,
                   1,
                   rungs,
                   Stream::Records,
                   true};
        const Result r = runSequence(cfg, q);
        report("CONTROL batch offset + 1 pixel", q, r);
        expect(r.colour.pixelsDiffering != 0, "control: batch records moved by one pixel change the picture");
    }
    {
        // the closing empty tag dropped from the records that have one: the two vertices after one of them then
        // continue its last strip, and the triangle without a PRIM is drawn as a sprite
        Sequence q{"batchmix",
                   batchMixScene,
                   [](Scene& s) {
                       for (Item& it : s.items)
                           if (it.isBatch)
                               it.batch.emptyTag = false;
                   },
                   1,
                   1,
                   rungs,
                   Stream::Records,
                   true};
        const Result r = runSequence(cfg, q);
        report("CONTROL no empty tag", q, r);
        expect(r.colour.pixelsDiffering != 0, "control: without the closing empty tags the picture changes");
    }
    {
        // a pass switched off for one vertex of the first faded record (its pass mask)
        Sequence q{"batch",
                   batchScene,
                   [&](Scene& s) {
                       if (BatchRun* b = firstBatchWith(s, vp::PassFade))
                           for (vp::BatchVertex& v : b->vertices)
                               v.passMask &= (1u << (b->passes.size() - 1u)) - 1u;
                   },
                   1,
                   1,
                   rungs,
                   Stream::Records,
                   true};
        const Result r = runSequence(cfg, q);
        report("CONTROL batch fade off", q, r);
        expect(r.colour.pixelsDiffering != 0, "control: a batch record without its fade pass changes pixels");
    }
    resetPath();
}

} // namespace

int main(int argc, char** argv) {
    Config cfg;
    uint32_t onlyScale = 0, toggleFrames = 64;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--warp"))
            cfg.forceWarp = true;
        else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
            onlyScale = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            toggleFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--out") && i + 1 < argc)
            g_outDir = argv[++i];
        else if (!std::strcmp(argv[i], "--pictures"))
            g_dumpPictures = true;
        else if (!std::strcmp(argv[i], "--gpu-api") && i + 1 < argc)
            cfg.gpuApi = parseRendererChoice(argv[++i]).gpuApi;
    }
    cfg.vsync = false;
    // textures as uploaded, decoded inside the draw: nothing that depends on worker timing or on files
    cfg.textureUpscale = 1;
    cfg.textureUpscaleSync = true;
    cfg.textureReplace = false;
    cfg.textureDump = false;
    // Whole frames at eight times the resolution need a hardware adapter (and a software one is not the product).
    if (!cfg.forceWarp)
        cfg.allowWarp = false;
    {
        const auto probe = Renderer::create(cfg);
        if (!probe) {
            std::printf("SKIP: no hardware adapter (--warp runs on the software one)\n");
            return 77;
        }
        const std::string adapter = probe->adapterName();
        const vpath::Capability cap = probe->vertexPathCapability();
        std::printf("adapter: %s\nvertex path of the backend: %s\n", adapter.c_str(),
                    !cap.backendHasPath ? "none (records are evaluated on the CPU)"
                    : !cap.adapterExact ? "present, adapter not exact"
                                        : "present");
        if (!cfg.forceWarp && cfg.gpuApi != Config::GpuApi::Auto &&
            adapter.find(std::string("(") + gpuApiName(cfg.gpuApi) + ")") == std::string::npos) {
            std::printf("SKIP: the renderer is not on %s\n", gpuApiName(cfg.gpuApi));
            return 77;
        }
    }
    for (const uint32_t scale : {1u, 3u, 8u})
        if (!onlyScale || onlyScale == scale)
            runScale(cfg, scale, toggleFrames);
    resetPath();

    const Totals& t = g_totals;
    std::printf("\nscene and mode combinations: %llu, in %llu sequences of frames (controls included)\n",
                static_cast<unsigned long long>(t.scenes), static_cast<unsigned long long>(t.sequences));
    std::printf("runs sent as records: %llu (%llu vertices); as packets to the second renderer: %llu (%llu vertices)\n",
                static_cast<unsigned long long>(t.runsAsRecords), static_cast<unsigned long long>(t.verticesAsRecords),
                static_cast<unsigned long long>(t.runsAsPackets), static_cast<unsigned long long>(t.verticesAsPackets));
    std::printf("of the records, batch records: %llu (%llu vertices sent)\n",
                static_cast<unsigned long long>(t.batchRecords), static_cast<unsigned long long>(t.batchVertices));
    std::printf("record vertices evaluated on the CPU: %llu, drawn by a shader: %llu, records counted as \"backend has "
                "no path\": %llu\n",
                static_cast<unsigned long long>(t.cpuVertices), static_cast<unsigned long long>(t.gpuVertices),
                static_cast<unsigned long long>(t.backendFallbacks));
    std::printf(
        "colour frames compared: %llu, differing: %llu; pixels compared: %llu, differing: %llu\n",
        static_cast<unsigned long long>(t.colour.frames), static_cast<unsigned long long>(t.colour.framesDiffering),
        static_cast<unsigned long long>(t.colour.pixels), static_cast<unsigned long long>(t.colour.pixelsDiffering));
    std::printf(
        "depth pictures compared: %llu, differing: %llu; pixels compared: %llu, differing: %llu\n",
        static_cast<unsigned long long>(t.depth.frames), static_cast<unsigned long long>(t.depth.framesDiffering),
        static_cast<unsigned long long>(t.depth.pixels), static_cast<unsigned long long>(t.depth.pixelsDiffering));
    std::printf("negative controls (meant to differ): colour frames %llu, differing %llu (%llu pixels); depth pictures "
                "%llu, differing %llu (%llu pixels)\n",
                static_cast<unsigned long long>(t.controlColour.frames),
                static_cast<unsigned long long>(t.controlColour.framesDiffering),
                static_cast<unsigned long long>(t.controlColour.pixelsDiffering),
                static_cast<unsigned long long>(t.controlDepth.frames),
                static_cast<unsigned long long>(t.controlDepth.framesDiffering),
                static_cast<unsigned long long>(t.controlDepth.pixelsDiffering));
    std::printf("%llu checks, %d failed\n", static_cast<unsigned long long>(g_checks), g_failures);
    return g_failures ? 1 : 0;
}
