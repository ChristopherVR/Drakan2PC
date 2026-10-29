// gsr::rhi on WebGPU: immediate-context semantics the GS renderer relies on. Exit 77 = no adapter.
#include "rhi.h"

#include <gpu/Gpu.h>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gsr::rhi;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

struct Vertex {
    float x, y, z, u, v;
};

// Pixel-space quads: uniforms carry the target size and a colour; optional texture modulation.
const char* kProgram = R"(
struct U { size: vec4f, color: vec4f, mode: vec4u }
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(8) var tex: texture_2d<f32>;
struct V { @builtin(position) pos: vec4f, @location(0) uv: vec2f }
@vertex fn vs(@location(0) p: vec3f, @location(1) uv: vec2f) -> V {
    var o: V;
    o.pos = vec4f(p.x / u.size.x * 2.0 - 1.0, 1.0 - p.y / u.size.y * 2.0, p.z, 1.0);
    o.uv = uv;
    return o;
}
@fragment fn fs(i: V) -> @location(0) vec4f {
    if (u.mode.x == 1u) { return textureLoad(tex, vec2i(i.uv), 0); }
    return u.color;
}
)";

const char* kDualSource = R"(
enable dual_source_blending;
struct U { size: vec4f, color: vec4f, mode: vec4u }
@group(0) @binding(0) var<uniform> u: U;
struct V { @builtin(position) pos: vec4f }
@vertex fn vs(@location(0) p: vec3f, @location(1) uv: vec2f) -> V {
    var o: V;
    o.pos = vec4f(p.x / u.size.x * 2.0 - 1.0, 1.0 - p.y / u.size.y * 2.0, p.z, 1.0);
    return o;
}
struct F { @location(0) @blend_src(0) c: vec4f, @location(0) @blend_src(1) f: vec4f }
@fragment fn fs(i: V) -> F { return F(u.color, vec4f(0.5)); }
)";

struct Uniforms {
    float size[4];
    float color[4];
    uint32_t mode[4];
};

std::vector<Vertex> quad(float x0, float y0, float x1, float y1, float z = 0.5f) {
    return {{x0, y0, z, x0, y0}, {x1, y0, z, x1, y0}, {x0, y1, z, x0, y1},
            {x1, y0, z, x1, y0}, {x1, y1, z, x1, y1}, {x0, y1, z, x0, y1}};
}

ProgramDesc programDesc(const char* wgsl, bool texture) {
    ProgramDesc d;
    d.wgsl = wgsl;
    d.uniformSlots = 1;
    d.textureSlots = texture ? 1 : 0;
    d.vertexStride = sizeof(Vertex);
    d.attributes = {{0, 0, 3}, {1, 12, 2}};
    d.label = "test";
    return d;
}

const uint8_t* px(const std::vector<uint8_t>& image, uint32_t width, uint32_t x, uint32_t y) {
    return &image[(size_t(y) * width + x) * 4];
}
bool rgba(const uint8_t* p, int r, int g, int b, int a, int tolerance = 1) {
    const int want[4] = {r, g, b, a};
    for (int i = 0; i < 4; ++i)
        if (std::abs(int(p[i]) - want[i]) > tolerance)
            return false;
    return true;
}
} // namespace

int main() {
    drakan::gpu::ContextOptions options;
    options.allowSoftwareAdapter = true;
    std::string error;
    auto context = drakan::gpu::Context::create(options, &error);
    if (!context) {
        std::fprintf(stderr, "SKIP: %s\n", error.c_str());
        return 77;
    }
    auto device = Device::create(*context);
    constexpr uint32_t W = 64, H = 32;
    auto target = device->createTexture({W, H, 1, Format::RGBA8, Sampled | RenderTarget | CopySrc | CopyDst, "rt"});
    auto depth = device->createTexture({W, H, 1, Format::Depth32F, RenderTarget | Sampled, "depth"});
    auto source = device->createTexture({8, 8, 1, Format::RGBA8, Sampled | CopyDst | CopySrc, "src"});
    auto program = device->createProgram(programDesc(kProgram, true), &error);
    check(program != nullptr, "program");
    if (!program)
        return 1;

    const float black[4] = {0, 0, 0, 1};
    device->clearColor(*target, black);
    device->clearDepth(*depth, 1.0f);
    device->setTargets(target.get(), depth.get());
    device->setProgram(program.get());
    device->setScissor({0, 0, W, H});
    Uniforms u{{float(W), float(H), 0, 0}, {1, 0, 0, 1}, {0, 0, 0, 0}};
    device->setTexture(0, source.get());

    // 1. Upload ordering: draw texture contents A, rewrite the texture, draw B elsewhere; the first draw keeps A.
    std::vector<uint32_t> texels(64, 0xff00ff00u); // green (ABGR in memory = RGBA bytes 00 ff 00 ff)
    device->writeTexture(*source, 0, 0, 0, 8, 8, texels.data());
    u.mode[0] = 1;
    device->setUniforms(0, &u, sizeof(u));
    auto q = quad(0, 0, 8, 8);
    device->draw(q.data(), uint32_t(q.size()));
    std::fill(texels.begin(), texels.end(), 0xffff0000u); // blue
    device->writeTexture(*source, 0, 0, 0, 8, 8, texels.data());
    q = quad(0, 8, 8, 16); // texel coordinates 0..8 again
    for (auto& vtx : q)
        vtx.v -= 8;
    device->draw(q.data(), uint32_t(q.size()));

    // 2. Constant colour with depth: near red quad, then a farther green quad over it (rejected) and a nearer blue.
    u.mode[0] = 0;
    device->setDepth({true, Compare::Less, true});
    u.color[0] = 1, u.color[1] = 0, u.color[2] = 0;
    device->setUniforms(0, &u, sizeof(u));
    q = quad(16, 0, 32, 16, 0.5f);
    device->draw(q.data(), uint32_t(q.size()));
    u.color[0] = 0, u.color[1] = 1;
    device->setUniforms(0, &u, sizeof(u));
    q = quad(16, 0, 24, 16, 0.7f);
    device->draw(q.data(), uint32_t(q.size()));
    u.color[1] = 0, u.color[2] = 1;
    device->setUniforms(0, &u, sizeof(u));
    q = quad(24, 0, 32, 16, 0.2f);
    device->draw(q.data(), uint32_t(q.size()));
    device->setDepth({});

    // 3. Additive blending and the write mask: grey + grey, alpha untouched.
    BlendState add;
    add.enable = true;
    add.src = add.dst = BlendFactor::One;
    add.writeMask = 0x7;
    device->setBlend(add);
    u.color[0] = u.color[1] = u.color[2] = 0.25f;
    u.color[3] = 0;
    device->setUniforms(0, &u, sizeof(u));
    q = quad(32, 0, 40, 8);
    device->draw(q.data(), uint32_t(q.size()));
    device->draw(q.data(), uint32_t(q.size()));
    device->setBlend({});

    // 4. Scissor: a full-target quad limited to x 40..47, y 0..7.
    u.color[0] = 1, u.color[1] = 1, u.color[2] = 0, u.color[3] = 1;
    device->setUniforms(0, &u, sizeof(u));
    device->setScissor({40, 0, 8, 8});
    q = quad(0, 0, W, H);
    device->draw(q.data(), uint32_t(q.size()));
    device->setScissor({0, 0, W, H});

    // 5. Copy a target region (the red/blue quads) to another place in the same texture via a temporary.
    auto temp = device->createTexture({16, 16, 1, Format::RGBA8, CopySrc | CopyDst | Sampled, "temp"});
    device->copyTexture(*target, 0, 16, 0, *temp, 0, 0, 0, 16, 16);
    device->copyTexture(*temp, 0, 0, 0, *target, 0, 48, 16, 16, 16);

    std::vector<uint8_t> image;
    check(device->readTexture(*target, 0, 0, 0, W, H, image), "readback");
    check(rgba(px(image, W, 3, 3), 0, 255, 0, 255), "upload ordering: first draw saw the first contents");
    check(rgba(px(image, W, 3, 12), 0, 0, 255, 255), "upload ordering: second draw saw the new contents");
    check(rgba(px(image, W, 20, 8), 255, 0, 0, 255), "depth: farther quad rejected");
    check(rgba(px(image, W, 28, 8), 0, 0, 255, 255), "depth: nearer quad drawn");
    check(rgba(px(image, W, 35, 4), 128, 128, 128, 255, 2), "additive blend, alpha masked");
    check(rgba(px(image, W, 44, 4), 255, 255, 0, 255), "scissor inside");
    check(rgba(px(image, W, 44, 12), 0, 0, 0, 255), "scissor outside");
    check(rgba(px(image, W, 52, 24), 255, 0, 0, 255) && rgba(px(image, W, 60, 24), 0, 0, 255, 255), "copy");

    // 6. Depth readback reflects the written depth.
    std::vector<uint8_t> depthBytes;
    check(device->readTexture(*depth, 0, 20, 8, 1, 1, depthBytes) && depthBytes.size() == 4, "depth readback");
    if (depthBytes.size() == 4) {
        float z = 0;
        std::memcpy(&z, depthBytes.data(), 4);
        check(z > 0.49f && z < 0.51f, "depth value");
    }

    // 7. An upload larger than the staging ring takes the dedicated path and still lands in order.
    {
        const uint32_t bw = 4352, bh = 4096; // 71 MB > 64 MB ring
        if (bw <= device->maxTextureSize() && bh <= device->maxTextureSize()) {
            auto big = device->createTexture({bw, bh, 1, Format::RGBA8, CopyDst | CopySrc, "big"});
            std::vector<uint32_t> data(size_t(bw) * bh, 0x11223344u);
            data.back() = 0x55667788u;
            device->writeTexture(*big, 0, 0, 0, bw, bh, data.data());
            std::vector<uint8_t> corner;
            check(device->readTexture(*big, 0, bw - 1, bh - 1, 1, 1, corner) && corner.size() == 4 &&
                      corner[0] == 0x88 && corner[3] == 0x55,
                  "large upload");
        }
    }

    // 8. Dual-source blending (the GS blend unit): dest = src * 0.5 + dest * (1 - 0.5).
    if (device->dualSourceBlending()) {
        auto dual = device->createProgram(programDesc(kDualSource, false), &error);
        check(dual != nullptr, "dual-source program");
        if (dual) {
            const float white[4] = {1, 1, 1, 1};
            device->clearColor(*target, white);
            device->setTargets(target.get(), nullptr);
            device->setProgram(dual.get());
            BlendState b;
            b.enable = true;
            b.src = BlendFactor::Src1Color;
            b.dst = BlendFactor::OneMinusSrc1Color;
            device->setBlend(b);
            Uniforms d{{float(W), float(H), 0, 0}, {0, 0, 0, 1}, {0, 0, 0, 0}};
            device->setUniforms(0, &d, sizeof(d));
            q = quad(0, 0, 8, 8);
            device->draw(q.data(), uint32_t(q.size()));
            check(device->readTexture(*target, 0, 4, 4, 1, 1, image) && rgba(image.data(), 128, 128, 128, 255, 2),
                  "dual-source blend");
        }
    } else {
        std::printf("dual-source blending unavailable on this adapter (shader blending path)\n");
    }

    check(!context->failed(), "no device errors");
    if (context->failed())
        std::fprintf(stderr, "last error: %s\n", context->lastError().c_str());
    if (g_failures)
        return 1;
    std::printf("rhi: all checks passed on %s\n", context->adapterName().c_str());
    return 0;
}
