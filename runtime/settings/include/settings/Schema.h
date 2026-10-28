// The settings schema: every row of the F1 overlay's Display, Audio, Controls (mouse and gamepad) and Gameplay pages,
// with its label, help text, choices / range, restart mark and enabled state, built from the settings model and the
// host's capabilities. The overlay (SettingsMenu) draws its rows from it, and the modern pages (gameui::SettingsPages)
// render the same schema, so a setting added here shows up in both. Changes from either UI go through
// applySettingChange(). Platform free (std + the gameui schema types).
#pragma once
#include <string>
#include <vector>

#include "gameui/SettingsSchema.h"
#include "settings/Host.h"
#include "settings/Types.h"

namespace dsettings {

// Shown in the PC settings whenever DLSS is available (NVIDIA RTX SDKs licence attribution).
inline constexpr const char* kNvidiaDlssCredit =
    "Uses NVIDIA DLSS. NVIDIA, RTX and DLSS are trademarks of NVIDIA Corporation.";

// What the schema depends on besides the settings.
struct SchemaEnv {
    HostCaps caps;
    MonitorInfo monitor;
    std::vector<std::string> audioDevices;
    bool linkSensitivity = true; // vertical mouse sensitivity follows the horizontal one (UI state, not stored)
};

// Pseudo keys (rows that are not one field of the ini files).
inline constexpr const char* kKeyWindowSize = "video.window_size";        // "1280x960": the size presets / modes
inline constexpr const char* kKeyWindowCustom = "video.window_custom";    // "1280x960": the overlay's free size field
inline constexpr const char* kKeyLinkSensitivity = "ui.link_sensitivity"; // "1" / "0"

// Page ids, in order: "display", "audio", "controls", "gameplay".
gameui::SettingsSchema buildSettingsSchema(const Settings& s, const SchemaEnv& env);
// The two binding layers ("game" = [bindings], "menu" = [menu_bindings]) as read-only views with conflicts and
// reserved keys (part of buildSettingsSchema's result).
std::vector<gameui::BindingLayerView> buildBindingViews(const Settings& s);

// Applies one row's new value (SettingRow::key, the value a SettingChange carries) to `s`. Field keys go through the
// store's field table (setSettingValue); the pseudo keys are handled here. Changing the vertical sensitivity on its
// own unlinks it. False for an unknown key.
bool applySettingChange(Settings& s, bool& linkSensitivity, const std::string& key, const std::string& value);

} // namespace dsettings
