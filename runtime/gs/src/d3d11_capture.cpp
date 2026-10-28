// Screen/framebuffer capture and opt-in diagnostic dumps.
#include "d3d11_backend_impl.h"

namespace gsr {

void Backend::Impl::validateObjectMotion(RT& rt, const nr::guides::Frame& guide,
                                         const nr::guides::FrameConstants& constants) {
    if (!runtimeFlags().motionValidate || !rt.motionTex || !motionMatchedBatches || constants.reset ||
        motionValidationSamples >= 12 || (motionValidationSamples >= 4 && motionGuideSerial % 30))
        return;
    ++motionValidationSamples;
    D3D11_TEXTURE2D_DESC desc{};
    rt.motionTex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, &staging)))
        return;
    ctx->CopyResource(staging.Get(), rt.motionTex.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)))
        return;
    uint64_t pixels = 0, moving = 0, independent = 0;
    double largest = 0;
    for (uint32_t y = 0; y < guide.guide.height; ++y) {
        const auto* values =
            reinterpret_cast<const float*>(static_cast<const uint8_t*>(map.pData) + size_t(y) * map.RowPitch);
        for (uint32_t x = 0; x < guide.guide.width; ++x) {
            const float* v = values + x * 4;
            if (v[2] < .5f || !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[3]))
                continue;
            const double depth = (v[3] - guide.farGs) / (guide.nearGs - guide.farGs);
            if (depth <= 0 || depth >= 1)
                continue;
            const double px = x + .5 - guide.jitterPixels[0], py = y + .5 - guide.jitterPixels[1];
            const double point[] = {px / guide.guide.width * 2 - 1, 1 - py / guide.guide.height * 2, depth, 1};
            double previous[4]{};
            for (unsigned r = 0; r < 4; ++r)
                for (unsigned c = 0; c < 4; ++c)
                    previous[c] += point[r] * constants.clipToPrevClip[r * 4 + c];
            if (previous[3] <= 1e-12)
                continue;
            const double cameraX = (previous[0] / previous[3] * .5 + .5) * guide.guide.width - px;
            const double cameraY = (.5 - previous[1] / previous[3] * .5) * guide.guide.height - py;
            const double residual = std::hypot(v[0] - cameraX, v[1] - cameraY);
            ++pixels;
            moving += std::abs(v[0]) > .05f || std::abs(v[1]) > .05f;
            independent += residual > .25;
            largest = std::max(largest, residual);
        }
    }
    ctx->Unmap(staging.Get(), 0);
    std::fprintf(stderr,
                 "[object-motion-validation] epoch=%llu pixels=%llu moving=%llu independent=%llu maxResidual=%.4f\n",
                 (unsigned long long)motionEpoch, (unsigned long long)pixels, (unsigned long long)moving,
                 (unsigned long long)independent, largest);
}

// Opt-in, single-frame pipeline evidence. Never read back during normal play.
void Backend::Impl::dumpTemporalStage(const char* stage, ID3D11Texture2D* texture) {
    const char* directory = cstr(runtimeFlags().temporalDumpDir);
    const char* requested = cstr(runtimeFlags().temporalDumpFrame);
    const char* epoch = cstr(runtimeFlags().temporalDumpEpoch);
    static uint64_t selected = UINT64_MAX;
    if (!directory || !*directory || ((!requested || !*requested) && (!epoch || !*epoch)) || !texture)
        return;
    const uint64_t target = std::strtoull(epoch ? epoch : requested, nullptr, 10);
    if (epoch ? motionEpoch != target : frame < target)
        return;
    if (selected == UINT64_MAX)
        selected = frame;
    if (frame != selected)
        return;
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    unsigned bytes = 0;
    switch (desc.Format) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            bytes = 8;
            break;
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            bytes = 4;
            break;
        default:
            return;
    }
    if (desc.SampleDesc.Count != 1 || desc.ArraySize != 1 || desc.MipLevels != 1)
        return;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> copy;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, &copy)))
        return;
    ctx->CopyResource(copy.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(copy.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        return;
    char path[2048];
    std::snprintf(path, sizeof(path), "%s/f%llu-s%llu-%s.raw", directory, (unsigned long long)frame,
                  (unsigned long long)motionGuideSerial, stage);
    if (FILE* file = std::fopen(path, "wb")) {
        const uint32_t header[] = {desc.Width, desc.Height, uint32_t(desc.Format), bytes};
        bool written = std::fwrite(header, sizeof(header), 1, file) == 1;
        for (uint32_t y = 0; y < desc.Height && written; ++y)
            written = std::fwrite(static_cast<const uint8_t*>(mapped.pData) + y * mapped.RowPitch, bytes, desc.Width,
                                  file) == desc.Width;
        written = std::fclose(file) == 0 && written;
        std::fprintf(stderr, "[temporal-dump] %s %ux%u format=%u written=%d\n", path, desc.Width, desc.Height,
                     unsigned(desc.Format), int(written));
    }
    ctx->Unmap(copy.Get(), 0);
}

bool Backend::Impl::readTexture(ID3D11Texture2D* tex, uint32_t w, uint32_t h, Image& out, bool keepAlpha) {
    ensureStaging(w, h);
    D3D11_BOX box{0, 0, 0, w, h, 1};
    ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, tex, 0, &box);
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms)))
        return false;
    out.width = w;
    out.height = h;
    out.rgba.resize((size_t)w * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(out.rgba.data() + (size_t)y * w * 4, (const uint8_t*)ms.pData + (size_t)y * ms.RowPitch, w * 4);
    ctx->Unmap(staging.Get(), 0);
    if (!keepAlpha)
        for (size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = 255;
    return true;
}

bool Backend::Impl::capture(const DisplayState& d, Image& out) {
    if (!ok)
        return false;
    uint32_t w, h;
    renderDisplay(d, w, h);
    return readTexture(dispTex.Get(), dispW, dispH, out);
}

bool Backend::Impl::captureLastGameplay(Image& out, float& aspect) {
    aspect = latchedGameWorld ? gameplayAspect : savePreviewAspect;
    return latchedGameWorld ? readTexture(dispTex.Get(), dispW, dispH, out)
                            : savePreviewTex && readTexture(savePreviewTex.Get(), savePreviewW, savePreviewH, out);
}

bool Backend::Impl::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) {
    if (!ok)
        return false;
    RT* rt = getRT(fbp, fbw, psm, h);
    if (rt->wstate == 1)
        finalizeWorld(*rt);
    syncRT(*rt);
    return readTexture(rt->tex.Get(), std::min(w, rt->w) * scale, std::min(h, rt->h) * scale, out);
}

} // namespace gsr
