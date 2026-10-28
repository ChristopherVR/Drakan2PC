#pragma once

#include "runtime/gs/gs_backend.h"
#include "runtime/gs/gs_texture_page_cache.h"

#include <array>
#include <mutex>
#include <memory>
#include <vector>

namespace gsr {
class TexturePack;
struct PackImage;
} // namespace gsr

namespace gs_gpu {
class Deferred;
class HighResolutionCache;
} // namespace gs_gpu

namespace gs_cpu {
class TileWorkers;
class HdSourceCache;
struct SourceImage;
struct TileContext;
} // namespace gs_cpu

class GSCpuBackend final : public GSRasterBackend {
public:
    GSCpuBackend();
    ~GSCpuBackend() override;

    void Initialize(uint8_t* vram, uint32_t vramSize) override;
    void Reset() override;

    void Submit(const GSPrimitiveBatch& batch) override;
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& texclut) override;
    void BeginTransfer(const GSTransferCommand& command) override;
    void UploadImage(const uint8_t* data, uint32_t sizeBytes) override;

    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest& request) override;

    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t maxBytes) override;

    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t>& out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    friend class GSAsyncCpuBackend;
    friend class gs_gpu::Deferred;
    void ResetUnlocked();
    void SubmitUnlocked(const GSPrimitiveBatch& batch);
    void LoadClutUnlocked(const GSTex0Reg& tex0, const GSTexClutReg& texclut);
    uint32_t ReadVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const;
    uint32_t ReadTextureVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y);
    void WriteVramUnlocked(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);

    struct PendingDraw {
        GSPrimitiveBatch batch;
        int minY = 0, maxY = 0;
        bool sprite = false;
        int minX = 0, maxX = 0;
        uint32_t palette = 0;
        uint64_t sequence = 0;
        std::shared_ptr<const gsr::PackImage> replacement;
    };
    struct PendingPalette {
        std::array<uint16_t, 512> colors;
        std::array<uint32_t, 256> rgba32{};
        GSMem::TexturePageCache cache;
        uint64_t sequence = 0;
    };
    std::shared_ptr<const gs_cpu::SourceImage> DecodeTextureSource(const GSDrawState& state,
                                                                   const GSMem::TexturePageCache& cache);
    std::shared_ptr<const gsr::PackImage> PrepareReplacement(const GSDrawState& state);
    std::unique_ptr<gsr::TexturePack> m_texturePack;
    std::unique_ptr<gs_cpu::HdSourceCache> m_hdSources;
    bool QueueDraw(const GSPrimitiveBatch& batch);
    void FlushDrawsUnlocked();
    enum class GpuFenceReason { Boundary, Source, Palette, Target, Read, Write, Cpu, Transfer, Count };
    void FlushGpuUnlocked(GpuFenceReason reason = GpuFenceReason::Boundary);
    std::shared_ptr<gs_gpu::Deferred> m_gpuDeferred;
    std::shared_ptr<gs_gpu::HighResolutionCache> m_highResolution;
    bool m_gpuFailed = false;
    bool CanTile(const GSDrawState& state, int minX, int maxX, int minY, int maxY, bool checkCache = true) const;
    bool DrawTiles(const GSPrimitiveBatch& batch, int minX, int maxX, int minY, int maxY, bool sprite);
    void DrawPrimitive(const GSPrimitiveBatch& batch, const std::shared_ptr<const gsr::PackImage>& prepared = {},
                       bool preparedNative = false);
    void DrawSprite(const GSPrimitiveBatch& batch);
    void DrawTriangle(const GSPrimitiveBatch& batch);
    template <bool Common, bool Alpha = false>
    void DrawTriangleImpl(const GSPrimitiveBatch& batch);
    void DrawLine(const GSPrimitiveBatch& batch);
    void WritePixel(const GSDrawState& state, int x, int y, int z, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                    uint8_t fog, bool depthPassed = false);
    uint32_t SampleTexture(const GSDrawState& state, float s, float t, float q, uint16_t u, uint16_t v, int x, int y);
    template <bool Common, bool Alpha = false>
    uint32_t SampleTextureImpl(const GSDrawState& state, float s, float t, float q, uint16_t u, uint16_t v, int x,
                               int y);
    uint32_t LookupCLUT(const GSDrawState& state, uint8_t index, uint8_t cpsm, uint8_t csm, uint8_t csa,
                        uint8_t sourcePsm);

    void PerformLocalToLocalTransfer();
    void PerformLocalToHostTransfer();
    PresentationFrame PresentFromLocalMemory(const GSPresentationRequest& request);
    bool CopyFrameToHostRgba(const GSFrameReg& frame, uint32_t width, uint32_t height, std::vector<uint8_t>& outPixels,
                             bool preserveAlpha, bool useLocalMemoryLayout, bool frameBaseIsPages,
                             uint32_t sourceOriginX, uint32_t sourceOriginY) const;

    using WriteVramFunc = void (*)(uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using ReadVramFunc = uint32_t (*)(uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t);

    static constexpr size_t kPsmHandlerCount = 1u << 6u;
    mutable std::mutex m_mutex;
    unsigned m_rasterThreads = 4;
    std::shared_ptr<gs_cpu::TileWorkers> m_tileWorkers;
    std::vector<PendingDraw> m_pendingDraws;
    std::vector<uint32_t> m_pendingSources;
    std::vector<PendingPalette> m_pendingPalettes;
    uint64_t m_clutGeneration = 0, m_pendingPaletteGeneration = 0;
    uint64_t m_pendingSequence = 0, m_pendingCacheSequence = 0;
    std::vector<gs_cpu::TileContext> m_rasterTiles;
    std::array<int, 4> m_pendingBounds{}; // minX, maxX, minY, maxY
    unsigned m_pendingRounding = 0;
    uint64_t m_pendingArea = 0;
    bool m_pendingTextured = false;
    uint8_t* m_vram = nullptr;
    uint32_t m_vramSize = 0;
    std::array<ReadVramFunc, kPsmHandlerCount> m_readVramFuncs{};
    std::array<WriteVramFunc, kPsmHandlerCount> m_writeVramFuncs{};
    std::array<uint16_t, 512> m_clut{};
    std::array<uint32_t, 2> m_clutCbp{};
    GSMem::TexturePageCache m_texturePageCache;

    GSTransferCommand m_transfer{};
    GSTransferSnapshot m_transferState{};
    std::vector<uint8_t> m_localToHostBuffer;
    size_t m_localToHostReadPos = 0;
};
