// Drakan: the platform-free half of the PC Settings host, shared by the Windows D3D11 host (drakan_settings_host.cpp)
// and the portable raylib host (drakan_portable_ui.cpp). Backends override capabilities() / applyVideo() for their
// renderer and call applyCommonVideo() for the settings that do not depend on it.
#pragma once
#include "drakan_dual_wield.h"
#include "drakan_region.h"
#include "drakan_fpsmeter.h"
#include "drakan_subtitles.h"
#include "drakan_tab_screen.h"
#include "drakan_widescreen.h"
#include "input/DrakanInput.h"
#include "settings/Host.h"
#include "settings/Hud.h"
#include "spu2/AudioOutput.h"

#include <algorithm>
#include <atomic>

namespace drakan_settings_host {

class CommonSettingsHost : public dsettings::ISettingsHost {
public:
    explicit CommonSettingsHost(std::atomic<spu2::AudioOutput*>& audio) : audio_(audio) {}

    // Capabilities every host provides; a backend adds its renderer's features.
    static dsettings::HostCaps commonCapabilities() {
        dsettings::HostCaps c;
        c.hdTextures = true;
        using drakan_region::Feature;
        using drakan_region::supports;
        c.promptStyle = supports(Feature::UiPrompts);
        c.directMouseLook = supports(Feature::DirectMouseLook);
        auto require = [&](Feature feature, std::initializer_list<const char*> keys) {
            if (!supports(feature))
                c.unavailableSettings.insert(keys.begin(), keys.end());
        };
        require(Feature::DirectMouseLook,
                {"input.mouse.free_combat", "input.mouse.camera_follow", "input.mouse.pitch_persist"});
        require(Feature::MovementInput, {"input.keyboard.strafe"});
        require(Feature::AdvancedInput,
                {"input.mouse.bow_aim", "input.menu.inventory_mouse", "general.skip_memcard_warning"});
        if (!supports(Feature::AdvancedInput))
            c.unavailableBindingActions = {"QUICKSAVE", "QUICKLOAD", "QUICK1",    "QUICK2",   "QUICK3",
                                           "QUICK4",    "QUICK5",    "QUICK6",    "QUICK7",   "QUICK8",
                                           "QUICK9",    "ACT:0x114", "ACT:0x115", "ACT:0x0e", "ACT:0x17"};
        require(Feature::Widescreen, {"video.aspect", "video.hud_scale", "video.fov", "video.hud_aspect_correct",
                                      "video.smooth_menu_highlight"});
        require(Feature::FrameRate, {"video.world_rate"});
        require(Feature::Menus,
                {"general.inventory_screen", "general.pause_menu", "general.save_menu", "general.title_menu"});
        require(Feature::DualWield, {"general.dual_wield"});
        require(Feature::Subtitles, {"subtitles.narration", "subtitles.size"});
        require(Feature::SceneHooks,
                {"video.dlss", "video.dlss_neural", "video.dlss_neural_resolution", "video.dlss_preset",
                 "video.dlss_sharpness", "video.ray_shadows", "video.ray_reflections", "video.modern_water"});
        c.restartKeys = {"video.world_rate", "general.language", "general.ui_prompts"};
        return c;
    }

    dsettings::ApplyResult applyAudio(const dsettings::AudioSettings& n, const dsettings::AudioSettings& p) override {
        if (spu2::AudioOutput* a = audio_.load()) {
            a->setMuted(!n.enabled);
            a->setMasterGain(std::max(0, n.volume) / 100.0f);
            if (n.device != p.device && capabilities().audioDeviceSelect)
                a->setOutputDevice(n.device.empty() ? "default" : n.device);
        }
        return {};
    }

    std::vector<std::string> audioDevices() const override {
        std::vector<std::string> v;
        for (const auto& d : spu2::AudioOutput::enumerateDevices())
            v.push_back(d.name);
        return v;
    }

    dsettings::ApplyResult applySubtitles(const dsettings::SubtitleSettings& n,
                                          const dsettings::SubtitleSettings&) override {
        drakan_subtitles::setEnabled(n.narration);
        drakan_subtitles::setSize(n.size);
        return {};
    }

    dsettings::ApplyResult applyGeneral(const dsettings::GeneralSettings& n,
                                        const dsettings::GeneralSettings& p) override {
        drakan_tab::setEnabled(n.inventoryScreen == dsettings::InventoryScreen::Modern);
        if (n.pauseMenu != p.pauseMenu) // applies from the next Esc (an open pause falls back to the native page)
            drakan_pause_screen::setEnabled(n.pauseMenu == dsettings::PauseMenu::Modern);
        // From the next opening of those screens; an open one falls back to its native page.
        if (n.saveMenu != p.saveMenu)
            drakan_menu_screens::setSaveModern(n.saveMenu == dsettings::SaveMenu::Modern);
        if (n.titleMenu != p.titleMenu)
            drakan_menu_screens::setTitleModern(n.titleMenu == dsettings::TitleMenu::Modern);
        drakan_dual_wield::setEnabled(n.dualWield);
        return {};
    }

    std::string defaultInputIni() const override { return drakan_input::Config::defaultIniText(); }

protected:
    // Video settings that do not depend on the renderer.
    static void applyCommonVideo(const dsettings::VideoSettings& n, const dsettings::VideoSettings& p) {
        if (n.showFps != p.showFps)
            drakan_fpsmeter::setMode(n.showFps);
        if (n.fpsPosition != p.fpsPosition)
            drakan_fpsmeter::setCornerText(n.fpsPosition.c_str());
        if (n.fieldOfView != p.fieldOfView)
            drakan_ws::setFieldOfView(n.fieldOfView);
    }

private:
    std::atomic<spu2::AudioOutput*>& audio_;
};

// The HUD layers that only read shared game state: the FPS counter and the modern bow aim crosshair (the camera's
// centre ray, so the centre of the game image). Subtitles register through drakan_subtitles::installHudLayer().
inline void installSharedHudLayers() {
    dsettings::HudLayer stats;
    stats.active = [] { return drakan_fpsmeter::mode() > 0; };
    stats.draw = [](const dsettings::HudView& view) {
        dsettings::drawStatsPanel(drakan_fpsmeter::lines(), drakan_fpsmeter::corner(), view.w, view.h, view.uiScale);
    };
    dsettings::hudLayers().set(dsettings::HudSlot::Stats, std::move(stats));
    dsettings::HudLayer crosshair;
    crosshair.active = [] { return drakan_input::module().bowCrosshairVisible(); };
    crosshair.draw = [](const dsettings::HudView& view) {
        dsettings::drawCrosshair(view.imageX() + view.imageW() * 0.5f, view.imageY() + view.imageH() * 0.5f,
                                 view.uiScale);
    };
    dsettings::hudLayers().set(dsettings::HudSlot::Crosshair, std::move(crosshair));
}

} // namespace drakan_settings_host
