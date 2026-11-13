// Images that enter or leave the GS outside its draws: captures (display, last gameplay image, frame buffers), HD
// movie frames drawn over the native frame (docs/reference/fmv.md section 9) and wide menu backdrops
// (<textureDir>/wide/<name>_<a>x<b>.png, docs/reference/widescreen.md). Shared by the hardware backends
// (gs_backend_shared.h): compiled once per backend with GSR_IMPL naming its Impl, after the backend's private header.
//
// Backend interface: the member readTarget, and hooks in namespace backend_hook defined by the backend file before it
// includes this unit (temporary: free functions over the Impl so that this unit needed no new declarations in the
// backends' private headers, to become members of the Impl when the backend state is shared):
//   readImage(s, texture, w, h, out)           the top-left w x h of one of the backend's textures on the CPU
//   maxImageSize(s)                            the largest texture side the backend creates
//   movieProgramReady(s), movieImageLoaded(s)  the movie program exists / the movie texture holds an image
//   createMovieTexture(s, w, h), writeMovieImage(s, image), drawMovieImage(s, rt, x, y, w, h)
//   createBackdropTexture(s, backdrop, px, w, h), backdropProgramReady(s)
//   drawBackdrop(s, rt, backdrop, rect, W, H)  rect = left top width height of the output, in parts of W x H
#include "title_backdrop.h"

#include <cstdio>
#include <cstdlib>

#ifndef GSR_IMPL
#error "define GSR_IMPL (the backend's Impl) and include the backend's private header first"
#endif

namespace gsr {

// ---- captures ----

bool GSR_IMPL::capture(const DisplayState& d, Image& out) {
    if (!ok)
        return false;
    uint32_t w, h;
    renderDisplay(d, w, h);
    return backend_hook::readImage(*this, dispTex, dispW, dispH, out);
}

bool GSR_IMPL::captureLastGameplay(Image& out, float& aspect) {
    aspect = latchedGameWorld ? gameplayAspect : savePreviewAspect;
    return latchedGameWorld
               ? backend_hook::readImage(*this, dispTex, dispW, dispH, out)
               : savePreviewTex && backend_hook::readImage(*this, savePreviewTex, savePreviewW, savePreviewH, out);
}

bool GSR_IMPL::captureFramebuffer(uint32_t fbp, uint32_t fbw, uint32_t psm, uint32_t w, uint32_t h, Image& out) {
    if (!ok)
        return false;
    RT* rt = getRT(fbp, fbw, psm, h);
    if (rt->wstate == 1)
        finalizeWorld(*rt);
    syncRT(*rt);
    return readTarget(*rt, std::min(w, rt->w) * scale, std::min(h, rt->h) * scale, out);
}

// ---- HD movie frames ----

void GSR_IMPL::drawMovieFrame(uint32_t dbp, uint32_t dbw, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              const MovieImage& image, uint64_t imageId) {
    const uint32_t maxSize = backend_hook::maxImageSize(*this);
    if (dbp % 32u || !w || !h || image.rgba.size() < (size_t)image.width * image.height || image.width > maxSize ||
        image.height > maxSize)
        return;
    if (!backend_hook::movieProgramReady(*this))
        return;
    if (movieTexId != imageId || !backend_hook::movieImageLoaded(*this)) {
        if (!movieTex || movieTexW != image.width || movieTexH != image.height) {
            if (!backend_hook::createMovieTexture(*this, image.width, image.height))
                return;
            movieTexW = image.width;
            movieTexH = image.height;
        }
        backend_hook::writeMovieImage(*this, image);
        cpuProfile.uploaded(uint64_t(image.width) * image.height * 4);
        movieTexId = imageId;
    }

    RT* rt = getRT(dbp / 32u, dbw, PSM_CT32, y + h);
    if (!rt || rt->wstate == 1)
        return;
    syncRT(*rt); // rows memory still leads reach the target first, so they cannot overwrite the HD frame later

    // Alpha 0x80 like the IPU's RGB32 output. Bilinear: the frame is resampled to the internal resolution.
    backend_hook::drawMovieImage(*this, *rt, x, y, w, h);
    rt->copyValid = false;
    ++rt->writes;
    ++st.movieFrames;
}

// ---- wide menu backdrops ----

const std::vector<GSR_IMPL::WideBackdrop>& GSR_IMPL::wideBackdropSet(const std::string& name) {
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
    const uint32_t maxSize = backend_hook::maxImageSize(*this);
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
        if (!read || !w || !h || w > maxSize || h > maxSize) {
            std::fprintf(stderr, "[gsr] wide backdrop '%s' could not be loaded\n", file.c_str());
            continue;
        }
        WideBackdrop bd;
        if (!backend_hook::createBackdropTexture(*this, bd, px, w, h))
            continue;
        bd.aspect = picture ? (float)w / (float)h : (float)a / (float)b;
        set.push_back(std::move(bd));
    }
    if (!set.empty())
        std::fprintf(stderr, "[gsr] wide backdrop '%s': %zu image(s)\n", name.c_str(), set.size());
    return set;
}

// Draws the best-fitting wide image for this GUI screen (falling back to "default") over the whole canvas.
// The caller has already cleared the target; false leaves it black.
// The screen's first frames can draw its art before its own GUI packet (the title's handler tick) reaches the renderer,
// so `guiScreen` may still name the previous screen (the logo); the screen the host saw when it switched the native
// menu canvas on is tried next, so the title gets its own image from its first frame instead of one "default" frame.
bool GSR_IMPL::drawWideBackdrop(RT& rt, int guiScreen) {
    if (!(hudScale > 0 && hudScale < 1)) {
        if (static const bool log = std::getenv("GSR_BACKDROP_LOG") != nullptr; log) {
            static int lastGui = -2;
            if (guiScreen != lastGui) {
                lastGui = guiScreen;
                std::fprintf(stderr, "[backdrop] frame %llu: none (hudScale %.2f) gui=%d menu=%d\n",
                             (unsigned long long)frame, (double)hudScale, guiScreen, nativeMenuScreen);
            }
        }
        return false;
    }
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
        if (!backend_hook::backdropProgramReady(*this))
            return false;
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
        const float rect[4] = {place.left, top, place.width, height};
        backend_hook::drawBackdrop(*this, rt, set[pick], rect, W, H);
        if (static const bool log = std::getenv("GSR_BACKDROP_LOG") != nullptr; log) {
            static std::string last;
            const std::string now =
                name + " gui=" + std::to_string(guiScreen) + " menu=" + std::to_string(nativeMenuScreen);
            if (now != last) {
                last = now;
                std::fprintf(stderr, "[backdrop] frame %llu: %s\n", (unsigned long long)frame, now.c_str());
            }
        }
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
