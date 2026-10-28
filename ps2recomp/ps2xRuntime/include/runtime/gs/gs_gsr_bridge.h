// Drakan: bridge between the runtime's GS interface and the standalone D3D11 renderer `gsr`
// (decomp/runtime/gs). Selected at runtime with PS2X_GS=d3d11 (default: software GS).
// This header deliberately does not include gsr headers or <windows.h>.
#pragma once
#include <cstdint>
#include <memory>

struct GSRegisters;
namespace gsr {
struct SceneGuideCamera;
struct ObjectMotionScope;
struct SceneGeometry;
struct SceneModels;
} // namespace gsr

class GsrBridge {
public:
    // parentHwnd: the native top-level game window (drakan_window); the flip-model swapchain is created directly on it.
    // Returns nullptr on failure.
    static std::unique_ptr<GsrBridge> create(void* parentHwnd, GSRegisters* priv);
    ~GsrBridge();

    void submitGif(uint8_t path, const uint8_t* data, uint32_t sizeBytes); // path 1..3
    void writeRegister(uint8_t reg, uint64_t value);
    void uploadImage(uint64_t bitbltbuf, uint64_t trxpos, uint64_t trxreg, uint64_t trxdir, const uint8_t* data,
                     uint32_t sizeBytes);
    uint32_t consumeLocalToHost(uint8_t* dst, uint32_t maxBytes);
    uint32_t readVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y);
    void writeVram(uint32_t psm, uint32_t base, uint32_t bw, uint32_t x, uint32_t y, uint32_t value);
    // Temporal upscaler camera tap (see docs/reference/dlss.md): a VIF1 UNPACK touched VU1 data rows 0..6; rows = 28
    // floats. Ordered with the GIF stream.
    void noteVuUpload(uint32_t vuAddr, uint32_t qwCount, const float* rows);
    void noteSceneCamera(const gsr::SceneGuideCamera& camera);
    void noteObjectMotion(const gsr::ObjectMotionScope& scope);
    void noteSceneGeometry(const gsr::SceneGeometry& geometry);
    void noteSceneModels(std::shared_ptr<const gsr::SceneModels> models);

    // Complete native 0x1e0048 transaction, called once after its guarded return.
    // Caller must process pending transfers and synchronize VU1 BEFORE calling,
    // outside GS/context locks, so all completed world/HUD work precedes this
    // immutable privileged-register snapshot in the renderer FIFO. No args:
    // the snapshot comes from this bridge's GSRegisters. No-op unless startup
    // video.world_rate / DRAKAN_WORLD_RATE is "uncapped". Does not signal vblank.
    // Blocks the producer until the queued frame retires, including its host
    // handoff (also in native mode). Call on the EE producer, never the host
    // presentation thread, and hold no GS/queue/context locks across this call.
    void commitDisplay();

    // Vertical blank: startup/field-sensitive fallback completes a frame;
    // after a safe native commit this only signals VSINT/FIELD. Does not present.
    void vsync(uint64_t vsyncTick);
    // Main-thread tick (once per host frame): follows the parent's client size, presents the display.
    void hostTick();
    // Borderless fullscreen on the monitor the window is on (Alt+Enter / F11 / drakan.ini [video] fullscreen).
    void setFullscreen(bool on);
    bool isFullscreen() const;

    struct Impl;

private:
    GsrBridge();
    std::unique_ptr<Impl> m_impl;
};
