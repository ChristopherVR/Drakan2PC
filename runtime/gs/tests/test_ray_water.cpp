#include "ray_water.h"
#include "ray_world_cache.h"
#include <d3d11.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cassert>
#include <array>
#include <iostream>
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <fstream>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
int main(int argc, char** argv) {
    // Compare warm blocks with cold packing across boundary and invalidation changes.
    {
        using namespace gsr::surface;
        RayWorldCache warm;
        std::vector<Triangle> input(513);
        for (size_t i = 0; i < input.size(); ++i) {
            auto& t = input[i];
            t.p[0] = {float(i), 0, 0};
            t.p[1] = {float(i), 2, 0};
            t.p[2] = {float(i), 0, 3};
            t.water = i % 3 != 0;
            t.shadowGeometry = i % 3 != 1;
            t.texture = uint32_t(i % 4);
        }
        for (unsigned step = 0; step < 12; ++step) {
            if (step == 2)
                input[255].opacity[1] = .25f;
            if (step == 3)
                input[256].uv[2][1] = .75f;
            if (step == 4)
                std::swap(input[0], input[512]);
            if (step == 5)
                input.resize(255);
            if (step == 6)
                input.resize(514, input[0]);
            if (step == 7) {
                input[300].vertexLit = true;
                input[300].lighting[1][2] = .8f;
            }
            if (step == 8) {
                input[301].alphaTest = 4;
                input[301].alphaReference = 64;
            }
            if (step == 9)
                input.clear();
            if (step == 10)
                input.resize(257);
            RayWorldCache cold;
            std::vector<Point> a, b;
            std::vector<std::array<float, 4>> x, y;
            uint32_t ac[3]{}, bc[3]{};
            Point origin{float(step), float(step * 2), -float(step)};
            const size_t textures = step < 8 ? 4 : 2;
            assert(warm.append(input, origin, textures, a, x, ac) == cold.append(input, origin, textures, b, y, bc));
            assert(std::memcmp(ac, bc, sizeof(ac)) == 0 && a.size() == b.size() && x == y);
            if (step == 1)
                assert(warm.rebuiltBlocks() == 0);
            if (step == 2 || step == 3)
                assert(warm.rebuiltBlocks() == 1);
            if (!a.empty())
                assert(std::memcmp(a.data(), b.data(), a.size() * sizeof(Point)) == 0);
        }
        std::cout << "World cache block boundaries, camera motion, mutations, shrink/grow and empty scenes passed\n";
    }
    // Welded vertex normals: a 30 degree ridge smooths at its shared corners, a cube corner (90 degree faces) stays
    // sharp, and the packed normal is an exact integer float.
    {
        using namespace gsr::surface;
        const float s = std::sin(15 * 3.14159265f / 180), k = std::cos(15 * 3.14159265f / 180);
        const Point ridge[2][3] = {{{-k, 0, -s}, {0, 0, 0}, {0, 1, 0}}, {{0, 0, 0}, {k, 0, -s}, {0, 1, 0}}};
        std::vector<Point> normals;
        smoothNormals(2, [&](size_t i, Point(&p)[3]) { std::copy(ridge[i], ridge[i] + 3, p); }, normals);
        assert(std::abs(normals[1].x) < 1e-6f && std::abs(normals[1].z - 1) < 1e-6f);     // shared corner
        assert(std::abs(normals[0].x + s) < 1e-6f && std::abs(normals[0].z - k) < 1e-6f); // unshared corner
        const Point cube[3][3] = {
            {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{0, 0, 0}, {0, 0, 1}, {1, 0, 0}}};
        smoothNormals(3, [&](size_t i, Point(&p)[3]) { std::copy(cube[i], cube[i] + 3, p); }, normals);
        for (unsigned i = 0; i < 3; ++i) {
            const auto face = faceNormal(cube[i]);
            assert(std::abs(normals[i * 3].x - face.x) + std::abs(normals[i * 3].y - face.y) +
                       std::abs(normals[i * 3].z - face.z) <
                   1e-6f);
        }
        for (const Point n : {Point{0, 0, 1}, Point{0, 0, -1}, Point{.6f, -.8f, 0}, Point{-.3f, .2f, -.93f}}) {
            const float packed = packNormal(n);
            assert(packed >= 0 && packed < 16777216 && packed == std::floor(packed));
        }
        std::cout << "Welded normals smooth shallow ridges, keep hard corners and pack exactly\n";
    }
    const bool benchmark = argc > 1 && std::strcmp(argv[1], "--benchmark") == 0;
    ComPtr<ID3D12Debug> debug;
    if (!benchmark && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        std::cout << "D3D12 debug layer enabled\n";
    } else
        std::cout << "D3D12 debug layer unavailable; hardware assertions still active\n";
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device,
                                   nullptr, &context);
    assert(SUCCEEDED(hr));
    gsr::surface::RayWater water(device.Get(), context.Get());
    std::cout << water.status() << '\n';
    if (!water.ready())
        return 77;
    const unsigned w = benchmark ? 1024 : 64, h = w;
    auto texture = [&](DXGI_FORMAT format) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = format;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        ComPtr<ID3D11Texture2D> t;
        assert(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &t)));
        return t;
    };
    auto color = texture(DXGI_FORMAT_R16G16B16A16_FLOAT), output = texture(DXGI_FORMAT_R16G16B16A16_FLOAT),
         depth = texture(DXGI_FORMAT_R32_FLOAT);
    auto clear = [&](ID3D11Texture2D* t, const std::array<float, 4>& c) {
        ComPtr<ID3D11RenderTargetView> r;
        assert(SUCCEEDED(device->CreateRenderTargetView(t, nullptr, &r)));
        context->ClearRenderTargetView(r.Get(), c.data());
    };
    clear(color.Get(), {.2f, .2f, .2f, .5f});
    clear(output.Get(), {.9f, .1f, .2f, .5f});
    clear(depth.Get(), {.001f, 0, 0, 0});
    gsr::nr::guides::FrameConstants c;
    c.currentViewToClip[0] = c.currentViewToClip[5] = 1;
    c.currentViewToClip[11] = -1;
    c.currentViewToClip[14] = 32;
    c.clipToCameraView[0] = c.clipToCameraView[5] = 1;
    c.clipToCameraView[11] = 1.f / 32;
    c.clipToCameraView[14] = -1;
    c.cameraRight[0] = 1;
    c.cameraUp[1] = 1;
    c.cameraForward[2] = -1;
    c.nearPlane = 32;
    std::vector<gsr::surface::Triangle> triangles;
    using P = gsr::surface::Point;
    auto quad = [&](P a, P b, P d, P e, std::array<float, 3> color, bool water) {
        gsr::surface::Triangle t{{a, b, d}, {color[0], color[1], color[2]}, water};
        triangles.push_back(t);
        t.p[0] = a;
        t.p[1] = d;
        t.p[2] = e;
        triangles.push_back(t);
    };
    quad({-1000, -100, -1}, {1000, -100, -1}, {1000, -100, -1000}, {-1000, -100, -1000}, {.5f, .5f, .5f}, false);
    quad({-400, -100, -200}, {400, -100, -200}, {400, 500, -200}, {-400, 500, -200}, {2, .2f, .2f}, false);
    quad({-300, -20, -30}, {300, -20, -30}, {300, -20, -300}, {-300, -20, -300}, {.2f, .3f, .4f}, true);
    std::ofstream snapshots;
    if (const char* path = std::getenv("GSR_RAY_TEST_SNAPSHOTS")) {
        snapshots.open(path, std::ios::binary | std::ios::trunc);
        assert(snapshots.is_open());
    }
    auto read = [&]() {
        D3D11_TEXTURE2D_DESC d{};
        output->GetDesc(&d);
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> t;
        assert(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &t)));
        context->CopyResource(t.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        assert(SUCCEEDED(context->Map(t.Get(), 0, D3D11_MAP_READ, 0, &m)));
        std::vector<uint16_t> data(w * h * 4);
        for (unsigned y = 0; y < h; ++y)
            std::memcpy(data.data() + y * w * 4, static_cast<const char*>(m.pData) + y * m.RowPitch, w * 8);
        context->Unmap(t.Get(), 0);
        if (snapshots.is_open()) {
            snapshots.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(data[0]));
            assert(snapshots.good());
        }
        return data;
    };
    if (benchmark) {
        // Fixed geometry, camera, time and input textures isolate renderer cost.
        // No validation readback or debug layer is inside the timed interval.
        std::vector<float> depths(w * h, .001f);
        for (unsigned y = 0; y < h / 2; ++y)
            for (unsigned x = 0; x < w; ++x)
                depths[y * w + x] = 0;
        context->UpdateSubresource(depth.Get(), 0, nullptr, depths.data(), w * 4, 0);
        gsr::SceneModels lighting;
        lighting.level = 1;
        lighting.lighting.ambient[0] = lighting.lighting.ambient[1] = lighting.lighting.ambient[2] = .25f;
        gsr::SceneLight sun;
        sun.vector[1] = .8f;
        sun.vector[2] = .6f;
        sun.color[0] = sun.color[1] = sun.color[2] = 1;
        lighting.lighting.lights.push_back(sun);
        const gsr::surface::RayOptions effects{true, true};
        std::vector<double> samples, gpu;
        // GPU time: the D3D11 timeline waits for the D3D12 ray pass, so timestamps around
        // one frame bracket its input copies, the pass itself and the output copy.
        D3D11_QUERY_DESC disjointDesc{D3D11_QUERY_TIMESTAMP_DISJOINT}, stampDesc{D3D11_QUERY_TIMESTAMP};
        ComPtr<ID3D11Query> disjoint, begin, finish;
        assert(SUCCEEDED(device->CreateQuery(&disjointDesc, &disjoint)) &&
               SUCCEEDED(device->CreateQuery(&stampDesc, &begin)) &&
               SUCCEEDED(device->CreateQuery(&stampDesc, &finish)));
        for (unsigned frame = 0; frame < 120; ++frame) {
            context->Begin(disjoint.Get());
            context->End(begin.Get());
            auto start = std::chrono::steady_clock::now();
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &lighting, effects));
            auto end = std::chrono::steady_clock::now();
            context->End(finish.Get());
            context->End(disjoint.Get());
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT frequency{};
            UINT64 t0 = 0, t1 = 0;
            while (context->GetData(disjoint.Get(), &frequency, sizeof(frequency), 0) == S_FALSE) {
            }
            while (context->GetData(begin.Get(), &t0, sizeof(t0), 0) == S_FALSE) {
            }
            while (context->GetData(finish.Get(), &t1, sizeof(t1), 0) == S_FALSE) {
            }
            if (frame >= 20) {
                samples.push_back(std::chrono::duration<double, std::milli>(end - start).count());
                if (!frequency.Disjoint)
                    gpu.push_back(double(t1 - t0) * 1000. / double(frequency.Frequency));
            }
        }
        std::sort(samples.begin(), samples.end());
        std::sort(gpu.begin(), gpu.end());
        if (!gpu.empty())
            std::cout << "BENCHMARK gpu_median_ms=" << gpu[gpu.size() / 2] << " gpu_samples=" << gpu.size() << "\n";
        auto result = read();
        for (unsigned i = 0; i < w * h; ++i)
            for (unsigned j = 0; j < 3; ++j)
                assert((result[i * 4 + j] & 0x7c00) != 0x7c00);
        std::cout << "BENCHMARK median_ms=" << (samples[49] + samples[50]) * .5 << " p95_ms=" << samples[94]
                  << " samples=" << samples.size() << "\n";
        return 0;
    }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false));
    auto off = read();
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    auto on = read();
    // Queue consecutive writes without readback synchronization. Each next
    // render must fence uploads/allocator reuse, and teardown must drain work.
    for (unsigned frame = 0; frame < 32; ++frame) {
        c.cameraPosition[0] = frame == 31 ? 0 : float(frame);
        clear(color.Get(),
              frame == 31 ? std::array<float, 4>{.2f, .2f, .2f, .5f} : std::array<float, 4>{.1f, .4f, .7f, .5f});
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    }
    assert(read() == on);
    // The asynchronous form: the pass thread may run several frames behind the D3D11 context, which waits for
    // each result on the GPU only. Inputs change between frames; the last frame's output must be exact.
    {
        auto models = std::make_shared<const gsr::SceneModels>();
        std::vector<gsr::surface::Triangle> frameTriangles;
        for (unsigned frame = 0; frame < 32; ++frame) {
            c.cameraPosition[0] = frame == 31 ? 0 : float(frame);
            clear(color.Get(),
                  frame == 31 ? std::array<float, 4>{.2f, .2f, .2f, .5f} : std::array<float, 4>{.1f, .4f, .7f, .5f});
            frameTriangles.assign(triangles.begin(), triangles.end());
            assert(water.submit(color.Get(), depth.Get(), output.Get(), c, frameTriangles, 1, true, {}, models));
            assert(frameTriangles.empty());
        }
        assert(read() == on);
        water.finish();
    }
    {
        gsr::surface::RayWater transient(device.Get(), context.Get());
        assert(transient.ready());
        assert(transient.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    }
    assert(read() == on);
    std::cout << "Queued camera/input updates and immediate renderer teardown preserve output\n";
    // Resident (static) world geometry keeps its acceleration structures across frames and is translated to the
    // camera by its instance transform. It must render exactly like the same triangles submitted per frame,
    // also when split between static and per-frame input and after the static base is rebased.
    {
        const std::span<const gsr::surface::Triangle> all(triangles);
        water.setStaticWorld(all, 1, 1);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, {}, 1, true));
        assert(read() == on);
        water.setStaticWorld(all.first(3), 2, 2);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, all.subspan(3), 1, true));
        assert(read() == on);
        c.cameraPosition[0] = 50000; // beyond the rebase distance: rebuilds around the new camera
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, all.subspan(3), 1, true));
        c.cameraPosition[0] = 0; // 50000 from the base again: rebased back to the camera
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, all.subspan(3), 1, true));
        assert(read() == on);
        water.setStaticWorld(all.first(5), 3, 3); // water triangle in the static part as well
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, all.subspan(5), 1, true));
        assert(read() == on);
        water.setStaticWorld({}, 0, 0);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
        assert(read() == on);
    }
    std::cout << "Static world, static/per-frame split and rebase match per-frame submission\n";
    // A water-scope pass reconstructed as fallback reflection geometry lies tilted through the water plane (its GS
    // depth scale differs). Rays leaving the surface must not reflect off it where it rises above the water.
    {
        const auto count = triangles.size();
        quad({-300, -24, -30}, {300, -24, -30}, {300, -14, -300}, {-300, -14, -300}, {0, 0, 0}, false);
        for (size_t i = count; i < triangles.size(); ++i)
            triangles[i].shadowGeometry = false;
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
        assert(read() == on);
        triangles.resize(count);
        std::cout << "Reconstructed water layers at the surface do not reflect\n";
    }
    unsigned changed = 0, waterPixels = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        assert(on[i * 4 + 3] == 0x3800); // exact original 0.5 alpha
        bool differs = false;
        for (unsigned j = 0; j < 3; ++j) {
            assert((on[i * 4 + j] & 0x7c00) != 0x7c00);
            differs |= on[i * 4 + j] != off[i * 4 + j];
        }
        changed += differs;
        waterPixels += on[i * 4] != 0x3266; // half(0.2), source is unchanged above the water
        if (i / w < 25)
            assert(on[i * 4] == 0x3266 && on[i * 4 + 1] == 0x3266 && on[i * 4 + 2] == 0x3266);
    }
    std::cout << water.status() << "; reflection changed pixels=" << changed << " water pixels=" << waterPixels << '\n';
    assert(changed > 25 && waterPixels > 100);
    // Fading water coverage must converge to the already-shadowed scene.
    // Blending against the unlit input creates a bright seam at the bank.
    {
        gsr::SceneModels lights;
        lights.lighting.ambient[0] = lights.lighting.ambient[1] = lights.lighting.ambient[2] = .25f;
        gsr::SceneLight sun;
        sun.vector[1] = .6f;
        sun.vector[2] = -.8f;
        sun.color[0] = sun.color[1] = sun.color[2] = 1;
        lights.lighting.lights.push_back(sun);
        const gsr::surface::RayOptions effects{true, true};
        auto withOpacity = [&](float alpha) {
            for (auto& tri : triangles)
                if (tri.water)
                    for (auto& a : tri.opacity)
                        a = alpha;
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &lights, effects));
            return read();
        };
        auto zero = withOpacity(0), faint = withOpacity(.001f);
        const unsigned pixel = (40 * w + 32) * 4;
        auto value = [](uint16_t bits) {
            return std::ldexp(float((bits & 1023) + ((bits & 0x7c00) ? 1024 : 0)),
                              int((bits >> 10) & 31) - 25 + ((bits & 0x7c00) ? 0 : 1));
        };
        assert(value(zero[pixel]) < .18f);
        assert(std::abs(value(faint[pixel]) - value(zero[pixel])) < .001f);
        assert(faint[pixel + 3] == zero[pixel + 3]);
        for (auto& tri : triangles)
            if (tri.water)
                for (auto& a : tri.opacity)
                    a = 1;
        std::cout << "Water coverage preserves shadow continuity at bank edges\n";
    }
    // Shallow water keeps its Fresnel reflection once the intersection spans
    // a pixel. A fixed world-space coverage fade creates a dark shoreline band.
    // The probe is inside the old 96-unit band (80 units along the ray) but
    // 40 units deep vertically, past the shoreline foam line.
    {
        const unsigned row = 50;
        const float slope = 2 * (row + .5f) / 64 - 1, waterZ = 20.f / slope;
        clear(depth.Get(), {32.f / (waterZ + 80.f / std::sqrt(1 + slope * slope)), 0, 0, 0});
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false));
        auto shallowOff = read();
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
        auto shallowOn = read();
        auto value = [](uint16_t bits) {
            return std::ldexp(float((bits & 1023) + ((bits & 0x7c00) ? 1024 : 0)),
                              int((bits >> 10) & 31) - 25 + ((bits & 0x7c00) ? 0 : 1));
        };
        const unsigned pixel = (row * w + 32) * 4;
        const float deep = std::abs(value(on[pixel]) - value(off[pixel]));
        const float shallow = std::abs(value(shallowOn[pixel]) - value(shallowOff[pixel]));
        assert(deep > .01f && shallow > deep * .8f);
        assert(shallowOn[pixel + 3] == 0x3800);
        // Water about 1 cm deep at the bank forms bright foam patches.
        const float bankZ = 20.f / ((40.5f / 64) * 2 - 1);
        clear(depth.Get(), {32.f / (bankZ + 20), 0, 0, 0});
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false));
        auto bank = read();
        unsigned foamPixels = 0;
        for (unsigned x = 8; x < w - 8; ++x) {
            const unsigned i = (40 * w + x) * 4;
            foamPixels += value(bank[i]) > value(off[i]) + .05f;
            assert(bank[i + 3] == 0x3800);
        }
        assert(foamPixels * 4 > w - 16);
        clear(depth.Get(), {.001f, 0, 0, 0});
        std::cout << "Shallow shoreline retains reflection beyond subpixel intersection\n";
    }
    c.jitterPixels[0] = 1;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    auto shifted = read();
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x + 1 < w; ++x)
            for (unsigned j = 0; j < 4; ++j)
                assert(on[(y * w + x) * 4 + j] == shifted[(y * w + x + 1) * 4 + j]);
    c.jitterPixels[0] = 0;
    for (auto& triangle : triangles)
        if (triangle.water)
            for (auto& opacity : triangle.opacity)
                opacity = 0;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    auto transparent = read();
    for (unsigned i = 0; i < w * h; ++i)
        for (unsigned j = 0; j < 3; ++j)
            assert(transparent[i * 4 + j] == 0x3266);
    for (auto& triangle : triangles)
        for (auto& opacity : triangle.opacity)
            opacity = 1;
    clear(depth.Get(), {1, 0, 0, 0});
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    auto occluded = read();
    for (unsigned i = 0; i < w * h; ++i)
        for (unsigned j = 0; j < 3; ++j)
            assert(occluded[i * 4 + j] == 0x3266);
    on = occluded;
    triangles.resize(4);
    assert(!water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    assert(read() == on);
    // Cold-start reflection: the entire reflector is above the pitched main
    // camera's frustum. No prior view, image cache or screen radiance exists.
    clear(depth.Get(), {.001f, 0, 0, 0});
    c.cameraForward[1] = -.70710678f;
    c.cameraForward[2] = -.70710678f;
    c.cameraUp[1] = .70710678f;
    c.cameraUp[2] = -.70710678f;
    triangles.clear();
    quad({-1000, 250, -400}, {1000, 250, -400}, {1000, 1200, -400}, {-1000, 1200, -400}, {1, 1, 1}, false);
    quad({-800, -100, -40}, {800, -100, -40}, {800, -100, -800}, {-800, -100, -800}, {.2f, .3f, .4f}, true);
    for (unsigned i = 0; i < 2; ++i) {
        triangles[i].texture = 0;
        for (unsigned j = 0; j < 3; ++j) {
            triangles[i].uv[j][0] = (triangles[i].p[j].x + 1000) / 2000;
            triangles[i].uv[j][1] = (triangles[i].p[j].y - 250) / 950;
        }
    }
    gsr::surface::Texture pattern;
    pattern.pixels.resize(pattern.size * pattern.size);
    for (unsigned y = 0; y < pattern.size; ++y)
        for (unsigned x = 0; x < pattern.size; ++x)
            pattern.pixels[y * pattern.size + x] = ((x / 16 + y / 16) & 1) ? 0xffe02020u : 0xff2020e0u;
    const std::array textures{pattern};
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures));
    auto cold = read();
    // The native CPU water path can submit a different GS Z scale. A coverage
    // triangle at half the distance must not move the real reflecting surface.
    const auto nativeCount = triangles.size();
    for (unsigned i = 2; i < nativeCount; ++i) {
        auto coverage = triangles[i];
        coverage.shadowGeometry = false;
        for (auto& p : coverage.p) {
            p.x *= .5f;
            p.y *= .5f;
            p.z *= .5f;
        }
        triangles.push_back(coverage);
    }
    const gsr::surface::RayOptions coveredWater{true, false, nullptr, true};
    assert(
        water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures, nullptr, coveredWater));
    assert(read() == cold);
    // Native water is blended layers: the surface plus faint overlay passes, whose CPU-clipped coverage may land
    // nearer than the surface layer's. A nearer faint overlay must not replace the surface's opacity (on the Surdana
    // pond that stamped hard-edged, half-faded regions wherever the nearest layer changed).
    {
        const auto surfaceCount = triangles.size();
        for (size_t i = nativeCount; i < surfaceCount; ++i)
            for (auto& a : triangles[i].opacity)
                a = .85f;
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures, nullptr,
                            coveredWater));
        const auto surfaceOnly = read();
        for (size_t i = nativeCount; i < surfaceCount; ++i) {
            auto overlay = triangles[i];
            for (auto& p : overlay.p) {
                p.x *= .8f;
                p.y *= .8f;
                p.z *= .8f;
            }
            for (auto& a : overlay.opacity)
                a = .38f;
            triangles.push_back(overlay);
        }
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures, nullptr,
                            coveredWater));
        assert(read() == surfaceOnly);
        triangles.resize(surfaceCount);
        for (size_t i = nativeCount; i < surfaceCount; ++i)
            for (auto& a : triangles[i].opacity)
                a = 1;
        std::cout << "Faint water overlay layers keep the surface layer's coverage opacity\n";
    }
    triangles.resize(nativeCount);
    assert(
        water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures, nullptr, coveredWater));
    auto hiddenWater = read();
    for (unsigned i = 0; i < w * h; ++i)
        assert(hiddenWater[i * 4] == 0x3266);
    for (unsigned i = 0; i < 2; ++i)
        triangles[i].texture = ~0u;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures));
    auto untextured = read();
    triangles.erase(triangles.begin(), triangles.begin() + 2);
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true));
    auto missing = read();
    unsigned coldHits = 0, textureDetail = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        coldHits += cold[i * 4] != missing[i * 4] || cold[i * 4 + 2] != missing[i * 4 + 2];
        textureDetail += cold[i * 4] != untextured[i * 4] || cold[i * 4 + 2] != untextured[i * 4 + 2];
    }
    assert(coldHits > 25 && textureDetail > 25);
    std::cout << "Cold off-frustum geometry affected " << coldHits << " pixels; actual material texture affected "
              << textureDetail << " pixels\n";
    // Instanced, never-submitted native props must equal the explicit geometry
    // result, including the material-base offset after the water BLAS.
    gsr::SceneModels models;
    auto model = std::make_shared<gsr::SceneModelMesh>();
    model->identity = 123;
    const P corners[4] = {{-1000, -475, 0}, {1000, -475, 0}, {1000, 475, 0}, {-1000, 475, 0}};
    const unsigned indices[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (const auto& face : indices) {
        gsr::SceneModelTriangle t;
        t.material = 0;
        for (unsigned j = 0; j < 3; ++j) {
            const auto p = corners[face[j]];
            t.position[j][0] = p.x;
            t.position[j][1] = p.y;
            t.position[j][2] = p.z;
            t.uv[j][0] = (p.x + 1000) / 2000;
            t.uv[j][1] = (p.y + 475) / 950;
        }
        model->triangles.push_back(t);
    }
    auto nativePattern = std::make_shared<gsr::SceneModelTexture>();
    nativePattern->pixels = pattern.pixels;
    for (auto& pixel : nativePattern->pixels)
        pixel = (pixel & 0xffffff) | 0x80000000;
    models.meshes.push_back(model);
    models.textures.push_back(nativePattern);
    gsr::SceneModelInstance instance;
    instance.transform[0] = instance.transform[5] = instance.transform[10] = 1;
    instance.transform[7] = 725;
    instance.transform[11] = -400;
    models.instances.push_back(instance);
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto instanced = read();
    assert(instanced == cold);
    // Native lights stand in for a native model's GS vertex colour, so they modulate the texel before it is
    // linearised: an ambient-lit model reflects exactly like the same surface with that vertex colour.
    {
        gsr::SceneModels lit = models;
        lit.lighting.ambient[0] = lit.lighting.ambient[1] = lit.lighting.ambient[2] = .5f;
        gsr::SceneLight behind; // faces away from the reflector and the water: ambient only
        behind.vector[2] = -1;
        behind.color[0] = behind.color[1] = behind.color[2] = 1;
        lit.lighting.lights.push_back(behind);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &lit));
        const auto litModel = read();
        assert(litModel != cold);
        gsr::SceneModels lightsOnly;
        lightsOnly.lighting = lit.lighting;
        const auto waterOnly = triangles;
        triangles.clear();
        quad({-1000, 250, -400}, {1000, 250, -400}, {1000, 1200, -400}, {-1000, 1200, -400}, {.5f, .5f, .5f}, false);
        for (auto& t : triangles) {
            t.texture = 0;
            for (unsigned j = 0; j < 3; ++j) {
                t.uv[j][0] = (t.p[j].x + 1000) / 2000;
                t.uv[j][1] = (t.p[j].y - 250) / 950;
            }
        }
        triangles.insert(triangles.end(), waterOnly.begin(), waterOnly.end());
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, textures, &lightsOnly));
        assert(read() == litModel);
        triangles = waterOnly;
        std::cout << "Native model lighting modulates the texel like a GS vertex colour\n";
    }
    // Animated poses: a new identity with the same topology refits the previous pose's BLAS in place. Moving the
    // pose away and back must reproduce the freshly built result exactly.
    {
        gsr::SceneModels posed = models;
        const auto pose = [&](uint64_t identity, float shift) {
            auto mesh = std::make_shared<gsr::SceneModelMesh>(*model);
            mesh->identity = identity;
            mesh->topology = 77;
            for (auto& t : mesh->triangles)
                for (auto& p : t.position)
                    p[0] += shift;
            posed.meshes[0] = mesh;
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &posed));
            return read();
        };
        assert(pose(300, 0) == cold);
        assert(pose(301, 1500) != cold);
        assert(pose(302, 0) == cold);
        water.finish();
        const auto status = water.status();
        assert(status.find(" 2 model BLAS refits") != std::string::npos);
    }
    // Add an opaque blue surface behind an alpha-tested front prop. Cutout
    // texels must let rays reach that surface, rather than acting as solid cards.
    auto back = std::make_shared<gsr::SceneModelMesh>(*model);
    back->identity = 124;
    for (auto& t : back->triangles)
        t.material = 1;
    auto blue = std::make_shared<gsr::SceneModelTexture>();
    blue->pixels.assign(128 * 128, 0x80f02020);
    models.meshes.push_back(back);
    models.textures.push_back(blue);
    instance.mesh = 1;
    instance.transform[11] = -600;
    models.instances.push_back(instance);
    nativePattern->flags = 8;
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 0; x < 64; ++x)
            nativePattern->pixels[y * 128 + x] &= 0xffffff;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto cutout = read();
    unsigned holes = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        holes += cutout[i * 4] != instanced[i * 4] || cutout[i * 4 + 2] != instanced[i * 4 + 2];
        assert(cutout[i * 4 + 3] == 0x3800);
    }
    assert(holes > 25);
    nativePattern->flags = 0x10000;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == cutout);
    // A half-transparent surface has a distinct result from opaque/cutout;
    // moving its instance must retire its old world-space contribution.
    nativePattern->flags = 0x80000;
    for (auto& pixel : nativePattern->pixels)
        pixel = (pixel & 0xffffff) | 0x40000000;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto layered = read();
    assert(layered != cutout && layered != instanced);
    models.instances[0].opacity = .5f;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto faded = read();
    assert(faded != layered);
    models.instances[0].opacity = 1;
    models.instances[0].transform[3] = 100000;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto moved = read();
    assert(moved != layered);
    // Fully faded instances must not consume the four reflection layers and
    // hide a visible prop behind them. Include a fade rounded to zero by the
    // native instance-opacity packing as well as exact zero.
    models.instances[0].transform[3] = 0;
    models.instances[0].opacity = 0;
    for (unsigned i = 0; i < 5; ++i) {
        auto invisible = models.instances[0];
        invisible.transform[11] -= float(i * 8);
        invisible.opacity = i == 4 ? .01f : 0;
        models.instances.push_back(invisible);
    }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == moved);
    models = {};
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == missing);
    std::cout
        << "Instanced cold geometry exactly matches explicit triangles; " << holes
        << " pixels changed through alpha holes; layered transparency, moving instances and scene retirement passed\n";
    std::cout << "DXR intersections, reflection contribution, jitter alignment, native occlusion, sky rejection, "
                 "finite RGB, alpha and no-water preservation passed\n";
    // Land-only shadows: a cold occluder casts onto the reconstructed depth
    // receiver even when no water or DLSS features exist.
    c.cameraForward[1] = 0;
    c.cameraForward[2] = -1;
    c.cameraUp[1] = 1;
    c.cameraUp[2] = 0;
    clear(depth.Get(), {.1f, 0, 0, 0});
    triangles.clear();
    quad({-1000, -1000, -320}, {1000, -1000, -320}, {1000, 1000, -320}, {-1000, 1000, -320}, {.2f, .2f, .2f}, false);
    quad({-80, -80, -200}, {80, -80, -200}, {80, 80, -200}, {-80, 80, -200}, {.5f, .5f, .5f}, false);
    models = {};
    models.lighting.ambient[0] = models.lighting.ambient[1] = models.lighting.ambient[2] = .25f;
    gsr::SceneLight sun;
    sun.vector[2] = 1;
    sun.color[0] = sun.color[1] = sun.color[2] = 1;
    models.lighting.lights.push_back(sun);
    const gsr::surface::RayOptions shadows{false, true};
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    auto shadowed = read();
    unsigned shadowPixels = 0;
    for (unsigned i = 0; i < w * h; ++i) {
        shadowPixels += shadowed[i * 4] < 0x3266;
        assert(shadowed[i * 4 + 3] == 0x3800);
        assert((shadowed[i * 4] & 0x7c00) != 0x7c00);
    }
    assert(shadowPixels > 25 && shadowPixels < w * h / 2);
    for (unsigned t = 2; t < triangles.size(); ++t)
        for (auto& p : triangles[t].p)
            p.x += 10000;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    auto noShadow = read();
    for (unsigned i = 0; i < w * h; ++i)
        assert(noShadow[i * 4] == 0x3266 && noShadow[i * 4 + 3] == 0x3800);
    // The native underwater tint is a 12-triangle box drawn around the camera (faces about 4096 units out). It is
    // not world geometry: it must not put every receiver inside it in shadow. A slightly off-centre camera, as the
    // native box trails the view, must not matter.
    {
        gsr::SceneModels shell = models;
        auto box = std::make_shared<gsr::SceneModelMesh>();
        box->identity = 900;
        const float e[3] = {5120, 4096, 5120};
        for (unsigned axis = 0; axis < 3; ++axis)
            for (float side : {-1.f, 1.f}) {
                const unsigned u = (axis + 1) % 3, v = (axis + 2) % 3;
                float q[4][3];
                for (unsigned k = 0; k < 4; ++k) {
                    q[k][axis] = side * e[axis];
                    q[k][u] = (k == 1 || k == 2) ? e[u] : -e[u];
                    q[k][v] = k >= 2 ? e[v] : -e[v];
                }
                for (const auto& face : {std::array<unsigned, 3>{0, 1, 2}, std::array<unsigned, 3>{0, 2, 3}}) {
                    gsr::SceneModelTriangle t;
                    for (unsigned j = 0; j < 3; ++j)
                        std::copy(q[face[j]], q[face[j]] + 3, t.position[j]);
                    box->triangles.push_back(t);
                }
            }
        shell.meshes.push_back(box);
        gsr::SceneModelInstance around;
        around.mesh = uint32_t(shell.meshes.size() - 1);
        around.transform[0] = around.transform[5] = around.transform[10] = 1;
        around.transform[3] = 103;
        around.transform[7] = 4;
        shell.instances.push_back(around);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &shell, shadows));
        assert(read() == noShadow);
        std::cout << "Camera-enclosing effect shell casts no shadow\n";
        // The native sky barrels are open rings centred on the camera (radius about 5000, starting above it).
        // They do not enclose the camera vertically, but they are not world geometry either.
        auto ring = std::make_shared<gsr::SceneModelMesh>();
        ring->identity = 901;
        constexpr unsigned kSegments = 16;
        for (unsigned i = 0; i < kSegments; ++i) {
            const float a0 = 6.2831853f * i / kSegments, a1 = 6.2831853f * (i + 1) / kSegments;
            const float q[4][3] = {{5000 * std::cos(a0), 200, 5000 * std::sin(a0)},
                                   {5000 * std::cos(a1), 200, 5000 * std::sin(a1)},
                                   {5000 * std::cos(a1), 9600, 5000 * std::sin(a1)},
                                   {5000 * std::cos(a0), 9600, 5000 * std::sin(a0)}};
            for (const auto& face : {std::array<unsigned, 3>{0, 1, 2}, std::array<unsigned, 3>{0, 2, 3}}) {
                gsr::SceneModelTriangle t;
                for (unsigned j = 0; j < 3; ++j)
                    std::copy(q[face[j]], q[face[j]] + 3, t.position[j]);
                ring->triangles.push_back(t);
            }
        }
        shell.instances.back().mesh = uint32_t(shell.meshes.size());
        shell.instances.back().transform[3] = 60;
        shell.instances.back().transform[7] = 0;
        shell.instances.back().transform[11] = -40;
        shell.meshes.push_back(ring);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &shell, shadows));
        assert(read() == noShadow);
        std::cout << "Camera-anchored open sky ring casts no shadow\n";
    }
    for (unsigned t = 2; t < triangles.size(); ++t)
        for (auto& p : triangles[t].p)
            p.x -= 10000;
    // Terrain LOD/depth mismatch must fade the receiver shadow before the
    // search interval ends, while aligned geometry keeps its full shadow.
    {
        auto atDepth = [&](float z) {
            clear(depth.Get(), {32.f / z, 0, 0, 0});
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
            return read();
        };
        auto mid = atDepth(336), inside = atDepth(351.9f), outside = atDepth(352.1f);
        const unsigned pixel = (32 * w + 32) * 4;
        assert(shadowed[pixel] < mid[pixel] && mid[pixel] < outside[pixel]);
        auto value = [](uint16_t bits) {
            return std::ldexp(float((bits & 1023) + ((bits & 0x7c00) ? 1024 : 0)),
                              int((bits >> 10) & 31) - 25 + ((bits & 0x7c00) ? 0 : 1));
        };
        assert(std::abs(value(inside[pixel]) - value(outside[pixel])) < .001f);
        assert(inside[pixel + 3] == 0x3800 && outside[pixel + 3] == 0x3800);
        clear(depth.Get(), {.1f, 0, 0, 0});
        std::cout << "Terrain receiver mismatch fades continuously; aligned shadows preserved\n";
    }
    // Alpha cutouts must cast their silhouette, not a solid card. A transparent
    // texel contributes no shadow, and fractional native opacity transmits light.
    gsr::surface::Texture shadowTexture;
    shadowTexture.pixels.assign(128 * 128, 0x00ffffff);
    for (unsigned t = 2; t < triangles.size(); ++t) {
        auto& tri = triangles[t];
        tri.texture = 0;
        tri.textureAlpha = true;
        tri.alphaTest = 6;
        for (unsigned j = 0; j < 3; ++j) {
            tri.uv[j][0] = (tri.p[j].x + 80) / 160;
            tri.uv[j][1] = (tri.p[j].y + 80) / 160;
        }
    }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {&shadowTexture, 1}, &models,
                        shadows));
    assert(read() == noShadow);
    for (unsigned y = 0; y < 128; ++y)
        for (unsigned x = 64; x < 128; ++x)
            shadowTexture.pixels[y * 128 + x] = 0x80ffffff;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {&shadowTexture, 1}, &models,
                        shadows));
    auto leafShadow = read();
    unsigned leafPixels = 0;
    for (unsigned i = 0; i < w * h; ++i)
        leafPixels += leafShadow[i * 4] < 0x3266;
    assert(leafPixels > 25 && leafPixels + 25 < shadowPixels);
    for (unsigned t = 2; t < triangles.size(); ++t) {
        auto& tri = triangles[t];
        tri.texture = ~0u;
        tri.textureAlpha = false;
        tri.alphaTest = 1;
        tri.transparent = true;
        for (auto& opacity : tri.opacity)
            opacity = .5f;
    }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    auto transmitted = read();
    for (unsigned i = 0; i < w * h; ++i)
        if (shadowed[i * 4] < noShadow[i * 4])
            assert(transmitted[i * 4] > shadowed[i * 4] && transmitted[i * 4] < noShadow[i * 4]);
    for (unsigned t = 2; t < triangles.size(); ++t) {
        triangles[t].shadowGeometry = false;
        triangles[t].transparent = false;
        for (auto& opacity : triangles[t].opacity)
            opacity = 1;
    }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    assert(read() == noShadow);
    for (unsigned t = 2; t < triangles.size(); ++t)
        triangles[t].shadowGeometry = true;
    models.lighting.lights[0].type = gsr::SceneLight::Point;
    models.lighting.lights[0].vector[2] = -100;
    models.lighting.lights[0].radiusSquared = 500 * 500;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    assert(read() != noShadow);
    // Blockers beyond a finite point light cannot cast onto this receiver
    // (and are out of ambient-occlusion reach, 820 units from it).
    for (unsigned t = 2; t < triangles.size(); ++t)
        for (auto& p : triangles[t].p)
            p.z = 500;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    assert(read() == noShadow);
    clear(depth.Get(), {0, 0, 0, 0});
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
    assert(read() == noShadow);
    std::cout << "Land-only native directional shadows affected " << shadowPixels
              << " pixels; moving blockers, finite point-light distance, sky and original alpha passed\n";
    // Ambient occlusion: with the sun behind the receiver only the native
    // ambient term lights it. A nearby blocker darkens it; open ground and a
    // blocker beyond the occlusion reach leave the image exactly unchanged.
    {
        clear(depth.Get(), {.1f, 0, 0, 0});
        auto& away = models.lighting.lights[0];
        away.type = gsr::SceneLight::Directional;
        away.vector[0] = away.vector[1] = 0;
        away.vector[2] = -1;
        auto blockerAt = [&](float z) {
            for (unsigned t = 2; t < triangles.size(); ++t)
                for (auto& p : triangles[t].p)
                    p.z = z;
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
            return read();
        };
        auto open = blockerAt(500), nearby = blockerAt(-170);
        unsigned occluded = 0;
        for (unsigned i = 0; i < w * h; ++i) {
            assert(open[i * 4] == 0x3266 && open[i * 4 + 3] == 0x3800);
            assert(nearby[i * 4] <= open[i * 4] && nearby[i * 4 + 3] == 0x3800);
            occluded += nearby[i * 4] < open[i * 4];
        }
        const unsigned centre = (32 * w + 32) * 4;
        assert(nearby[centre] < open[centre] && occluded > 25);
        std::cout << "Ambient occlusion darkened " << occluded << " pixels; open ground unchanged\n";
        // A shallow terrain crease (25 degrees) is one smoothly lit surface: neither face may occlude
        // the other, or ambient occlusion draws dark lines along triangle edges.
        triangles.clear();
        quad({-1000, -1000, -320}, {0, -1000, -320}, {0, 1000, -320}, {-1000, 1000, -320}, {.2f, .2f, .2f}, false);
        const float rise = -320 + 300 * std::tan(25 * 3.14159265f / 180);
        quad({0, -1000, -320}, {300, -1000, rise}, {300, 1000, rise}, {0, 1000, -320}, {.2f, .2f, .2f}, false);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
        auto crease = read();
        for (unsigned i = 0; i < w * h; ++i)
            assert(crease[i * 4] == 0x3266 && crease[i * 4 + 3] == 0x3800);
        std::cout << "Shallow terrain crease leaves ambient occlusion unchanged\n";
        // A low sun across the same crease: the rising face would shadow a band of the flat face beside the edge,
        // though the Gouraud-lit GS surface is smoothly lit there.
        const float elevation = 10 * 3.14159265f / 180;
        away.vector[0] = std::cos(elevation);
        away.vector[2] = std::sin(elevation);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
        auto lowSun = read();
        for (unsigned i = 0; i < w * h; ++i)
            assert(lowSun[i * 4] == 0x3266 && lowSun[i * 4 + 3] == 0x3800);
        std::cout << "Low sun over a shallow terrain crease casts no per-face band\n";
        // A native model does not shadow or occlude itself (a low-poly face would print its own facets), but
        // another model's identical geometry still shadows it.
        auto modelQuad = [](gsr::SceneModelMesh& mesh, float half, float z) {
            const float q[4][3] = {{-half, -half, z}, {half, -half, z}, {half, half, z}, {-half, half, z}};
            for (const auto& face : {std::array<unsigned, 3>{0, 1, 2}, std::array<unsigned, 3>{0, 2, 3}}) {
                gsr::SceneModelTriangle t;
                for (unsigned j = 0; j < 3; ++j)
                    std::copy(q[face[j]], q[face[j]] + 3, t.position[j]);
                mesh.triangles.push_back(t);
            }
        };
        away.vector[0] = 0;
        away.vector[2] = 1;
        auto receiver = std::make_shared<gsr::SceneModelMesh>(), blocker = std::make_shared<gsr::SceneModelMesh>(),
             both = std::make_shared<gsr::SceneModelMesh>();
        receiver->identity = 950;
        blocker->identity = 951;
        both->identity = 952;
        modelQuad(*receiver, 1000, -320);
        modelQuad(*blocker, 80, -250);
        modelQuad(*both, 1000, -320);
        modelQuad(*both, 80, -250);
        gsr::SceneModels actors = models;
        gsr::SceneModelInstance actor;
        actor.transform[0] = actor.transform[5] = actor.transform[10] = 1;
        actors.meshes = {both};
        actors.instances = {actor};
        triangles.clear();
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &actors, shadows));
        auto selfShadow = read();
        for (unsigned i = 0; i < w * h; ++i)
            assert(selfShadow[i * 4] == 0x3266 && selfShadow[i * 4 + 3] == 0x3800);
        actors.meshes = {receiver, blocker};
        actors.instances = {actor, actor};
        actors.instances[1].mesh = 1;
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &actors, shadows));
        auto castShadow = read();
        unsigned cast = 0;
        for (unsigned i = 0; i < w * h; ++i)
            cast += castShadow[i * 4] < 0x3266;
        assert(castShadow[(32 * w + 32) * 4] < 0x3266 && cast > 25);
        std::cout << "Native model receives " << cast << " shadowed pixels from another model and none from itself\n";
        // A shadowed resident ridge (two faces 30 degrees apart) is lit per vertex by the GS. Its shadow must darken
        // smoothly across the ridge rather than in one flat-normal step per face.
        const float slope = std::tan(15 * 3.14159265f / 180), ridgeBack = -320 - 1000 * slope;
        quad({-1000, -1000, ridgeBack}, {0, -1000, -320}, {0, 1000, -320}, {-1000, 1000, ridgeBack}, {.2f, .2f, .2f},
             false);
        quad({0, -1000, -320}, {1000, -1000, ridgeBack}, {1000, 1000, ridgeBack}, {0, 1000, -320}, {.2f, .2f, .2f},
             false);
        std::vector<float> ridgeDepth(w * h);
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x)
                ridgeDepth[y * w + x] = 32 * (1 - std::abs((x + .5f) / w * 2 - 1) * slope) / 320;
        context->UpdateSubresource(depth.Get(), 0, nullptr, ridgeDepth.data(), w * 4, 0);
        water.setStaticWorld(triangles, 10, 10);
        triangles.clear();
        // The sun is fully blocked beyond ambient-occlusion reach, so only the shading normal varies the ratio.
        const P sunward{.5f, 0, .8660254f}, across{-.8660254f, 0, .5f};
        const P blockerCentre{300, 0, 200};
        auto corner = [&](float a, float b) {
            return P{blockerCentre.x + across.x * a, blockerCentre.y + b, blockerCentre.z + across.z * a};
        };
        quad(corner(-3000, -3000), corner(3000, -3000), corner(3000, 3000), corner(-3000, 3000), {.5f, .5f, .5f},
             false);
        away.vector[0] = sunward.x;
        away.vector[1] = sunward.y;
        away.vector[2] = sunward.z;
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
        auto ridge = read();
        water.setStaticWorld({}, 0, 0);
        clear(depth.Get(), {.1f, 0, 0, 0});
        auto linear = [](uint16_t bits) {
            return std::ldexp(float((bits & 1023) + ((bits & 0x7c00) ? 1024 : 0)),
                              int((bits >> 10) & 31) - 25 + ((bits & 0x7c00) ? 0 : 1));
        };
        const unsigned row = 32 * w * 4;
        float largestStep = 0;
        for (unsigned x = 1; x < w; ++x)
            largestStep =
                (std::max)(largestStep, std::abs(linear(ridge[row + x * 4]) - linear(ridge[row + x * 4 - 4])));
        const float left = linear(ridge[row + 4]), right = linear(ridge[row + (w - 2) * 4]);
        assert(left < .2f && right < .2f && std::abs(left - right) > .003f);
        assert(largestStep < .25f * std::abs(left - right));
        std::cout << "Shadowed ridge darkens smoothly: largest step " << largestStep << " across a "
                  << std::abs(left - right) << " shading range\n";
        // Coarse terrain LOD draws the ground up to half a metre above or below the resident grid. At grazing views
        // that offset lies far outside the view-ray search, so the receiver must be found vertically instead.
        triangles.clear();
        quad({-6000, -300, 0}, {6000, -300, 0}, {6000, -300, -12000}, {-6000, -300, -12000}, {.2f, .2f, .2f}, false);
        quad({-6000, 400, 0}, {6000, 400, 0}, {6000, 400, -12000}, {-6000, 400, -12000}, {.5f, .5f, .5f}, false);
        away.vector[0] = away.vector[2] = 0;
        away.vector[1] = 1;
        for (const float drawn : {-150.f, -450.f}) {
            std::vector<float> groundDepth(w * h, 0);
            for (unsigned y = h / 2; y < h; ++y)
                for (unsigned x = 0; x < w; ++x)
                    groundDepth[y * w + x] = 32 * ((y + .5f) / h * 2 - 1) / -drawn;
            context->UpdateSubresource(depth.Get(), 0, nullptr, groundDepth.data(), w * 4, 0);
            assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, false, {}, &models, shadows));
            auto ground = read();
            unsigned shaded = 0;
            for (unsigned i = w * h / 2; i < w * h; ++i)
                shaded += ground[i * 4] < 0x3000;
            for (unsigned i = 0; i < w * h / 2; ++i)
                assert(ground[i * 4] == 0x3266);
            assert(shaded > w * h / 2 * 9 / 10);
            std::cout << "Ground drawn at " << drawn << " over resident terrain at -300 shades " << shaded
                      << " receiver pixels\n";
        }
        clear(depth.Get(), {.1f, 0, 0, 0});
    }
    // Canyon sky can be a tiny gap that all 15 legacy environment probes miss.
    // The reduction must observe it, retain weather offscreen, and clear it
    // when the native level changes rather than reflecting the water's color.
    c.cameraForward[1] = -.70710678f;
    c.cameraForward[2] = -.70710678f;
    c.cameraUp[1] = .70710678f;
    c.cameraUp[2] = -.70710678f;
    triangles.clear();
    quad({-800, -100, -40}, {800, -100, -40}, {800, -100, -800}, {-800, -100, -800}, {.2f, .3f, .4f}, true);
    clear(color.Get(), {.2f, .2f, .2f, .5f});
    clear(depth.Get(), {.001f, 0, 0, 0});
    models = {};
    models.level = 100;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto noSky = read();
    std::vector<uint16_t> skyColor(w * h * 4);
    std::vector<float> skyDepth(w * h, .001f);
    for (unsigned i = 0; i < w * h; ++i) {
        skyColor[i * 4] = skyColor[i * 4 + 1] = skyColor[i * 4 + 2] = 0x3266;
        skyColor[i * 4 + 3] = 0x3800;
    }
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 58; x < 60; ++x) {
            const auto i = y * w + x;
            skyColor[i * 4] = 0x3a00;
            skyColor[i * 4 + 1] = 0x3800;
            skyColor[i * 4 + 2] = 0x3400;
            skyDepth[i] = 0;
        }
    context->UpdateSubresource(color.Get(), 0, nullptr, skyColor.data(), w * 8, 0);
    context->UpdateSubresource(depth.Get(), 0, nullptr, skyDepth.data(), w * 4, 0);
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto narrowSky = read();
    unsigned skyReflections = 0;
    for (unsigned i = 30 * w; i < w * h; ++i) {
        skyReflections += narrowSky[i * 4] != noSky[i * 4];
        assert(narrowSky[i * 4 + 3] == 0x3800);
    }
    assert(skyReflections > 25);
    clear(color.Get(), {.2f, .2f, .2f, .5f});
    clear(depth.Get(), {.001f, 0, 0, 0});
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto retainedSky = read();
    for (unsigned i = 30 * w; i < w * h; ++i)
        for (unsigned j = 0; j < 4; ++j)
            assert(retainedSky[i * 4 + j] == narrowSky[i * 4 + j]);
    models.level = 101;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == noSky);
    std::cout << "Narrow native sky gap affected " << skyReflections
              << " reflection pixels; offscreen weather retention and level reset passed\n";
    // Learn two differently coloured sky directions, then hide all sky pixels.
    // Offscreen water misses must retain direction, not collapse to one tint.
    models.level = 103;
    for (unsigned i = 0; i < w * h; ++i) {
        const bool sky = i / w < 24, left = i % w < w / 2;
        skyDepth[i] = sky ? 0 : .001f;
        skyColor[i * 4] = sky ? (left ? 0x3c00 : 0) : 0x3266;
        skyColor[i * 4 + 1] = sky ? 0 : 0x3266;
        skyColor[i * 4 + 2] = sky ? (left ? 0 : 0x3c00) : 0x3266;
    }
    context->UpdateSubresource(color.Get(), 0, nullptr, skyColor.data(), w * 8, 0);
    context->UpdateSubresource(depth.Get(), 0, nullptr, skyDepth.data(), w * 4, 0);
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    clear(color.Get(), {.2f, .2f, .2f, .5f});
    clear(depth.Get(), {.001f, 0, 0, 0});
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto directionalSky = read();
    const unsigned left = (35 * w + 20) * 4, right = (35 * w + 43) * 4;
    assert(directionalSky[left] > directionalSky[left + 2]);
    assert(directionalSky[right + 2] > directionalSky[right]);
    c.cameraRight[0] = -1;
    c.cameraForward[2] *= -1;
    c.cameraUp[2] *= -1;
    for (auto& t : triangles)
        for (auto& p : t.p) {
            p.x = -p.x;
            p.z = -p.z;
        }
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto turnedSky = read();
    assert(turnedSky[left + 2] > turnedSky[left] && turnedSky[right] > turnedSky[right + 2]);
    c.cameraRight[0] = 1;
    c.cameraForward[2] *= -1;
    c.cameraUp[2] *= -1;
    for (auto& t : triangles)
        for (auto& p : t.p) {
            p.x = -p.x;
            p.z = -p.z;
        }
    models.level = 104;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == noSky);
    std::cout << "Offscreen sky retains distinct world directions and clears all lobes on level change\n";
    // Sky can be confined to the lower viewport by pitching the camera or
    // looking through an opening. It must still seed retained radiance.
    models.level = 105;
    for (unsigned i = 0; i < w * h; ++i) {
        skyDepth[i] = .001f;
        for (unsigned j = 0; j < 3; ++j)
            skyColor[i * 4 + j] = 0x3266;
    }
    for (unsigned y = 56; y < 58; ++y)
        for (unsigned x = 58; x < 60; ++x) {
            unsigned i = y * w + x;
            skyDepth[i] = 0;
            for (unsigned j = 0; j < 3; ++j)
                skyColor[i * 4 + j] = 0x3c00;
        }
    context->UpdateSubresource(color.Get(), 0, nullptr, skyColor.data(), w * 8, 0);
    context->UpdateSubresource(depth.Get(), 0, nullptr, skyDepth.data(), w * 4, 0);
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    clear(color.Get(), {.2f, .2f, .2f, .5f});
    clear(depth.Get(), {.001f, 0, 0, 0});
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    auto lowerSky = read();
    assert(lowerSky[left] > noSky[left]);
    models.level = 106;
    assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, 1, true, {}, &models));
    assert(read() == noSky);
    std::cout << "Lower-viewport sky observation, offscreen retention and reset passed\n";
    // Crossing the screen-radiance depth tolerance must fade to the exported
    // material, not abruptly exchange a bright screen sample for a dark prop.
    c.cameraForward[1] = 0;
    c.cameraForward[2] = -1;
    c.cameraUp[1] = 1;
    c.cameraUp[2] = 0;
    triangles.clear();
    models = {};
    models.level = 102;
    quad({-400, -100, -200}, {400, -100, -200}, {400, 500, -200}, {-400, 500, -200}, {.1f, .1f, .1f}, false);
    quad({-300, -20, -30}, {300, -20, -30}, {300, -20, -300}, {-300, -20, -300}, {.2f, .3f, .4f}, true);
    for (unsigned i = 0; i < w * h; ++i) {
        skyColor[i * 4] = skyColor[i * 4 + 1] = skyColor[i * 4 + 2] = i / w < 32 ? 0x3c00 : 0x3266;
        skyColor[i * 4 + 3] = 0x3800;
    }
    context->UpdateSubresource(color.Get(), 0, nullptr, skyColor.data(), w * 8, 0);
    float reflectionTime = 1;
    auto sampleDepth = [&](float d) {
        for (unsigned i = 0; i < w * h; ++i)
            skyDepth[i] = i / w < 32 ? d : .001f;
        context->UpdateSubresource(depth.Get(), 0, nullptr, skyDepth.data(), w * 4, 0);
        assert(water.render(color.Get(), depth.Get(), output.Get(), c, triangles, reflectionTime, true, {}, &models));
        return read();
    };
    auto matched = sampleDepth(.16f), edgeInside = sampleDepth(.16160f), edgeOutside = sampleDepth(.16164f);
    auto half = [](uint16_t bits) {
        return std::ldexp(float((bits & 1023) + ((bits & 0x7c00) ? 1024 : 0)),
                          int((bits >> 10) & 31) - 25 + ((bits & 0x7c00) ? 0 : 1));
    };
    float largestJump = 0, screenContribution = 0;
    for (unsigned i = 48 * w; i < w * h; ++i) {
        largestJump = (std::max)(largestJump, std::abs(half(edgeInside[i * 4]) - half(edgeOutside[i * 4])));
        screenContribution = (std::max)(screenContribution, std::abs(half(matched[i * 4]) - half(edgeOutside[i * 4])));
        assert(edgeInside[i * 4 + 3] == 0x3800 && edgeOutside[i * 4 + 3] == 0x3800);
    }
    std::cout << "Reflection depth transition maximum jump=" << largestJump
              << "; valid screen contribution=" << screenContribution << '\n';
    assert(screenContribution > .01f && largestJump < .01f);
    // High-contrast reflected detail must change smoothly
    // through wave motion. Nearest screen sampling produces jumps
    // even though the ray continues to hit the same physical surface.
    // Steps are 1 ms: the waves move at deep-water dispersion speeds.
    for (unsigned i = 0; i < w * h; ++i)
        for (unsigned j = 0; j < 3; ++j)
            skyColor[i * 4 + j] = i / w < 32 ? ((i % w) & 1 ? 0x3c00 : 0) : 0x3266;
    context->UpdateSubresource(color.Get(), 0, nullptr, skyColor.data(), w * 8, 0);
    auto previous = sampleDepth(.16f);
    float motionJump = 0;
    for (unsigned step = 1; step <= 100; ++step) {
        reflectionTime = 1 + float(step) * .001f;
        auto current = sampleDepth(.16f);
        for (unsigned y = 48; y < h; ++y)
            for (unsigned x = 8; x < w - 8; ++x) {
                unsigned i = (y * w + x) * 4;
                motionJump = (std::max)(motionJump, std::abs(half(current[i]) - half(previous[i])));
                assert(current[i + 3] == 0x3800);
            }
        previous = std::move(current);
    }
    c.jitterPixels[0] = 0;
    std::cout << "Subpixel reflected detail maximum jump=" << motionJump << '\n';
    assert(motionJump < .02f);
}
