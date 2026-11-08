#pragma once
// A picture that stands in for the disc's title art (the wide `screen8` set) on the title (GUI screen 8) and on the
// loading screen after it, so a campaign such as Order of the Flame shows its own art instead of The Ancients' Gates.
// Set by the host with gsr::setTitleBackdropImage (process wide); both renderers' drawWideBackdrop look it up.
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gsr::title_backdrop {

inline std::mutex& lock() {
    static std::mutex m;
    return m;
}
inline std::string& current() {
    static std::string path;
    return path;
}
inline void set(const std::string& path) {
    std::lock_guard<std::mutex> l(lock());
    current() = path;
}
inline std::string get() {
    std::lock_guard<std::mutex> l(lock());
    return current();
}

inline constexpr char kPrefix[] = "screen8|";
// The wide set to look up for `name`: the picture's own set ("screen8|<path>") while one is set, else `name`.
inline std::string setName(const std::string& name, const std::string& picture) {
    return name == "screen8" && !picture.empty() ? std::string(kPrefix) + picture : name;
}
inline bool isPicture(const std::string& set) {
    return set.rfind(kPrefix, 0) == 0;
}
inline std::string picturePath(const std::string& set) {
    return set.substr(sizeof(kPrefix) - 1);
}

// A player's picture can be far larger than any output (the Order of the Flame art is 7680 x 4800, 147 MB as a
// texture): box-filter it by a whole factor until it is at most maxWidth wide. RGBA8 pixels, row-major.
inline void shrink(std::vector<uint32_t>& px, uint32_t& w, uint32_t& h, uint32_t maxWidth = 3840) {
    const uint32_t f = (w + maxWidth - 1) / maxWidth;
    if (f <= 1 || w / f == 0 || h / f == 0)
        return;
    const uint32_t nw = w / f, nh = h / f;
    std::vector<uint32_t> out(static_cast<size_t>(nw) * nh);
    for (uint32_t y = 0; y < nh; ++y)
        for (uint32_t x = 0; x < nw; ++x) {
            uint32_t sum[4] = {};
            for (uint32_t dy = 0; dy < f; ++dy)
                for (uint32_t dx = 0; dx < f; ++dx) {
                    const uint32_t p = px[static_cast<size_t>(y * f + dy) * w + x * f + dx];
                    for (int c = 0; c < 4; ++c)
                        sum[c] += (p >> (8 * c)) & 0xffu;
                }
            uint32_t q = 0;
            for (int c = 0; c < 4; ++c)
                q |= ((sum[c] + f * f / 2) / (f * f)) << (8 * c);
            out[static_cast<size_t>(y) * nw + x] = q;
        }
    px.swap(out);
    w = nw;
    h = nh;
}

// Decoded pictures, by path. Decoding the 7680 x 4800 Order of the Flame PNG takes long enough that doing it on the
// render thread when the game is chosen left the disc's art on screen meanwhile; the host asks for every campaign's
// picture at start-up (gsr::preloadTitleBackdropImage) and a worker decodes and shrinks it ahead of time.
struct Picture {
    std::vector<uint32_t> px;
    uint32_t w = 0, h = 0;
};
using PicturePtr = std::shared_ptr<const Picture>;
using Decode = bool (*)(const std::string& path, Picture& out);
inline std::map<std::string, std::shared_future<PicturePtr>>& pictures() {
    static std::map<std::string, std::shared_future<PicturePtr>> m;
    return m;
}
inline std::shared_future<PicturePtr> startDecode(const std::string& path, Decode decode, bool worker) {
    std::lock_guard<std::mutex> l(lock());
    auto it = pictures().find(path);
    if (it != pictures().end())
        return it->second;
    auto job = [path, decode]() -> PicturePtr {
        auto p = std::make_shared<Picture>();
        if (!decode(path, *p) || !p->w || !p->h)
            return nullptr;
        shrink(p->px, p->w, p->h);
        return p;
    };
    std::shared_future<PicturePtr> f =
        std::async(worker ? std::launch::async : std::launch::deferred, std::move(job)).share();
    pictures()[path] = f;
    return f;
}
// The renderers' decoder (gs_core.cpp: texup::readImageFile).
bool readPicture(const std::string& path, Picture& out);
// Start decoding on a worker (no-op when already started).
inline void preload(const std::string& path, Decode decode) {
    if (!path.empty())
        (void)startDecode(path, decode, true);
}
// The decoded picture: waits for a preload still running, decodes now when none was asked for; null = unreadable.
inline PicturePtr picture(const std::string& path, Decode decode) {
    return startDecode(path, decode, false).get();
}

// Where an image of `imageAspect` goes so it fills the whole wide output (cropping the long side), as fractions of the
// native canvas width and of its height. aspectScale = (4/3) / output aspect, the canvas spans the full output width.
struct Cover {
    float left, width, top, height;
};
constexpr Cover cover(float imageAspect, float aspectScale) {
    const float width = imageAspect * aspectScale * (3.0f / 4.0f); // at the canvas height
    if (width >= 1.0f)
        return {0.5f - 0.5f * width, width, 0.0f, 1.0f};
    const float height = 1.0f / width; // narrower than the output: full width, cropped top and bottom
    return {0.0f, 1.0f, 0.5f - 0.5f * height, height};
}

} // namespace gsr::title_backdrop
