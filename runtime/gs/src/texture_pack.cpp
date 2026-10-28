#include "gsr/texture_pack.h"
#include "gs_texup.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__EMSCRIPTEN__)
#include <emscripten/fetch.h>
#endif
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace gsr {
namespace {
bool loadImage(const std::string& path, PackImage& image) {
#if defined(__EMSCRIPTEN__)
    if (const char* base = std::getenv("DRAKAN_TEXTURE_URL")) {
        const std::string url = std::string(base) + path.substr(path.find_last_of('/') + 1);
        emscripten_fetch_attr_t attributes;
        emscripten_fetch_attr_init(&attributes);
        std::strcpy(attributes.requestMethod, "GET");
        attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;
        attributes.timeoutMSecs = 10000;
        auto* fetch = emscripten_fetch(&attributes, url.c_str());
        bool loaded = false;
        if (fetch && fetch->status == 200 && fetch->numBytes <= 64u * 1024u * 1024u) {
            std::vector<uint8_t> bytes(fetch->data, fetch->data + fetch->numBytes);
            loaded = texup::readImageBytes(bytes, image.pixels, image.width, image.height);
        }
        if (fetch)
            emscripten_fetch_close(fetch);
        return loaded;
    }
#endif
    return texup::readImageFile(path, image.pixels, image.width, image.height);
}
bool parseHash(const std::string& text, uint64_t& hash) {
    if (text.size() != 16 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c); }))
        return false;
    hash = std::strtoull(text.c_str(), nullptr, 16);
    return true;
}
} // namespace

struct TexturePack::Impl {
    struct Entry {
        std::shared_ptr<PackImage> image;
        uint64_t used = 0;
        bool pending = true;
    };
    std::unordered_map<uint64_t, std::string> files;
    std::unordered_map<uint64_t, uint64_t> aliases;
    std::unordered_set<uint64_t> excluded;
    std::unordered_map<uint64_t, Entry> cache;
    std::mutex mutex;
    size_t budget, bytes = 0, pendingBytes = 0;
    uint64_t clock = 0;
    // Declared last so jobs finish before the data they use is destroyed.
    texup::Pool pool{1};

    explicit Impl(const std::string& directory, size_t limit) : budget(limit) {
        auto names = texup::listDir(directory);
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            if (name.size() != 20)
                continue;
            std::string extension = name.substr(16);
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            uint64_t hash;
            if ((extension == ".png" || extension == ".dds") && parseHash(name.substr(0, 16), hash))
                files[hash] = directory + "/" + name;
        }
        const auto readIndex = [&](const char* file, bool alias) {
            if (auto* input = std::fopen((directory + "/" + file).c_str(), "r")) {
                char line[512];
                while (std::fgets(line, sizeof(line), input)) {
                    const std::string text(line);
                    uint64_t from, to;
                    if (text.size() >= 16 && parseHash(text.substr(0, 16), from)) {
                        if (!alias)
                            excluded.insert(from);
                        else if (text.size() >= 33 && parseHash(text.substr(17, 16), to))
                            aliases[from] = to;
                    }
                }
                std::fclose(input);
            }
        };
        readIndex("alias.txt", true);
        readIndex("exclude.txt", false);
    }
};

TexturePack::TexturePack(const std::string& directory, size_t budgetBytes)
    : impl_(std::make_unique<Impl>(directory, budgetBytes)) {}
TexturePack::~TexturePack() = default;
bool TexturePack::empty() const {
    return impl_->files.empty();
}

std::array<uint64_t, 2> TexturePack::hashes(const std::vector<uint32_t>& original, uint32_t width, uint32_t height) {
    return {texup::hashContent(original.data(), width, height), texup::hashContentRgb(original.data(), width, height)};
}
std::shared_ptr<const PackImage> TexturePack::find(const std::vector<uint32_t>& original, uint32_t width,
                                                   uint32_t height) {
    if (!width || !height || original.size() != size_t(width) * height)
        return {};
    const auto content = hashes(original, width, height);
    return findHashed(original, width, height, content[0], content[1]);
}

std::shared_ptr<const PackImage> TexturePack::findHashed(const std::vector<uint32_t>& original, uint32_t width,
                                                         uint32_t height, uint64_t hash, uint64_t rgb) {
    if (!width || !height || original.size() != size_t(width) * height || empty())
        return {};
    auto& p = *impl_;
    if (p.excluded.count(hash))
        return {};
    auto file = p.files.find(hash);
    bool useGameAlpha = false;
    if (file == p.files.end()) {
        const auto alias = p.aliases.find(rgb);
        file = p.files.find(alias == p.aliases.end() ? rgb : alias->second);
        useGameAlpha = true;
    }
    if (file == p.files.end())
        return {};
    std::lock_guard lock(p.mutex);
    if (const auto found = p.cache.find(hash); found != p.cache.end()) {
        found->second.used = ++p.clock;
        return found->second.image;
    }
    // Bound both completed and in-flight metadata, including broken files.
    if (p.pendingBytes + original.size() * sizeof(uint32_t) > 16u * 1024u * 1024u)
        return {};
    if (p.cache.size() >= 2048) {
        auto oldest = p.cache.end();
        for (auto it = p.cache.begin(); it != p.cache.end(); ++it)
            if (!it->second.pending && (!it->second.image || it->second.image.use_count() == 1) &&
                (oldest == p.cache.end() || it->second.used < oldest->second.used))
                oldest = it;
        if (oldest == p.cache.end())
            return {};
        if (oldest->second.image)
            p.bytes -= oldest->second.image->pixels.size() * sizeof(uint32_t);
        p.cache.erase(oldest);
    }
    p.pendingBytes += original.size() * sizeof(uint32_t);
    p.cache.emplace(hash, Impl::Entry{{}, ++p.clock, true});
    const std::string path = file->second;
    p.pool.submit([&p, hash, path, original, width, height, useGameAlpha] {
        auto image = std::make_shared<PackImage>();
        bool valid = loadImage(path, *image);
        const size_t size = image->pixels.size() * sizeof(uint32_t);
        valid = valid && image->width && image->height && image->width <= 8192 && image->height <= 8192 &&
                image->pixels.size() == size_t(image->width) * image->height && size <= p.budget &&
                std::has_single_bit(image->width) && std::has_single_bit(image->height) && image->width >= width &&
                image->height >= height && image->width % width == 0 && image->height % height == 0 &&
                std::has_single_bit(image->width / width) && std::has_single_bit(image->height / height);
        if (valid) {
            image->scaleX = image->width / width;
            image->scaleY = image->height / height;
        }
        if (valid && useGameAlpha)
            for (uint32_t y = 0; y < image->height; ++y)
                for (uint32_t x = 0; x < image->width; ++x) {
                    const auto alpha = original[size_t(uint64_t(y) * height / image->height) * width +
                                                uint64_t(x) * width / image->width] &
                                       0xff000000u;
                    auto& pixel = image->pixels[size_t(y) * image->width + x];
                    pixel = (pixel & 0x00ffffffu) | alpha;
                }
        std::lock_guard guard(p.mutex);
        bool retry = false;
        if (valid) {
            while (p.bytes + size > p.budget) {
                auto oldest = p.cache.end();
                for (auto it = p.cache.begin(); it != p.cache.end(); ++it)
                    if (it->second.image && it->second.image.use_count() == 1 &&
                        (oldest == p.cache.end() || it->second.used < oldest->second.used))
                        oldest = it;
                if (oldest == p.cache.end()) {
                    valid = false;
                    retry = true;
                    break;
                }
                p.bytes -= oldest->second.image->pixels.size() * sizeof(uint32_t);
                p.cache.erase(oldest);
            }
        }
        p.pendingBytes -= original.size() * sizeof(uint32_t);
        if (retry) {
            p.cache.erase(hash);
            return;
        }
        auto& entry = p.cache.at(hash);
        entry.pending = false;
        if (valid) {
            if (std::getenv("DRAKAN_HD_TEXTURE_LOG"))
                std::fprintf(stderr, "[texture-pack] %s %ux%u -> %ux%u (%zu bytes)\n", texup::hashName(hash).c_str(),
                             width, height, image->width, image->height, size);
            p.bytes += size;
            entry.image = std::move(image);
        }
    });
    return {};
}
} // namespace gsr
