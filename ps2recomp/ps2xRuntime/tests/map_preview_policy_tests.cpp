// drakan_map_preview_policy.h: a match no longer captures a random frame as the map's picture, and old captures in the
// user's data folder stop showing (issue #1).
#include "../src/lib/drakan_map_preview_policy.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {
bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

int main() {
    using namespace drakan_map_preview;
    // The in-match capture is off unless asked for with exactly "1".
    assert(!autoCaptureEnabled(nullptr));
    assert(!autoCaptureEnabled(""));
    assert(!autoCaptureEnabled("0"));
    assert(!autoCaptureEnabled("10"));
    assert(!autoCaptureEnabled("yes"));
    assert(autoCaptureEnabled("1"));

    const std::string exe = "C:/Drakan/", data = "C:/Users/p/AppData/Local/Drakan2PC/previews/";
    // Default: the player's own picture, the mod packs' pictures, then the generated pictures of the built-in maps
    // (map_previews.py); the old captures are not read.
    const auto plain = searchDirs(exe, "", data, false);
    assert((plain == std::vector<std::string>{exe + "previews/", exe + "previews/mods/", exe + "previews/maps/"}));
    assert(!has(plain, data));
    assert(!has(plain, exe + "previews/captured/"));
    // An explicit DRAKAN_PREVIEW_DIR is a picture source set on purpose: read after the exe folders, capture or not.
    const auto expl = searchDirs(exe, "D:/pics/", data, false);
    assert((expl ==
            std::vector<std::string>{exe + "previews/", exe + "previews/mods/", exe + "previews/maps/", "D:/pics/"}));
    // Capture on: the data folder and the old fallback come back, after the player's and the mods' pictures.
    const auto cap = searchDirs(exe, "", data, true);
    assert((cap == std::vector<std::string>{exe + "previews/", exe + "previews/mods/", exe + "previews/maps/", data,
                                            exe + "previews/captured/"}));
    // The data folder may be the fallback itself (no LOCALAPPDATA); listing it twice is harmless.
    assert(searchDirs(exe, "", "", true).size() == 4);
    std::puts("map_preview_policy: ok");
    return 0;
}
