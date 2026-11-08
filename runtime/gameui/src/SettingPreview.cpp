#include "gameui/SettingPreview.h"

#include <cstdio>
#include <utility>

namespace gameui {

namespace {
SettingShotResolver g_shotResolver;
SettingShotReady g_shotReady;

// The settings with pictures (scripts/harness/setting_shots.py PLAN).
const char* const kKeys[] = {
    "video.aspect",       "video.fov",
    "video.hud_scale",    "video.hud_aspect_correct",
    "video.scale",        "video.filter",
    "video.anisotropic",  "video.hd_textures",
    "video.modern_water", "video.ray_reflections",
    "video.ray_shadows",  "video.ray_gi",
    "video.foliage_wind", "video.foliage_wind_strength",
};
} // namespace

void setSettingShotResolver(SettingShotResolver resolver) {
    g_shotResolver = std::move(resolver);
}

std::string settingShotValue(const SettingRow& row, const PreviewState& st) {
    if (row.kind == SettingKind::Toggle)
        return st.on ? "on" : "off";
    if (row.kind == SettingKind::Choice)
        return st.value;
    char b[32];
    std::snprintf(b, sizeof b, "%g", static_cast<double>(st.number));
    return b;
}

bool hasSettingPreview(const std::string& key) {
    for (const char* k : kKeys)
        if (key == k)
            return true;
    return false;
}

uint32_t settingShotId(const SettingRow& row, const PreviewState& st) {
    return g_shotResolver && hasSettingPreview(row.key) ? g_shotResolver(row.key, settingShotValue(row, st)) : 0;
}

void setSettingShotReady(SettingShotReady ready) {
    g_shotReady = std::move(ready);
}

bool settingShotReady(uint32_t id) {
    return id != 0 && (!g_shotReady || g_shotReady(id));
}

uint32_t settingShotToDraw(uint32_t wanted, uint32_t& shown) {
    if (!wanted)
        return 0;
    if (settingShotReady(wanted)) {
        shown = wanted;
        return wanted;
    }
    return settingShotReady(shown) ? shown : wanted;
}

} // namespace gameui
