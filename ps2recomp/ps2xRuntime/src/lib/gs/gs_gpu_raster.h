#pragma once
#include "runtime/gs/gs_types.h"
#include <array>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>
#include <unordered_set>
#include "gsr/texture_pack.h"

class GSCpuBackend;

namespace gsr::gpu {
class Compute;
}

namespace gs_gpu {
struct Draw {
    const GSPrimitiveBatch* primitive = nullptr;
    std::array<int, 4> bounds{};
    std::span<const uint32_t> texture;
    unsigned width = 1, height = 1, scaleX = 1, scaleY = 1;
    std::shared_ptr<const gsr::PackImage> image; // Immutable lease for deferred submission / loss recovery.
    bool replacement = false;
};
bool supported(const GSPrimitiveBatch& batch);
// Presentation samples are separate from guest VRAM. Odd scales retain an
// exact native sample at the centre of each pixel, so CPU-visible color/depth
// and feedback remain bit-identical to native rendering.
struct HighResolutionSurface {
    unsigned scale = 3;
    std::array<int, 4> bounds{};
    unsigned frameBase = 0, frameWidth = 0, depthBase = 0, depthPsm = 0, storedScale = 0;
    std::vector<uint32_t> pixels; // Interleaved RGBA/depth, row-major at scaled resolution.
    std::vector<uint8_t> valid;   // Per native pixel: color=1, depth=2.
    uint8_t validChannels = 0;    // Whole-surface invalidation is lazy for chunked uploads.
    void clear() { pixels.clear(); }
};
// Caller proves common color/used-depth storage, disjoint color/depth stores
// and immutable texture sources. Unused depth register values may differ.
// A failed job does not modify VRAM, allowing the complete run to fall back.
bool rasterize(std::span<const Draw> draws, std::array<int, 4> bounds, uint8_t* vram,
               gsr::gpu::Compute* compute = nullptr, HighResolutionSurface* surface = nullptr);
uint64_t completedJobs();
struct MemoryRange {
    uint64_t begin = 0, end = 0;
};
unsigned configuredScale();
uint32_t presentationSample(const HighResolutionSurface* surface, unsigned x, unsigned y, unsigned subX, unsigned subY,
                            uint32_t native, unsigned psm, bool preserveAlpha);
class HighResolutionCache {
public:
    const HighResolutionSurface* find(unsigned baseBlocks, unsigned widthPages) const;
    HighResolutionSurface* acquire(const GSContext& target);
    void reconcile(const HighResolutionSurface& written, bool writesDepth);
    void invalidateMemory(MemoryRange range);
    void invalidateDraw(const GSPrimitiveBatch& batch);
    bool present(PresentationFrame& frame, unsigned baseBlocks, unsigned widthPages, unsigned originX,
                 unsigned originY) const;

private:
    std::vector<HighResolutionSurface> surfaces_;
};
MemoryRange memoryRange(unsigned psm, unsigned base, unsigned width, unsigned x, unsigned y, unsigned countX,
                        unsigned countY);
// Retain one ordered destination run across disjoint texture uploads. Public
// CPU-visible fences resolve it. Device loss replays the owned immutable draws.
class Deferred {
public:
    struct Item {
        GSPrimitiveBatch primitive;
        std::array<int, 4> bounds;
        std::shared_ptr<const gsr::PackImage> image;
        bool replacement = false;
    };
    bool append(std::span<const Draw> draws, unsigned rounding);
    bool touches(MemoryRange range) const;
    bool touchesSource(const GSDrawState& state) const;
    bool empty() const { return draws_.empty(); }
    unsigned rounding() const { return rounding_; }
    size_t size() const { return draws_.size(); }
    bool flush(GSCpuBackend& backend, gsr::gpu::Compute* compute = nullptr);

private:
    std::vector<Item> draws_;
    std::unordered_set<const gsr::PackImage*> images_;
    std::array<int, 4> bounds_{};
    size_t imageBytes_ = 0;
    uint64_t area_ = 0;
    bool usesDepth_ = false;
    unsigned depthBase_ = 0, depthPsm_ = 0;
    unsigned rounding_ = 0;
};
} // namespace gs_gpu
