// The settings overlay's WebGPU renderer: Dear ImGui drawn through the GS renderer's device (gsr::rhi), so the overlay
// shares the game's device, command stream and window surface on every platform.
#pragma once
#include <memory>

#include "settings/OverlayRenderer.h"

namespace gsr::rhi {
class Device;
}

namespace dsettings {

// Frame target: a gsr::rhi::Texture* (the window surface's image). ImTextureIDs are gsr::rhi::Texture*. Calls must come
// from the thread that drives `device`; it must outlive the renderer.
std::unique_ptr<OverlayRenderer> createWebGpuOverlayRenderer(gsr::rhi::Device& device);

} // namespace dsettings
