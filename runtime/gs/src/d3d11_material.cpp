// Material detail on Direct3D 11: GPU objects, loading of a texture's material map next to its HD image, and the
// per-draw binding. The table, the eligibility rule, the constants and the packed texel are in material_detail.h
// (backend-neutral, unit tested); the shader fragment is gs_material.inc.
#include "d3d11_backend_impl.h"

namespace gsr {

namespace {
float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v)
        return fallback;
    char* end = nullptr;
    const float f = std::strtof(v, &end);
    return end != v && std::isfinite(f) ? f : fallback;
}
} // namespace

void D3D11Backend::Impl::initMaterial() {
    materialMode = (material::Mode)std::min(cfg.materialDetail, 2u);
    materialSettings.strength = cfg.materialDetailStrength;
    materialSettings.specular = cfg.materialSpecular;
    if (const char* v = std::getenv("GSR_MATERIAL_DETAIL"); v && *v) {
        if (iequals(v, "all") || v[0] == '2')
            materialMode = material::Mode::All;
        else if (iequals(v, "hard") || iequals(v, "on") || v[0] == '1')
            materialMode = material::Mode::Hard;
        else
            materialMode = material::Mode::Off;
    }
    materialSettings.strength = std::clamp(envFloat("GSR_MATERIAL_STRENGTH", materialSettings.strength), 0.0f, 2.0f);
    materialSettings.specular = std::clamp(envFloat("GSR_MATERIAL_SPECULAR", materialSettings.specular), 0.0f, 1.0f);
    materialSettings.occlusion = std::clamp(envFloat("GSR_MATERIAL_OCCLUSION", materialSettings.occlusion), 0.0f, 1.0f);
    materialSettings.debugView = (uint32_t)std::clamp(envFloat("GSR_MATERIAL_DEBUG", 0.0f), 0.0f, 4.0f);
}

void D3D11Backend::Impl::scanMaterials() {
    materialTable.clear();
    if (materialMode == material::Mode::Off)
        return;
    materialTable.load(texDir + "/materials/materials.txt");
    if (upLog || std::getenv("GSR_MATERIAL_LOG"))
        std::fprintf(stderr, "[material] mode %u: %zu materials in %s/materials\n", (unsigned)materialMode,
                     materialTable.size(), texDir.c_str());
}

const material::Record* D3D11Backend::Impl::materialFor(uint64_t packHash) const {
    if (materialMode == material::Mode::Off || materialTable.empty())
        return nullptr;
    const material::Record* r = materialTable.find(packHash);
    return r && (material::classMask(materialMode) & (1u << r->cls)) ? r : nullptr;
}

void D3D11Backend::Impl::loadMaterialLevels(UpDone& d, const std::string& packDir, uint64_t packHash,
                                            const material::Record& record, uint32_t logicalW,
                                            uint32_t logicalH) const {
    std::vector<uint32_t> normal, orm;
    uint32_t nw = 0, nh = 0, ow = 0, oh = 0;
    if (!texup::readImageFile(material::mapPath(packDir, packHash, "n"), normal, nw, nh) || !nw || !nh)
        return;
    // Whole multiples of the logical size only: the map is sampled with the texture's own coordinates.
    if (nw % logicalW || nh % logicalH || nw > 4096 || nh > 4096)
        return;
    if (!texup::readImageFile(material::mapPath(packDir, packHash, "orm"), orm, ow, oh) || ow != nw || oh != nh)
        orm.clear();
    std::vector<uint32_t> level;
    material::packTexels(normal, orm, size_t(nw) * nh, record.roughness, level);
    d.materialW = nw;
    d.materialH = nh;
    d.materialClass = record.cls;
    d.materialLevels.push_back(std::move(level));
    for (uint32_t w = nw, h = nh; w > 1 || h > 1; w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
        std::vector<uint32_t> next;
        material::downsample(d.materialLevels.back(), w, h, next);
        d.materialLevels.push_back(std::move(next));
    }
}

void D3D11Backend::Impl::createMaterialTexture(UpDone& d) const {
    if (d.materialLevels.empty())
        return;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = d.materialW;
    td.Height = d.materialH;
    td.MipLevels = (UINT)d.materialLevels.size();
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<D3D11_SUBRESOURCE_DATA> init(d.materialLevels.size());
    uint32_t w = d.materialW, h = d.materialH;
    for (size_t l = 0; l < d.materialLevels.size(); ++l) {
        init[l].pSysMem = d.materialLevels[l].data();
        init[l].SysMemPitch = w * 4;
        d.materialBytes += uint64_t(w) * h * 4;
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }
    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(dev->CreateTexture2D(&td, init.data(), &texture)) ||
        FAILED(dev->CreateShaderResourceView(texture.Get(), nullptr, &d.materialSrv))) {
        d.materialSrv.Reset();
        d.materialBytes = 0;
    }
    d.materialLevels.clear();
    d.materialLevels.shrink_to_fit();
}

bool D3D11Backend::Impl::ensureMaterialGpu() {
    auto& g = materialGpu;
    if (g.failed)
        return false;
    if (g.psHw && g.psSb && g.cb)
        return true;
    ComPtr<ID3DBlob> hw, sb;
    if (!compile("PSMain", "ps_5_0", "1", hw, true) || !compile("PSMain", "ps_5_0", "0", sb, true) ||
        FAILED(dev->CreatePixelShader(hw->GetBufferPointer(), hw->GetBufferSize(), nullptr, &g.psHw)) ||
        FAILED(dev->CreatePixelShader(sb->GetBufferPointer(), sb->GetBufferSize(), nullptr, &g.psSb))) {
        g.failed = true;
        std::fprintf(stderr, "[material] the material shader could not be built; material detail is off\n");
        return false;
    }
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(material::Consts);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&bd, nullptr, &g.cb))) {
        g.failed = true;
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.MaxAnisotropy = 8;
        sd.AddressU = (i & 1) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = (i & 2) ? D3D11_TEXTURE_ADDRESS_CLAMP : D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        if (FAILED(dev->CreateSamplerState(&sd, &g.sampler[i]))) {
            g.failed = true;
            return false;
        }
    }
    return true;
}

bool D3D11Backend::Impl::prepareMaterial(const GsDrawState& s, ID3D11ShaderResourceView* materialSrv,
                                         uint8_t materialClass, bool haveTex, bool texIsTarget, const RT& rt,
                                         bool depthTested, uint32_t pixelFlags, float dsx, float dsy, bool hwBlend) {
    if (materialMode == material::Mode::Off || !materialSrv || materialGpu.failed)
        return false;
    if (!(material::classMask(materialMode) & (1u << materialClass)))
        return false;
    // A steep terrain face drawn from the side samples its colour with other coordinates than the map's.
    if (pixelFlags & PF_TERRAIN_SIDE)
        return false;
    const uint32_t need = SceneGuideCamera::Valid | SceneGuideCamera::World | SceneGuideCamera::HaveViewport;
    const SceneGuideCamera& camera = (sceneCamera.flags & need) == need ? sceneCamera : waterWorldCamera;
    if ((camera.flags & need) != need || rt.w < 480 || rt.w < camera.frameWidth || rt.h < camera.frameHeight)
        return false;
    material::DrawTraits d;
    d.worldOpen = engineWorldOpen;
    d.textured = haveTex && !texIsTarget;
    d.sprite = (s.flags & (DF_SPRITE | DF_LINE | DF_POINT)) != 0;
    d.fst = (s.flags & DF_FST) != 0;
    d.depthTested = depthTested;
    d.textureFunction = (uint32_t)bitsOf(s.tex0, 35, 2);
    d.wrapS = (uint32_t)bitsOf(s.clamp, 0, 2);
    d.wrapT = (uint32_t)bitsOf(s.clamp, 2, 2);
    // (Cs - 0) * C + Cd adds the texture to the frame: a glow or a light shaft, not a surface.
    d.additive =
        (s.flags & DF_ABE) && bitsOf(s.alpha, 0, 2) == 0 && bitsOf(s.alpha, 2, 2) == 2 && bitsOf(s.alpha, 6, 2) == 1;
    if (!material::eligible(d) || !ensureMaterialGpu())
        return false;
    material::Light light = material::defaultLight();
    if (nativeModels)
        for (const auto& l : nativeModels->lighting.lights)
            if (l.type == SceneLight::Directional) {
                light = material::sunLight(l.vector, l.color, true);
                break;
            }
    material::Consts k;
    const uint32_t tw = 1u << std::min<uint64_t>(bitsOf(s.tex0, 26, 4), 10);
    const uint32_t th = 1u << std::min<uint64_t>(bitsOf(s.tex0, 30, 4), 10);
    if (!material::makeConsts(camera, s.xyoffset, dsx, dsy, light, materialSettings, tw, th, k))
        return false;
    auto& g = materialGpu;
    if (!g.contentValid || std::memcmp(&g.content, &k, sizeof(k)) != 0) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(ctx->Map(g.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
            return false;
        std::memcpy(m.pData, &k, sizeof(k));
        ctx->Unmap(g.cb.Get(), 0);
        g.content = k;
        g.contentValid = true;
    }
    ctx->PSSetShader(hwBlend ? g.psHw.Get() : g.psSb.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(4, 1, g.cb.GetAddressOf());
    ctx->PSSetShaderResources(3, 1, &materialSrv);
    ctx->PSSetSamplers(3, 1, g.sampler[(d.wrapS & 1u) | ((d.wrapT & 1u) << 1)].GetAddressOf());
    ++g.draws;
    return true;
}

} // namespace gsr
