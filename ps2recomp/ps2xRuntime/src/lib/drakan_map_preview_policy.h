// drakan_map_preview_policy.h: where the multiplayer menu looks for a map's picture, and whether a match captures one.
//
// The game used to capture "the current view" about 12 s into the first match on a map without a picture and cache it
// in the user's data folder. That is whatever the player happened to look at (a wall, a fall, a half-drawn frame), and
// the menu then showed it as the map's picture for good (issue #1). The automatic capture is now opt in
// (DRAKAN_MP_CAPTURE=1), and the data folder those captures went to is only read when it is on, so the old captures
// left on a player's disk stop showing. Maps without a picture get the drawn sketch.
#pragma once

#include <string>
#include <vector>

namespace drakan_map_preview {

// DRAKAN_MP_CAPTURE: "1" turns the in-match capture on; unset, empty, "0" or anything else leaves it off.
inline bool autoCaptureEnabled(const char* env) {
    return env && env[0] == '1' && env[1] == '\0';
}

// The folders searched for `<safe map name>.png`, first hit wins: the player's own picture next to the exe, then the
// mod packs' pictures, then an explicit DRAKAN_PREVIEW_DIR (a picture source set on purpose, e.g. by the harnesses),
// then, only with the capture on, the game's own captures (the data folder, the old previews/captured/ fallback).
// Every folder ends in '/'.
inline std::vector<std::string> searchDirs(const std::string& exeDir, const std::string& explicitDir,
                                           const std::string& dataDir, bool capturesOn) {
    std::vector<std::string> dirs{exeDir + "previews/", exeDir + "previews/mods/"};
    if (!explicitDir.empty())
        dirs.push_back(explicitDir);
    else if (capturesOn && !dataDir.empty())
        dirs.push_back(dataDir);
    if (capturesOn)
        dirs.push_back(exeDir + "previews/captured/");
    return dirs;
}

} // namespace drakan_map_preview
