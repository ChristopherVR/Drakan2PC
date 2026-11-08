// The present latch of a ray output (modern water, the ray-traced scene) and the settings menu's modern water status
// text, shared by the D3D11 and WebGPU backends. Pure: no device, unit tested (tests/test_ray_status.cpp).
#pragma once
#include <cstdint>
#include <string>

namespace gsr::raystatus {

// A ray output belongs to the presented world image only when a pass wrote it for that image's world epoch. Epoch 0
// means no pass has written one yet: the first world images (logos, the title backdrop) also carry world epoch 0, so a
// bare equality claimed a water image that never existed, and the status text then read the pass that was still being
// built on the prewarm thread (a null renderer: the title crashed while ray tracing was starting).
inline bool outputLatched(bool gameWorld, uint64_t outputEpoch, uint64_t worldEpoch) {
    return gameWorld && outputEpoch != 0 && outputEpoch == worldEpoch;
}

// What the status text knows of the backend's water pass: `exists` false while it is not built yet (the prewarm thread
// still creates it, or no world asked for it).
struct PassView {
    bool exists = false;
    bool ready = false;
    std::string status;
};

// The settings menu's modern water line. `reflections` names the backend's reflection pass ("DXR reflections").
inline std::string waterStatus(bool modernWater, const PassView& pass, bool lastPresentedWater, bool rayReflections,
                               const char* reflections) {
    if (!modernWater)
        return "Modern water off";
    if (pass.exists && !pass.ready)
        return pass.status;
    if (pass.exists && lastPresentedWater)
        return std::string("Modern water pass completed for the last gameplay image; ") + reflections +
               (rayReflections ? " on. " : " off. ") + pass.status;
    if (pass.exists)
        return "Modern water enabled; no water output in the last displayed image. Last pass: " + pass.status;
    return "Modern water enabled; no eligible water world in the current frame. Coverage follows native water layers.";
}

} // namespace gsr::raystatus
