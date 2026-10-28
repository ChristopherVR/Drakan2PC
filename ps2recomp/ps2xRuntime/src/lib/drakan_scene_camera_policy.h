#pragma once
#include <cmath>
#include <cstdint>

namespace drakan_scene_camera::policy {
inline bool worldPacket(bool engineWorld, bool preview, bool sourceMatchesWorld, bool guiAllowed, bool haveViewport) {
    return engineWorld && !preview && sourceMatchesWorld && guiAllowed && haveViewport;
}

enum : uint32_t {
    NonWorld = 1,
    FirstWorld = 2,
    GuiChange = 4,
    TickGap = 8,
    BackwardsTick = 16,
    Jump = 32,
    Turn = 64,
    SizeChange = 128,
    ProjectionChange = 256
};
struct View {
    uint64_t tick = 0;
    int gui = -1;
    uint32_t width = 0, height = 0;
    float projectionX = 0, projectionY = 0;
};
inline uint32_t reasons(bool world, bool hadWorld, bool hasTicks, const View& now, const View& old, float distance2,
                        float forwardDot) {
    uint32_t result = 0;
    if (!world)
        result |= NonWorld;
    if (!hadWorld)
        result |= FirstWorld;
    if (now.gui != old.gui)
        result |= GuiChange;
    if (hasTicks && now.tick - old.tick > 6)
        result |= TickGap;
    if (hasTicks && now.tick < old.tick)
        result |= BackwardsTick;
    if (world && hadWorld) {
        if (distance2 > 4096.0f * 4096.0f)
            result |= Jump;
        if (forwardDot < 0.5f)
            result |= Turn;
        if (now.width != old.width || now.height != old.height)
            result |= SizeChange;
        if (std::fabs(now.projectionX - old.projectionX) > 1e-4f ||
            std::fabs(now.projectionY - old.projectionY) > 1e-4f)
            result |= ProjectionChange;
    }
    return result;
}
inline bool reset(uint32_t reasons) {
    return (reasons & ~BackwardsTick) != 0;
}
inline void updateAnchorContext(bool world, bool preview, int gui, bool& hadWorld, int& lastGui) {
    // A HUD preview still closes its own render context. It is not a new world
    // camera or a GUI transition when both GUI observations are gameplay/-1.
    if (!world && preview && gui == -1 && lastGui == -1)
        return;
    hadWorld = world;
    lastGui = gui;
}
} // namespace drakan_scene_camera::policy
