// Setting previews: a picture of the game itself under the focused setting in the PC Settings help panel. The pictures
// are screenshots of this game with the value applied (previews/settings/<row key>/<value>.png next to the executable),
// made on the player's machine by scripts/harness/setting_shots.py (the build runs it) and never committed. The host
// installs a resolver that maps (row key, shown value) to a picture id it can draw as ImageKind::LevelPreview, 0 when
// there is no capture of that value. A setting without a capture shows plain help: nothing is drawn in its place.
// Settings with no visible effect on a still picture (window mode, VSync, volume, mouse speed ...) have no pictures at
// all. std only, platform free. Contract: runtime/gameui/README.md, "PC Settings pages".
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "gameui/SettingsSchema.h"

namespace gameui {

// What the picture is chosen by: the value the row shows right now (not necessarily the applied one).
struct PreviewState {
    bool on = false;   // Toggle: shown state
    std::string value; // Choice: the shown choice's value ("4:3", "bilinear", "8")
    float number = 0;  // Slider: the shown number
};

using SettingShotResolver = std::function<uint32_t(const std::string& key, const std::string& value)>;
void setSettingShotResolver(SettingShotResolver resolver);
// The capture name for the shown value: "on" / "off", the choice value ("16:9"), the slider number ("90").
std::string settingShotValue(const SettingRow& row, const PreviewState& st);
// The picture id of the capture for the shown value, 0 when there is none (or no resolver).
uint32_t settingShotId(const SettingRow& row, const PreviewState& st);
// True when `key` is a setting that has pictures (the ones setting_shots.py makes).
bool hasSettingPreview(const std::string& key);

} // namespace gameui
