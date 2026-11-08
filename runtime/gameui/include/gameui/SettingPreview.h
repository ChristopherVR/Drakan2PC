// Setting previews: a small animated illustration of what a settings row does, drawn with plain DrawList primitives
// (no game art, nothing stored). The PC Settings help panel shows it for the focused row and it follows the row's live
// value: toggling VSync, stepping anisotropic filtering or dragging the field of view redraws the picture at once.
// Toggles are drawn as a split "Off | On" comparison with the current side lit; choices and sliders show the current
// value. std only, platform free. Contract: runtime/gameui/README.md, "PC Settings pages".
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "gameui/Draw.h"
#include "gameui/Layout.h"
#include "gameui/SettingsSchema.h"

namespace gameui {

// What the preview draws: the value the row shows right now (not necessarily the applied one).
struct PreviewState {
    bool on = false;   // Toggle: shown state
    std::string value; // Choice: the shown choice's value ("4:3", "bilinear", "8"); Toggle: unused
    float number = 0;  // Slider: the shown number
    double time = 0;   // seconds, drives the animation
};

// Real pictures: screenshots of the game itself, captured on the player's own machine (scripts/harness/setting_shots.py
// writes previews/settings/<key>/<value>.png next to the executable; nothing is stored in the repository). The host
// installs a resolver that maps (row key, shown value) to a picture id it can draw as ImageKind::LevelPreview (0 = no
// such capture). Settings with no visible effect on a still picture (window mode, VSync, volume, mouse speed ...) have
// no picture at all; the drawn illustration below is only the stand-in for a visual setting whose capture is missing.
using SettingShotResolver = std::function<uint32_t(const std::string& key, const std::string& value)>;
void setSettingShotResolver(SettingShotResolver resolver);
// The capture name for the shown value: "on" / "off", the choice value ("16:9"), the slider number ("90").
std::string settingShotValue(const SettingRow& row, const PreviewState& st);
// The picture id of the capture for the shown value, 0 when there is none (or no resolver).
uint32_t settingShotId(const SettingRow& row, const PreviewState& st);

// True when `key` has an illustration (a row key such as "video.vsync").
bool hasSettingPreview(const std::string& key);
// One line under the picture ("Showing: Bilinear", "Off | On"); empty when there is nothing to say.
std::string settingPreviewCaption(const SettingRow& row, const PreviewState& st);
// Draws the illustration for `row` into `box` (pixels). Returns false (and draws nothing) when the row has none.
bool drawSettingPreview(DrawList& d, const Rect& box, const SettingRow& row, const PreviewState& st, const Theme& th);

} // namespace gameui
