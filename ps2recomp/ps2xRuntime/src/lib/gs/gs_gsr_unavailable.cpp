// A software-only build cannot create the D3D11 bridge. Keep its ownership/API
// symbols linkable without importing Windows renderer objects into the runtime.
#include "runtime/gs/gs_gsr_bridge.h"

struct GsrBridge::Impl {};
GsrBridge::GsrBridge() = default;
GsrBridge::~GsrBridge() = default;
std::unique_ptr<GsrBridge> GsrBridge::create(void*, GSRegisters*) {
    return nullptr;
}
void GsrBridge::hostTick() {}
void GsrBridge::vsync(uint64_t) {}
void GsrBridge::submitGif(uint8_t, const uint8_t*, uint32_t) {}
void GsrBridge::writeRegister(uint8_t, uint64_t) {}
void GsrBridge::uploadImage(uint64_t, uint64_t, uint64_t, uint64_t, const uint8_t*, uint32_t) {}
uint32_t GsrBridge::consumeLocalToHost(uint8_t*, uint32_t) {
    return 0;
}
uint32_t GsrBridge::readVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {
    return 0;
}
void GsrBridge::writeVram(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void GsrBridge::noteVuUpload(uint32_t, uint32_t, const float*) {}
void GsrBridge::noteSceneCamera(const gsr::SceneGuideCamera&) {}
void GsrBridge::noteObjectMotion(const gsr::ObjectMotionScope&) {}
void GsrBridge::noteSceneGeometry(const gsr::SceneGeometry&) {}
void GsrBridge::noteSceneModels(std::shared_ptr<const gsr::SceneModels>) {}
void GsrBridge::commitDisplay() {}
void GsrBridge::setFullscreen(bool) {}
bool GsrBridge::isFullscreen() const {
    return false;
}
