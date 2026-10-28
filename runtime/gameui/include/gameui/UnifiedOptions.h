// One Options screen for the modern menus (PauseConfig::unifiedOptions): the PC settings pages (Display, Audio,
// Controls, Gameplay, Key Bindings) with the game's own seven options folded into their pages. Audio gets the game's
// Master / Music volume, Subtitles and Stereo, Controls the Control scheme, Vibration and Reverse pitch, each in a
// "Game" section at the top of the page. Their rows carry the keys "game.<option>"; PauseScreen turns their
// SettingsActions into the pause stage 2 PauseActions (Set*, with the native Audio / Controller screen entered while
// that page is shown) instead of passing them to dsettings::ModernSettings. std only, platform free.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "gameui/PauseOptions.h"
#include "gameui/SettingsSchema.h"

namespace gameui {

inline constexpr const char* kGameOptionPrefix = "game.";

// "game.master_volume", "game.scheme" ...
std::string gameOptionKey(PauseOption o);
std::optional<PauseOption> gameOptionFromKey(const std::string& key);
// The settings page that shows the option: "audio" or "controls".
const char* gameOptionPage(PauseOption o);
// The game's defaults (what its Default Settings restores: 100 / 80 / on / on / A / on / off).
int32_t gameOptionDefault(PauseOption o);

// `out` = `pc` with the game's options (values from `o`; greyed out while `o.valid` is false) in the Audio and
// Controls pages. The PC master volume is labelled "Output volume" there (it scales the whole mix after the game's
// own volumes) and the Audio page's note about the game's own Options screen is replaced.
void foldGameOptions(const SettingsSchema& pc, const PauseOptionsState& o, SettingsSchema& out);

} // namespace gameui
