#pragma once
#include "runtime/gs/gs_backend.h"
#include <memory>

// An ordered CPU rendering queue. Guest producers retain command order while
// rasterization overlaps VU/GIF work; CPU-visible reads are completion fences.
class GSAsyncCpuBackend final : public GSRasterBackend {
public:
    GSAsyncCpuBackend();
    ~GSAsyncCpuBackend() override;
    void Initialize(uint8_t* vram, uint32_t size) override;
    void Reset() override;
    void Submit(const GSPrimitiveBatch& batch) override;
    void LoadClut(const GSTex0Reg& tex0, const GSTexClutReg& clut) override;
    void BeginTransfer(const GSTransferCommand& command) override;
    void UploadImage(const uint8_t* data, uint32_t size) override;
    void Flush() override;
    void TextureFlush() override;
    void Sync(GSSyncReason reason) override;
    PresentationFrame Present(const GSPresentationRequest& request) override;
    bool ClearFramebuffer(const GSContext& context, uint32_t rgba) override;
    uint32_t ConsumeLocalToHostBytes(uint8_t* dst, uint32_t size) override;
    uint32_t ReadVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y) const override;
    void WriteVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value) override;
    void SnapshotVram(std::vector<uint8_t>& out) const override;
    GSTransferSnapshot GetTransferSnapshot() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::unique_ptr<GSRasterBackend> makeCpuRasterBackend();
