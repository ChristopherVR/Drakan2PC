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
// Vertical blanks (60 per second) between two world cameras after which the temporal history is dropped. It was 6:
// a world that takes longer than a tenth of a second, i.e. anything under 10 world frames per second, then reset the
// upscaler's history and the object-motion history on every such frame (in a busy scene at 8 to 10 fps between a
// tenth and two thirds of all worlds had no object motion at all), exactly where the picture needs the history most.
// A slow frame is not a cut: menus, loading screens and level changes are caught by GuiChange / NonWorld /
// FirstWorld, a moved camera by Jump / Turn. Half a second still drops the history after a real pause.
inline constexpr uint64_t kResetTickGap = 30;
inline uint32_t reasons(bool world, bool hadWorld, bool hasTicks, const View& now, const View& old, float distance2,
                        float forwardDot, uint64_t tickGap = kResetTickGap) {
    uint32_t result = 0;
    if (!world)
        result |= NonWorld;
    if (!hadWorld)
        result |= FirstWorld;
    if (now.gui != old.gui)
        result |= GuiChange;
    if (hasTicks && now.tick - old.tick > tickGap)
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
