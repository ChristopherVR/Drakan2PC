// Order-of-the-Flame style views of the inventory screen (drakan_inventory_views.inl): state shared with the scene
// camera packets (drakan_scene_camera.cpp).
#pragma once
#include <atomic>
#include <cstdint>

namespace drakan_views {
// Inventory screen object whose page draws as a centred view, 0 = none. EE thread writes (the view hooks) and reads
// (the inventory draw packet, which marks the frame with gsr::SceneGuideCamera::CentredPage).
inline std::atomic<uint32_t> g_centredScreen{0};
} // namespace drakan_views
