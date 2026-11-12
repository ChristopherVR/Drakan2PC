// Terrain detail on Direct3D 11: GPU objects, per-draw classification and constants. The policy and the maths are in
// terrain_detail.h (shared with the WebGPU backend, unit tested); the shader fragment is gs_terrain.inc.
#include "d3d11_backend_impl.h"

#include <map>

namespace gsr {

void D3D11Backend::Impl::initTerrain() {
    terrainCfg.detail = cfg.terrainDetail;
    terrainCfg.cliffs = cfg.terrainCliffs;
    terrainCfg.strength = cfg.terrainDetailStrength;
    const RuntimeFlags& f = runtimeFlags();
    if (f.terrainDetail)
        terrainCfg.detail = *f.terrainDetail;
    if (f.terrainCliffs)
        terrainCfg.cliffs = *f.terrainCliffs;
    if (f.terrainStrength)
        terrainCfg.strength = *f.terrainStrength;
    terrainCfg.debug = f.terrainDebug;
}

namespace {
bool createTerrainGpu(ID3D11Device* dev, ComPtr<ID3D11Buffer>& cb, ComPtr<ID3D11ShaderResourceView>& noise,
                      ComPtr<ID3D11SamplerState>& sampler) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(terrain::ShaderConsts);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &cb)))
        return false;
    // The noise volume with its whole mip chain (box filtered on the CPU: every level keeps the mean, so distant
    // terrain converges to the picture without detail).
    std::vector<std::vector<uint8_t>> levels;
    levels.push_back(terrain::buildNoiseVolume(terrain::kNoiseSize));
    for (uint32_t size = terrain::kNoiseSize; size > 1; size /= 2)
        levels.push_back(terrain::halveVolume(levels.back(), size));
    D3D11_TEXTURE3D_DESC td{};
    td.Width = td.Height = td.Depth = terrain::kNoiseSize;
    td.MipLevels = (UINT)levels.size();
    td.Format = DXGI_FORMAT_R8G8_UNORM;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(levels.size());
    uint32_t size = terrain::kNoiseSize;
    for (size_t i = 0; i < levels.size(); ++i, size /= 2) {
        init[i].pSysMem = levels[i].data();
        init[i].SysMemPitch = size * terrain::kNoiseChannels;
        init[i].SysMemSlicePitch = size * size * terrain::kNoiseChannels;
    }
    ComPtr<ID3D11Texture3D> texture;
    if (FAILED(dev->CreateTexture3D(&td, init.data(), &texture)) ||
        FAILED(dev->CreateShaderResourceView(texture.Get(), nullptr, &noise)))
        return false;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.MaxAnisotropy = 1;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    return SUCCEEDED(dev->CreateSamplerState(&sd, &sampler));
}
} // namespace

uint32_t D3D11Backend::Impl::prepareTerrain(const GsDrawState& s, const DrawVertex* v, uint32_t count, bool haveTex,
                                            bool texIsTarget, const RT& rt, bool depthMasked, float dsx, float dsy,
                                            PSConsts& k) {
    const bool audit = runtimeFlags().terrainAudit;
    if ((!terrainCfg.any() && !audit && !runtimeFlags().terrainGpuTiming) || !engineWorldOpen || terrainGpu.failed)
        return 0;
    // GSR_TERRAIN_GPU_TIMING also reports the CPU time spent in this function.
    struct CpuTime {
        double* total;
        LARGE_INTEGER start{};
        explicit CpuTime(double* t) : total(t) {
            if (total)
                QueryPerformanceCounter(&start);
        }
        ~CpuTime() {
            if (!total)
                return;
            LARGE_INTEGER end, frequency;
            QueryPerformanceCounter(&end);
            QueryPerformanceFrequency(&frequency);
            *total += 1000.0 * double(end.QuadPart - start.QuadPart) / double(frequency.QuadPart);
        }
    } cpuTime(runtimeFlags().terrainGpuTiming ? &terrainGpu.timedCpuMs : nullptr);
    // The world camera of this engine phase; a mid-world preview viewport replaces sceneCamera for a while.
    const uint32_t need = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    const SceneGuideCamera& camera = (sceneCamera.flags & need) == need ? sceneCamera : waterWorldCamera;
    if ((camera.flags & need) != need || rt.w < 480 || rt.w < camera.frameWidth || rt.h < camera.frameHeight)
        return 0;
    if (s.flags & (DF_SPRITE | DF_LINE | DF_POINT))
        return 0;
    terrain::DrawInfo info;
    info.triangles = true;
    info.textured = (s.flags & DF_TME) && haveTex;
    info.textureIsTarget = texIsTarget;
    info.fst = (s.flags & DF_FST) != 0;
    info.blend = (s.flags & DF_ABE) != 0;
    info.tex0 = s.tex0;
    info.clamp = s.clamp;
    info.test = s.test;
    info.alpha = s.alpha;
    info.zbuf = s.zbuf;
    const bool classified = terrain::classify(info);
    if (classified && runtimeFlags().terrainGpuTiming)
        beginTerrainTiming(); // also with every terrain setting off: the same draws are the baseline
    if (!classified && !audit)
        return 0;

    auto& g = terrainGpu;
    const float key[4] = {dsx, dsy, k.h[0], k.h[1]};
    const bool cameraChanged =
        g.cameraSerial != camera.serial || g.xyoffset != s.xyoffset || std::memcmp(g.key, key, sizeof(key)) != 0;
    if (cameraChanged) {
        terrain::buildUnproject(camera, s.xyoffset, dsx, dsy, k.h[0], k.h[1], g.unproject);
        g.cameraSerial = camera.serial;
        g.xyoffset = s.xyoffset;
        std::memcpy(g.key, key, sizeof(key));
    }
    if (!g.unproject.valid)
        return 0;
    if (audit) {
        auditShadowProjection(s, texIsTarget, depthMasked);
        auditTerrain(s, v, count, classified, depthMasked, g.unproject, dsx, dsy, k);
    }
    if (!classified || !terrainCfg.any())
        return 0;
    if (!g.cb) {
        if (!createTerrainGpu(dev.Get(), g.cb, g.noise, g.sampler)) {
            g.failed = true;
            std::fprintf(stderr, "[gsr] terrain detail unavailable: its GPU objects could not be created\n");
            return 0;
        }
    }
    bool gridChanged = false;
    if (terrainCfg.cliffs && g.gridVersion != residentScene.geometryVersion) {
        // Level geometry changed (a level load): the smooth normals of its height field, as a texture array.
        g.gridVersion = residentScene.geometryVersion;
        gridChanged = true;
        g.grid.Reset();
        const auto& triangles = residentScene.triangles;
        std::vector<uint32_t> land;
        land.reserve(triangles.size());
        for (uint32_t i = 0; i < triangles.size(); ++i)
            if (!triangles[i].water)
                land.push_back(i);
        g.gridInfo = terrain::buildNormalGrid(land.size(),
                                              [&](size_t i, int corner) { return &triangles[land[i]].p[corner].x; });
        if (!g.gridInfo.empty()) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width = g.gridInfo.width;
            td.Height = g.gridInfo.height;
            td.MipLevels = 1;
            td.ArraySize = terrain::kGridSlots;
            td.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            td.SampleDesc.Count = 1;
            td.Usage = D3D11_USAGE_IMMUTABLE;
            td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA init[terrain::kGridSlots]{};
            for (uint32_t slot = 0; slot < terrain::kGridSlots; ++slot) {
                init[slot].pSysMem = g.gridInfo.at(slot, 0, 0);
                init[slot].SysMemPitch = g.gridInfo.width * 16;
            }
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(dev->CreateTexture2D(&td, init, &texture)) ||
                FAILED(dev->CreateShaderResourceView(texture.Get(), nullptr, &g.grid)))
                g.grid.Reset();
        }
        std::fprintf(stderr, "[gsr] terrain normals: %ux%u grid vertices, %u without a slot, from %zu triangles%s\n",
                     g.gridInfo.width, g.gridInfo.height, g.gridInfo.dropped, land.size(),
                     g.grid ? "" : " (none: steep faces use face normals)");
        g.gridInfo.texels = {};
        if (!g.grid)
            g.gridInfo.width = g.gridInfo.height = 0;
    }
    if (cameraChanged || gridChanged || !g.contentValid || !(g.settings == terrainCfg)) {
        terrain::ShaderConsts consts;
        // Derivatives are taken in render pixels. Under the temporal upscaler the world is drawn smaller than it is
        // shown, so the footprint is scaled like the texture LOD bias of world draws; 0.7 keeps the grain crisp on
        // ground seen at a low angle (the volume is filtered isotropically).
        const float gradient = 0.7f * std::clamp(dsx / (float)scale, 0.25f, 1.0f);
        terrain::buildConsts(g.unproject, terrainCfg, gradient, consts, g.grid ? &g.gridInfo : nullptr);
        if (!g.contentValid || std::memcmp(&consts, &g.content, sizeof(consts)) != 0) {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(ctx->Map(g.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                return 0;
            std::memcpy(m.pData, &consts, sizeof(consts));
            ctx->Unmap(g.cb.Get(), 0);
            g.content = consts;
            g.contentValid = true;
        }
        g.settings = terrainCfg;
    }
    uint32_t flags = 0;
    if (terrainCfg.detail || terrainCfg.debug)
        flags |= PF_TERRAIN;
    if (terrainCfg.cliffs && count % 3 == 0) {
        // Texels per world unit of the mapping from above, from the first triangles that give one.
        const float W = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10));
        const float H = (float)(1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10));
        float density = 0, rawDensity = 0;
        for (uint32_t i = 0, tried = 0; i + 2 < count && tried < 8 && density == 0; i += 3) {
            float p[3][3], t[3][2];
            bool ok = true;
            for (unsigned j = 0; j < 3 && ok; ++j) {
                const DrawVertex& d = v[i + j];
                ok = d.q > 1e-12f;
                if (!ok)
                    break;
                terrain::relativeAt(g.unproject, d.x * dsx + k.h[0], d.y * dsy + k.h[1], d.q, p[j]);
                t[j][0] = d.s / d.q * W;
                t[j][1] = d.t / d.q * H;
            }
            if (!ok)
                continue;
            ++tried;
            rawDensity = terrain::texelDensity(p, t);
            density = terrain::sideDensity(rawDensity, (uint32_t)W, (uint32_t)H);
        }
        if (audit) {
            // which layer textures take the side projection: cells per tile, by texture size
            static std::map<std::string, uint64_t> seen;
            static uint64_t printed = 0;
            char text[96];
            std::snprintf(text, sizeof(text), "%ux%u %s cells/tile %.2f tcc=%u", (unsigned)W, (unsigned)H,
                          density > 0 ? "side" : "no-side", rawDensity > 0 ? W / rawDensity / 2048.0f : 0.0f,
                          (unsigned)bitsOf(s.tex0, 34, 1));
            ++seen[text];
            if (frame >= printed + 600) {
                printed = frame;
                for (const auto& [name, n] : seen)
                    std::fprintf(stderr, "[terrain-audit]   layer %s: %llu draws\n", name.c_str(),
                                 (unsigned long long)n);
                seen.clear();
            }
        }
        if (density > 0) {
            flags |= PF_TERRAIN_SIDE;
            k.g[3] = density;
            ++g.sideDraws;
        }
    }
    if (!flags)
        return 0;
    ++g.draws;
    ctx->PSSetConstantBuffers(3, 1, g.cb.GetAddressOf());
    // t2 and t4: t3, s3 and b4 belong to the material maps (gs_material.inc)
    ctx->PSSetShaderResources(2, 1, g.noise.GetAddressOf());
    ctx->PSSetShaderResources(4, 1, g.grid.GetAddressOf());
    ctx->PSSetSamplers(2, 1, g.sampler.GetAddressOf());
    return flags;
}

// GSR_TERRAIN_GPU_TIMING=1: GPU time between the start and the end of every terrain layer draw, summed per frame and
// printed as an average every 300 frames with terrain. The same draws are timed whatever the terrain settings are, so
// toggling a setting inside one run compares like with like.
void D3D11Backend::Impl::beginTerrainTiming() {
    auto& g = terrainGpu;
    if (g.timingOpen) // a draw that returned before its Draw call
        endTerrainTiming();
    if (g.timed.empty())
        g.timed.resize(1024);
    // collect finished queries (oldest first), without waiting
    for (auto& t : g.timed) {
        if (!t.pending || frame < t.frame + 3)
            continue;
        UINT64 a = 0, b = 0;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (ctx->GetData(t.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ctx->GetData(t.begin.Get(), &a, sizeof(a), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK ||
            ctx->GetData(t.end.Get(), &b, sizeof(b), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        t.pending = false;
        if (dj.Disjoint || !dj.Frequency || b < a) {
            ++g.timedLost;
            continue;
        }
        g.timedMs += 1000.0 * double(b - a) / double(dj.Frequency);
        ++g.timedDraws;
    }
    if (!(g.timedSettings == terrainCfg)) { // a setting changed: start a new window, drop what is in flight
        g.timedSettings = terrainCfg;
        g.timedMs = g.timedCpuMs = 0;
        g.timedDraws = g.timedFrames = g.timedLost = 0;
        for (auto& t : g.timed)
            t.pending = false;
    }
    if (g.timedFrame != frame) {
        g.timedFrame = frame;
        if (++g.timedFrames >= 300) {
            std::fprintf(stderr,
                         "[terrain-gpu] detail=%d cliffs=%d: GPU %.3f ms per frame in %.1f terrain draws per frame, "
                         "CPU %.3f ms per frame in prepareTerrain (%llu frames, %llu lost)\n",
                         (int)terrainCfg.detail, (int)terrainCfg.cliffs, g.timedMs / double(g.timedFrames),
                         double(g.timedDraws) / double(g.timedFrames), g.timedCpuMs / double(g.timedFrames),
                         (unsigned long long)g.timedFrames, (unsigned long long)g.timedLost);
            g.timedMs = g.timedCpuMs = 0;
            g.timedDraws = g.timedFrames = g.timedLost = 0;
        }
    }
    auto& t = g.timed[g.timedNext];
    if (t.pending) { // ring full: skip this draw
        ++g.timedLost;
        return;
    }
    if (!t.begin) {
        D3D11_QUERY_DESC qd{D3D11_QUERY_TIMESTAMP, 0};
        D3D11_QUERY_DESC dd{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(dev->CreateQuery(&qd, &t.begin)) || FAILED(dev->CreateQuery(&qd, &t.end)) ||
            FAILED(dev->CreateQuery(&dd, &t.disjoint)))
            return;
    }
    ctx->Begin(t.disjoint.Get());
    ctx->End(t.begin.Get());
    g.timingOpen = true;
}
void D3D11Backend::Impl::endTerrainTiming() {
    auto& g = terrainGpu;
    if (!g.timingOpen)
        return;
    g.timingOpen = false;
    auto& t = g.timed[g.timedNext];
    ctx->End(t.end.Get());
    ctx->End(t.disjoint.Get());
    t.pending = true;
    t.frame = frame;
    g.timedNext = (g.timedNext + 1) % g.timed.size();
}

// GSR_TERRAIN_AUDIT=1, second part: the engine's projected character shadow. It is a draw of its own over the terrain
// (a 128 px 32 bit texture the character was rendered into, clamped, depth writes masked, alpha test GREATER 2), so it
// never takes the terrain flags. This counts, per 300 world frames, in how many frames the guest submitted such a draw
// and whether its texture came from the render target or from memory, next to the terrain settings in force: a shadow
// that comes and goes with the same counts whatever the settings are is not the terrain code's doing.
void D3D11Backend::Impl::auditShadowProjection(const GsDrawState& s, bool textureIsTarget, bool depthMasked) {
    static uint64_t lastFrame = ~0ull, frames = 0, framesWith = 0, fromTarget = 0, fromMemory = 0;
    static bool seenThisFrame = false;
    if (lastFrame != frame) {
        lastFrame = frame;
        seenThisFrame = false;
        if (++frames >= 300) {
            std::fprintf(stderr,
                         "[terrain-audit] shadow projection, detail=%d cliffs=%d: a draw in %llu of %llu world frames "
                         "(texture from the target %llu draws, from memory %llu)\n",
                         (int)terrainCfg.detail, (int)terrainCfg.cliffs, (unsigned long long)framesWith,
                         (unsigned long long)frames, (unsigned long long)fromTarget, (unsigned long long)fromMemory);
            frames = framesWith = fromTarget = fromMemory = 0;
        }
    }
    const bool signature = (s.flags & DF_TME) && !(s.flags & DF_FST) && depthMasked && (s.clamp & 0xf) == 5 &&
                           (s.test & 0xfff) == 0x02d && bitsOf(s.tex0, 20, 6) == 0 && bitsOf(s.tex0, 26, 4) == 7 &&
                           bitsOf(s.tex0, 30, 4) == 7;
    if (!signature)
        return;
    ++(textureIsTarget ? fromTarget : fromMemory);
    if (!seenThisFrame) {
        seenThisFrame = true;
        ++framesWith;
    }
}

// GSR_TERRAIN_AUDIT=1: how well the state signature of classify() separates terrain from everything else. Ground
// truth is geometric and independent of the signature: a terrain vertex lies on the 2048 unit grid in x and z, and a
// terrain triangle spans one cell. Triangles cut by the engine's clipper have off-grid vertices and count as "cut".
void D3D11Backend::Impl::auditTerrain(const GsDrawState& s, const DrawVertex* v, uint32_t count, bool classified,
                                      bool depthMasked, const terrain::Unproject& u, float dsx, float dsy,
                                      const PSConsts& k) {
    struct Row {
        uint64_t draws = 0, triangles = 0, grid = 0, partial = 0;
        double pixels = 0;
    };
    static std::map<std::string, Row> rows;
    static uint64_t lastPrint = 0;
    const bool zte = (s.test >> 16) & 1;
    if (!zte || count % 3 != 0 || !(s.flags & DF_TME) || (s.flags & DF_FST))
        return;
    char name[160];
    std::snprintf(name, sizeof(name), "%s test=%05llx alpha=%02llx tfx=%u tcc=%u clamp=%x abe=%d zmsk=%d fge=%d",
                  classified ? "TERRAIN" : "other  ", (unsigned long long)(s.test & 0xfffff),
                  (unsigned long long)(s.alpha & 0xff), (unsigned)bitsOf(s.tex0, 35, 2),
                  (unsigned)bitsOf(s.tex0, 34, 1), (unsigned)(s.clamp & 0xf), (int)!!(s.flags & DF_ABE),
                  (int)depthMasked, (int)!!(s.flags & DF_FGE));
    Row& row = rows[name];
    ++row.draws;
    for (uint32_t i = 0; i + 2 < count; i += 3) {
        unsigned onGrid = 0;
        bool valid = true;
        for (unsigned j = 0; j < 3; ++j) {
            const DrawVertex& d = v[i + j];
            if (!(d.q > 1e-12f)) {
                valid = false;
                break;
            }
            float rel[3];
            terrain::relativeAt(u, d.x * dsx + k.h[0], d.y * dsy + k.h[1], d.q, rel);
            const double x = rel[0] + u.anchor[0], z = rel[2] + u.anchor[2];
            const double tolerance = 3.0 + 0.002 / d.q; // grows with distance: screen x / y are 1/16 pixel steps
            const double ex = std::abs(x - 2048.0 * std::round(x / 2048.0));
            const double ez = std::abs(z - 2048.0 * std::round(z / 2048.0));
            onGrid += ex < tolerance && ez < tolerance;
        }
        if (!valid)
            continue;
        ++row.triangles;
        row.grid += onGrid == 3;
        row.partial += onGrid == 1 || onGrid == 2;
        row.pixels += 0.5 * std::abs((v[i + 1].x - v[i].x) * (v[i + 2].y - v[i].y) -
                                     (v[i + 2].x - v[i].x) * (v[i + 1].y - v[i].y));
    }
    if (frame >= lastPrint + 600) {
        lastPrint = frame;
        std::fprintf(stderr, "[terrain-audit] frame %llu, classified draws %llu (side projection %llu)\n",
                     (unsigned long long)frame, (unsigned long long)terrainGpu.draws,
                     (unsigned long long)terrainGpu.sideDraws);
        for (const auto& [text, r] : rows)
            std::fprintf(stderr,
                         "[terrain-audit]   %s: draws %llu triangles %llu on-grid %.1f%% part-grid %.1f%% area %.0f\n",
                         text.c_str(), (unsigned long long)r.draws, (unsigned long long)r.triangles,
                         r.triangles ? 100.0 * r.grid / r.triangles : 0.0,
                         r.triangles ? 100.0 * r.partial / r.triangles : 0.0, r.pixels);
        rows.clear();
    }
}

} // namespace gsr
