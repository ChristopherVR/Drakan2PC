// Setting previews (SettingPreview.h) and their place in the PC Settings help panel: the illustration follows the
// focused row's live value, and a Mods page section shows its pack picture and details beside the list.
#include <algorithm>
#include <cstdio>
#include <string>

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

void testEveryIllustrationDraws() {
    const Theme th;
    const Rect box{0, 0, 496, 224};
    const char* toggles[] = {"video.vsync",
                             "video.hd_textures",
                             "video.flicker_filter",
                             "video.sharp_present",
                             "video.hud_aspect_correct",
                             "video.poly_near_clip",
                             "video.smooth_menu_highlight",
                             "video.ray_shadows",
                             "video.ray_reflections",
                             "video.ray_gi",
                             "video.modern_water",
                             "video.foliage_wind",
                             "audio.enabled",
                             "input.mouse.invert_y"};
    for (const char* key : toggles) {
        CHECK(hasSettingPreview(key));
        SettingRow r;
        r.key = key;
        r.kind = SettingKind::Toggle;
        for (bool on : {false, true}) {
            DrawList d;
            PreviewState st;
            st.on = on;
            st.time = 1.25;
            CHECK(drawSettingPreview(d, box, r, st, th));
            CHECK(d.size() > 4);
        }
        CHECK(settingPreviewCaption(r, {}).find('|') != std::string::npos);
    }
    struct Ch {
        const char* key;
        const char* values[4];
    };
    const Ch choices[] = {{"video.aspect", {"4:3", "16:9", "21:9", "stretch"}},
                          {"video.scale", {"auto", "1", "4", "8"}},
                          {"video.filter", {"game", "point", "bilinear", "bilinear"}},
                          {"video.anisotropic", {"1", "2", "8", "16"}},
                          {"video.world_rate", {"uncapped", "60", "30", "30"}},
                          {"video.frame_limit", {"0", "30", "60", "144"}},
                          {"video.window_mode", {"windowed", "borderless", "exclusive", "exclusive"}},
                          {"video.hud_scale", {"0", "0.5", "1", "1.5"}},
                          {"general.ui_prompts", {"keyboard", "ps2", "ps2", "keyboard"}}};
    for (const Ch& c : choices) {
        CHECK(hasSettingPreview(c.key));
        SettingRow r;
        r.key = c.key;
        r.kind = SettingKind::Choice;
        for (const char* v : c.values) {
            DrawList d;
            PreviewState st;
            st.value = v;
            st.time = 0.7;
            CHECK(drawSettingPreview(d, box, r, st, th));
            CHECK(d.size() > 3);
        }
    }
    const char* sliders[] = {"video.fov", "video.foliage_wind_strength", "audio.volume", "input.mouse.sensitivity_x",
                             "input.gamepad.deadzone"};
    for (const char* key : sliders) {
        CHECK(hasSettingPreview(key));
        SettingRow r;
        r.key = key;
        r.kind = SettingKind::Slider;
        r.min = 0.0003f;
        r.max = 0.5f;
        r.logarithmic = std::string(key) == "input.mouse.sensitivity_x";
        for (float v : {0.0003f, 0.01f, 0.5f}) {
            DrawList d;
            PreviewState st;
            st.number = v;
            CHECK(drawSettingPreview(d, box, r, st, th));
        }
    }
    // A row without an illustration draws nothing.
    SettingRow none;
    none.key = "video.texture_dump";
    DrawList nd;
    CHECK(!hasSettingPreview(none.key));
    CHECK(!drawSettingPreview(nd, box, none, {}, th) && nd.size() == 0);
    // Different values draw different pictures (the preview follows the row).
    SettingRow aspect;
    aspect.key = "video.aspect";
    aspect.kind = SettingKind::Choice;
    DrawList a, b;
    PreviewState s43, s169;
    s43.value = "4:3";
    s169.value = "16:9";
    drawSettingPreview(a, box, aspect, s43, th);
    drawSettingPreview(b, box, aspect, s169, th);
    bool differs = a.size() != b.size();
    for (size_t i = 0; !differs && i < a.size(); ++i)
        differs = !(a.commands()[i].rect == b.commands()[i].rect);
    CHECK(differs);
    // The animation moves with time.
    SettingRow vs;
    vs.key = "video.vsync";
    vs.kind = SettingKind::Toggle;
    DrawList t0, t1;
    PreviewState p0, p1;
    p0.time = 0.0;
    p1.time = 0.9;
    drawSettingPreview(t0, box, vs, p0, th);
    drawSettingPreview(t1, box, vs, p1, th);
    bool moved = false;
    for (size_t i = 0; i < std::min(t0.size(), t1.size()); ++i)
        moved = moved || !(t0.commands()[i].rect == t1.commands()[i].rect);
    CHECK(moved);
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
    for (int i = 0; i < 20 && p.pg.focusedKey() != "video.vsync"; ++i)
        o = p.frame(UiKey::Down, true);
    CHECK(p.pg.focusedKey() == "video.vsync");
    o = p.frame(UiKey::Down, false);
    CHECK(hasText(o.draw, "tearing") || hasText(o.draw, "in step with the screen"));
    CHECK(hasText(o.draw, "Off") && hasText(o.draw, "On"));
    // A row with no illustration keeps the plain help.
    for (int i = 0; i < 20 && p.pg.focusedKey() != "video.texture_dump"; ++i)
        o = p.frame(UiKey::Down, true);
    o = p.frame(UiKey::Down, false);
    CHECK(!hasText(o.draw, "tearing") && !hasText(o.draw, "in step with the screen"));
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
    const char* keys[] = {"video.vsync",
                          "video.scale",
                          "video.fov",
                          "video.flicker_filter",
                          "video.hd_textures",
                          "video.sharp_present",
                          "video.smooth_menu_highlight",
                          "video.window_mode"};
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
    testEveryIllustrationDraws();
    testHelpPanelShowsPreview();
    testModPackSection();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
