// The GPU reconstruction of streamed draws (surface_gpu.h, GSR_VERTEX_SURFACE=gpu) must give, slot for slot and bit
// for bit, the frame triangles the CPU path appends (drawParams, reconstructDraw, the ray pass's packing), and count
// the resident observes it leaves out. Synthetic draws under perspective cameras against a resident grid: random and
// resident-aligned triangles, invalid vertices, flat and Gouraud colours, blend and alpha-test states, zero and tiny Q.
// Needs a Direct3D 11 hardware adapter with double-precision division; returns 77 (skipped) without one.
#include "surface_gpu_d3d11.h"

#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace gsr;
using namespace gsr::surface;
using Microsoft::WRL::ComPtr;

namespace {
uint32_t seed = 24681357u;
uint32_t random32() {
    seed = seed * 1664525u + 1013904223u;
    return seed;
}
float random01() {
    return float(random32() >> 8) / float(1u << 24);
}

SceneGeometry grid() {
    SceneGeometry g;
    g.identity = 7;
    for (int gx = -6; gx < 6; ++gx)
        for (int gz = -6; gz < 6; ++gz)
            for (unsigned half = 0; half < 2; ++half)
                for (unsigned layer = 0; layer < 2; ++layer) {
                    SceneTriangle t;
                    const float x = gx * 900.f, z = gz * 900.f, y = layer ? 600.f : random01() * 150.f;
                    const float corners[2][3][2] = {{{0, 0}, {900, 0}, {0, 900}}, {{900, 0}, {900, 900}, {0, 900}}};
                    for (unsigned v = 0; v < 3; ++v) {
                        t.position[v][0] = x + corners[half][v][0];
                        t.position[v][1] = y + (layer ? 0.f : random01() * 30.f);
                        t.position[v][2] = z + corners[half][v][1];
                        for (unsigned c = 0; c < 3; ++c)
                            t.color[v][c] = .5f;
                    }
                    t.layer = layer * 1000 + uint32_t(gx + 6) * 16 + uint32_t(gz + 6);
                    t.water = layer;
                    g.triangles.push_back(t);
                }
    return g;
}

// A perspective camera (clip.w = view depth) looking down at the grid, row vectors: clip = world * M.
SceneGuideCamera camera(float yaw, float height) {
    SceneGuideCamera c;
    c.serial = 1;
    c.flags = SceneGuideCamera::Valid | SceneGuideCamera::World;
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    // view = world rotated about y, tilted down, then translated; clip = (x * 1.2, y * 1.6, z * a + b, z).
    const float pitch = .6f, cp = std::cos(pitch), sp = std::sin(pitch);
    float r[16] = {cs, -sn * sp, sn * cp, 0, 0, cp, sp, 0, -sn, -cs * sp, cs * cp, 0, 0, -height, 4000, 1};
    const float p[16] = {1.2f, 0, 0, 0, 0, 1.6f, 0, 0, 0, 0, -1.0001f, 1, 0, 0, -32, 0};
    float m[16]{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                m[i * 4 + j] += r[i * 4 + k] * p[k * 4 + j];
    std::memcpy(c.worldToClipVU, m, sizeof m);
    c.viewportScale[0] = 320;
    c.viewportScale[1] = -224;
    c.viewportScale[2] = -65536.f * 16;
    c.viewportOffset[0] = 2048;
    c.viewportOffset[1] = 2048;
    c.viewportOffset[2] = 65536.f * 32;
    return c;
}

// The raw vertex of a world point under `c` (the inverse of RasterToWorld), with some jitter in the low bits.
RawVertex project(const SceneGuideCamera& c, uint64_t xyoffset, float x, float y, float z) {
    float clip[4]{};
    const float w[4] = {x, y, z, 1};
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            clip[j] += w[i] * c.worldToClipVU[i * 4 + j];
    RawVertex r{};
    const double ofx = double(xyoffset & 0xFFFF) / 16, ofy = double((xyoffset >> 32) & 0xFFFF) / 16;
    const double rx = clip[0] / clip[3] * c.viewportScale[0] + c.viewportOffset[0] - ofx;
    const double ry = clip[1] / clip[3] * c.viewportScale[1] + c.viewportOffset[1] - ofy;
    const double rz = (clip[2] / clip[3] * c.viewportScale[2] + c.viewportOffset[2]) * 16;
    r.place[0] = uint32_t(std::lround(rx * 16) + int(xyoffset & 0xFFFF)) & 0xFFFF;
    r.place[1] = uint32_t(std::lround(ry * 16) + int((xyoffset >> 32) & 0xFFFF)) & 0xFFFF;
    r.place[2] = rz > 0 && rz < 4294967040.0 ? uint32_t(rz) : 0;
    r.place[3] = (random32() & 0xFF) << 4;
    for (auto& colour : r.colour)
        colour = random32() & 0xFF;
    if (random32() % 3)
        r.colour[3] = 128 + random32() % 128;
    const float q = random32() % 17 == 0 ? 0.f : random32() % 19 == 0 ? 1e-13f : .5f + random01();
    const float s = random01() * q, t = random01() * q;
    std::memcpy(&r.stq[0], &s, 4);
    std::memcpy(&r.stq[1], &t, 4);
    std::memcpy(&r.stq[2], &q, 4);
    return r;
}
} // namespace

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_1;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &level, 1, D3D11_SDK_VERSION, &dev,
                                 nullptr, &ctx))) {
        std::puts("surface_gpu: no Direct3D 11.1 hardware device, skipped");
        return 77;
    }
    if (!SurfaceGpuKernel::supported(dev.Get())) {
        std::puts("surface_gpu: no double-precision division on this adapter, skipped");
        return 77;
    }
    SurfaceGpuKernel kernel;
    if (!kernel.init(dev.Get())) {
        std::printf("surface_gpu: FAIL kernel: %s\n", kernel.why().c_str());
        return 1;
    }
    ResidentScene scene;
    scene.replace(grid());
    GpuMatchTable match;
    buildMatchTable(scene, match);
    match.serial = 1;

    // Draws: several cameras and states.
    std::vector<RawVertex> stream;
    std::vector<GpuDraw> draws;
    std::vector<GpuCamera> cameras;
    std::vector<RasterToWorld> transforms;
    std::vector<DrawInput> inputs;
    const uint64_t xyoffsets[2] = {(uint64_t(0x7000) << 32) | 0x7800, (uint64_t(0x6c00) << 32) | 0x7400};
    for (unsigned k = 0; k < 4; ++k) {
        SceneGuideCamera c = camera(.3f + k * 1.1f, 1500.f + 700.f * k);
        c.serial = k + 1;
        RasterToWorld t;
        t.configure(c, xyoffsets[k & 1]);
        if (!t.valid) {
            std::puts("surface_gpu: FAIL camera inversion");
            return 1;
        }
        transforms.push_back(t);
        cameras.push_back(gpuCameraOf(t));
    }
    {
        RasterToWorld invalid; // the builder before any camera: no triangle converts
        transforms.push_back(invalid);
        cameras.push_back(gpuCameraOf(invalid));
    }
    uint32_t slot = 0;
    for (unsigned d = 0; d < 400; ++d) {
        const uint32_t cam = d % 5;
        const uint64_t xy = xyoffsets[cam & 1];
        const SceneGuideCamera& c = transforms[cam].camera;
        DrawInput in;
        in.s = GsDrawState{};
        in.s.xyoffset = xy;
        const uint32_t state = random32();
        in.s.flags = (state & 1 ? DF_IIP : 0) | (state & 2 ? DF_ABE : 0) | DF_TME;
        in.s.alpha = state & 4 ? 0x44 : 0x48;
        in.s.zbuf = uint64_t(state & 8 ? 1 : 0) << 32;
        const uint64_t atst = (state >> 4) % 8, aref = (state >> 8) & 0xFF;
        in.s.test = (state & 0x10000 ? 1 : 0) | (atst << 1) | (aref << 4);
        in.s.tex0 = (uint64_t((state >> 17) & 1) << 34) | (uint64_t((state >> 18) & 3) << 35) | (uint64_t(7) << 26) |
                    (uint64_t(7) << 30);
        in.haveTex = (state >> 20) % 5 != 0;
        in.texIsRT = (state >> 23) % 11 == 0;
        in.texMinimumAlpha = uint8_t(random32() & 0xFF);
        in.reflectionTexture = (state >> 26) % 7 == 0 ? ~0u : (state >> 26) % 40;
        in.residentWater = false;
        GpuDraw g;
        bool water = false;
        if (!gpuDrawOf(in, g, water) || water) {
            std::puts("surface_gpu: FAIL a dry draw was refused");
            return 1;
        }
        g.first = uint32_t(stream.size());
        g.camera = cam;
        g.slot = slot;
        const uint32_t triangles = 1 + random32() % 300;
        for (uint32_t t = 0; t < triangles; ++t) {
            const uint32_t kind = random32() % 8;
            float p[3][3];
            if (kind < 3) { // a resident triangle, slightly moved: often matches
                const auto& r = scene.triangles[random32() % scene.triangles.size()];
                for (unsigned v = 0; v < 3; ++v) {
                    p[v][0] = r.p[v].x + (random01() - .5f) * 4;
                    p[v][1] = r.p[v].y + (random01() - .5f) * 20;
                    p[v][2] = r.p[v].z + (random01() - .5f) * 4;
                }
            } else { // a random object triangle
                const float cx = (random01() - .5f) * 9000, cy = random01() * 1200, cz = (random01() - .5f) * 9000;
                for (unsigned v = 0; v < 3; ++v) {
                    p[v][0] = cx + (random01() - .5f) * 600;
                    p[v][1] = cy + (random01() - .5f) * 600;
                    p[v][2] = cz + (random01() - .5f) * 600;
                }
            }
            for (unsigned v = 0; v < 3; ++v) {
                RawVertex r = project(c, xy, p[v][0], p[v][1], p[v][2]);
                if (kind == 7 && v == 1)
                    r.place[2] = random32() % 2 ? 0 : 0xFFFFFF00u; // z = 0, or the far end of the range
                stream.push_back(r);
            }
        }
        g.count = triangles * 3;
        slot += triangles;
        draws.push_back(g);
        inputs.push_back(in);
    }
    const GpuSurfaceLayout layout{slot};
    const Point origin{123.25f, -77.5f, 4100.125f};
    const uint32_t textures = 32;
    const float packedMaterial = packRayMaterial(.75f, .04f);

    // GPU.
    ComPtr<ID3D11Buffer> streamBuffer;
    ComPtr<ID3D11Texture2D> outTexture;
    ComPtr<ID3D11ShaderResourceView> streamSrv;
    ComPtr<ID3D11UnorderedAccessView> outUav;
    {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = UINT(stream.size() * sizeof(RawVertex));
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_STREAM_OUTPUT;
        D3D11_SUBRESOURCE_DATA init{stream.data(), 0, 0};
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = DXGI_FORMAT_R32_UINT;
        v.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        v.Buffer.NumElements = UINT(stream.size() * 12);
        if (FAILED(dev->CreateBuffer(&d, &init, &streamBuffer)) ||
            FAILED(dev->CreateShaderResourceView(streamBuffer.Get(), &v, &streamSrv)) ||
            !createOutputTexture(dev.Get(), layout, 0, outTexture, outUav)) {
            std::puts("surface_gpu: FAIL buffers");
            return 1;
        }
    }
    // GPU time of the kernel (both passes), for the record: a world of the game has about as many slots.
    ComPtr<ID3D11Query> disjoint, begin, end;
    D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT}, stampDesc{D3D11_QUERY_TIMESTAMP};
    const bool timed = SUCCEEDED(dev->CreateQuery(&disjointDesc, &disjoint)) &&
                       SUCCEEDED(dev->CreateQuery(&stampDesc, &begin)) && SUCCEEDED(dev->CreateQuery(&stampDesc, &end));
    double best = 1e9;
    for (unsigned round = 0; round < (timed ? 5u : 1u); ++round) {
        if (timed) {
            ctx->Begin(disjoint.Get());
            ctx->End(begin.Get());
        }
        if (!kernel.dispatch(dev.Get(), ctx.Get(), streamSrv.Get(), draws, cameras, match, outUav.Get(), layout, origin,
                             textures, packedMaterial)) {
            std::puts("surface_gpu: FAIL dispatch");
            return 1;
        }
        if (timed) {
            ctx->End(end.Get());
            ctx->End(disjoint.Get());
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT f{};
            UINT64 t0 = 0, t1 = 0;
            while (ctx->GetData(disjoint.Get(), &f, sizeof(f), 0) == S_FALSE) {
            }
            while (ctx->GetData(begin.Get(), &t0, sizeof(t0), 0) == S_FALSE) {
            }
            while (ctx->GetData(end.Get(), &t1, sizeof(t1), 0) == S_FALSE) {
            }
            if (!f.Disjoint)
                best = std::min(best, double(t1 - t0) * 1000.0 / double(f.Frequency));
        }
    }
    if (timed)
        std::printf("surface_gpu: kernel GPU time %.3f ms for %u slots (best of 5)\n", best, slot);
    std::vector<uint32_t> words;
    uint32_t lost = 0, emittedGpu = 0, overflow = 0;
    std::vector<GpuObserve> records;
    if (!readWordsNow(dev.Get(), ctx.Get(), outTexture.Get(), layout.bytes() / 4, words) ||
        !kernel.countersNow(ctx.Get(), lost, emittedGpu, &records, &overflow)) {
        std::puts("surface_gpu: FAIL read-back");
        return 1;
    }

    // CPU reference: the vertices as the core queues them, drawParams, reconstructDraw without observe.
    std::vector<std::vector<Triangle>> lists(draws.size());
    std::vector<const Triangle*> expected(slot, nullptr);
    uint64_t observes = 0, appendedTotal = 0;
    std::vector<ReconstructProbe::Observed> observedCpu;
    std::vector<uint32_t> observedSlots;
    for (size_t d = 0; d < draws.size(); ++d) {
        const GpuDraw& g = draws[d];
        std::vector<DrawVertex> v(g.count);
        for (uint32_t i = 0; i < g.count; ++i)
            v[i] = drawVertexOf(stream[g.first + i], inputs[d].s.xyoffset);
        if (!(inputs[d].s.flags & DF_IIP))
            flatShade(v.data(), g.count);
        DrawSurfaceParams params;
        if (!drawParams(inputs[d], v.data(), g.count, params))
            continue;
        CachedRasterToWorld cam;
        cam.transform = transforms[g.camera];
        std::vector<uint32_t> appended;
        ReconstructProbe probe{&appended};
        const size_t before = observedCpu.size();
        probe.observed = &observedCpu;
        lists[d].reserve(g.count / 3);
        reconstructDraw(params, v.data(), g.count, cam, scene, lists[d], &probe);
        observes += probe.observes;
        for (size_t k = before; k < observedCpu.size(); ++k)
            observedSlots.push_back(g.slot + observedCpu[k].triangle);
        for (size_t k = 0; k < appended.size(); ++k)
            expected[g.slot + appended[k]] = &lists[d][k];
        appendedTotal += appended.size();
    }
    uint64_t active = 0;
    std::string first;
    const uint64_t differ = compareSlots(words, layout, expected, origin, textures, &active, &first);
    std::printf("surface_gpu: %u slots, %llu appended by the CPU path (%llu checked), %llu differ; observes left out: "
                "CPU %llu, GPU %u; GPU emitted %u\n",
                slot, (unsigned long long)appendedTotal, (unsigned long long)active, (unsigned long long)differ,
                (unsigned long long)observes, lost, emittedGpu);
    // The observe records: the CPU path's calls in its order (slot order), the same resident triangle and inputs.
    bool recordsSame = overflow == 0 && records.size() == observedCpu.size();
    for (size_t k = 0; recordsSame && k < records.size(); ++k) {
        const Triangle t = observedTriangle(records[k]);
        const Triangle& c = observedCpu[k].tri;
        recordsSame = records[k].slot == observedSlots[k] && int(records[k].index) == observedCpu[k].index &&
                      t.texture == c.texture && std::memcmp(t.p, c.p, sizeof(t.p)) == 0 &&
                      std::memcmp(t.uv, c.uv, sizeof(t.uv)) == 0;
    }
    std::printf("surface_gpu: observe records %zu, the CPU path's calls %zu, same %d\n", records.size(),
                observedCpu.size(), int(recordsSame));
    if (differ || active != appendedTotal || observes != lost || emittedGpu != appendedTotal || !recordsSame) {
        std::printf("surface_gpu: FAIL %s\n", first.c_str());
        return 1;
    }
    if (appendedTotal < slot / 4 || observes == 0 || appendedTotal == slot) {
        std::puts("surface_gpu: FAIL the synthetic draws do not cover appended, matched and refused triangles");
        return 1;
    }
    std::puts("surface_gpu: PASS");
    return 0;
}
