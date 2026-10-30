// The settings schema (settings/Schema.h) both settings UIs draw, and ModernSettings, the modern pages' apply path.
// In-memory files and a fake host; no window, GPU or ImGui.
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>

#include "settings/Bindings.h"
#include "settings/ModernSettings.h"
#include "settings/Schema.h"
#include "settings/Store.h"

using namespace dsettings;

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

struct Host final : ISettingsHost {
    int video = 0, audio = 0, general = 0, reloads = 0, restarts = 0;
    VideoSettings lastVideo;
    AudioSettings lastAudio;
    std::vector<std::string> restartReasons;
    HostCaps caps;
    Host() {
        caps.frameLimiter = caps.anisotropic = caps.mipBias = caps.textureUpscale = true;
        caps.hdTextures = caps.textureDump = caps.languageSelect = caps.promptStyle = true;
        caps.textureUpscaleFilters = {"default", "xbrz"};
        caps.dlss = caps.dlssNeural = caps.modernWater = caps.exclusiveFullscreen = caps.rendererChoice = true;
        caps.restartKeys = {"video.world_rate", "general.language", "general.ui_prompts"};
    }
    HostCaps capabilities() const override { return caps; }
    MonitorInfo monitor() const override {
        MonitorInfo m;
        m.nativeWidth = 2560;
        m.nativeHeight = 1440;
        m.modes = {{1920, 1080, 60}, {2560, 1440, 144}};
        return m;
    }
    std::vector<std::string> audioDevices() const override { return {"Speakers", "Headphones"}; }
    ApplyResult applyVideo(const VideoSettings& n, const VideoSettings&) override {
        ++video;
        lastVideo = n;
        return {};
    }
    ApplyResult applyAudio(const AudioSettings& n, const AudioSettings&) override {
        ++audio;
        lastAudio = n;
        return {};
    }
    ApplyResult applyGeneral(const GeneralSettings&, const GeneralSettings&) override {
        ++general;
        return {};
    }
    void reloadBindings(const std::string&) override { ++reloads; }
    void requestRestart(const std::vector<std::string>& r) override {
        ++restarts;
        restartReasons = r;
    }
};

struct Files {
    std::string drakan, input;
    int writes = 0;
    FileIO io() {
        FileIO f;
        f.read = [this](const std::string& path, std::string& out) {
            out = path == "drakan.ini" ? drakan : input;
            return true;
        };
        f.write = [this](const std::string& path, const std::string& text) {
            ++writes;
            (path == "drakan.ini" ? drakan : input) = text;
            return true;
        };
        return f;
    }
};

std::vector<const gameui::SettingRow*> allRows(const gameui::SettingsSchema& sc) {
    std::vector<const gameui::SettingRow*> v;
    for (const auto& p : sc.pages)
        for (const auto& s : p.sections)
            for (const auto& r : s.rows)
                v.push_back(&r);
    return v;
}

SchemaEnv env(const Host& h, bool link = true) {
    SchemaEnv e;
    e.caps = h.capabilities();
    e.monitor = h.monitor();
    e.audioDevices = h.audioDevices();
    e.linkSensitivity = link;
    return e;
}

void testCoverage() {
    Host h;
    const Settings s;
    const gameui::SettingsSchema sc = buildSettingsSchema(s, env(h));
    CHECK(sc.pages.size() == 4 && sc.pages[0].id == "display" && sc.pages[3].id == "gameplay");
    std::set<std::string> keys;
    for (const auto* r : allRows(sc)) {
        CHECK(!r->key.empty() && !r->label.empty());
        CHECK(keys.insert(r->key).second); // unique
        if (r->kind != gameui::SettingKind::Info)
            CHECK(!r->help.empty());
    }
    // Every setting of the field table has a row (the window size keys are the size presets row and the overlay's
    // free size field; `fullscreen` is written together with window_mode). This is what makes a new setting show up
    // in the F1 overlay and the modern pages: add its row to Schema.cpp.
    for (const std::string& k : settingKeys()) {
        if (k == "video.fullscreen" || k == "video.window_width" || k == "video.window_height")
            continue;
        if (!keys.count(k))
            std::printf("  no schema row for %s\n", k.c_str());
        CHECK(keys.count(k) == 1);
    }
    CHECK(keys.count(kKeyWindowSize) && keys.count(kKeyWindowCustom) && keys.count(kKeyLinkSensitivity));
    CHECK(keys.count("general.dual_wield"));
    // Scopes: the overlay's own size and free size field are overlay only; the link switch is modern only.
    CHECK(sc.find("general.settings_ui_scale")->scope == gameui::SettingScope::OverlayOnly);
    CHECK(sc.find(kKeyWindowCustom)->scope == gameui::SettingScope::OverlayOnly);
    CHECK(sc.find(kKeyLinkSensitivity)->scope == gameui::SettingScope::ModernOnly);
    // Restart marks and risky rows.
    CHECK(sc.find("video.world_rate")->restart && sc.find("general.language")->restart);
    CHECK(sc.find("video.window_mode")->confirm && sc.find(kKeyWindowSize)->confirm && sc.find("video.scale")->confirm);
    CHECK(!sc.find("video.vsync")->confirm);
    // Bindings: both layers, every action of the catalogue, conflicts listed.
    CHECK(sc.bindings.size() == 2 && sc.bindings[0].id == "game" && sc.bindings[1].id == "menu");
    size_t n = 0;
    for (const auto& g : sc.bindings[0].groups)
        n += g.actions.size();
    CHECK(n == gameActions().size());
}

void testRoundTrip() {
    Host h;
    Settings s;
    bool link = true;
    const gameui::SettingsSchema sc = buildSettingsSchema(s, env(h));
    for (const auto* r0 : allRows(sc)) {
        const std::string key = r0->key;
        std::vector<std::string> values;
        if (r0->kind == gameui::SettingKind::Toggle)
            values = {r0->onValue, r0->offValue};
        else if (r0->kind == gameui::SettingKind::Choice)
            for (const auto& c : r0->choices)
                values.push_back(c.value);
        for (const std::string& v : values) {
            Settings t = s;
            CHECK(applySettingChange(t, link, key, v));
            const gameui::SettingsSchema sc2 = buildSettingsSchema(t, env(h, link));
            const gameui::SettingRow* r = sc2.find(key);
            if (!r || r->kind == gameui::SettingKind::Info)
                continue; // the row may move (window size in fullscreen)
            if (r->currentValue != v)
                std::printf("  %s: set %s, reads %s\n", key.c_str(), v.c_str(), r->currentValue.c_str());
            CHECK(r->currentValue == v);
            if (r->kind == gameui::SettingKind::Choice && key != kKeyWindowSize)
                CHECK(r->current >= 0 && r->choices[static_cast<size_t>(r->current)].value == v);
            if (r->kind == gameui::SettingKind::Toggle)
                CHECK(r->on == (v == r->onValue));
        }
    }
    // Sliders accept the numbers the pages send.
    Settings t;
    CHECK(applySettingChange(t, link, "video.fov", "95") && t.video.fieldOfView == 95.0f);
    CHECK(applySettingChange(t, link, "audio.volume", "150") && t.audio.volume == 150);
    CHECK(applySettingChange(t, link, kKeyWindowSize, "1600x900") && t.video.windowWidth == 1600 &&
          t.video.windowHeight == 900);
    CHECK(!applySettingChange(t, link, kKeyWindowSize, "big"));
    CHECK(!applySettingChange(t, link, "video.no_such_key", "1"));
    // Linked sensitivity: x drags y along; y alone unlinks.
    link = true;
    CHECK(applySettingChange(t, link, "input.mouse.sensitivity_x", "0.004") && t.controls.sensY == t.controls.sensX);
    CHECK(applySettingChange(t, link, "input.mouse.sensitivity_y", "0.003") && !link && t.controls.sensX > 0.0039f);
    CHECK(applySettingChange(t, link, kKeyLinkSensitivity, "1") && link && t.controls.sensY == t.controls.sensX);
    // Dependent rows say what they wait for.
    const gameui::SettingsSchema sc3 = buildSettingsSchema(Settings{}, env(h));
    CHECK(!sc3.find("video.dlss_preset")->enabled && !sc3.find("video.dlss_preset")->needs.empty());
    CHECK(!sc3.find("video.ray_reflections")->enabled);
}

void testModernApply() {
    Files f;
    f.drakan = "[video]\nvsync = 1\n";
    SettingsStore store({}, f.io());
    store.load();
    Host h;
    ModernSettings ms(h, store);
    ms.open();
    CHECK(ms.schema().find("video.vsync")->on);
    auto change = [&](const char* k, const char* v, bool final = true) {
        gameui::SettingsAction a;
        a.key = k;
        a.value = v;
        a.final = final;
        return ms.apply(a);
    };
    // A live setting: applied through the host and saved, nothing else.
    gameui::SettingsFeedback fb = change("video.vsync", "0");
    CHECK(fb.ok && fb.restartReasons.empty());
    CHECK(h.video == 1 && !h.lastVideo.vsync);
    CHECK(f.drakan.find("vsync = 0") != std::string::npos);
    CHECK(!ms.schema().find("video.vsync")->on);
    // A restart setting: saved, never sent live, the restart card's reason.
    fb = change("video.world_rate", "60");
    CHECK(fb.restartReasons == std::vector<std::string>{"World frame rate"});
    CHECK(h.video == 1); // world rate alone: no live video apply
    CHECK(f.drakan.find("world_rate = 60") != std::string::npos);
    // A slider drag: live while held, saved once on release.
    const int writes = f.writes;
    change("audio.volume", "150", false);
    change("audio.volume", "160", false);
    CHECK(h.audio == 2 && h.lastAudio.volume == 160 && f.writes == writes);
    change("audio.volume", "160", true);
    CHECK(f.writes == writes + 1 && f.drakan.find("volume = 160") != std::string::npos);
    // A revert (the keep card) goes through the same path.
    change("video.window_mode", "borderless");
    CHECK(h.lastVideo.windowMode == WindowMode::Borderless);
    gameui::SettingsAction rv;
    rv.key = "video.window_mode";
    rv.value = "windowed";
    rv.revert = true;
    ms.apply(rv);
    CHECK(h.lastVideo.windowMode == WindowMode::Windowed &&
          f.drakan.find("window_mode = windowed") != std::string::npos);
    // Bindings: bind, clear, reset; drakan_input.ini written and reloaded.
    gameui::SettingsAction b;
    b.kind = gameui::SettingsActionKind::Bind;
    b.layer = "game";
    b.action = "TRIANGLE";
    b.slot = 1;
    b.source = "Q";
    ms.apply(b);
    CHECK(h.reloads == 1);
    CHECK(std::find(ms.applied().controls.game.sourcesFor("TRIANGLE").begin(),
                    ms.applied().controls.game.sourcesFor("TRIANGLE").end(),
                    "Q") != ms.applied().controls.game.sourcesFor("TRIANGLE").end());
    {
        // Saved: the store re-read drakan_input.ini.
        const auto src = store.loaded().controls.game.sourcesFor("TRIANGLE");
        CHECK(std::find(src.begin(), src.end(), "Q") != src.end() && f.input.find("[bindings]") != std::string::npos);
    }
    {
        const auto& acts = ms.schema().bindings[0];
        bool shown = false;
        for (const auto& g : acts.groups)
            for (const auto& a : g.actions)
                if (a.id == "TRIANGLE")
                    for (const auto& sl : a.slots)
                        shown = shown || sl.source == "Q";
        CHECK(shown);
    }
    gameui::SettingsAction rs;
    rs.kind = gameui::SettingsActionKind::ResetBindings;
    rs.layer = "game";
    ms.apply(rs);
    CHECK(ms.applied().controls.game == store.defaults().controls.game);
    // Page defaults.
    gameui::SettingsAction rp;
    rp.kind = gameui::SettingsActionKind::ResetPage;
    rp.page = "audio";
    ms.apply(rp);
    CHECK(ms.applied().audio.volume == 100 && f.drakan.find("volume = 100") != std::string::npos);
    // Restart now.
    gameui::SettingsAction rr;
    rr.kind = gameui::SettingsActionKind::Restart;
    rr.value = "World frame rate\nLanguage";
    ms.apply(rr);
    CHECK(h.restarts == 1 && h.restartReasons == (std::vector<std::string>{"World frame rate", "Language"}));
    // Problems.
    fb = change("video.no_such_key", "1");
    CHECK(!fb.ok && !fb.error.empty());
    // The schema revision moves with every apply.
    const uint32_t r0 = ms.schema().revision;
    change("video.vsync", "1");
    CHECK(ms.schema().revision > r0);
}

} // namespace

int main() {
    {
        Host host;
        host.caps.internalResolution = false;
        host.caps.directMouseLook = false;
        host.caps.unavailableBindingActions = {"QUICKSAVE", "QUICKLOAD"};
        host.caps.unavailableSettings = {"video.aspect"};
        auto schema = buildSettingsSchema(Settings{}, env(host));
        bool scale = false, aspect = false, audio = false, look = false;
        for (const auto* row : allRows(schema)) {
            if (row->key == "input.general.look_mode") {
                look = true;
                CHECK(!row->enabled && row->currentValue == "stick");
                CHECK(row->current == 1 && row->choices[1].label == "Right stick emulation");
            }
            if (row->key == "video.scale") {
                scale = true;
                CHECK(!row->enabled);
            }
            if (row->key == "video.aspect") {
                aspect = true;
                CHECK(!row->enabled);
            }
            if (row->key == "audio.volume") {
                audio = true;
                CHECK(row->enabled);
            }
        }
        CHECK(scale && aspect && audio && look);
        bool pauseBinding = false;
        for (const auto& layer : schema.bindings)
            for (const auto& group : layer.groups)
                for (const auto& action : group.actions) {
                    CHECK(action.id != "QUICKSAVE" && action.id != "QUICKLOAD");
                    pauseBinding = pauseBinding || action.id == "START";
                }
        CHECK(pauseBinding);
    }
    {
        // The NVIDIA credit belongs to NVIDIA DLSS (Direct3D 11), not to the WebGPU temporal AA behind the same rows.
        const auto credited = [](bool nvidia) {
            Host host;
            host.caps.dlssNeural = false;
            host.caps.dlssNvidia = nvidia;
            const auto schema = buildSettingsSchema(Settings{}, env(host));
            const auto rows = allRows(schema);
            return std::any_of(rows.begin(), rows.end(),
                               [](const auto* row) { return row->key == "video.dlss_credit"; });
        };
        CHECK(credited(true));
        CHECK(!credited(false));
    }
    testCoverage();
    testRoundTrip();
    testModernApply();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
