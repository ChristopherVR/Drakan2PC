#include "nr_guides.h"
#include <d3d11sdklayers.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
using namespace gsr::nr::guides;
namespace {
void require(bool b, const std::string& message) {
    if (!b)
        throw std::runtime_error(message);
}
void expectNear(float a, float b, float eps, const char* label) {
    require(std::isfinite(a) && std::abs(a - b) <= eps,
            std::string(label) + ": " + std::to_string(a) + " expected " + std::to_string(b));
}
float half(uint16_t v) {
    return DirectX::PackedVector::XMConvertHalfToFloat(v);
}
uint16_t half(float v) {
    return DirectX::PackedVector::XMConvertFloatToHalf(v);
}
struct Fixture {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11InfoQueue> debug;
    ComPtr<ID3D11Texture2D> color, depth;
    ComPtr<ID3D11ShaderResourceView> colorSrv, depthSrv;
    Frame frame{};
    explicit Fixture(D3D_DRIVER_TYPE driver) {
        D3D_FEATURE_LEVEL got{};
        D3D_FEATURE_LEVEL wanted[] = {D3D_FEATURE_LEVEL_11_0};
        HRESULT hr = D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_DEBUG, wanted, 1,
                                       D3D11_SDK_VERSION, &device, &got, &context);
        if (FAILED(hr))
            hr = D3D11CreateDevice(nullptr, driver, nullptr, 0, wanted, 1, D3D11_SDK_VERSION, &device, &got, &context);
        require(SUCCEEDED(hr), "D3D11 synthetic test device");
        device.As(&debug);
        frame.colorTexture = {11, 9};
        frame.depthTexture = {13, 10};
        frame.frame = {64, 48};
        frame.guide = {7, 5};
        frame.colorX = 2;
        frame.colorY = 3;
        frame.depthX = 4;
        frame.depthY = 2;
        auto& c = frame.current;
        c.valid = true;
        for (int i = 0; i < 4; ++i)
            c.vp[i * 4 + i] = 1;
        c.xyOffset[0] = 2016 * 16;
        c.xyOffset[1] = 2024 * 16;
        c.scale[0] = 32;
        c.scale[1] = -24;
        c.scale[2] = frame.zmax / 16;
        c.offset[0] = 2048;
        c.offset[1] = 2048;
        frame.previous = c;
        std::vector<uint32_t> rgba(11 * 9);
        for (size_t i = 0; i < rgba.size(); ++i)
            rgba[i] = uint32_t(i % 256) | 64u << 8 | 128u << 16 | uint32_t((i * 37) % 256) << 24;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = 11;
        d.Height = 9;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA data{rgba.data(), 11 * 4, 0};
        require(SUCCEEDED(device->CreateTexture2D(&d, &data, &color)), "source RGBA8");
        require(SUCCEEDED(device->CreateShaderResourceView(color.Get(), nullptr, &colorSrv)), "source color SRV");
        d.Width = 13;
        d.Height = 10;
        d.Format = DXGI_FORMAT_R32_FLOAT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<float> z(13 * 10, .5f);
        data = {z.data(), 13 * 4, 0};
        require(SUCCEEDED(device->CreateTexture2D(&d, &data, &depth)), "source depth");
        require(SUCCEEDED(device->CreateShaderResourceView(depth.Get(), nullptr, &depthSrv)), "source depth SRV");
        frame.sourceColor = colorSrv.Get();
        frame.sourceDepth = depthSrv.Get();
    }
    void depthValues(const std::vector<float>& values) {
        context->UpdateSubresource(depth.Get(), 0, nullptr, values.data(), 13 * 4, 0);
    }
    std::vector<uint8_t> read(ID3D11Texture2D* texture, size_t bytesPerPixel) {
        D3D11_TEXTURE2D_DESC d{};
        texture->GetDesc(&d);
        d.Usage = D3D11_USAGE_STAGING;
        d.BindFlags = 0;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        require(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &staging)), "staging texture");
        context->CopyResource(staging.Get(), texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "staging read");
        std::vector<uint8_t> out(size_t(d.Width) * d.Height * bytesPerPixel);
        for (UINT y = 0; y < d.Height; ++y)
            std::memcpy(out.data() + size_t(y) * d.Width * bytesPerPixel,
                        static_cast<uint8_t*>(map.pData) + size_t(y) * map.RowPitch, size_t(d.Width) * bytesPerPixel);
        context->Unmap(staging.Get(), 0);
        return out;
    }
    void checkMotion(Guides& g, float dx, float dy, bool all = false, float zeroTolerance = 0) {
        auto bytes = read(g.textures().motion.texture, 4);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) {
                uint16_t v[2];
                std::memcpy(v, bytes.data() + (y * 7 + x) * 4, 4);
                const bool interior = all || (x > 0 && x < 6 && y > 0 && y < 4);
                require(std::isfinite(half(v[0])) && std::isfinite(half(v[1])), "finite RG16F motion");
                if (interior) {
                    expectNear(half(v[0]), dx, dx == 0 ? zeroTolerance : .003f, "motion X");
                    expectNear(half(v[1]), dy, dy == 0 ? zeroTolerance : .003f, "motion Y");
                }
            }
    }
    void checkDebug() {
        if (!debug) {
            std::cout << "D3D11 debug layer unavailable; functional WARP checks ran\n";
            return;
        }
        const auto count = debug->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T n = 0;
            debug->GetMessage(i, nullptr, &n);
            std::vector<uint8_t> data(n);
            auto* m = reinterpret_cast<D3D11_MESSAGE*>(data.data());
            debug->GetMessage(i, m, &n);
            if (m->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
                throw std::runtime_error(std::string("D3D11 debug: ") + m->pDescription);
        }
        std::cout << "D3D11 debug: no warnings/errors\n";
    }
};
void run(const Shaders& shaders, D3D_DRIVER_TYPE driver) {
    Fixture f(driver);
    Guides g(f.device.Get(), f.context.Get(), &shaders);
    require(g.ready(), g.status());
    std::cout << (driver == D3D_DRIVER_TYPE_WARP ? "WARP" : "hardware") << " offscreen synthetic device\n";
    // Caller state deliberately aliases an input RTV. Isolation must restore it.
    ComPtr<ID3D11RenderTargetView> sentinel;
    require(SUCCEEDED(f.device->CreateRenderTargetView(f.color.Get(), nullptr, &sentinel)), "sentinel RTV");
    ID3D11RenderTargetView* rtv = sentinel.Get();
    f.context->OMSetRenderTargets(1, &rtv, nullptr);
    require(g.prepare(f.frame), g.status());
    require(g.textures().historyReset, "first-frame reset");
    f.checkMotion(g, 0, 0, true);
    ComPtr<ID3D11RenderTargetView> restored;
    f.context->OMGetRenderTargets(1, &restored, nullptr);
    require(restored.Get() == sentinel.Get(), "caller OM state preserved");
    auto t = g.textures();
    DXGI_FORMAT formats[] = {DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R16G16_FLOAT,
                             DXGI_FORMAT_R16G16B16A16_FLOAT};
    Texture textures[] = {t.color, t.depth, t.motion, t.output};
    for (int i = 0; i < 4; ++i) {
        D3D11_TEXTURE2D_DESC d{};
        textures[i].texture->GetDesc(&d);
        require(d.Width == 7 && d.Height == 5 && d.Format == formats[i] && d.MipLevels == 1 && d.ArraySize == 1 &&
                    d.SampleDesc.Count == 1 && textures[i].srv && textures[i].uav,
                "exact-sized four guide resources");
    }
    auto color = f.read(t.color.texture, 8);
    uint16_t c[4];
    std::memcpy(c, color.data(), 8);
    expectNear(half(c[0]), float(3 * 11 + 2) / 255, .001f, "RGBA8 region copy R");
    expectNear(half(c[1]), 64.0f / 255, .001f, "RGBA8 G");
    f.frame.reset = false;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    require(!g.textures().historyReset, "static history");
    f.checkMotion(g, 0, 0, true);
    std::cout << "PASS exact formats/sizes, differing source dimensions/origins, RGBA8 conversion, static zero motion, "
                 "state restoration\n";
    {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = 13;
        d.Height = 10;
        d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
        d.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::vector<float> object(13 * 10 * 4);
        for (size_t i = 0; i < 13 * 10; ++i) {
            object[i * 4] = 3;
            object[i * 4 + 1] = -2;
            object[i * 4 + 2] = 1;
            object[i * 4 + 3] = .5f;
        }
        D3D11_SUBRESOURCE_DATA initial{object.data(), 13 * 16, 0};
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        require(SUCCEEDED(f.device->CreateTexture2D(&d, &initial, &texture)), "object motion source");
        require(SUCCEEDED(f.device->CreateShaderResourceView(texture.Get(), nullptr, &view)), "object motion SRV");
        f.frame.sourceObjectMotion = view.Get();
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 3, -2, true);
        // A foreground layer with different actual Z must not borrow old flow.
        for (size_t i = 0; i < 13 * 10; ++i)
            object[i * 4 + 3] = .25f;
        f.context->UpdateSubresource(texture.Get(), 0, nullptr, object.data(), 13 * 16, 0);
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 0, 0, true);
        // Reverse-Z far surfaces can differ substantially in world distance
        // while their GS depths differ by less than the old absolute epsilon.
        f.depthValues(std::vector<float>(13 * 10, 1e-6f));
        for (size_t i = 0; i < 13 * 10; ++i)
            object[i * 4 + 3] = 1.05e-6f;
        f.context->UpdateSubresource(texture.Get(), 0, nullptr, object.data(), 13 * 16, 0);
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 0, 0, true);
        for (size_t i = 0; i < 13 * 10; ++i)
            object[i * 4 + 3] = std::nextafter(1e-6f, 2e-6f);
        f.context->UpdateSubresource(texture.Get(), 0, nullptr, object.data(), 13 * 16, 0);
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 3, -2, true);
        // A tracked model cannot override near-plane saturation or a cut.
        f.depthValues(std::vector<float>(13 * 10, 1.f));
        for (size_t i = 0; i < 13 * 10; ++i)
            object[i * 4 + 3] = 1.f;
        f.context->UpdateSubresource(texture.Get(), 0, nullptr, object.data(), 13 * 16, 0);
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 0, 0, true);
        f.depthValues(std::vector<float>(13 * 10, .5f));
        for (size_t i = 0; i < 13 * 10; ++i)
            object[i * 4 + 3] = .5f;
        f.context->UpdateSubresource(texture.Get(), 0, nullptr, object.data(), 13 * 16, 0);
        f.frame.reset = true;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 0, 0, true);
        f.frame.reset = false;
        f.frame.sourceObjectMotion = nullptr;
        std::cout << "PASS moving foreground overrides a stationary camera; mismatched object Z rejects stale flow\n";
    }
    {
        // Native CPU-clipped character draws have no GIF motion sidecar. A
        // proven consecutive pose supplies motion only at its visible depth.
        std::vector<ModelMotionVertex> mesh(6);
        const float xy[6][2] = {{-1, -1}, {-1, 1}, {1, 1}, {-1, -1}, {1, 1}, {1, -1}};
        for (unsigned i = 0; i < 6; ++i) {
            mesh[i].current[0] = xy[i][0];
            mesh[i].current[1] = xy[i][1];
            mesh[i].current[2] = .5f;
            std::copy(mesh[i].current, mesh[i].current + 3, mesh[i].previous);
            mesh[i].previous[0] += 2.f / 7;
        }
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        // Raster interpolation (including WARP's subpixel quantization) uses
        // the same .003-pixel tolerance on both axes. Camera/reset checks
        // still require exact zero.
        require(g.modelMotion(mesh, {}), g.status());
        f.checkMotion(g, 1, 0, true, .003f);
        f.frame.jitterPixels[0] = 1;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, {}), g.status());
        f.checkMotion(g, 1, 0, false, .003f);
        f.frame.jitterPixels[0] = 0;
        for (auto& v : mesh)
            v.current[2] = .25f;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, {}), g.status());
        f.checkMotion(g, 0, 0, true);
        std::vector<uint32_t> alpha(128 * 128, 0x00ffffff);
        for (auto& v : mesh) {
            v.current[2] = .5f;
            v.alphaTexture = 0;
        }
        const ModelMotionTexture tile{0, alpha};
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, {&tile, 1}), g.status());
        f.checkMotion(g, 0, 0, true);
        std::fill(alpha.begin(), alpha.end(), 0x80ffffff);
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, {&tile, 1}), g.status());
        f.checkMotion(g, 1, 0, true, .003f);
        // Material visibility changes must not repeatedly allocate the array.
        // Reordered opaque/cutout slots must still upload the correct contents.
        std::vector<uint32_t> transparent(128 * 128, 0x00ffffff);
        std::array<ModelMotionTexture, 3> tiles{{{101, alpha}, {102, transparent}, {103, alpha}}};
        for (auto& v : mesh)
            v.alphaTexture = 2;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, tiles), g.status());
        f.checkMotion(g, 1, 0, true, .003f);
        const auto allocations = g.modelAlphaAllocations();
        for (unsigned step = 0; step < 12; ++step) {
            std::swap(tiles[0], tiles[1]);
            for (auto& v : mesh)
                v.alphaTexture = 0;
            ++f.frame.frameIndex;
            require(g.prepare(f.frame), g.status());
            require(g.modelMotion(mesh, {tiles.data(), step % 3 + 1}), g.status());
            f.checkMotion(g, tiles[0].identity == 102 ? 0 : 1, 0, true, .003f);
        }
        require(g.modelAlphaAllocations() == allocations, "material visibility must reuse alpha capacity");
        std::cout << "PASS varying material counts and reordered alpha slots without reallocation\n";
        f.frame.reset = true;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        require(g.modelMotion(mesh, {&tile, 1}), g.status());
        f.checkMotion(g, 0, 0, true);
        f.frame.reset = false;
        std::cout << "PASS native posed-model motion, depth occlusion, alpha cutouts, jitter and cuts\n";
    }
    // Prior camera translates clip X by 2/7 and Y by -2/5: +1,+1 guide px.
    f.frame.previous.vp[12] = 2.0f / 7;
    f.frame.previous.vp[13] = -2.0f / 5;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 1, 1);
    f.frame.reset = true;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    std::cout << "PASS known camera shift (+1,+1 pixels), reset zero motion\n";
    // Sky and corrupt depth: no NaNs in depth/motion; saturation has no motion.
    std::vector<float> z(13 * 10, .5f);
    const float invalid[] = {0, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1, 2,
                             1};
    for (int i = 0; i < 6; ++i)
        z[2 * 13 + 4 + i] = invalid[i];
    f.depthValues(z);
    f.frame.reset = false;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    auto depth = f.read(g.textures().depth.texture, 4), motion = f.read(g.textures().motion.texture, 4);
    for (int i = 0; i < 6; ++i) {
        float v;
        std::memcpy(&v, depth.data() + i * 4, 4);
        expectNear(v, i == 5 ? 1.0f : 0.0f, 0, "invalid/sky depth");
        uint16_t mv[2];
        std::memcpy(mv, motion.data() + i * 4, 4);
        expectNear(half(mv[0]), 0, 0, "invalid/sky MV X");
        expectNear(half(mv[1]), 0, 0, "invalid/sky MV Y");
    }
    // RGB-only composition must retain all 256 possible byte alpha values.
    std::vector<uint16_t> nr(7 * 5 * 4);
    for (size_t i = 0; i < nr.size(); i += 4) {
        nr[i] = half(.8f);
        nr[i + 1] = half(.3f);
        nr[i + 2] = half(.1f);
        nr[i + 3] = half(0.f);
    }
    auto output = g.textures().output.texture;
    f.context->UpdateSubresource(output, 0, nullptr, nr.data(), 7 * 8, 0);
    D3D11_TEXTURE2D_DESC td{};
    td.Width = 7;
    td.Height = 5;
    td.MipLevels = td.ArraySize = td.SampleDesc.Count = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> dest;
    require(SUCCEEDED(f.device->CreateTexture2D(&td, nullptr, &dest)), "composition destination");
    ComPtr<ID3D11RenderTargetView> destRtv;
    require(SUCCEEDED(f.device->CreateRenderTargetView(dest.Get(), nullptr, &destRtv)), "composition RTV");
    for (unsigned base = 0; base < 256; base += 35) {
        std::vector<uint32_t> pixels(35);
        for (unsigned i = 0; i < 35; ++i)
            pixels[i] = ((base + i) % 256) << 24 | 0x123456;
        f.context->UpdateSubresource(dest.Get(), 0, nullptr, pixels.data(), 7 * 4, 0);
        require(g.composeRgb(destRtv.Get()), g.status());
        auto result = f.read(dest.Get(), 4);
        for (unsigned i = 0; i < 35; ++i) {
            require(result[i * 4 + 3] == uint8_t((base + i) % 256), "bit-exact original alpha");
            require(std::abs(int(result[i * 4]) - 204) <= 1, "NR RGB composition");
        }
    }
    f.context->OMGetRenderTargets(1, restored.ReleaseAndGetAddressOf(), nullptr);
    require(restored.Get() == sentinel.Get(), "composition state preserved");
    std::cout << "PASS sky/NaN/Inf/saturation guards; composition preserves all 256 original alpha bytes\n";
    // Non-finite HDR inputs cannot overflow RGBA16F or leak NaNs downstream.
    D3D11_TEXTURE2D_DESC hdrDesc{};
    hdrDesc.Width = 11;
    hdrDesc.Height = 9;
    hdrDesc.MipLevels = hdrDesc.ArraySize = hdrDesc.SampleDesc.Count = 1;
    hdrDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    hdrDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    std::vector<float> hdrPixels(11 * 9 * 4, .5f);
    const size_t base = (3 * 11 + 2) * 4;
    hdrPixels[base] = std::numeric_limits<float>::quiet_NaN();
    hdrPixels[base + 1] = std::numeric_limits<float>::infinity();
    hdrPixels[base + 2] = 1e30f;
    hdrPixels[base + 3] = -1e30f;
    ComPtr<ID3D11Texture2D> hdrTex;
    ComPtr<ID3D11ShaderResourceView> hdrSrv;
    D3D11_SUBRESOURCE_DATA hdrData{hdrPixels.data(), 11 * 16, 0};
    require(SUCCEEDED(f.device->CreateTexture2D(&hdrDesc, &hdrData, &hdrTex)), "HDR source");
    require(SUCCEEDED(f.device->CreateShaderResourceView(hdrTex.Get(), nullptr, &hdrSrv)), "HDR source SRV");
    f.frame.sourceColor = hdrSrv.Get();
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    auto sanitized = f.read(g.textures().color.texture, 8);
    uint16_t hdr[4];
    std::memcpy(hdr, sanitized.data(), 8);
    expectNear(half(hdr[0]), 0, 0, "NaN color sanitized");
    expectNear(half(hdr[1]), 0, 0, "Inf color sanitized");
    expectNear(half(hdr[2]), 65504, 0, "positive half overflow bounded");
    expectNear(half(hdr[3]), -65504, 0, "negative half overflow bounded");
    f.frame.sourceColor = f.colorSrv.Get();
    std::cout << "PASS HDR NaN/Inf/half-range sanitization\n";
    // Exact engine-style infinite reverse-Z, including FTOI4 factor and GS offset.
    z.assign(13 * 10, .5f);
    f.depthValues(z);
    f.frame.current = {};
    auto& cam = f.frame.current;
    cam.valid = true;
    cam.vp[0] = -.9f;
    cam.vp[5] = -1.7f;
    cam.vp[11] = -1;
    cam.vp[14] = 32;
    cam.scale[0] = -256;
    cam.scale[1] = 256;
    cam.scale[2] = 268431360;
    cam.offset[0] = cam.offset[1] = 2048;
    cam.xyOffset[0] = cam.xyOffset[1] = 1792 * 16;
    f.frame.frame = {512, 512};
    f.frame.nearGs = 16 * cam.scale[2] / f.frame.zmax;
    f.frame.farGs = 0;
    f.frame.previous = cam;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    float view[16]{};
    for (int i = 0; i < 4; ++i)
        view[i * 4 + i] = 1;
    FrameConstants constants;
    require(g.frameConstants(view, constants), g.status());
    expectNear(constants.currentViewToClip[0], .9f, 1e-6f, "D3D projection X");
    expectNear(constants.currentViewToClip[5], 1.7f, 1e-6f, "D3D projection Y");
    expectNear(constants.currentViewToClip[14], 32, 1e-5f, "GS normalized near coefficient");
    expectNear(constants.nearPlane, 32, 1e-4f, "derived near");
    // Independent frustum ratio: width=2*n/.9, height=2*n/1.7.
    // This intentionally differs from both 7/5 guide and 512/512 frame aspect.
    expectNear(constants.aspectRatio, 1.7f / .9f, 1e-6f, "view-space projection aspect");
    require(constants.infiniteFar && constants.depthInverted, "exact infinite reverse-Z metadata");
    expectNear(constants.cameraForward[2], -1, 0, "negative Z camera forward");
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    // Independent world point (-10, 20, -100): CPU project through VU+GS,
    // then through derived D3D row-vector projection, including viewport offset.
    const double point[4] = {-10, 20, -100, 1};
    double clip[4]{};
    for (int j = 0; j < 4; ++j)
        for (int k = 0; k < 4; ++k)
            clip[j] += point[k] * constants.currentViewToClip[k * 4 + j];
    const double rawClipX = -.9 * point[0], rawClipY = -1.7 * point[1], distance = 100;
    const double gx = 2048 - 1792 - 256 * rawClipX / distance, gy = 2048 - 1792 + 256 * rawClipY / distance;
    expectNear(float(clip[0] / clip[3]), float(2 * gx / 512 - 1), 1e-6f, "independent VU/GS X projection");
    expectNear(float(clip[1] / clip[3]), float(1 - 2 * gy / 512), 1e-6f, "independent VU/GS Y projection");
    expectNear(float(clip[2] / clip[3]), .32f, 1e-6f, "independent infinite depth");
    std::cout << "PASS GS viewport including FTOI4 Z, row-vector projection, infinite far, camera basis, no anamorphic "
                 "aspect assumption\n";
    // Camera displacement must vary with reconstructed depth (not a screen
    // offset approximation). Use independent view-space projection arithmetic.
    for (float d : {.25f, .5f, .75f}) {
        z.assign(13 * 10, d * f.frame.nearGs);
        f.depthValues(z);
        f.frame.previous = cam;
        f.frame.previous.vp[12] = cam.vp[0] * 10;
        f.frame.previous.vp[13] = cam.vp[5] * 3;
        ++f.frame.frameIndex;
        require(g.prepare(f.frame), g.status());
        f.checkMotion(g, 7 * .5f * .9f * 10 * d / 32, -5 * .5f * 1.7f * 3 * d / 32);
        auto depths = f.read(g.textures().depth.texture, 4);
        float actual{};
        std::memcpy(&actual, depths.data(), 4);
        expectNear(actual, d, 1e-6f, "normalized GS depth");
    }
    // Pure yaw, non-commuting projection/rotation; independently reconstruct a
    // world point from each current pixel and project it into the prior view.
    z.assign(13 * 10, .5f * f.frame.nearGs);
    f.depthValues(z);
    const float angle = .025f, co = std::cos(angle), si = std::sin(angle);
    f.frame.previous = cam;
    f.frame.previous.vp[0] = cam.vp[0] * co;
    f.frame.previous.vp[3] = si;
    f.frame.previous.vp[8] = cam.vp[0] * si;
    f.frame.previous.vp[11] = -co;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    auto yaw = f.read(g.textures().motion.texture, 4);
    for (int y = 1; y < 4; ++y)
        for (int x = 1; x < 6; ++x) {
            const double wx = ((x + .5) / 7 * 2 - 1) * 64 / .9, wy = (1 - (y + .5) / 5 * 2) * 64 / 1.7, wz = -64;
            const double px = co * wx + si * wz, py = wy, pz = -si * wx + co * wz;
            const float dx = static_cast<float>((.9 * px / -pz * .5 + .5) * 7 - (x + .5));
            const float dy = static_cast<float>((.5 - 1.7 * py / -pz * .5) * 5 - (y + .5));
            uint16_t mv[2];
            std::memcpy(mv, yaw.data() + (y * 7 + x) * 4, 4);
            expectNear(half(mv[0]), dx, .003f, "perspective yaw X");
            expectNear(half(mv[1]), dy, .003f, "perspective yaw Y");
        }
    // Behind-camera, out-of-frame, excessive motion and discontinuous frame
    // indices must never yield a finite-looking but unusable motion vector.
    f.frame.previous = cam;
    f.frame.previous.vp[11] = 1;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    f.frame.previous = cam;
    f.frame.previous.vp[12] = 1e6f;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    f.frame.previous = cam;
    f.frame.previous.vp[12] = cam.vp[0] * 10;
    f.frame.maxMotionPixels = .1f;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    f.frame.maxMotionPixels = 16384;
    f.frame.frameIndex += 3;
    require(g.prepare(f.frame), g.status());
    require(g.textures().historyReset, "frame discontinuity reset");
    f.checkMotion(g, 0, 0, true);
    f.frame.previous = cam;
    f.frame.jitterPixels[0] = .3f;
    f.frame.jitterPixels[1] = -.2f;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    f.frame.jitterPixels[0] = f.frame.jitterPixels[1] = 0;
    std::cout << "PASS perspective depth-dependent translation, yaw, exact static/jitter-only zeros, "
                 "behind/out-of-frame/large-motion bounds, frame continuity\n";
    // Engine-sized world coordinates: float VP construction followed by double
    // decomposition must not turn an infinite camera into bogus finite metadata.
    const Frame saved = f.frame;
    float largeView[16]{};
    const float lc = std::cos(.7f), ls = std::sin(.7f);
    const float eye[3] = {449110.5f, 171693.03125f, 346415.09375f};
    largeView[0] = lc;
    largeView[2] = -ls;
    largeView[5] = 1;
    largeView[8] = ls;
    largeView[10] = lc;
    largeView[15] = 1;
    largeView[12] = -(eye[0] * lc + eye[2] * ls);
    largeView[13] = -eye[1];
    largeView[14] = -(-eye[0] * ls + eye[2] * lc);
    float largeVP[16]{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                largeVP[i * 4 + j] += largeView[i * 4 + k] * saved.current.vp[k * 4 + j];
    std::copy(largeVP, largeVP + 16, f.frame.current.vp);
    f.frame.previous = f.frame.current;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    f.checkMotion(g, 0, 0, true);
    FrameConstants largeConstants;
    require(g.frameConstants(largeView, largeConstants), g.status());
    expectNear(largeConstants.nearPlane, 32, .01f, "large-world near plane");
    require(largeConstants.infiniteFar, "large-world infinite far");
    for (int i = 0; i < 3; ++i)
        expectNear(largeConstants.cameraPosition[i], eye[i], .125f, "large-world camera position");
    f.frame = saved;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    std::cout << "PASS large-world float camera, inverse and infinite-far metadata\n";
    // Singular/NaN current cameras fail closed; invalid previous resets safely.
    f.frame.previous.valid = false;
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    require(g.textures().historyReset, "missing previous reset");
    f.checkMotion(g, 0, 0, true);
    f.frame.current.vp[0] = std::numeric_limits<float>::quiet_NaN();
    require(!g.prepare(f.frame), "NaN camera rejected");
    require(!g.textures().color.texture, "failed preparation not exposed");
    f.frame.current.vp[0] = 0;
    require(!g.prepare(f.frame), "singular camera rejected");
    f.frame.current = cam; // cam aliases current, restore explicitly below
    f.frame.current.vp[0] = -.9f;
    f.frame.guide = {5, 3};
    ++f.frame.frameIndex;
    require(g.prepare(f.frame), g.status());
    require(g.textures().historyReset && g.textures().size.width == 5, "resize reset");
    g.reset();
    require(!g.textures().color.texture, "explicit reset invalidates handles");
    f.checkDebug();
    std::cout << "PASS invalid/missing camera, resize and explicit reset\n";
}
// The SIMD model-vertex projection must return what the scalar reference returns, bit for bit, including the
// rejection of non-finite inputs, out-of-range results and invalid material slots.
void modelProjectionMatchesReference() {
    uint32_t seed = 4242;
    auto random = [&] {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / float(1u << 24);
    };
    std::vector<uint32_t> pixels(128 * 128, 0x80ffffffu), shortTexels(16);
    const ModelMotionTexture textures[] = {{1, pixels}, {2, shortTexels}};
    unsigned accepted = 0, rejected = 0;
    for (unsigned round = 0; round < 400; ++round) {
        double cur[16], prev[16];
        for (unsigned i = 0; i < 16; ++i) {
            cur[i] = (random() - .5) * (round % 50 == 7 ? 1e29 : 4.0);
            prev[i] = (random() - .5) * 4.0 + cur[i] * 1e-3;
        }
        std::vector<ModelMotionVertex> vertices(3 * (1 + unsigned(random() * 40)));
        for (auto& v : vertices) {
            for (unsigned c = 0; c < 3; ++c) {
                v.current[c] = (random() - .5f) * 20000;
                v.previous[c] = v.current[c] + (random() - .5f) * 10;
            }
            v.uv[0] = random();
            v.uv[1] = random();
            v.alphaTexture = random() < .3f ? 0u : ~0u;
        }
        if (round % 9 == 3)
            vertices[vertices.size() / 2].current[unsigned(random() * 3)] = std::numeric_limits<float>::infinity();
        if (round % 13 == 5)
            vertices.back().previous[1] = std::numeric_limits<float>::quiet_NaN();
        if (round % 17 == 2)
            vertices.front().alphaTexture = 1; // wrong texel count
        if (round % 19 == 4)
            vertices.front().alphaTexture = 5; // out of range
        std::vector<detail::ModelGpuVertex> simd(vertices.size()), reference(vertices.size());
        std::memset(simd.data(), 0, simd.size() * sizeof(simd[0]));
        std::memset(reference.data(), 0, reference.size() * sizeof(reference[0]));
        const char* a = detail::projectModelVertices(vertices, textures, cur, prev, simd.data());
        const char* b = detail::projectModelVerticesReference(vertices, textures, cur, prev, reference.data());
        require((a == nullptr) == (b == nullptr) && (!a || std::strcmp(a, b) == 0), "projection status matches");
        if (!a) {
            require(std::memcmp(simd.data(), reference.data(), simd.size() * sizeof(simd[0])) == 0,
                    "projected vertices match bit for bit");
            ++accepted;
        } else
            ++rejected;
    }
    require(accepted > 200 && rejected > 50, "projection test covers accepted and rejected inputs");
    std::cout << "PASS SIMD model projection equals the scalar reference (" << accepted << " accepted, " << rejected
              << " rejected inputs)\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        Shaders shaders;
        std::string status;
        require(compileShaders(shaders, status), status);
        const std::filesystem::path path = argc > 2 ? argv[2] : ".";
        std::filesystem::create_directories(path);
        const std::pair<const char*, const std::vector<uint8_t>*> binaries[] = {{"prepare.cso", &shaders.prepare},
                                                                                {"compose_vs.cso", &shaders.vertex},
                                                                                {"compose_ps.cso", &shaders.compose}};
        for (auto& b : binaries) {
            std::ofstream f(path / b.first, std::ios::binary);
            f.write(reinterpret_cast<const char*>(b.second->data()), static_cast<std::streamsize>(b.second->size()));
            require(bool(f), "write offline shader binary");
        }
        std::ofstream(path / "nr_guides.hlsl") << shaderSource();
        modelProjectionMatchesReference();
        std::cout << "PASS offline cs_5_0/vs_5_0/ps_5_0 compilation, warnings as errors\n";
        if (argc < 2 || std::strcmp(argv[1], "--compile-only") != 0)
            run(shaders, argc > 1 && std::strcmp(argv[1], "--test-hardware") == 0 ? D3D_DRIVER_TYPE_HARDWARE
                                                                                  : D3D_DRIVER_TYPE_WARP);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
