// OverlayRenderer: the graphics half of SettingsOverlay (Dear ImGui draw data, textures, the back buffer copy and
// readback) on one GPU API. SettingsOverlay owns everything else (menu, screen layer, HUD, input).
//
// Implementations: Direct3D 11 (settings/OverlayD3D11.h) and WebGPU over the GS renderer's device
// (settings/OverlayWebGpu.h). The `target` handed to the frame calls is API-defined: an ID3D11RenderTargetView* or a
// gsr::rhi::Texture*. ImTextureIDs are API-defined too (an ID3D11ShaderResourceView* or a gsr::rhi::Texture*), so
// textures owned by the renderer that drew the game (the grid capture) can be drawn directly.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

#include "imgui.h"

namespace dsettings {

class OverlayRenderer {
public:
    virtual ~OverlayRenderer() = default;

    // The ImGui context is current for every call below.
    virtual bool init() = 0;
    virtual void shutdown() = 0;
    virtual void newFrame() = 0;        // (re)creates the font texture when it is missing
    virtual void invalidateFonts() = 0; // the font atlas changed: drop the font texture

    // An RGBA8 texture (rows top down, tightly packed); 0 on failure. Freed with releaseTexture.
    virtual ImTextureID createTexture(const void* rgba, uint32_t w, uint32_t h) = 0;
    virtual void releaseTexture(ImTextureID texture) = 0;

    // A copy of `target` as it is now (before the overlay draws), into one texture reused every call; 0 when the
    // target cannot be copied.
    virtual ImTextureID copyTarget(void* target, uint32_t* w, uint32_t* h) = 0;
    // Draw-list callbacks switching the blend (reset with ImDrawCallback_ResetRenderState): additive, and "opaque"
    // blended by a constant factor (callback data: one float, the screen's fade; 1 without).
    virtual ImDrawCallback additiveCallback() = 0;
    virtual ImDrawCallback opaqueCallback() = 0;

    virtual void render(void* target, uint32_t w, uint32_t h, ImDrawData* data) = 0;
    // Synchronous readback of `target`: RGBA8 rows top down (test screenshots).
    virtual bool readTarget(void* target, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgba) = 0;
};

} // namespace dsettings
