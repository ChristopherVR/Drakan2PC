// Wide menu backdrops: optional pack images (<textureDir>/wide/<name>_<a>x<b>.png) drawn behind the centered native
// 4:3 menu canvas (WebGPU backend; see d3d11_backdrop.cpp and docs/reference/widescreen.md).
#include "wgpu_backend_impl.h"
#include "title_backdrop.h"

#include <cstdio>

namespace gsr {

const std::vector<WgpuBackend::Impl::WideBackdrop>& WgpuBackend::Impl::wideBackdropSet(const std::string& name) {
    auto it = wideBackdrops.find(name);
    if (it != wideBackdrops.end())
        return it->second;
    std::vector<WideBackdrop>& set = wideBackdrops[name];
    const std::string dir = texDir + "/wide";
    const std::string prefix = name + "_";
    // The host's title picture (title_backdrop.h): one image of its own aspect.
    const bool picture = title_backdrop::isPicture(name);
    std::vector<std::string> files;
    if (picture)
        files.push_back(title_backdrop::picturePath(name));
    else
        files = texup::listDir(dir);
    for (const std::string& file : files) {
        unsigned a = 0, b = 0;
        int used = 0;
        if (!picture && (file.compare(0, prefix.size(), prefix) != 0 ||
                         std::sscanf(file.c_str() + prefix.size(), "%ux%u.png%n", &a, &b, &used) < 2 || !a || !b ||
                         prefix.size() + used != file.size()))
            continue;
        std::vector<uint32_t> px;
        uint32_t w = 0, h = 0;
        bool read = false;
        if (picture) { // decoded and shrunk ahead on a worker (preloadTitleBackdropImage), else now
            if (const title_backdrop::PicturePtr pic = title_backdrop::picture(file, &title_backdrop::readPicture)) {
                px = pic->px;
                w = pic->w;
                h = pic->h;
                read = true;
            }
        } else {
            read = texup::readImageFile(dir + "/" + file, px, w, h);
        }
        if (!read || !w || !h || w > gpu->maxTextureSize() || h > gpu->maxTextureSize()) {
            std::fprintf(stderr, "[gsr] wide backdrop '%s' could not be loaded\n", file.c_str());
            continue;
        }
        WideBackdrop bd;
        bd.tex = createTexture(w, h, rhi::Sampled | rhi::CopyDst, rhi::Format::RGBA8, 1, "wide backdrop");
        if (!bd.tex)
            continue;
        gpu->writeTexture(*bd.tex, 0, 0, 0, w, h, px.data(), w * 4);
        bd.aspect = picture ? (float)w / (float)h : (float)a / (float)b;
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
    // Loading (screen 14) after the title: the art on screen is the title's, so its wide image comes first.
    const std::string names[4] = {
        guiScreen == 14 && titleBackdropRecent() ? std::string("screen8") : std::string(), screenName(guiScreen),
        nativeMenuScreen != guiScreen ? screenName(nativeMenuScreen) : std::string(), "default"};
    const std::string picture = title_backdrop::get();
    for (const std::string& name : names) {
        if (name.empty())
            continue;
        const std::string setKey = title_backdrop::setName(name, picture);
        const auto& set = wideBackdropSet(setKey);
        std::vector<float> aspects;
        for (const auto& bd : set)
            aspects.push_back(bd.aspect);
        const int pick = hud::pickWideAspect(aspects.data(), (unsigned)aspects.size(), target);
        if (pick < 0)
            continue;
        const float W = (float)(rt.w * scale), H = (float)(std::min(rt.h, 512u) * scale);
        auto place = hud::widePlacement(set[pick].aspect, hudScale);
        float top = 0, height = 1;
        if (title_backdrop::isPicture(setKey)) { // any aspect: fill the output, cropping the long side
            const auto c = title_backdrop::cover(set[pick].aspect, hudScale);
            place = {c.left, c.width};
            top = c.top;
            height = c.height;
        }
        // Alpha 0x80 like the native canvas.
        const float rect[4] = {place.left * W, top * H, (place.left + place.width) * W, (top + height) * H};
        const float alpha[4] = {0, 0, 0, 128.0f / 255.0f};
        drawRect(*rt.tex, rect, set[pick].tex.get(), alpha, {0, 0, (uint32_t)W, (uint32_t)H});
        if (name == "screen8" && guiScreen == 8)
            titleBackdropAt = std::chrono::steady_clock::now();
        if (name != "default") {
            wideReplaceFrame = frame;
            wideReplaceRT = &rt;
        }
        return true;
    }
    return false;
}

} // namespace gsr
