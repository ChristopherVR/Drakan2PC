#include "drakan_save_previews.h"
#if defined(PS2X_HAVE_GSR)
#include "ps2_runtime.h"
#include "Kernel/Stubs/MemoryCard.h"
#include <gsr/gsr.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <windows.h>
#undef min
#undef max

namespace drakan_save_previews {
namespace {
std::mutex mutex;
gsr::Renderer* renderer = nullptr;
std::atomic<uint64_t> revision{0};
uint64_t fingerprint(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size < 16 || size > 32 * 1024 * 1024)
        return 0;
    std::ifstream file(path, std::ios::binary);
    uint64_t hash = 1469598103934665603ull;
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount())
        for (std::streamsize i = 0; i < file.gcount(); ++i)
            hash = (hash ^ static_cast<unsigned char>(buffer[i])) * 1099511628211ull;
    return file.eof() ? hash : 0;
}
std::string keyName(uint64_t key) {
    char s[24];
    std::snprintf(s, sizeof(s), "%016llx", static_cast<unsigned long long>(key));
    return s;
}
void written(const std::filesystem::path& path) {
    // A successful close after writes, never a read/scan or a canceled dialog.
    if (path.extension() != ".rsg" || path.parent_path().filename().string().find("BESCES-50006Slot") != 0)
        return;
    gsr::Image source;
    float aspect = 4.0f / 3.0f;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!renderer || !renderer->captureLastGameplay(source, aspect))
            return;
    }
    const uint64_t key = fingerprint(path);
    if (!key || !source || !std::isfinite(aspect) || aspect <= 0)
        return;
    // The retail thumbnail aperture is 168x96 on the 4:3 GUI. Fit rather
    // than stretch the gameplay view into that physical aperture.
    constexpr unsigned size = 1024;
    constexpr float aperture = 7.0f / 3.0f;
    const float fitX = std::min(1.0f, aspect / aperture), fitY = std::min(1.0f, aperture / aspect);
    const unsigned width = std::max(1u, unsigned(size * fitX)), height = std::max(1u, unsigned(size * fitY));
    const unsigned left = (size - width) / 2, top = (size - height) / 2;
    gsr::Image preview;
    preview.width = preview.height = size;
    preview.rgba.resize(size * size * 4, 0);
    for (size_t n = 3; n < preview.rgba.size(); n += 4)
        preview.rgba[n] = 255;
    // Area sampling: each saved pixel comes from the rendered high resolution
    // frame, not from the PS2's 128x128 screenshot or an artificial upscale.
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            const unsigned x0 = uint64_t(x) * source.width / width,
                           x1 = std::max(x0 + 1, unsigned(uint64_t(x + 1) * source.width / width));
            const unsigned y0 = uint64_t(y) * source.height / height,
                           y1 = std::max(y0 + 1, unsigned(uint64_t(y + 1) * source.height / height));
            uint64_t sum[3]{};
            unsigned count = 0;
            for (unsigned sy = y0; sy < std::min(y1, source.height); ++sy)
                for (unsigned sx = x0; sx < std::min(x1, source.width); ++sx) {
                    const auto* p = source.rgba.data() + (size_t(sy) * source.width + sx) * 4;
                    for (int c = 0; c < 3; ++c)
                        sum[c] += p[c];
                    ++count;
                }
            auto* p = preview.rgba.data() + (size_t(y + top) * size + x + left) * 4;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>((sum[c] + count / 2) / count);
        }
    const auto output = std::filesystem::path(path.string() + "." + keyName(key) + ".preview.png");
    const auto temporary = std::filesystem::path(output.string() + ".tmp.png");
    if (gsr::writePng(temporary.string().c_str(), preview) &&
        MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ++revision;
        std::fprintf(stderr, "[save-preview] saved 1024x1024 companion for %s (source %ux%u)\n",
                     path.filename().string().c_str(), source.width, source.height);
        // Replacing a save replaces its companion too. Keep disk use bounded
        // to one image per save; only this feature's exact generated names.
        const auto prefix = path.filename().string() + ".";
        const std::string suffix = ".preview.png";
        std::error_code ec;
        for (std::filesystem::directory_iterator it(path.parent_path(), ec), end; !ec && it != end; it.increment(ec)) {
            const auto old = it->path();
            const auto name = old.filename().string();
            if (old == output || name.size() != prefix.size() + 16 + suffix.size() ||
                name.compare(0, prefix.size(), prefix) ||
                name.compare(name.size() - suffix.size(), suffix.size(), suffix))
                continue;
            const auto token = name.substr(prefix.size(), 16);
            if (token.find_first_not_of("0123456789abcdef") == std::string::npos)
                std::filesystem::remove(old, ec);
        }
    }
}
} // namespace
void attach(gsr::Renderer* value) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        renderer = value;
    }
    ps2_stubs::setMcFileWrittenCallback(value ? &written : nullptr);
}
uint64_t generation() {
    return revision.load();
}
std::string previewPath(int slot) {
    if (slot < 1 || slot > 32)
        return {};
    const auto& io = PS2Runtime::getIoPaths();
    const auto root = io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
    const auto folder = root / ("BESCES-50006Slot" + std::to_string(slot));
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        const auto path = it->path();
        if (path.extension() != ".rsg")
            continue;
        const auto key = fingerprint(path);
        if (!key)
            continue;
        const auto preview = path.string() + "." + keyName(key) + ".preview.png";
        if (std::filesystem::is_regular_file(preview, ec) && !ec)
            return preview;
    }
    return {};
}
void registerSlot(int slot, const std::vector<uint32_t>& nativePixels) {
    if (slot < 1 || slot > 32 || nativePixels.size() != 128 * 128)
        return;
    if (const std::string preview = previewPath(slot); !preview.empty()) {
        auto pixels = nativePixels;
        for (int orientation = 0; orientation < 2; ++orientation) {
            for (uint32_t alpha : {0u, 0x80000000u}) {
                for (auto& p : pixels)
                    p = (p & 0xffffffu) | alpha;
                gsr::registerSavePreview(gsr::textureContentHash(pixels.data(), 128, 128), preview, orientation != 0);
            }
            for (unsigned y = 0; y < 64; ++y)
                for (unsigned x = 0; x < 128; ++x)
                    std::swap(pixels[y * 128 + x], pixels[(127 - y) * 128 + x]);
        }
        std::fprintf(stderr, "[save-preview] registered companion for slot %d\n", slot);
    }
}
} // namespace drakan_save_previews
#else
namespace drakan_save_previews {
void attach(gsr::Renderer*) {}
uint64_t generation() {
    return 0;
}
void registerSlot(int, const std::vector<uint32_t>&) {}
std::string previewPath(int) {
    return {};
}
} // namespace drakan_save_previews
#endif
