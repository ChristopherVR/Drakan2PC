// Wide menu backdrops: optional pack images (<textureDir>/wide/<name>_<a>x<b>.png) drawn behind the centered native
// 4:3 menu canvas (WebGPU backend; see d3d11_backdrop.cpp and docs/reference/widescreen.md).
#include "wgpu_backend_impl.h"

#include <cstdio>

namespace gsr {

const std::vector<WgpuBackend::Impl::WideBackdrop>& WgpuBackend::Impl::wideBackdropSet(const std::string& name) {
    auto it = wideBackdrops.find(name);
    if (it != wideBackdrops.end())
        return it->second;
    std::vector<WideBackdrop>& set = wideBackdrops[name];
    const std::string dir = texDir + "/wide";
    const std::string prefix = name + "_";
    for (const std::string& file : texup::listDir(dir)) {
        unsigned a = 0, b = 0;
        int used = 0;
        if (file.compare(0, prefix.size(), prefix) != 0 ||
            std::sscanf(file.c_str() + prefix.size(), "%ux%u.png%n", &a, &b, &used) < 2 || !a || !b ||
            prefix.size() + used != file.size())
            continue;
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        if (!texup::readImageFile(dir + "/" + file, px, w, h) || !w || !h || w > gpu->maxTextureSize() ||
            h > gpu->maxTextureSize()) {
            std::fprintf(stderr, "[gsr] wide backdrop '%s' could not be loaded\n", file.c_str());
            continue;
        }
        WideBackdrop bd;
        bd.tex = createTexture(w, h, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "wide backdrop");
        if (!bd.tex)
            continue;
        gpu->writeTexture(*bd.tex, 0, 0, 0, w, h, px.data(), w * 4);
        bd.aspect = (float)a / (float)b;
        set.push_back(std::move(bd));
    }
    if (!set.empty())
        std::fprintf(stderr, "[gsr] wide backdrop '%s': %zu image(s)\n", name.c_str(), set.size());
    return set;
}

// Draws the best-fitting wide image for this GUI screen (falling back to "default") over the whole canvas; the
// screen the host saw when it switched the native menu canvas on is tried before "default" (see d3d11_backdrop.cpp).
bool WgpuBackend::Impl::drawWideBackdrop(RT& rt, int guiScreen) {
    if (!(hudScale > 0 && hudScale < 1))
        return false;
    const float target = (4.0f / 3) / hudScale;
    auto screenName = [](int id) { return id >= 0 ? "screen" + std::to_string(id) : std::string(); };
    const std::string names[3] = {
        screenName(guiScreen), nativeMenuScreen != guiScreen ? screenName(nativeMenuScreen) : std::string(), "default"};
    for (const std::string& name : names) {
        if (name.empty())
            continue;
        const auto& set = wideBackdropSet(name);
        std::vector<float> aspects;
        for (const auto& bd : set)
            aspects.push_back(bd.aspect);
        const int pick = hud::pickWideAspect(aspects.data(), (unsigned)aspects.size(), target);
        if (pick < 0)
            continue;
        const float W = (float)(rt.w * scale), H = (float)(std::min(rt.h, 512u) * scale);
        const auto place = hud::widePlacement(set[pick].aspect, hudScale);
        // Alpha 0x80 like the native canvas.
        const float rect[4] = {place.left * W, 0, (place.left + place.width) * W, H};
        const float alpha[4] = {0, 0, 0, 128.0f / 255.0f};
        drawRect(*rt.tex, rect, set[pick].tex.get(), alpha, {0, 0, (uint32_t)W, (uint32_t)H});
        if (name != "default") {
            wideReplaceFrame = frame;
            wideReplaceRT = &rt;
        }
        return true;
    }
    return false;
}

} // namespace gsr
