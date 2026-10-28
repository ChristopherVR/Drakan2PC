#include "gs_gpu_raster.h"
#include "portable_gpu.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/ps2_gs_memory.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <unordered_map>

namespace gsr::gpu {
// Weak ownership validates identity without pinning the decoded/HD CPU caches.
// Expired images are uploaded again, including recycled pointer values.
struct RasterTextureCache {
    struct Entry {
        std::weak_ptr<const gsr::PackImage> image;
        unsigned offset;
    };
    std::unordered_map<const gsr::PackImage*, Entry> images;
    unsigned words = 0;
};
} // namespace gsr::gpu

namespace gs_gpu {
namespace {
std::atomic<uint64_t> jobs{0};
void putFloat(uint32_t* destination, float value) {
    *destination = std::bit_cast<uint32_t>(value);
}
} // namespace
uint64_t completedJobs() {
    return jobs.load();
}
bool supported(const GSPrimitiveBatch& batch) {
    const auto& s = batch.state;
    const auto& c = s.context;
    if (batch.vertexCount < 2 || (s.prim.type != GS_PRIM_TRIANGLE && s.prim.type != GS_PRIM_TRISTRIP &&
                                  s.prim.type != GS_PRIM_TRIFAN && s.prim.type != GS_PRIM_SPRITE))
        return false;
    // The CPU reference currently ignores AA1 for these primitives. Accepting
    // that flag preserves its coverage; this does not implement GS antialiasing.
    if ((c.frame.psm != GS_PSM_CT32 && c.frame.psm != GS_PSM_CT24) ||
        ((!c.zbuf.zmask || ((c.test >> 17) & 3) >= 2) && c.zbuf.psm != GS_PSM_Z32 && c.zbuf.psm != GS_PSM_Z24) ||
        s.scanmsk)
        return false;
    const unsigned count = s.prim.type == GS_PRIM_SPRITE ? 2 : 3;
    for (unsigned i = 0; i < count; ++i) {
        const auto& v = batch.vertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.s) ||
            !std::isfinite(v.t) || !std::isfinite(v.q) || v.z < 0 || v.z > UINT32_MAX || std::floor(v.z) != v.z)
            return false;
    }
    if (s.prim.tme) {
        const unsigned modeU = c.clamp & 3, modeV = (c.clamp >> 2) & 3;
        const auto bounded = [](unsigned mode, unsigned low, unsigned high, unsigned size) {
            return mode <= 1 || (mode == 2 ? low <= high && high < size : (low | high) < size);
        };
        // Region modes are safe when every addressed texel is in the decoded
        // image; outside-region reads can still observe other guest pages.
        if (!s.textureWidth || !s.textureHeight ||
            !bounded(modeU, (c.clamp >> 4) & 1023, (c.clamp >> 14) & 1023, s.textureWidth) ||
            !bounded(modeV, (c.clamp >> 24) & 1023, (c.clamp >> 34) & 1023, s.textureHeight))
            return false;
    }
    return true;
}
bool rasterize(std::span<const Draw> draws, std::array<int, 4> bounds, uint8_t* vram, gsr::gpu::Compute* compute,
               HighResolutionSurface* surface) {
    if (draws.empty() || !draws.front().primitive || bounds[0] < 0 || bounds[2] < 0 || bounds[1] < bounds[0] ||
        bounds[3] < bounds[2])
        return false;
    thread_local std::unique_ptr<gsr::gpu::Compute> owned;
    thread_local bool attempted = false;
    if (!compute && !attempted) {
        owned = gsr::gpu::createCompute();
        attempted = true;
    }
    auto* device = compute ? compute : owned.get();
    if (!device)
        return false;
    static const bool profiling = std::getenv("DRAKAN_FRAME_METRICS") != nullptr;
    using Clock = std::chrono::steady_clock;
    const auto begin = profiling ? Clock::now() : Clock::time_point{};
    const unsigned scale = surface ? surface->scale : 1;
    if (scale != 1 && scale != 3 && scale != 5)
        return false;
    const auto& target = draws.front().primitive->state.context;
    const bool sameSurface = surface && surface->storedScale == scale && surface->frameBase == target.frame.fbp * 32 &&
                             surface->frameWidth == std::max(1u, target.frame.fbw) && !surface->pixels.empty();
    gsr::gpu::Job job;
    const unsigned nativeWidth = unsigned(bounds[1] - bounds[0] + 1);
    const unsigned nativeHeight = unsigned(bounds[3] - bounds[2] + 1);
    if (nativeWidth > 2048 || nativeHeight > 2048)
        return false;
    job.width = nativeWidth * scale;
    job.height = nativeHeight * scale;
    if (uint64_t(job.width) * job.height > 2048 * 2048)
        return false;
    // The 4x4 experiment increased bin traffic without improving live frame
    // pacing. Retain 8x8 bins; wire jobs still carry their explicit bin size.
    constexpr unsigned kTileShift = 3, kTileSize = 1u << kTileShift;
    const unsigned tilesX = (nativeWidth + kTileSize - 1) / kTileSize;
    const unsigned tilesY = (nativeHeight + kTileSize - 1) / kTileSize;
    job.draws.resize(8 + draws.size() * gsr::gpu::kDrawWords);
    job.draws[0] = job.width;
    job.draws[1] = job.height;
    job.draws[2] = bounds[0];
    job.draws[3] = bounds[2];
    job.draws[4] = tilesX;
    job.draws[5] = static_cast<unsigned>(draws.size());
    job.draws[6] = kTileShift | (scale == 1 ? 0 : scale << 8);
    const auto tileBounds = [&](const Draw& item) {
        return std::array<int, 4>{std::max(0, (item.bounds[0] - bounds[0]) / int(kTileSize)),
                                  std::min(int(tilesX) - 1, (item.bounds[1] - bounds[0]) / int(kTileSize)),
                                  std::max(0, (item.bounds[2] - bounds[2]) / int(kTileSize)),
                                  std::min(int(tilesY) - 1, (item.bounds[3] - bounds[2]) / int(kTileSize))};
    };
    struct TextureKey {
        const uint32_t* data;
        size_t count;
        bool operator==(const TextureKey&) const = default;
    };
    struct TextureHash {
        size_t operator()(const TextureKey& key) const {
            return std::hash<const uint32_t*>{}(key.data) ^ (key.count * size_t(0x9e3779b9));
        }
    };
    std::unordered_map<TextureKey, unsigned, TextureHash> textureOffsets;
    std::vector<std::span<const uint32_t>> uniqueTextures;
    std::vector<std::shared_ptr<const gsr::PackImage>> textureOwners;
    std::vector<unsigned> counts(tilesX * tilesY), cursors(counts.size());
    size_t textureWords = 0, auxiliaryWords = 0, references = 0;
    for (const auto& item : draws) {
        if (!item.primitive || !supported(*item.primitive) || item.bounds[0] < bounds[0] ||
            item.bounds[1] > bounds[1] || item.bounds[2] < bounds[2] || item.bounds[3] > bounds[3] ||
            item.bounds[0] > item.bounds[1] || item.bounds[2] > item.bounds[3])
            return false;
        if (item.primitive->state.prim.tme) {
            if (item.texture.size() != size_t(item.width) * item.height)
                return false;
            if (textureOffsets.try_emplace({item.texture.data(), item.texture.size()}, unsigned(textureWords)).second) {
                uniqueTextures.push_back(item.texture);
                // Only validated immutable leases may survive this dispatch.
                textureOwners.push_back(item.image && item.image->pixels.data() == item.texture.data() &&
                                                item.image->pixels.size() == item.texture.size()
                                            ? item.image
                                            : nullptr);
                textureWords += item.texture.size();
            }
            if (item.primitive->state.prim.type == GS_PRIM_SPRITE)
                auxiliaryWords += size_t(item.bounds[1] - item.bounds[0] + item.bounds[3] - item.bounds[2] + 2) * scale;
        }
        const auto tile = tileBounds(item);
        for (int y = tile[2]; y <= tile[3]; ++y)
            for (int x = tile[0]; x <= tile[1]; ++x) {
                ++counts[unsigned(y) * tilesX + unsigned(x)];
                ++references;
            }
    }
    constexpr size_t kStorageWords = (128u * 1024u * 1024u) / sizeof(uint32_t);
    if (textureWords + auxiliaryWords > kStorageWords || counts.size() * 2 + references > kStorageWords)
        return false;
    if (!device->rasterTextures)
        device->rasterTextures = std::make_shared<gsr::gpu::RasterTextureCache>();
    auto& residency = *device->rasterTextures;
    const auto resident = [&](const std::shared_ptr<const gsr::PackImage>& image) {
        const auto found = residency.images.find(image.get());
        return image && found != residency.images.end() && found->second.image.lock() == image;
    };
    size_t missingWords = auxiliaryWords;
    for (size_t i = 0; i < uniqueTextures.size(); ++i)
        if (!resident(textureOwners[i]))
            missingWords += uniqueTextures[i].size();
    // Reset as a unit when the bounded atlas fills. Every source needed by this
    // job is then uploaded again before any command can refer to its offset.
    if (residency.words + missingWords > kStorageWords) {
        residency.images.clear();
        residency.words = 0;
        missingWords = textureWords + auxiliaryWords;
    }
    job.textureWords = unsigned(kStorageWords);
    job.textures.reserve(std::min(textureWords + auxiliaryWords, missingWords));
    const auto upload = [&](unsigned offset, std::span<const uint32_t> image) {
        job.textureWrites.push_back(offset);
        job.textureWrites.push_back(unsigned(image.size()));
        job.textures.insert(job.textures.end(), image.begin(), image.end());
    };
    // Allocate persistent entries first; transient uploads never consume the
    // next job's persistent allocation space.
    for (size_t i = 0; i < uniqueTextures.size(); ++i) {
        const auto& owner = textureOwners[i];
        if (!owner)
            continue;
        if (!resident(owner)) {
            residency.images.insert_or_assign(owner.get(), gsr::gpu::RasterTextureCache::Entry{owner, residency.words});
            upload(residency.words, uniqueTextures[i]);
            residency.words += unsigned(uniqueTextures[i].size());
        }
        textureOffsets[{uniqueTextures[i].data(), uniqueTextures[i].size()}] = residency.images.at(owner.get()).offset;
    }
    unsigned transientEnd = residency.words;
    for (size_t i = 0; i < uniqueTextures.size(); ++i)
        if (!textureOwners[i]) {
            textureOffsets[{uniqueTextures[i].data(), uniqueTextures[i].size()}] = transientEnd;
            upload(transientEnd, uniqueTextures[i]);
            transientEnd += unsigned(uniqueTextures[i].size());
        }
    const size_t auxiliaryStart = job.textures.size();
    job.bins.resize(counts.size() * 2 + references);
    unsigned offset = unsigned(counts.size() * 2);
    for (unsigned i = 0; i < counts.size(); ++i) {
        job.bins[2 * i] = cursors[i] = offset;
        job.bins[2 * i + 1] = counts[i];
        offset += counts[i];
    }
    for (unsigned index = 0; index < draws.size(); ++index) {
        const auto& item = draws[index];
        const auto& batch = *item.primitive;
        if (!supported(batch))
            return false;
        const auto& s = batch.state;
        const auto& c = s.context;
        auto* out = job.draws.data() + 8 + index * gsr::gpu::kDrawWords;
        for (unsigned i = 0; i < 4; ++i)
            out[i] = item.bounds[i];
        out[4] = unsigned(s.prim.iip) | (unsigned(s.prim.tme) << 1) | (unsigned(s.prim.fge) << 2) |
                 (unsigned(s.prim.abe) << 3) | (unsigned(s.pabe) << 4) | (unsigned(s.prim.fst) << 5) |
                 (unsigned(s.prim.type == GS_PRIM_SPRITE) << 6) | (unsigned(item.replacement) << 7) |
                 (unsigned(s.prim.type == GS_PRIM_SPRITE &&
                           (batch.vertices[0].x == batch.vertices[1].x || batch.vertices[0].y == batch.vertices[1].y))
                  << 8);
        out[5] = uint32_t(c.test);
        out[6] = uint32_t(c.alpha);
        out[7] = (c.alpha >> 32) & 255;
        out[8] = c.frame.fbmsk;
        out[9] = uint32_t(s.fogR) | (uint32_t(s.fogG) << 8) | (uint32_t(s.fogB) << 16);
        out[10] = c.fba & 1;
        if (s.prim.tme)
            out[11] = textureOffsets.at({item.texture.data(), item.texture.size()});
        out[12] = item.width;
        out[13] = item.height;
        out[14] = s.textureWidth;
        out[15] = s.textureHeight;
        out[16] = c.clamp & 3;
        out[17] = (c.clamp >> 2) & 3;
        out[18] = (c.clamp >> 4) & 1023;
        out[19] = (c.clamp >> 14) & 1023;
        out[20] = (c.clamp >> 24) & 1023;
        out[21] = (c.clamp >> 34) & 1023;
        out[22] = c.zbuf.zmask;
        out[23] = c.frame.psm;
        out[24] = c.zbuf.psm;
        out[25] = c.tex0.tfx;
        out[26] = c.tex0.tcc;
        out[27] = s.linearFilter;
        out[28] = item.scaleX;
        out[29] = item.scaleY;
        for (unsigned i = 0; i < batch.vertexCount; ++i) {
            const auto& v = batch.vertices[i];
            auto* p = out + 32 + 8 * i;
            putFloat(p, v.x - float(c.xyoffset.ofx >> 4));
            putFloat(p + 1, v.y - float(c.xyoffset.ofy >> 4));
            p[2] = uint32_t(v.z);
            p[3] = uint32_t(v.r) | (uint32_t(v.g) << 8) | (uint32_t(v.b) << 16) | (uint32_t(v.a) << 24);
            putFloat(p + 4, s.prim.fst ? float(v.u) : v.s);
            putFloat(p + 5, s.prim.fst ? float(v.v) : v.t);
            putFloat(p + 6, v.q);
            putFloat(p + 7, v.fog);
            if (s.prim.type == GS_PRIM_SPRITE) {
                putFloat(p, float(int(v.x) - int(c.xyoffset.ofx >> 4)));
                putFloat(p + 1, float(int(v.y) - int(c.xyoffset.ofy >> 4)));
                const float q = std::fabs(v.q) > 1e-8f ? v.q : 1.0f;
                putFloat(p + 4, s.prim.fst ? float(v.u >> 4) : (v.s / q) * s.textureWidth);
                putFloat(p + 5, s.prim.fst ? float(v.v >> 4) : (v.t / q) * s.textureHeight);
            }
        }
        const auto& v = batch.vertices;
        if (s.prim.type == GS_PRIM_SPRITE && s.prim.tme) {
            const int loX = std::min(int(v[0].x), int(v[1].x)) - int(c.xyoffset.ofx >> 4);
            const int loY = std::min(int(v[0].y), int(v[1].y)) - int(c.xyoffset.ofy >> 4);
            const int spanX = std::max(1, std::abs(int(v[1].x) - int(v[0].x)));
            const int spanY = std::max(1, std::abs(int(v[1].y) - int(v[0].y)));
            const auto coordinate = [&](bool vertical, int pixel, unsigned sample) {
                const float size = vertical ? s.textureHeight : s.textureWidth;
                const auto endpoint = [&](const GSVertex& a) {
                    if (s.prim.fst)
                        return float((vertical ? a.v : a.u) >> 4);
                    const float q = std::fabs(a.q) > 1e-8f ? a.q : 1.0f;
                    return ((vertical ? a.t : a.s) / q) * size;
                };
                const float first = endpoint(v[0]), last = endpoint(v[1]);
                const float offset = float(int(sample) - int(scale / 2)) / float(scale);
                const float fraction =
                    (float(pixel - (vertical ? loY : loX)) + 0.5f + offset) / float(vertical ? spanY : spanX);
                const float interpolated = first + (last - first) * fraction;
                if (s.prim.fst)
                    return float(std::clamp(int(interpolated * 16.0f + 0.5f), 0, 65535)) / 16.0f;
                return (interpolated / size) * size;
            };
            if (job.textures.size() +
                    (size_t(item.bounds[1] - item.bounds[0] + 1) + size_t(item.bounds[3] - item.bounds[2] + 1)) *
                        scale >
                (128u * 1024u * 1024u) / sizeof(uint32_t))
                return false;
            out[30] = transientEnd + unsigned(job.textures.size() - auxiliaryStart);
            for (int x = item.bounds[0]; x <= item.bounds[1]; ++x)
                for (unsigned sample = 0; sample < scale; ++sample)
                    job.textures.push_back(std::bit_cast<uint32_t>(coordinate(false, x, sample)));
            out[31] = transientEnd + unsigned(job.textures.size() - auxiliaryStart);
            for (int y = item.bounds[2]; y <= item.bounds[3]; ++y)
                for (unsigned sample = 0; sample < scale; ++sample)
                    job.textures.push_back(std::bit_cast<uint32_t>(coordinate(true, y, sample)));
        }
        const float x0 = v[0].x - float(c.xyoffset.ofx >> 4), y0 = v[0].y - float(c.xyoffset.ofy >> 4);
        const float x1 = v[1].x - float(c.xyoffset.ofx >> 4), y1 = v[1].y - float(c.xyoffset.ofy >> 4);
        const float x2 = v[2].x - float(c.xyoffset.ofx >> 4), y2 = v[2].y - float(c.xyoffset.ofy >> 4);
        const float denom = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
        putFloat(out + 56, x2);
        putFloat(out + 57, y2);
        putFloat(out + 58, y1 - y2);
        putFloat(out + 59, x2 - x1);
        putFloat(out + 60, y2 - y0);
        putFloat(out + 61, x0 - x2);
        putFloat(out + 62, std::fabs(denom) >= 0.001f ? (denom < 0 ? -1.0f : 1.0f) : 0.0f);
        putFloat(out + 63, std::fabs(denom) >= 0.001f ? 1.0f / std::fabs(denom) : 0.0f);
        const auto tile = tileBounds(item);
        for (int y = tile[2]; y <= tile[3]; ++y)
            for (int x = tile[0]; x <= tile[1]; ++x)
                job.bins[cursors[unsigned(y) * tilesX + unsigned(x)]++] = index;
    }
    if (job.textures.size() > auxiliaryStart) {
        job.textureWrites.push_back(transientEnd);
        job.textureWrites.push_back(unsigned(job.textures.size() - auxiliaryStart));
    }
    job.pixels.resize(size_t(job.width) * job.height * 2);
    const auto& c = draws.front().primitive->state.context;
    const auto frame = c.frame.fbp * 32, bw = std::max(1u, c.frame.fbw);
    const auto depthDraw = std::find_if(draws.begin(), draws.end(), [](const Draw& draw) {
        const auto& context = draw.primitive->state.context;
        return !context.zbuf.zmask || ((context.test >> 17) & 3) >= 2;
    });
    const bool usesDepth = depthDraw != draws.end();
    const auto depth =
        usesDepth ? depthDraw->primitive->state.context.zbuf.zbp * 32 : (sameSurface ? surface->depthBase : 0);
    const auto depthPsm =
        usesDepth ? unsigned(depthDraw->primitive->state.context.zbuf.psm) : (sameSurface ? surface->depthPsm : 0);
    const auto packed = profiling ? Clock::now() : Clock::time_point{};
    const bool retain = sameSurface;
    const unsigned oldWidth = retain ? unsigned(surface->bounds[1] - surface->bounds[0] + 1) * scale : 0;
    for (unsigned y = 0; y < nativeHeight; ++y)
        for (unsigned x = 0; x < nativeWidth; ++x) {
            const unsigned gx = x + bounds[0], gy = y + bounds[2];
            const auto color = GSMem::ReadCT32(vram, frame, bw, gx, gy);
            const auto z = usesDepth ? GSMem::ReadZ32(vram, depth, bw, gx, gy) : 0;
            const bool inside = retain && int(gx) >= surface->bounds[0] && int(gx) <= surface->bounds[1] &&
                                int(gy) >= surface->bounds[2] && int(gy) <= surface->bounds[3];
            const size_t old =
                inside ? (size_t(gy - surface->bounds[2]) * scale * oldWidth + (gx - surface->bounds[0]) * scale) * 2
                       : 0;
            const size_t centre = old + (size_t(scale / 2) * oldWidth + scale / 2) * 2;
            const size_t nativeIndex =
                inside ? size_t(gy - surface->bounds[2]) * (oldWidth / scale) + gx - surface->bounds[0] : 0;
            const bool keepColor = inside && (surface->validChannels & surface->valid[nativeIndex] & 1) &&
                                   surface->pixels[centre] == color;
            const bool keepDepth = inside && (surface->validChannels & surface->valid[nativeIndex] & 2) &&
                                   surface->depthBase == depth && surface->depthPsm == depthPsm &&
                                   (!usesDepth || surface->pixels[centre + 1] == z);
            for (unsigned sy = 0; sy < scale; ++sy)
                for (unsigned sx = 0; sx < scale; ++sx) {
                    const size_t offset = (size_t(y * scale + sy) * job.width + x * scale + sx) * 2;
                    const size_t previous = old + (size_t(sy) * oldWidth + sx) * 2;
                    job.pixels[offset] = keepColor ? surface->pixels[previous] : color;
                    job.pixels[offset + 1] = keepDepth ? surface->pixels[previous + 1] : z;
                }
        }
    const auto read = profiling ? Clock::now() : Clock::time_point{};
    if (!device->run(job)) {
        if (surface)
            surface->clear();
        device->rasterTextures.reset();
        if (!compute)
            owned.reset(); // Preserve the CPU fallback after device loss.
        return false;
    }
    const auto computed = profiling ? Clock::now() : Clock::time_point{};
    for (unsigned y = 0; y < nativeHeight; ++y)
        for (unsigned x = 0; x < nativeWidth; ++x) {
            const size_t offset = (size_t(y * scale + scale / 2) * job.width + x * scale + scale / 2) * 2;
            GSMem::WriteCT32(vram, frame, bw, x + bounds[0], y + bounds[2], job.pixels[offset]);
            if (usesDepth)
                GSMem::WriteZ32(vram, depth, bw, x + bounds[0], y + bounds[2], job.pixels[offset + 1]);
        }
    if (surface) {
        auto retainedBounds = bounds;
        if (sameSurface) {
            retainedBounds = {std::min(bounds[0], surface->bounds[0]), std::max(bounds[1], surface->bounds[1]),
                              std::min(bounds[2], surface->bounds[2]), std::max(bounds[3], surface->bounds[3])};
            if (uint64_t(retainedBounds[1] - retainedBounds[0] + 1) * (retainedBounds[3] - retainedBounds[2] + 1) *
                    scale * scale >
                2048u * 2048u)
                retainedBounds = bounds;
        }
        // Dispatch only the changed rectangle. Retaining earlier presentation
        // samples must not turn a tiny HUD update into a full-frame GPU job.
        const size_t retainedWidth = size_t(retainedBounds[1] - retainedBounds[0] + 1);
        const size_t retainedHeight = size_t(retainedBounds[3] - retainedBounds[2] + 1);
        // A complete replacement already owns a tightly packed result. For a
        // partial update with unchanged bounds, reuse the retained allocation.
        const bool replacesAll = retainedBounds == bounds;
        const bool reusesStorage = !replacesAll && sameSurface && retainedBounds == surface->bounds;
        std::vector<uint32_t> retained;
        std::vector<uint8_t> validity;
        if (replacesAll) {
            retained = std::move(job.pixels);
            validity.assign(retainedWidth * retainedHeight, uint8_t(3));
        } else if (reusesStorage) {
            retained = std::move(surface->pixels);
            validity = std::move(surface->valid);
        } else {
            retained.resize(retainedWidth * retainedHeight * scale * scale * 2);
            validity.resize(retainedWidth * retainedHeight, 0);
        }
        const size_t retainedPitch = retainedWidth * scale;
        if (!replacesAll && sameSurface && retainedBounds[0] <= surface->bounds[0] &&
            retainedBounds[1] >= surface->bounds[1] && retainedBounds[2] <= surface->bounds[2] &&
            retainedBounds[3] >= surface->bounds[3]) {
            const size_t dx = size_t(surface->bounds[0] - retainedBounds[0]),
                         dy = size_t(surface->bounds[2] - retainedBounds[2]);
            const size_t rows = size_t(surface->bounds[3] - surface->bounds[2] + 1);
            if (!reusesStorage) {
                for (size_t y = 0; y < rows * scale; ++y)
                    std::copy_n(surface->pixels.begin() + y * oldWidth * 2, oldWidth * 2,
                                retained.begin() + ((dy * scale + y) * retainedPitch + dx * scale) * 2);
                for (size_t y = 0; y < rows; ++y)
                    std::copy_n(surface->valid.begin() + y * (oldWidth / scale), oldWidth / scale,
                                validity.begin() + (dy + y) * retainedWidth + dx);
            }
            const uint8_t retainedChannels =
                surface->validChannels & (surface->depthBase != depth || surface->depthPsm != depthPsm ? 1 : 3);
            if (retainedChannels != 3)
                for (auto& bits : validity)
                    bits &= retainedChannels;
        }
        const size_t dx = size_t(bounds[0] - retainedBounds[0]), dy = size_t(bounds[2] - retainedBounds[2]);
        if (!replacesAll) {
            for (size_t y = 0; y < job.height; ++y)
                std::copy_n(job.pixels.begin() + y * job.width * 2, job.width * 2,
                            retained.begin() + ((dy * scale + y) * retainedPitch + dx * scale) * 2);
            for (size_t y = 0; y < nativeHeight; ++y)
                std::fill_n(validity.begin() + (dy + y) * retainedWidth + dx, nativeWidth, uint8_t(3));
        }
        surface->bounds = retainedBounds;
        surface->frameBase = frame;
        surface->frameWidth = bw;
        surface->depthBase = depth;
        surface->depthPsm = depthPsm;
        surface->storedScale = scale;
        surface->pixels = std::move(retained);
        surface->valid = std::move(validity);
        surface->validChannels = 3;
    }
    if (profiling) {
        const auto end = Clock::now();
        struct Profile {
            Clock::time_point last = Clock::now();
            uint64_t jobs = 0, draws = 0, textureBytes = 0, binWords = 0;
            double packMs = 0, readMs = 0, computeMs = 0, writeMs = 0;
        };
        static thread_local Profile profile;
        const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
        ++profile.jobs;
        profile.draws += draws.size();
        profile.textureBytes += job.textures.size() * 4;
        profile.binWords += job.bins.size();
        profile.packMs += ms(packed - begin);
        profile.readMs += ms(read - packed);
        profile.computeMs += ms(computed - read);
        profile.writeMs += ms(end - computed);
        if (end - profile.last >= std::chrono::seconds(2)) {
            std::fprintf(stderr,
                         "[portable-gpu-times] wallMs=%.3f jobs=%llu draws=%llu textureMiB=%.3f binWords=%llu "
                         "packMs=%.3f readMs=%.3f computeMs=%.3f writeMs=%.3f\n",
                         ms(end - profile.last), static_cast<unsigned long long>(profile.jobs),
                         static_cast<unsigned long long>(profile.draws), double(profile.textureBytes) / 1048576.0,
                         static_cast<unsigned long long>(profile.binWords), profile.packMs, profile.readMs,
                         profile.computeMs, profile.writeMs);
            profile = {};
        }
    }
    const auto completed = jobs.fetch_add(1) + 1;
    if ((completed == 1 || (completed & 255) == 0) && std::getenv("DRAKAN_FRAME_METRICS"))
        std::fprintf(stderr, "[portable-gpu] completedJobs=%llu draws=%zu extent=%ux%u textureBytes=%zu\n",
                     static_cast<unsigned long long>(completed), draws.size(), job.width, job.height,
                     job.textures.size() * 4);
    return true;
}

namespace {
constexpr uint64_t kVramBytes = GSMem::MEMORY_SIZE;
MemoryRange targetRange(const GSContext& context, const std::array<int, 4>& bounds, bool depth) {
    const uint64_t start = uint64_t(depth ? context.zbuf.zbp : context.frame.fbp) * 8192;
    const uint64_t pages = uint64_t(bounds[3] / 32) * std::max(1u, context.frame.fbw) + bounds[1] / 64 + 1;
    return {start, start + pages * 8192};
}
bool overlap(MemoryRange a, MemoryRange b) {
    return a.begin < a.end && b.begin < b.end && a.begin < b.end && b.begin < a.end;
}
bool depthUsed(const GSContext& context) {
    return !context.zbuf.zmask || ((context.test >> 17) & 3) >= 2;
}
bool sameTarget(const GSContext& a, const GSContext& b) {
    return a.frame.fbp == b.frame.fbp && a.frame.fbw == b.frame.fbw && a.frame.psm == b.frame.psm;
}
} // namespace
MemoryRange memoryRange(unsigned psm, unsigned base, unsigned width, unsigned x, unsigned y, unsigned countX,
                        unsigned countY) {
    if (!countX || !countY)
        return {};
    unsigned pageWidth = 64, pageHeight = 32;
    switch (psm) {
        case GS_PSM_T8:
            pageWidth = 128;
            pageHeight = 64;
            break;
        case GS_PSM_T4:
            pageWidth = pageHeight = 128;
            break;
        case GS_PSM_CT16:
        case GS_PSM_CT16S:
        case GS_PSM_Z16:
        case GS_PSM_Z16S:
            pageHeight = 64;
            break;
        case GS_PSM_CT32:
        case GS_PSM_CT24:
        case GS_PSM_Z32:
        case GS_PSM_Z24:
        case GS_PSM_T8H:
        case GS_PSM_T4HL:
        case GS_PSM_T4HH:
            break;
        default:
            return {0, kVramBytes};
    }
    const uint64_t start = uint64_t(base / 32) * 8192;
    const uint64_t maxX = uint64_t(x) + countX - 1, maxY = uint64_t(y) + countY - 1;
    // Include the unaligned first block and all preceding rows conservatively.
    const uint64_t pages =
        (maxY / pageHeight) * (uint64_t(std::max(1u, width)) * 64 / pageWidth) + maxX / pageWidth + 2;
    const uint64_t end = start + pages * 8192;
    return end > kVramBytes ? MemoryRange{0, kVramBytes} : MemoryRange{start, end};
}
unsigned configuredScale() {
    const char* value = std::getenv("DRAKAN_GPU_SCALE");
    const unsigned scale = value ? unsigned(std::strtoul(value, nullptr, 10)) : 1;
    return scale == 3 || scale == 5 ? scale : 1;
}
namespace {
MemoryRange surfaceRange(const HighResolutionSurface& surface, bool depth) {
    GSContext context{};
    context.frame.fbp = surface.frameBase / 32;
    context.frame.fbw = surface.frameWidth;
    context.zbuf.zbp = surface.depthBase / 32;
    return targetRange(context, surface.bounds, depth);
}
void invalidateRect(HighResolutionSurface& surface, std::array<int, 4> bounds, uint8_t bits) {
    bits &= surface.validChannels;
    if (!bits)
        return;
    const int x0 = std::max(bounds[0], surface.bounds[0]), x1 = std::min(bounds[1], surface.bounds[1]);
    const int y0 = std::max(bounds[2], surface.bounds[2]), y1 = std::min(bounds[3], surface.bounds[3]);
    if (x0 == surface.bounds[0] && x1 == surface.bounds[1] && y0 == surface.bounds[2] && y1 == surface.bounds[3]) {
        surface.validChannels &= uint8_t(~bits);
        return;
    }
    const size_t width = size_t(surface.bounds[1] - surface.bounds[0] + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            surface.valid[size_t(y - surface.bounds[2]) * width + x - surface.bounds[0]] &= uint8_t(~bits);
}
} // namespace
uint32_t presentationSample(const HighResolutionSurface* surface, unsigned x, unsigned y, unsigned subX, unsigned subY,
                            uint32_t native, unsigned psm, bool preserveAlpha) {
    uint32_t color = native;
    if (surface && int(x) >= surface->bounds[0] && int(x) <= surface->bounds[1] && int(y) >= surface->bounds[2] &&
        int(y) <= surface->bounds[3]) {
        const size_t w = size_t(surface->bounds[1] - surface->bounds[0] + 1), scale = surface->storedScale;
        const size_t px = x - surface->bounds[0], py = y - surface->bounds[2];
        if (surface->validChannels & surface->valid[py * w + px] & 1)
            color = surface->pixels[((py * scale + subY) * w * scale + px * scale + subX) * 2];
    }
    if (!preserveAlpha)
        return color | 0xff000000u;
    if (psm == GS_PSM_CT24)
        return (color & 0xffffffu) | 0x80000000u;
    return color;
}
const HighResolutionSurface* HighResolutionCache::find(unsigned baseBlocks, unsigned widthPages) const {
    for (const auto& surface : surfaces_)
        if (!surface.pixels.empty() && (surface.validChannels & 1) && surface.frameBase == baseBlocks &&
            surface.frameWidth == widthPages &&
            std::any_of(surface.valid.begin(), surface.valid.end(), [](auto b) { return (b & 1) != 0; }))
            return &surface;
    return nullptr;
}
HighResolutionSurface* HighResolutionCache::acquire(const GSContext& target) {
    const unsigned scale = configuredScale();
    if (scale == 1)
        return nullptr;
    for (auto& surface : surfaces_)
        if (surface.frameBase == target.frame.fbp * 32 && surface.frameWidth == std::max(1u, target.frame.fbw))
            return &surface;
    // Two bounded color targets cover the native double buffer. Eviction loses
    // presentation detail only; guest VRAM remains authoritative.
    if (surfaces_.size() == 2)
        surfaces_.erase(surfaces_.begin());
    auto& surface = surfaces_.emplace_back();
    surface.scale = scale;
    surface.frameBase = target.frame.fbp * 32;
    surface.frameWidth = std::max(1u, target.frame.fbw);
    return &surface;
}
void HighResolutionCache::reconcile(const HighResolutionSurface& written, bool writesDepth) {
    const auto color = surfaceRange(written, false), depth = surfaceRange(written, true);
    for (auto& surface : surfaces_) {
        if (&surface == &written || surface.pixels.empty())
            continue;
        uint8_t bits = 0;
        if (overlap(surfaceRange(surface, false), color) ||
            (writesDepth && overlap(surfaceRange(surface, false), depth)))
            bits |= 1;
        if (overlap(surfaceRange(surface, true), color) || (writesDepth && overlap(surfaceRange(surface, true), depth)))
            bits |= 2;
        if (bits)
            invalidateRect(surface, surface.bounds, bits);
    }
}
void HighResolutionCache::invalidateMemory(MemoryRange range) {
    for (auto& surface : surfaces_) {
        if (surface.pixels.empty())
            continue;
        const uint8_t bits = uint8_t(overlap(range, surfaceRange(surface, false))) |
                             (uint8_t(overlap(range, surfaceRange(surface, true))) << 1);
        if (bits)
            invalidateRect(surface, surface.bounds, bits);
    }
}
void HighResolutionCache::invalidateDraw(const GSPrimitiveBatch& batch) {
    if (!batch.vertexCount)
        return;
    const auto& context = batch.state.context;
    const int ox = context.xyoffset.ofx >> 4, oy = context.xyoffset.ofy >> 4;
    std::array<int, 4> box{2048, 0, 2048, 0};
    for (unsigned i = 0; i < batch.vertexCount; ++i) {
        const auto& v = batch.vertices[i];
        box[0] = std::min(box[0], int(std::floor(v.x - ox)));
        box[1] = std::max(box[1], int(std::ceil(v.x - ox)));
        box[2] = std::min(box[2], int(std::floor(v.y - oy)));
        box[3] = std::max(box[3], int(std::ceil(v.y - oy)));
    }
    box[0] = std::max(box[0], int(context.scissor.x0));
    box[1] = std::min(box[1], int(context.scissor.x1));
    box[2] = std::max(box[2], int(context.scissor.y0));
    box[3] = std::min(box[3], int(context.scissor.y1));
    if (box[0] > box[1] || box[2] > box[3])
        return;
    const auto color = targetRange(context, box, false), depth = targetRange(context, box, true);
    for (auto& surface : surfaces_) {
        if (surface.pixels.empty())
            continue;
        const bool alignedColor = surface.frameBase == context.frame.fbp * 32 &&
                                  surface.frameWidth == context.frame.fbw &&
                                  (context.frame.psm == GS_PSM_CT32 || context.frame.psm == GS_PSM_CT24);
        const bool alignedDepth = surface.depthBase == context.zbuf.zbp * 32 && surface.frameWidth == context.frame.fbw;
        if (overlap(surfaceRange(surface, false), color))
            invalidateRect(surface, alignedColor ? box : surface.bounds, 1);
        if (overlap(surfaceRange(surface, true), color))
            invalidateRect(surface, surface.bounds, 2);
        if (!context.zbuf.zmask) {
            if (overlap(surfaceRange(surface, false), depth))
                invalidateRect(surface, surface.bounds, 1);
            if (overlap(surfaceRange(surface, true), depth))
                invalidateRect(surface, alignedDepth ? box : surface.bounds, 2);
        }
    }
}
bool HighResolutionCache::present(PresentationFrame& frame, unsigned baseBlocks, unsigned widthPages, unsigned originX,
                                  unsigned originY) const {
    const bool profile = std::getenv("DRAKAN_FRAME_METRICS") != nullptr;
    const auto begin = profile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    for (const auto& surface : surfaces_) {
        if (surface.pixels.empty() || !(surface.validChannels & 1) || surface.frameBase != baseBlocks ||
            surface.frameWidth != widthPages ||
            std::none_of(surface.valid.begin(), surface.valid.end(), [](auto bits) { return (bits & 1) != 0; }))
            continue;
        const unsigned scale = surface.storedScale, width = frame.width, height = frame.height;
        if (!scale || width > 640 || height > 512 || frame.pixels.size() < size_t(640) * height * 4)
            return false;
        const size_t sw = size_t(surface.bounds[1] - surface.bounds[0] + 1);
        const size_t pitch = sw * scale;
        std::vector<uint8_t> image(size_t(width) * height * scale * scale * 4);
        bool detail = false;
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const unsigned gx = x + originX, gy = y + originY;
                const bool inside = int(gx) >= surface.bounds[0] && int(gx) <= surface.bounds[1] &&
                                    int(gy) >= surface.bounds[2] && int(gy) <= surface.bounds[3];
                const size_t nativeIndex = inside ? size_t(gy - surface.bounds[2]) * sw + gx - surface.bounds[0] : 0;
                const bool valid = inside && (surface.validChannels & surface.valid[nativeIndex] & 1);
                detail |= valid;
                const size_t source =
                    inside ? (size_t(gy - surface.bounds[2]) * scale * pitch + (gx - surface.bounds[0]) * scale) * 2
                           : 0;
                uint32_t native;
                std::memcpy(&native, frame.pixels.data() + (size_t(y) * 640 + x) * 4, 4);
                for (unsigned sy = 0; sy < scale; ++sy)
                    for (unsigned sx = 0; sx < scale; ++sx) {
                        const auto color =
                            (valid ? surface.pixels[source + (size_t(sy) * pitch + sx) * 2] : native) | 0xff000000u;
                        const size_t dest = (size_t(y * scale + sy) * width * scale + x * scale + sx) * 4;
                        std::memcpy(image.data() + dest, &color, 4);
                    }
            }
        if (!detail)
            continue;
        if (std::getenv("DRAKAN_FRAME_METRICS")) {
            static thread_local unsigned reports = 0;
            if (reports++ < 4 || (reports & 255) == 0)
                std::fprintf(stderr, "[portable-hires] source=%ux%u output=%ux%u scale=%u\n", width, height,
                             width * scale, height * scale, scale);
        }
        frame.width *= scale;
        frame.height *= scale;
        frame.pixels = std::move(image);
        if (profile) {
            static thread_local auto last = begin;
            static thread_local unsigned frames = 0;
            static thread_local double milliseconds = 0;
            const auto now = std::chrono::steady_clock::now();
            ++frames;
            milliseconds += std::chrono::duration<double, std::milli>(now - begin).count();
            if (now - last >= std::chrono::seconds(2)) {
                std::fprintf(stderr, "[portable-hires-times] frames=%u copyMs=%.3f\n", frames, milliseconds);
                last = now;
                frames = 0;
                milliseconds = 0;
            }
        }
        return true;
    }
    return false;
}
bool Deferred::touches(MemoryRange range) const {
    if (empty())
        return false;
    auto context = draws_.front().primitive.state.context;
    context.zbuf.zbp = depthBase_;
    return overlap(range, targetRange(context, bounds_, false)) ||
           (usesDepth_ && overlap(range, targetRange(context, bounds_, true)));
}
bool Deferred::touchesSource(const GSDrawState& state) const {
    if (!state.prim.tme || empty())
        return false;
    const auto bounded = [](unsigned mode, unsigned low, unsigned high, unsigned size) {
        return mode <= 1 || (mode == 2 ? low <= high && high < size : (low | high) < size);
    };
    const auto clamp = state.context.clamp;
    if (!bounded(clamp & 3, (clamp >> 4) & 1023, (clamp >> 14) & 1023, state.textureWidth) ||
        !bounded((clamp >> 2) & 3, (clamp >> 24) & 1023, (clamp >> 34) & 1023, state.textureHeight))
        return true;
    const auto& texture = state.context.tex0;
    return touches(memoryRange(texture.psm, texture.tbp0, texture.tbw, 0, 0, state.textureWidth, state.textureHeight));
}
bool Deferred::append(std::span<const Draw> draws, unsigned rounding) {
    if (draws.empty() || !draws.front().primitive || draws_.size() + draws.size() > 65536 ||
        (!empty() && (rounding_ & 0xffc0u) != (rounding & 0xffc0u)))
        return false;
    auto combined = empty() ? draws.front().bounds : bounds_;
    const auto& context = empty() ? draws.front().primitive->state.context : draws_.front().primitive.state.context;
    uint64_t addedArea = 0;
    bool usesDepth = usesDepth_;
    unsigned depthBase = depthBase_, depthPsm = depthPsm_;
    size_t addedBytes = 0;
    std::unordered_set<const gsr::PackImage*> newImages;
    for (const auto& draw : draws) {
        if (!draw.primitive || !supported(*draw.primitive) || !sameTarget(context, draw.primitive->state.context) ||
            draw.bounds[0] < 0 || draw.bounds[2] < 0 || draw.bounds[1] < draw.bounds[0] ||
            draw.bounds[3] < draw.bounds[2])
            return false;
        if (draw.primitive->state.prim.tme) {
            if (!draw.image || draw.texture.data() != draw.image->pixels.data() ||
                draw.texture.size() != draw.image->pixels.size() || draw.width != draw.image->width ||
                draw.height != draw.image->height || draw.scaleX != draw.image->scaleX ||
                draw.scaleY != draw.image->scaleY)
                return false;
            if (!images_.contains(draw.image.get()) && newImages.insert(draw.image.get()).second)
                addedBytes += draw.texture.size() * sizeof(uint32_t);
        }
        const auto& drawContext = draw.primitive->state.context;
        if (depthUsed(drawContext)) {
            if (usesDepth && (depthBase != drawContext.zbuf.zbp || depthPsm != drawContext.zbuf.psm))
                return false;
            usesDepth = true;
            depthBase = drawContext.zbuf.zbp;
            depthPsm = drawContext.zbuf.psm;
        }
        combined[0] = std::min(combined[0], draw.bounds[0]);
        combined[1] = std::max(combined[1], draw.bounds[1]);
        combined[2] = std::min(combined[2], draw.bounds[2]);
        combined[3] = std::max(combined[3], draw.bounds[3]);
        addedArea += uint64_t(draw.bounds[1] - draw.bounds[0] + 1) * (draw.bounds[3] - draw.bounds[2] + 1);
        if (draw.primitive->state.prim.tme && draw.primitive->state.prim.type == GS_PRIM_SPRITE)
            addedBytes += size_t(draw.bounds[1] - draw.bounds[0] + draw.bounds[3] - draw.bounds[2] + 2) * 4;
    }
    if (area_ + addedArea > 64u * 1024u * 1024u || imageBytes_ + addedBytes > 96u * 1024u * 1024u ||
        combined[1] >= int(std::max(1u, context.frame.fbw) * 64) || combined[3] >= 2048 ||
        uint64_t(combined[1] - combined[0] + 1) * (combined[3] - combined[2] + 1) > 2048u * 2048u)
        return false;
    auto depthContext = context;
    depthContext.zbuf.zbp = depthBase;
    const auto color = targetRange(context, combined, false), depth = targetRange(depthContext, combined, true);
    if (color.end > kVramBytes || (usesDepth && (depth.end > kVramBytes || overlap(color, depth))))
        return false;
    for (const auto& draw : draws)
        draws_.push_back({*draw.primitive, draw.bounds, draw.image, draw.replacement});
    images_.insert(newImages.begin(), newImages.end());
    imageBytes_ += addedBytes;
    area_ += addedArea;
    usesDepth_ = usesDepth;
    depthBase_ = depthBase;
    depthPsm_ = depthPsm;
    bounds_ = combined;
    rounding_ = rounding;
    return true;
}
bool Deferred::flush(GSCpuBackend& backend, gsr::gpu::Compute* compute) {
    if (empty())
        return true;
    std::vector<Draw> commands;
    commands.reserve(draws_.size());
    for (const auto& draw : draws_) {
        Draw command{};
        command.primitive = &draw.primitive;
        command.bounds = draw.bounds;
        command.image = draw.image;
        command.replacement = draw.replacement;
        if (draw.image) {
            command.texture = draw.image->pixels;
            command.width = draw.image->width;
            command.height = draw.image->height;
            command.scaleX = draw.image->scaleX;
            command.scaleY = draw.image->scaleY;
        }
        commands.push_back(command);
    }
    if (!backend.m_highResolution && configuredScale() > 1)
        backend.m_highResolution = std::make_shared<HighResolutionCache>();
    auto* surface =
        backend.m_highResolution ? backend.m_highResolution->acquire(draws_.front().primitive.state.context) : nullptr;
    const bool complete = rasterize(commands, bounds_, backend.m_vram, compute, surface);
    if (complete && surface)
        backend.m_highResolution->reconcile(*surface, std::any_of(draws_.begin(), draws_.end(), [](const auto& draw) {
            return !draw.primitive.state.context.zbuf.zmask;
        }));
    if (!complete) {
        // A failed compute dispatch leaves VRAM intact. Replay from immutable
        // decoded images even if guest texture upload slots have since changed.
        const auto cache = backend.m_texturePageCache;
        const auto palette = backend.m_clut;
        try {
            for (const auto& draw : draws_)
                backend.DrawPrimitive(draw.primitive, draw.image, !draw.replacement);
        } catch (...) {
            backend.m_texturePageCache = cache;
            backend.m_clut = palette;
            throw;
        }
        backend.m_texturePageCache = cache;
        backend.m_clut = palette;
        std::fprintf(stderr, "[portable-gpu] replayed %zu immutable draws after unavailable/failed compute\n",
                     draws_.size());
    }
    draws_.clear();
    images_.clear();
    imageBytes_ = 0;
    area_ = 0;
    usesDepth_ = false;
    depthBase_ = depthPsm_ = 0;
    return complete;
}
} // namespace gs_gpu
