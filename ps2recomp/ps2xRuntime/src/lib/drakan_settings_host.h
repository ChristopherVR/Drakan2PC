// Drakan: glue between the in-game settings overlay (decomp/runtime/settings, namespace dsettings) and the runtime.
//   * the overlay is drawn by gsr at the end of every present (Renderer::setOverlayCallback),
//   * DrakanSettingsHost (drakan_settings_host.cpp) implements dsettings::ISettingsHost on top of the live setters of
//     gsr (video), spu2::AudioOutput (volume / mute / device) and drakan_input (reloadConfig / setMenuActive),
//   * isOpen() is the pause gate read by the EE thread (PadMgr_Update hook in drakan_input_glue.cpp),
//   * requestOpen() is called by the Options > "PC Settings" hook (drakan_overrides.cpp, EE thread).
// Without PS2X_HAVE_DRAKAN_SETTINGS every function is an inline no-op.
#pragma once
#include <cstdint>
#include <memory>

namespace gsr {
class Renderer;
}
namespace hle {
class IsoFs;
}
namespace spu2 {
class AudioOutput;
}
class PS2Runtime;

namespace drakan_settings_host {
// Window operations the host cannot do from inside the overlay callback (they run later, on the window thread, outside
// of the present): implemented by GsrBridge.
struct WindowOps {
    void* ctx = nullptr;
    void (*requestFullscreen)(void* ctx, bool on) = nullptr;
    void (*requestWindowSize)(void* ctx, int clientW, int clientH) = nullptr;
    bool (*isFullscreen)(void* ctx) = nullptr;
    // 0 selects persistent output-driven Auto; 1..8 selects a fixed scale.
    void (*requestInternalScale)(void* ctx, uint32_t scale) = nullptr;
    // Queue mode safety alongside renderer setters, before the next GS boundary.
    void (*presentationFieldMode)(void* ctx, uint32_t deinterlace, bool flicker, uint32_t changedMask) = nullptr;
};

#if defined(PS2X_HAVE_DRAKAN_SETTINGS) || defined(PS2X_HAVE_PORTABLE_UI)
// Installs the overlay callback on the renderer (call after the bridge subclassed the window).
void attachRenderer(gsr::Renderer* gs, void* parentHwnd, const WindowOps& ops);
// Destroys the overlay (before the renderer / the window are destroyed).
void detachRenderer();
void setAudio(spu2::AudioOutput* audio);
bool isOpen();      // thread safe; true while the overlay is shown (game thread waits in PadMgr_Update)
void requestOpen(); // thread safe; opens the overlay at the next present
// Thread safe; opens the overlay at the next present on its Controls page (the modern pause menu's Key Bindings).
void requestOpenControls();
int frameLimit(); // fps cap for the host loop, 0 = off
// The disc the game runs from: the overlay decodes the game's own UI textures (INTERFCE.TXD 0x40: frame, stone, font
// ...) from it at run time to draw in the game's style (nothing is stored). Called once after the file system is open
// (drakan_overrides.cpp).
void setArtSource(hle::IsoFs* fs);
#else
inline void attachRenderer(gsr::Renderer*, void*, const WindowOps&) {}
inline void detachRenderer() {}
inline void setAudio(spu2::AudioOutput*) {}
inline bool isOpen() {
    return false;
}
inline void requestOpen() {}
inline void requestOpenControls() {}
inline int frameLimit() {
    return 0;
}
inline void setArtSource(hle::IsoFs*) {}
#endif
// Portable presentation-thread backend; no platform graphics types cross this
// boundary. The source game frame remains available for the modern UI's crops.
#if !defined(_WIN32)
void drawPortableUi(PS2Runtime& runtime, int width, int height, unsigned frameTexture);
void drawPortableFrame(unsigned texture, unsigned width, unsigned height, float x, float y, float w, float h);
void shutdownPortableUi();
#endif
} // namespace drakan_settings_host
