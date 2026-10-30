// ISettingsHost: everything the settings UI needs from the running game. The runtime owner implements it once; the UI
// never touches the renderer, the audio mixer or the input module directly. Platform free (std only).
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "settings/Types.h"

namespace dsettings {

struct DisplayMode {
    int width = 0, height = 0, refreshHz = 0;
};

struct MonitorInfo {
    std::string name = "Display";
    int nativeWidth = 1920, nativeHeight = 1080, refreshHz = 60;
    std::vector<DisplayMode> modes; // exclusive-fullscreen modes (may be empty)
};

// What this build can do. Anything false is shown greyed out ("not available in this build").
struct HostCaps {
    bool exclusiveFullscreen = false;
    bool frameLimiter = true;
    bool internalResolution = true;
    std::set<std::string> unavailableSettings;       // Host-specific capabilities outside the common renderer flags.
    std::set<std::string> unavailableBindingActions; // Keep unsupported actions out of the binding editor.
    bool textureUpscale = false;                     // gs owner: texture_upscale / texture_upscale_filter
    bool anisotropic = false;                        // gs owner: anisotropic
    bool mipBias = false;                            // gs owner: mip_bias
    bool hdTextures = false;                         // hle override layer: hd_textures
    bool textureDump = false;
    bool languageSelect = false; // language tree switch (always needs a restart)
    bool audioDeviceSelect = true;
    bool gamepad = true;
    bool directMouseLook = true; // false: this disc uses right-stick emulation, including after reload
    bool promptStyle = true;     // keyboard vs PS2 icons (drakan_ui)
    bool dlss =
        false; // DLSS module present, NVIDIA GPU, nvngx_dlss.dll available (dlss / dlss_preset / dlss_sharpness)
    std::string dlssStatus;  // one line shown under the DLSS rows (mode in use or why it is unavailable)
    bool dlssNvidia = false; // the DLSS rows drive NVIDIA DLSS (Direct3D 11), not the portable temporal AA: credit it
    bool dlssNeural = false;
    std::string dlssNeuralStatus;
    bool modernWater = false;
    std::string modernWaterStatus;
    bool rendererChoice = false; // the build has both GS renderers (Windows: WebGPU and Direct3D 11): video.renderer
    std::string rayTracingStatus;
    std::vector<std::string> textureUpscaleFilters; // e.g. {"default","bicubic","xbrz"}
    // Setting keys ("section.key", lower case) that cannot be applied while the game runs. A change to one of these
    // makes the UI offer a restart (requestRestart) instead of calling the live apply path for it. Default list below
    // matches the current runtime. video.world_rate is startup-only; the menu always requires a restart for it, even
    // with a custom list.
    std::set<std::string> restartKeys = {"video.renderer",
                                         "video.scale",
                                         "video.world_rate",
                                         "general.language",
                                         "general.ui_prompts",
                                         "video.texture_upscale",
                                         "video.texture_upscale_filter",
                                         "video.hd_textures",
                                         "audio.device"};
};

// Result of a live apply.
struct ApplyResult {
    bool ok = true;
    bool needsRestart = false; // part of the change was not applied live
    std::string message;       // shown to the user when !ok
};

// A texture of the game's own UI art (INTERFCE.TXD), decoded at runtime from the user's disc by the host. Upright RGBA,
// 4 bytes per pixel (R, G, B, A; straight alpha 0..255). Never stored on disk by the settings library.
struct GameImage {
    int w = 0, h = 0;
    std::vector<uint32_t> rgba; // little endian 0xAABBGGRR
};

// The game's own menu sounds (played by the host through the game's sound manager, the same calls the game's menus
// make). The game's UI sound properties: Tick = "Next Option" (0x2a), Confirm = "Button Click Sound" (0x24), Back =
// "Exit Sound" (0x2d), On / Off = "Check Box On / Off" (0x25 / 0x26).
enum class UiSound { Tick, Confirm, Back, On, Off };

struct Paths {
    std::string drakanIni = "drakan.ini";
    std::string inputIni = "drakan_input.ini";
};

class ISettingsHost {
public:
    virtual ~ISettingsHost() = default;

    virtual HostCaps capabilities() const { return {}; }
    virtual MonitorInfo monitor() const { return {}; }
    virtual std::vector<std::string> audioDevices() const { return {}; }
    // Current size of the output window (client area) in pixels, used for the auto UI scale and "current" labels.
    virtual void windowSize(int& w, int& h) const {
        w = 1280;
        h = 960;
    }

    // ---- live apply. `next` is the full new section, `prev` what is active now. Return needsRestart when something
    // could not be applied. ----
    virtual ApplyResult applyVideo(const VideoSettings& next, const VideoSettings& prev) = 0;
    virtual ApplyResult applyAudio(const AudioSettings& next, const AudioSettings& prev) = 0;
    virtual ApplyResult applySubtitles(const SubtitleSettings& next, const SubtitleSettings& prev) {
        (void)next;
        (void)prev;
        return {};
    }
    virtual ApplyResult applyGeneral(const GeneralSettings& next, const GeneralSettings& prev) {
        (void)next;
        (void)prev;
        return {};
    }
    // drakan_input.ini was rewritten: re-read it (drakan_input has no reload call yet, see README: add
    // `reloadConfig(path)`).
    virtual void reloadBindings(const std::string& inputIniPath) = 0;
    // Rebuild the generated key-cap textures / text overrides from the (new) bindings. May be a no-op when the prompts
    // are only built at load.
    virtual void regeneratePrompts() {}
    // Ask the application to restart itself (or tell the user to). `reasons` lists the settings that need it.
    virtual void requestRestart(const std::vector<std::string>& reasons) = 0;
    // "Quit to Desktop": close the game cleanly (the host saves / fades the audio / releases the window and exits the
    // process).
    virtual void requestQuit() {}

    // Default content of drakan_input.ini ([bindings]/[menu_bindings] reset). Return
    // drakan_input::Config::defaultIniText() here.
    virtual std::string defaultInputIni() const;

    // ---- authentic look: the game's UI textures and sounds (all optional; without them the menu falls back to the
    // plain style) ---- INTERFCE.TXD (type 0x40) texture `id` of the running game. False when it is not available
    // (yet).
    virtual bool gameTexture(int id, GameImage& out) const {
        (void)id;
        (void)out;
        return false;
    }
    virtual void playUiSound(UiSound s) { (void)s; }

    // ---- overlay life cycle (pause the emulated machine / release the mouse here) ----
    virtual void onOverlayOpened() {}
    virtual void onOverlayClosed() {}
};

} // namespace dsettings
