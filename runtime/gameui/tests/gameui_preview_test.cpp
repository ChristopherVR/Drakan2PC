// Setting previews (SettingPreview.h) and their place in the PC Settings help panel: the illustration follows the
// focused row's live value, and a Mods page section shows its pack picture and details beside the list.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <cstdlib>
#include <filesystem>

#include "DrawListJson.h"
#include "SettingsFixture.h"
#include "gameui/SettingPreview.h"
#include "gameui/SettingsPages.h"

using namespace gameui;

static int checks = 0, failures = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(c)) {                                                                                                    \
            ++failures;                                                                                                \
            std::printf("FAIL %d: %s\n", __LINE__, #c);                                                                \
        }                                                                                                              \
    } while (0)

namespace {

bool hasText(const DrawList& d, const std::string& s) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Text && c.text.find(s) != std::string::npos)
            return true;
    return false;
}

void testPictureKeys() {
    for (const char* key :
         {"video.aspect", "video.fov", "video.hud_scale", "video.hud_aspect_correct", "video.scale", "video.filter",
          "video.anisotropic", "video.hd_textures", "video.modern_water", "video.ray_reflections", "video.ray_shadows",
          "video.ray_gi", "video.foliage_wind", "video.foliage_wind_strength"})
        CHECK(hasSettingPreview(key));
    // Settings with no visible effect on a still picture have no picture at all.
    for (const char* key :
         {"video.vsync", "video.world_rate", "video.frame_limit", "video.window_mode", "video.flicker_filter",
          "video.sharp_present", "video.smooth_menu_highlight", "audio.enabled", "audio.volume", "input.mouse.invert_y",
          "input.mouse.sensitivity_x", "input.gamepad.deadzone", "general.ui_prompts", "video.texture_dump"})
        CHECK(!hasSettingPreview(key));
    // The capture is named by the shown value.
    SettingRow t, c, sl;
    t.key = "video.hd_textures";
    t.kind = SettingKind::Toggle;
    c.key = "video.aspect";
    c.kind = SettingKind::Choice;
    sl.key = "video.fov";
    sl.kind = SettingKind::Slider;
    PreviewState st;
    st.on = true;
    st.value = "16:9";
    st.number = 93;
    CHECK(settingShotValue(t, st) == "on" && settingShotValue(c, st) == "16:9" && settingShotValue(sl, st) == "93");
    // Without a resolver there is no picture; with one, only a setting that has pictures asks it.
    CHECK(settingShotId(sl, st) == 0);
    setSettingShotResolver([](const std::string& key, const std::string& value) -> uint32_t {
        return key == "video.fov" && value == "93" ? 7u : 0u;
    });
    CHECK(settingShotId(sl, st) == 7);
    SettingRow other;
    other.key = "video.vsync";
    other.kind = SettingKind::Toggle;
    CHECK(settingShotId(other, st) == 0);
    setSettingShotResolver({});
}

struct Pages {
    test::SettingsWorld w;
    SettingsPages pg;
    double t = 1.0;
    SettingsFrameOutput frame(UiKey k, bool press) {
        SettingsInput in;
        if (press)
            in.ui.keys = {k};
        t += 1.0 / 60.0;
        in.now = t;
        return pg.frame(w.schema, in, {0, 0, 1920, 1080});
    }
};

void testHelpPanelShowsPreview() {
    Pages p;
    p.pg.open("display");
    SettingsFrameOutput o = p.frame(UiKey::Down, false);
    for (int i = 0; i < 20 && p.pg.focusedKey() != "video.hd_textures"; ++i)
        o = p.frame(UiKey::Down, true);
    CHECK(p.pg.focusedKey() == "video.hd_textures");
    o = p.frame(UiKey::Down, false);
    // Without a capture nothing is drawn in the picture's place (no stand-in illustration).
    bool shot = false;
    for (const DrawCmd& c : o.draw.commands())
        shot = shot || (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview);
    CHECK(!shot);
    // With a real capture of the game for the shown value, the help panel draws that picture.
    setSettingShotResolver([](const std::string& key, const std::string& value) -> uint32_t {
        return key == "video.hd_textures" && (value == "on" || value == "off") ? 0x77u : 0u;
    });
    o = p.frame(UiKey::Down, false);
    shot = false;
    for (const DrawCmd& c : o.draw.commands())
        shot = shot || (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview && c.image.index == 0x77);
    CHECK(shot);
    // A setting with no visible effect gets neither a picture nor an illustration, whatever the resolver says.
    setSettingShotResolver([](const std::string&, const std::string&) -> uint32_t { return 0x99u; });
    for (int i = 0; i < 40 && p.pg.focusedKey() != "video.vsync"; ++i)
        o = p.frame(UiKey::Up, true);
    o = p.frame(UiKey::Up, false);
    shot = false;
    for (const DrawCmd& c : o.draw.commands())
        shot = shot || (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview && c.image.index == 0x99);
    CHECK(p.pg.focusedKey() == "video.vsync" && !shot);
    setSettingShotResolver({});
}

uint32_t shownShot(const DrawList& d) {
    for (const DrawCmd& c : d.commands())
        if (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview)
            return c.image.index;
    return 0;
}

// A capture that the host has not decoded yet must not blank the box: the last ready picture stays up until the new
// one is ready, and a picture that was ready once is drawn again at once (no reload, no flash).
void testPreviewKeepsLastReadyPicture() {
    uint32_t pick = 0;
    CHECK(settingShotToDraw(0, pick) == 0 && pick == 0);
    std::vector<uint32_t> ready;
    setSettingShotReady([&ready](uint32_t id) { return std::find(ready.begin(), ready.end(), id) != ready.end(); });
    CHECK(settingShotToDraw(5, pick) == 5 && pick == 0); // nothing ready yet: the wanted one (an empty box)
    ready.push_back(5);
    CHECK(settingShotToDraw(5, pick) == 5 && pick == 5);
    CHECK(settingShotToDraw(6, pick) == 5 && pick == 5); // 6 decoding: 5 stays
    ready.push_back(6);
    CHECK(settingShotToDraw(6, pick) == 6 && pick == 6);
    CHECK(settingShotToDraw(5, pick) == 5 && pick == 5); // back to a decoded one: immediately

    // Through the help panel: toggling HD textures switches on -> off while "off" is still decoding.
    Pages p;
    p.pg.open("display");
    SettingsFrameOutput o = p.frame(UiKey::Down, false);
    for (int i = 0; i < 20 && p.pg.focusedKey() != "video.hd_textures"; ++i)
        o = p.frame(UiKey::Down, true);
    CHECK(p.pg.focusedKey() == "video.hd_textures");
    setSettingShotResolver([](const std::string& key, const std::string& value) -> uint32_t {
        return key == "video.hd_textures" ? (value == "on" ? 0x10u : 0x20u) : 0u;
    });
    ready = {0x10u, 0x20u};
    o = p.frame(UiKey::Down, false);
    const uint32_t first = shownShot(o.draw);
    CHECK(first == 0x10u || first == 0x20u);
    const uint32_t second = first == 0x10u ? 0x20u : 0x10u;
    ready = {first}; // the other value's capture is not decoded yet
    o = p.frame(UiKey::Right, true);
    CHECK(shownShot(o.draw) == first);
    for (int i = 0; i < 5; ++i) {
        o = p.frame(UiKey::Right, false);
        CHECK(shownShot(o.draw) == first); // every frame: no empty flash in between
    }
    ready.push_back(second);
    o = p.frame(UiKey::Right, false);
    CHECK(shownShot(o.draw) == second);
    setSettingShotResolver({});
    setSettingShotReady({});
}

void testModPackSection() {
    Pages p;
    SettingPage mods;
    mods.id = "mods";
    mods.title = "Mods";
    mods.description = "A mod pack changes the game.";
    SettingSection s{"Wild Ruins  v1.2", {}};
    s.rows.push_back(test::toggleRow("mods.enable.wild", "Enabled", false, "Turn this pack on or off."));
    s.picture = 0x1234;
    s.details = {{"By", "Ada"}, {"About", "Overgrown ruins and a new valley."}};
    s.hint = "Off. Select Enabled and press Enter to turn it on.";
    mods.sections.push_back(s);
    SettingSection plain{"Other", {}};
    plain.rows.push_back(test::toggleRow("mods.enable.plain", "Enabled", false, "Turn this pack on or off."));
    plain.details = {{"Status", "Disabled"}};
    mods.sections.push_back(plain);
    p.w.schema.pages.push_back(mods);
    p.pg.open("mods");
    SettingsFrameOutput o = p.frame(UiKey::Down, false);
    CHECK(p.pg.focusedKey() == "mods.enable.wild");
    bool image = false;
    for (const DrawCmd& c : o.draw.commands())
        image =
            image || (c.kind == DrawKind::Image && c.image.kind == ImageKind::LevelPreview && c.image.index == 0x1234);
    CHECK(image);
    CHECK(hasText(o.draw, "By: Ada") && hasText(o.draw, "Overgrown ruins"));
    CHECK(hasText(o.draw, "press Enter to turn it on"));
    // A pack without a picture says so instead of leaving a hole.
    for (int i = 0; i < 6 && p.pg.focusedKey() != "mods.enable.plain"; ++i)
        o = p.frame(UiKey::Down, true);
    o = p.frame(UiKey::Down, false);
    CHECK(p.pg.focusedKey() == "mods.enable.plain");
    CHECK(hasText(o.draw, "No preview picture"));
}

} // namespace

// GAMEUI_PREVIEW_DIR=<dir>: writes the help panel with several previews as JSON lines (render_drawlist.py).
void dumpFrames(const char* dir) {
    std::filesystem::create_directories(dir);
    const char* keys[] = {"video.scale", "video.fov", "video.hd_textures", "video.aspect"};
    for (const char* key : keys) {
        Pages p;
        p.pg.open("display");
        SettingsFrameOutput o = p.frame(UiKey::Down, false);
        for (int i = 0; i < 30 && p.pg.focusedKey() != key; ++i)
            o = p.frame(UiKey::Down, true);
        for (int i = 0; i < 10; ++i)
            o = p.frame(UiKey::Down, false);
        std::string name = std::string("help_") + key;
        test::write(dir, name, {0, 0, 1920, 1080}, "world", {}, o.draw);
    }
    Pages p;
    SettingPage mods;
    mods.id = "mods";
    mods.title = "Mods";
    mods.description = "A mod pack changes the game.";
    SettingSection s{"Wild Ruins  v1.2", {}};
    s.rows.push_back(test::toggleRow("mods.enable.wild", "Enabled", true, "Turn this pack on or off."));
    s.picture = 0;
    s.details = {{"By", "Ada"},
                 {"Status", "Active"},
                 {"Target disc", "any disc"},
                 {"About", "Overgrown ruins and a new valley to explore after the first level."}};
    s.hint = "On. Select Enabled and press Enter to turn it off.";
    mods.sections.push_back(s);
    p.w.schema.pages.push_back(mods);
    p.pg.open("mods");
    SettingsFrameOutput o = p.frame(UiKey::Down, false);
    for (int i = 0; i < 5; ++i)
        o = p.frame(UiKey::Down, false);
    test::write(dir, "help_mods", {0, 0, 1920, 1080}, "world", {}, o.draw);
}

int main() {
    if (const char* dir = std::getenv("GAMEUI_PREVIEW_DIR"))
        dumpFrames(dir);
    testPictureKeys();
    testHelpPanelShowsPreview();
    testPreviewKeepsLastReadyPicture();
    testModPackSection();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
