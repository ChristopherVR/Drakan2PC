// settings_pages_preview <outdir>
// The modern PC Settings pages (gameui::SettingsPages) over the real schema (dsettings::buildSettingsSchema) with the
// defaults of in-memory settings files, driven through ModernSettings like the game's glue, inside the modern pause
// menu (PauseConfig::pcSettingsPages). Writes each scene's DrawList as JSON lines; render them with
// runtime/gameui/tests/render_drawlist.py. No game data, no window.
#include <cstdio>
#include <filesystem>
#include <string>

#include "DrawListJson.h"
#include "gameui/PauseScreen.h"
#include "settings/ModernSettings.h"
#include "settings/Store.h"

using namespace gameui;

namespace {

struct PreviewHost final : dsettings::ISettingsHost {
    dsettings::HostCaps capabilities() const override {
        dsettings::HostCaps c; // as drakan_settings_host.cpp reports on an RTX machine
        c.frameLimiter = true;
        c.textureUpscale = c.anisotropic = c.mipBias = true;
        c.hdTextures = c.textureDump = true;
        c.textureUpscaleFilters = {"default", "xbrz", "smooth"};
        c.languageSelect = c.promptStyle = true;
        c.dlss = true;
        c.dlssStatus = "DLSS Super Resolution 310.2 ready on NVIDIA GeForce RTX 4080.";
        c.modernWater = true;
        c.rayTracingStatus = "DXR 1.1: shadows and reflections available.";
        c.restartKeys = {"video.world_rate", "general.language", "general.ui_prompts"};
        return c;
    }
    dsettings::MonitorInfo monitor() const override {
        dsettings::MonitorInfo m;
        m.nativeWidth = 2560;
        m.nativeHeight = 1440;
        m.refreshHz = 144;
        return m;
    }
    std::vector<std::string> audioDevices() const override {
        return {"Speakers (Realtek High Definition Audio)", "Headphones (USB Audio)"};
    }
    dsettings::ApplyResult applyVideo(const dsettings::VideoSettings&, const dsettings::VideoSettings&) override {
        return {};
    }
    dsettings::ApplyResult applyAudio(const dsettings::AudioSettings&, const dsettings::AudioSettings&) override {
        return {};
    }
    void reloadBindings(const std::string&) override {}
    void requestRestart(const std::vector<std::string>&) override {}
};

struct Scene {
    PreviewHost host;
    std::string drakan, input;
    dsettings::SettingsStore store;
    dsettings::ModernSettings ms;
    PauseScreen pause;
    PauseState st;
    Snapshot snap;
    Viewport vp;
    double now = 1.0;

    static dsettings::FileIO io(std::string& d, std::string& i) {
        dsettings::FileIO f;
        f.read = [&](const std::string& path, std::string& out) {
            out = path == "drakan.ini" ? d : i;
            return true;
        };
        f.write = [&](const std::string& path, const std::string& text) {
            (path == "drakan.ini" ? d : i) = text;
            return true;
        };
        return f;
    }
    static PauseConfig config() {
        PauseConfig c;
        c.hostPages = c.quitCard = c.pcSettingsPages = true;
        return c;
    }
    explicit Scene(Viewport v = {0, 0, 1920, 1080})
        : store({}, io(drakan, input)), ms(host, store), pause(config()), vp(v) {
        ms.open();
        st.options.valid = true;
        pause.open();
        frame({});
        frame({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}); // Options
        frame({UiKey::Down, UiKey::Down, UiKey::Confirm});              // PC Settings
    }
    PauseFrameOutput frame(std::initializer_list<UiKey> k, UiInput in = {}) {
        for (UiKey x : k)
            in.keys.push_back(x);
        now += 1.0 / 60.0;
        st.now = now;
        st.pcSettings = &ms.schema();
        PauseFrameOutput o = pause.frame(snap, st, in, vp);
        ms.applyAll(o.settings, pause.pcSettings());
        st.capturedKey.clear();
        return o;
    }
    // One more frame without input (the schema now shows the applied values), written out.
    void shot(const std::filesystem::path& dir, const std::string& name, UiInput in = {}) {
        const PauseFrameOutput o = frame({}, in);
        test::write(dir, name, vp, "world", {}, o.draw);
    }
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: settings_pages_preview <outdir>\n");
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    struct Aspect {
        const char* name;
        Viewport vp;
    } aspects[] = {{"16x9", {0, 0, 1920, 1080}}, {"21x9", {0, 0, 2560, 1080}}, {"4x3", {0, 0, 1440, 1080}}};

    for (const Aspect& a : aspects) {
        {
            Scene s(a.vp);
            s.shot(dir, std::string("pcs_landing_") + a.name);
        }
        {
            Scene s(a.vp);
            s.frame({UiKey::Confirm});
            s.shot(dir, std::string("pcs_display_") + a.name);
        }
    }
    {
        Scene s;
        s.frame({UiKey::Confirm});
        for (int i = 0; i < 12; ++i)
            s.frame({UiKey::Down});
        s.shot(dir, "pcs_display_scrolled_16x9");
        for (int i = 0; i < 16; ++i)
            s.frame({UiKey::Down});
        s.shot(dir, "pcs_display_dlss_16x9");
    }
    {
        Scene s; // the aspect ratio list card
        s.frame({UiKey::Confirm});
        s.frame({UiKey::Down, UiKey::Down});
        s.frame({UiKey::Confirm});
        s.shot(dir, "pcs_display_choices_16x9");
    }
    {
        Scene s; // window mode -> borderless: the keep card
        s.frame({UiKey::Confirm});
        s.frame({UiKey::Right});
        s.shot(dir, "pcs_display_keep_16x9");
    }
    {
        Scene s; // world frame rate -> 60: the restart card, then the help panel's footer
        s.frame({UiKey::Confirm});
        for (int i = 0; i < 6; ++i)
            s.frame({UiKey::Down});
        s.frame({UiKey::Right});
        s.shot(dir, "pcs_display_restart_16x9");
        s.frame({UiKey::Confirm}); // Later
        s.shot(dir, "pcs_display_restart_later_16x9");
    }
    {
        Scene s; // Audio, dragging the master volume
        s.frame({UiKey::Down, UiKey::Confirm});
        s.frame({UiKey::Down});
        const Rect t = s.pause.pcSettings().valueRect("audio.volume");
        UiInput in;
        in.pointerValid = in.pointerMoved = in.leftClick = in.leftDown = true;
        in.pointerX = t.x + t.w * 0.62f;
        in.pointerY = t.cy();
        s.frame({}, in);
        in.leftClick = false;
        in.pointerX = t.x + t.w * 0.7f;
        s.shot(dir, "pcs_audio_drag_16x9", in);
    }
    {
        Scene s;
        s.frame({UiKey::Down, UiKey::Down, UiKey::Confirm});
        s.frame({UiKey::Down, UiKey::Down});
        s.shot(dir, "pcs_controls_16x9");
    }
    {
        Scene s;
        s.frame({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
        s.frame({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down});
        s.shot(dir, "pcs_gameplay_16x9");
    }
    for (const Aspect& a : aspects) {
        Scene s(a.vp); // Key Bindings: gameplay keys, focus on Jump
        s.frame({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm});
        for (int i = 0; i < 6; ++i)
            s.frame({UiKey::Down});
        s.shot(dir, std::string("pcs_bindings_") + a.name);
        if (std::string(a.name) != "16x9")
            continue;
        s.frame({UiKey::Confirm});
        s.shot(dir, "pcs_bindings_capture_16x9");
        s.st.capturedKey = "W"; // Forward's key: the conflict question
        s.frame({});
        s.shot(dir, "pcs_bindings_conflict_16x9");
    }
    {
        Scene s; // the Controller page's Key Bindings row opens the gameplay keys directly
        Scene* p = &s;
        p->frame({UiKey::Back});                                           // back to Options
        p->frame({UiKey::Up, UiKey::Up, UiKey::Confirm});                  // Controller
        p->frame({UiKey::Down, UiKey::Down, UiKey::Down, UiKey::Confirm}); // Key Bindings
        p->shot(dir, "pcs_keybindings_from_controller_16x9");
    }
    return 0;
}
