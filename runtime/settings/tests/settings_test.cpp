// Unit tests for the settings library core (ini round trips, bindings editing). No ImGui, no window.
//   settings_test [path\to\real\drakan_input.ini] [path\to\real\drakan.ini]
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "settings/Bindings.h"
#include "settings/Store.h"

using namespace dsettings;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)
static std::string toS(const std::string& s) {
    return s;
}
static std::string toS(const char* s) {
    return s;
}
template <class T, class = std::enable_if_t<std::is_arithmetic_v<T>>>
static std::string toS(T v) {
    return std::to_string(v);
}
#define CHECK_EQ(a, b)                                                                                                 \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!((a) == (b))) {                                                                                           \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s == %s\n   got: [%s]\n", __FILE__, __LINE__, #a, #b, toS(a).c_str());           \
        }                                                                                                              \
    } while (0)

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}
static size_t countOf(const std::string& s, const std::string& sub) {
    size_t n = 0, p = 0;
    while ((p = s.find(sub, p)) != std::string::npos) {
        ++n;
        p += sub.size();
    }
    return n;
}

static const char* kDrakanIni =
    "; drakan.ini - Drakan port settings. Environment variables (named in the comments) override these values.\r\n"
    "[general]\r\n"
    "; auto-dismiss the memory card warning\r\n"
    "skip_memcard_warning = 1\r\n"
    "elf = C:\\Games\\SCES_500.06 ; my path\r\n"
    "\r\n"
    "[video]\r\n"
    "; 4:3 (original), 16:9 (stretched)\r\n"
    "renderer = d3d11\r\n"
    "scale = auto\r\n"
    "aspect = 4:3\r\n"
    "window_width = 1280\r\n"
    "window_height = 960\r\n"
    "fullscreen = 0\r\n"
    "vsync = 1   ; keep\r\n"
    "filter = game\r\n"
    "\r\n"
    "[audio]\r\n"
    "enabled = 1\r\n"
    "volume = 100\r\n"
    "device = Speakers (Realtek #2)\r\n"
    "\r\n"
    "[other_module]\r\n"
    "mystery = 42  # keep me\r\n";

static const char* kInputIni = "; my input file\n"
                               "[general]\n"
                               "capture_mouse = 1          ; hide cursor\n"
                               "look_mode = direct\n"
                               "toggle_key = F10\n"
                               "\n"
                               "[mouse]\n"
                               "sensitivity_x = 0.0020     ; radians per count\n"
                               "sensitivity_y = 0.0020\n"
                               "invert_y = 0\n"
                               "custom_key = hello         # unknown keys survive\n"
                               "\n"
                               "[bindings]\n"
                               "; movement\n"
                               "W = LSTICK_UP\n"
                               "SPACE = TRIANGLE           ; Jump\n"
                               "LCONTROL = CROSS\n"
                               "C = CROSS\n"
                               "MOUSE1 = SQUARE\n"
                               "ESC = START\n"
                               "; weapon slots\n"
                               "1 = ACT:0x02\n"
                               "\n"
                               "[menu_bindings]\n"
                               "ENTER = CROSS\n"
                               "ESC = START | CIRCLE       ; back and resume\n"
                               "\n"
                               "[unknown_section]\n"
                               "keep = me\n";

static void testIniDoc() {
    // identity
    for (const char* t : {kDrakanIni, kInputIni, "", "[a]\nk=v", "\xEF\xBB\xBF[a]\r\nk = v\r\n", "k = v\n\n\n"}) {
        IniDoc d = IniDoc::parse(t, ";#");
        CHECK(d.serialize() == t);
    }
    IniDoc d = IniDoc::parse(kDrakanIni, ";");
    CHECK_EQ(d.get("video", "scale"), "auto");
    CHECK_EQ(d.get("VIDEO", "Scale"), "auto");                   // case insensitive
    CHECK_EQ(d.get("general", "elf"), "C:\\Games\\SCES_500.06"); // trailing comment stripped
    CHECK_EQ(d.get("audio", "device"), "Speakers (Realtek #2)"); // '#' is not a comment in drakan.ini
    CHECK_EQ(d.get("video", "vsync"), "1");
    CHECK(!d.has("video", "nope"));

    // change one value: only that line differs, spacing + comment kept
    d.set("video", "vsync", "0");
    std::string s = d.serialize();
    CHECK(contains(s, "vsync = 0   ; keep\r\n"));
    CHECK(contains(s, "; 4:3 (original), 16:9 (stretched)\r\n"));
    CHECK(contains(s, "elf = C:\\Games\\SCES_500.06 ; my path\r\n"));
    CHECK_EQ(countOf(s, "\r\n"), countOf(kDrakanIni, "\r\n"));
    CHECK(!contains(s, "\r\r"));

    // new key in existing section: after the last key of that section, before the blank line
    d.set("video", "frame_limit", "60", "frame rate cap");
    s = d.serialize();
    const size_t pFilter = s.find("filter = game\r\n"), pFl = s.find("frame_limit = 60\r\n"),
                 pAudio = s.find("[audio]");
    CHECK(pFilter != std::string::npos && pFl != std::string::npos && pFilter < pFl && pFl < pAudio);
    CHECK(contains(s, "; frame rate cap\r\nframe_limit = 60\r\n\r\n[audio]"));

    // new section at the end
    d.set("settings_new", "x", "1");
    s = d.serialize();
    CHECK(contains(s, "mystery = 42  # keep me\r\n\r\n[settings_new]\r\nx = 1\r\n"));

    // last section without trailing newline
    IniDoc e = IniDoc::parse("[a]\nk = v", ";");
    e.set("a", "n", "2");
    CHECK_EQ(e.serialize(), "[a]\nk = v\nn = 2");
    e.set("b", "z", "3");
    CHECK_EQ(e.serialize(), "[a]\nk = v\nn = 2\n\n[b]\nz = 3\n");

    // remove
    IniDoc r = IniDoc::parse("[a]\nk = 1\nk = 2\nj = 3\n", ";");
    CHECK_EQ(r.get("a", "k"), "2"); // last wins
    r.remove("a", "k");
    CHECK_EQ(r.serialize(), "[a]\nj = 3\n");
    // empty value / key without spaces
    IniDoc q = IniDoc::parse("[a]\nk=v;c\nn=\n", ";");
    CHECK_EQ(q.get("a", "k"), "v;c"); // ';' needs leading whitespace
    CHECK_EQ(q.get("a", "n"), "");
    q.set("a", "k", "w");
    CHECK_EQ(q.serialize(), "[a]\nk=w\nn=\n");
}

static Settings loadAll(const char* d, const char* i) {
    SettingsStore st;
    st.loadFromText(d, i);
    return st.loaded();
}

static void testDefaults() {
    SettingsStore st;
    const Settings& d = st.defaults();
    CHECK(d.video.vsync && d.video.windowWidth == 1280 && d.video.windowHeight == 960 && d.video.scaleAuto);
    CHECK(d.video.aspect == Aspect::Ratio4x3 && d.video.filter == TexFilter::Game &&
          d.video.deinterlace == Deinterlace::Weave);
    CHECK(d.audio.enabled && d.audio.volume == 100 && d.audio.device == "default");
    CHECK(d.general.skipMemcardWarning);
    CHECK(!d.video.dlssNeural);
    CHECK(d.controls.lookMode == LookMode::Direct && d.controls.toggleKey == "F10" && !d.controls.invertY);
    CHECK(d.controls.game.lines.size() > 25 && d.controls.menu.lines.size() > 15);
    CHECK((d.controls.game.sourcesFor("TRIANGLE") == std::vector<std::string>{"SPACE"}));
    CHECK((d.controls.game.sourcesFor("CROSS") == std::vector<std::string>{"LCONTROL", "C"}));
    CHECK((d.controls.menu.sourcesFor("CIRCLE") == std::vector<std::string>{"MOUSE2", "BACKSPACE", "ESC"}));
    CHECK((d.controls.menu.sourcesFor("START") == std::vector<std::string>{"MOUSE2", "BACKSPACE", "ESC"}));
    // every catalogue action of the game layer has a default binding (except the unbound L3 / RSTICK_*)
    for (const auto& a : gameActions()) {
        const std::string id = a.id;
        if (id == "L3" || id.rfind("RSTICK_", 0) == 0)
            continue;
        CHECK(!d.controls.game.sourcesFor(id).empty());
    }
    for (const auto& a : menuActions())
        CHECK(!d.controls.menu.sourcesFor(a.id).empty());
    // all default sources are known names
    for (const auto& l : d.controls.game.lines)
        CHECK(isKnownSource(l.source));
    for (const auto& l : d.controls.menu.lines)
        CHECK(isKnownSource(l.source));
    // loading nothing equals the defaults
    Settings z = loadAll("", "");
    CHECK(z.video == d.video && z.audio == d.audio && z.general == d.general);
    CHECK(z.controls.game.lines.size() == d.controls.game.lines.size());
}

static void testLoad() {
    Settings s = loadAll(kDrakanIni, kInputIni);
    CHECK(s.video.vsync && s.video.aspect == Aspect::Ratio4x3 && s.video.scaleAuto);
    CHECK(s.audio.device == "Speakers (Realtek #2)");
    CHECK(s.controls.captureMouse && s.controls.sensX == 0.0020f);
    CHECK_EQ(s.controls.game.lines.size(), 9u); // seven explicit bindings plus migrated F5/F8
    CHECK_EQ(s.controls.menu.lines.size(), 2u);
    CHECK((s.controls.menu.sourcesFor("CIRCLE") == std::vector<std::string>{"ESC"}));
    CHECK(s.controls.game.lines[1].docLine >= 0);
    // fullscreen=1 -> borderless; window_mode overrides
    Settings f = loadAll("[video]\nfullscreen = 1\n", "");
    CHECK(f.video.windowMode == WindowMode::Borderless);
    f = loadAll("[video]\nfullscreen = 1\nwindow_mode = exclusive\n", "");
    CHECK(f.video.windowMode == WindowMode::Exclusive);
    f = loadAll("[video]\nscale = 5\naspect = 16:9\nvsync = off\nfilter = BILINEAR\ndeinterlace = nonsense\n", "");
    CHECK(!f.video.scaleAuto && f.video.scale == 5 && f.video.aspect == Aspect::Ratio16x9 && !f.video.vsync &&
          f.video.filter == TexFilter::Bilinear);
    CHECK(f.video.deinterlace == Deinterlace::Weave); // unknown value -> default
    f = loadAll("[video]\ndlss_neural = on\ndlss = off\n", "");
    CHECK(f.video.dlssNeural && f.video.dlss == "off");
    CHECK(f.video.dlssNeuralResolution == "output"); // default
    f = loadAll("[video]\ndlss_neural_resolution = Render\n", "");
    CHECK(f.video.dlssNeuralResolution == "render");
    f = loadAll("[video]\ndlss_neural_resolution = half\n", "");
    CHECK(f.video.dlssNeuralResolution == "output"); // unknown value -> default
}

static void testWorldRate() {
    SettingsStore st;
    CHECK_EQ(VideoSettings{}.worldRate, 0);
    CHECK_EQ(st.defaults().video.worldRate, 0);
    CHECK(HostCaps{}.restartKeys.count("video.world_rate") == 1);
    CHECK_EQ(settingLabel("video.world_rate"), "World frame rate");
    CHECK_EQ(settingLabel("video.frame_limit"), "Presentation limiter");
    auto rateText = [](int rate) { return rate == 0 ? std::string("uncapped") : std::to_string(rate); };
    for (int rate : {30, 60, 0}) {
        const std::string text =
            "[video]\nworld_rate = " + rateText(rate) + " ; keep\nframe_limit = 144\ncustom = untouched\n";
        st.loadFromText(text, kInputIni);
        CHECK_EQ(st.loaded().video.worldRate, rate);
        CHECK_EQ(st.loaded().video.frameLimit, 144); // Presentation caps above 60 remain valid.
        std::string a, b;
        st.render(st.loaded(), a, b);
        CHECK_EQ(a, text);
        CHECK_EQ(b, kInputIni);
        for (int nextRate : {30, 60, 0}) {
            if (nextRate == rate)
                continue;
            Settings next = st.loaded();
            next.video.worldRate = nextRate;
            CHECK(!(next.video == st.loaded().video));
            CHECK((changedKeys(st.loaded(), next) == std::vector<std::string>{"video.world_rate"}));
            st.render(next, a, b);
            CHECK_EQ(a, "[video]\nworld_rate = " + rateText(nextRate) +
                            " ; keep\nframe_limit = 144\ncustom = untouched\n");
            CHECK_EQ(b, kInputIni);
            SettingsStore roundtrip;
            roundtrip.loadFromText(a, b);
            CHECK_EQ(roundtrip.loaded().video.worldRate, nextRate);
        }
    }
    for (const char* token : {"UNCAPPED", "UnCaPpEd", "  uncapped  "}) {
        const std::string text = std::string("[video]\nworld_rate = ") + token + " ; keep\n";
        st.loadFromText(text, kInputIni);
        CHECK_EQ(st.loaded().video.worldRate, 0);
        std::string a, b;
        st.render(st.loaded(), a, b);
        CHECK_EQ(a, text);
        CHECK_EQ(b, kInputIni);
    }
    for (const char* invalid : {"", "0", "-30", "29", "31", "59", "61", "120", "144", "auto", "60.0", "60fps",
                                "uncappedfps", "unlimited", "0x3c", "4294967356", "99999999999999999999"}) {
        const std::string text = std::string("[video]\nworld_rate = ") + invalid + "\n";
        st.loadFromText(text, kInputIni);
        CHECK_EQ(st.loaded().video.worldRate, 30);
        std::string a, b;
        st.render(st.loaded(), a, b);
        CHECK_EQ(a, text); // Preserve hand-edited invalid text on an unchanged save.
    }
    st.loadFromText(kDrakanIni, kInputIni); // Missing keys use uncapped without rewriting the file.
    CHECK_EQ(st.loaded().video.worldRate, 0);
    Settings next = st.loaded();
    std::string a, b;
    st.render(next, a, b);
    CHECK_EQ(a, kDrakanIni);
    next.video.worldRate = 30;
    st.render(next, a, b);
    CHECK(contains(a, "world_rate = 30\r\n"));
    SettingsStore originalRoundtrip;
    originalRoundtrip.loadFromText(a, b);
    CHECK_EQ(originalRoundtrip.loaded().video.worldRate, 30);
    st.loadFromText(a, b);
    next = st.loaded();
    next.video.worldRate = 0;
    st.render(next, a, b);
    CHECK(contains(a, "world_rate = uncapped\r\n"));
    CHECK(contains(a, "default, no fixed upper limit"));
    CHECK(contains(a, "restart required\r\n"));
    CHECK(!contains(a, "frame_limit"));
    CHECK_EQ(b, kInputIni);
    SettingsStore uncappedRoundtrip;
    uncappedRoundtrip.loadFromText(a, b);
    CHECK_EQ(uncappedRoundtrip.loaded().video.worldRate, 0);
    next.video.worldRate = 60;
    st.render(next, a, b);
    CHECK(contains(a, "world_rate = 60\r\n"));
    CHECK(contains(a, "restart required\r\n"));
    CHECK(contains(a, "frame_limit") == false);
    CHECK_EQ(b, kInputIni);
    SettingsStore roundtrip;
    roundtrip.loadFromText(a, b);
    CHECK_EQ(roundtrip.loaded().video.worldRate, 60);
    for (int invalid : {-1, 120, 144}) {
        next = uncappedRoundtrip.loaded();
        next.video.worldRate = invalid; // Programmatic invalid values still serialize as 30.
        uncappedRoundtrip.render(next, a, b);
        CHECK(contains(a, "world_rate = 30\r\n"));
        CHECK(!contains(a, "world_rate = " + std::to_string(invalid)));
    }
}

static void testSaveMinimalDiff() {
    SettingsStore st;
    st.loadFromText(kDrakanIni, kInputIni);
    Settings s = st.loaded();
    std::string a, b;
    st.render(s, a, b);
    CHECK_EQ(a, kDrakanIni); // no change -> byte identical
    CHECK_EQ(b, kInputIni);

    // video: vsync off, aspect 16:9, window mode borderless, new keys
    s.video.vsync = false;
    s.video.aspect = Aspect::Ratio16x9;
    s.video.windowMode = WindowMode::Borderless;
    s.video.frameLimit = 120;
    s.video.anisotropic = 8;
    s.video.dlssNeural = true;
    s.video.dlssNeuralResolution = "render";
    s.audio.volume = 80;
    st.render(s, a, b);
    CHECK(contains(a, "vsync = 0   ; keep\r\n"));
    CHECK(contains(a, "aspect = 16:9\r\n"));
    CHECK(contains(a, "fullscreen = 1\r\n"));
    CHECK(contains(a, "window_mode = borderless\r\n"));
    CHECK(contains(a, "frame_limit = 120\r\n"));
    CHECK(contains(a, "anisotropic = 8\r\n"));
    CHECK(contains(a, "dlss_neural = 1\r\n"));
    CHECK(contains(a, "dlss_neural_resolution = render\r\n"));
    CHECK(contains(a, "volume = 80\r\n"));
    CHECK(contains(a, "device = Speakers (Realtek #2)\r\n"));
    CHECK(contains(a, "mystery = 42  # keep me\r\n"));
    CHECK(contains(a, "elf = C:\\Games\\SCES_500.06 ; my path\r\n"));
    CHECK_EQ(b, kInputIni); // input file untouched
    // reload what we wrote and compare
    SettingsStore st2;
    st2.loadFromText(a, b);
    CHECK(st2.loaded().video == s.video);
    CHECK(st2.loaded().audio == s.audio);
    // changedKeys
    auto ch = changedKeys(st.loaded(), s);
    CHECK(std::find(ch.begin(), ch.end(), "video.vsync") != ch.end());
    CHECK(std::find(ch.begin(), ch.end(), "video.dlss_neural") != ch.end());
    CHECK(std::find(ch.begin(), ch.end(), "video.dlss_neural_resolution") != ch.end());
    CHECK(std::find(ch.begin(), ch.end(), "video.frame_limit") != ch.end());
    CHECK(std::find(ch.begin(), ch.end(), "audio.volume") != ch.end());
    CHECK(std::find(ch.begin(), ch.end(), "audio.device") == ch.end());
    CHECK(changedKeys(st.loaded(), st.loaded()).empty());

    // input: sensitivity / invert, unknown key kept, comments kept
    Settings t = st.loaded();
    t.controls.sensX = 0.0035f;
    t.controls.invertY = true;
    t.controls.invertX = true;
    t.controls.lookMode = LookMode::Stick;
    t.controls.toggleRun = true;
    st.render(t, a, b);
    CHECK_EQ(a, kDrakanIni);
    CHECK(contains(b, "sensitivity_x = 0.0035     ; radians per count\n"));
    CHECK(contains(b, "invert_y = 1\n"));
    CHECK(contains(b, "invert_x = 1"));
    SettingsStore roundtrip;
    roundtrip.loadFromText(a, b);
    CHECK(roundtrip.loaded().controls.invertX && roundtrip.loaded().controls.invertY);
    CHECK(roundtrip.loaded().controls.toggleRun);
    CHECK(contains(b, "look_mode = stick\n"));
    CHECK(contains(b, "custom_key = hello         # unknown keys survive\n"));
    CHECK(contains(b, "[unknown_section]\nkeep = me\n"));
    CHECK(!contains(b, "\r")); // LF file stays LF
}

static void testBindings() {
    {
        SettingsStore quick;
        quick.loadFromText("", "[bindings]\nW = LSTICK_UP\n");
        Settings s = quick.loaded();
        CHECK((s.controls.game.sourcesFor("QUICKSAVE") == std::vector<std::string>{"F5"}));
        CHECK((s.controls.game.sourcesFor("QUICKLOAD") == std::vector<std::string>{"F8"}));
        s.controls.game.clearAction("QUICKSAVE");
        std::string a, b;
        quick.render(s, a, b);
        CHECK(contains(b, "F5 = NONE"));
        SettingsStore reload;
        reload.loadFromText(a, b);
        CHECK(reload.loaded().controls.game.sourcesFor("QUICKSAVE").empty());
        quick.loadFromText("", "[bindings]\nL = QUICKLOAD\n");
        s = quick.loaded();
        s.controls.game.clearAction("QUICKLOAD");
        quick.render(s, a, b);
        CHECK(contains(b, "F8 = NONE"));
        CHECK(contains(b, "L = NONE"));
    }
    SettingsStore st;
    st.loadFromText(kDrakanIni, kInputIni);
    Settings s = st.loaded();
    std::string a, b;
    // the old slot action is shown as quick slot 1 (input Config::migrate does the same); the file line is kept
    CHECK((s.controls.game.actionsFor("1") == std::vector<std::string>{"QUICK1"}));

    // rebind Jump from SPACE to X
    s.controls.game.unbind("TRIANGLE", "SPACE");
    s.controls.game.bind("TRIANGLE", "X");
    st.render(s, a, b);
    CHECK(!contains(b, "SPACE = TRIANGLE"));
    CHECK(contains(b, "ESC = START\n; weapon slots\n1 = ACT:0x02\n"));
    CHECK(contains(b, "X = TRIANGLE\n\n[menu_bindings]")); // new line after the last binding
    CHECK(contains(b, "; movement\nW = LSTICK_UP\nLCONTROL = CROSS\n"));
    CHECK(contains(b, "ESC = START | CIRCLE       ; back and resume\n"));
    SettingsStore st2;
    st2.loadFromText(a, b);
    CHECK((st2.loaded().controls.game.sourcesFor("TRIANGLE") == std::vector<std::string>{"X"}));
    CHECK(st2.loaded().controls.game == s.controls.game);

    // second source on an existing line: append the action to it ("C = CROSS" gets "| TRIANGLE")
    Settings t = st.loaded();
    t.controls.game.bind("TRIANGLE", "C");
    st.render(t, a, b);
    CHECK(contains(b, "C = CROSS | TRIANGLE\n"));

    // remove an action from a multi-action line, inline comment survives
    Settings u = st.loaded();
    u.controls.menu.unbind("CIRCLE", "ESC");
    st.render(u, a, b);
    CHECK(contains(b, "ESC = START       ; back and resume\n") || contains(b, "ESC = START ; back and resume\n"));
    // clear an action entirely (both lines of CROSS) -> lines deleted
    Settings w = st.loaded();
    w.controls.game.clearAction("CROSS");
    st.render(w, a, b);
    CHECK(!contains(b, "LCONTROL = CROSS") && !contains(b, "C = CROSS"));
    CHECK(contains(b, "; movement\nW = LSTICK_UP\nSPACE = TRIANGLE           ; Jump\nMOUSE1 = SQUARE\n"));

    // steal a key: clearSource removes it from the other action
    Settings x = st.loaded();
    x.controls.game.clearSource("MOUSE1");
    x.controls.game.bind("CIRCLE", "MOUSE1");
    CHECK((x.controls.game.sourcesFor("SQUARE").empty()));
    CHECK((x.controls.game.sourcesFor("CIRCLE") == std::vector<std::string>{"MOUSE1"}));

    // file without bindings section: defaults are used; unchanged -> nothing written; changed -> section created
    SettingsStore nb;
    nb.loadFromText("", "[general]\ncapture_mouse = 1\n");
    CHECK(nb.loaded().controls.game.lines.size() > 25);
    nb.render(nb.loaded(), a, b);
    CHECK_EQ(b, "[general]\ncapture_mouse = 1\n");
    Settings y = nb.loaded();
    y.controls.game.unbind("TRIANGLE", "SPACE");
    y.controls.game.bind("TRIANGLE", "F");
    nb.render(y, a, b);
    CHECK(contains(b, "[bindings]\nW = LSTICK_UP\n"));
    CHECK(contains(b, "F = TRIANGLE") || contains(b, "F = R2 | TRIANGLE"));
    CHECK(!contains(b, "[menu_bindings]")); // untouched layer is not written
    SettingsStore nb2;
    nb2.loadFromText(a, b);
    CHECK(nb2.loaded().controls.game == y.controls.game ||
          nb2.loaded().controls.game.lines.size() == y.controls.game.lines.size());

    // display names
    CHECK_EQ(sourceDisplayName("LCONTROL"), "Left Ctrl");
    CHECK_EQ(sourceDisplayName("MOUSE2"), "Right Mouse");
    CHECK_EQ(sourceDisplayName("NUMPAD7"), "Num 7");
    CHECK_EQ(sourceDisplayName("W"), "W");
    CHECK(!reservedReason("F1", "F10").empty() && !reservedReason("F10", "F10").empty() &&
          reservedReason("W", "F10").empty());
}

static void testFileIo() {
    // in-memory FileIO + write-failure handling
    std::string store[2];
    FileIO io;
    io.read = [&](const std::string& p, std::string& out) {
        if (p == "a.ini") {
            if (store[0].empty())
                return false;
            out = store[0];
            return true;
        }
        if (store[1].empty())
            return false;
        out = store[1];
        return true;
    };
    io.write = [&](const std::string& p, const std::string& d) {
        if (p == "a.ini")
            store[0] = d;
        else
            store[1] = d;
        return true;
    };
    Paths p;
    p.drakanIni = "a.ini";
    p.inputIni = "b.ini";
    SettingsStore st(p, io);
    std::string w;
    st.load(&w);
    CHECK(contains(w, "a.ini") && contains(w, "b.ini")); // missing files reported
    Settings s = st.loaded();
    s.video.vsync = false;
    s.controls.invertY = true;
    std::string err;
    CHECK(st.save(s, &err));
    CHECK(contains(store[0], "[video]") && contains(store[0], "vsync = 0"));
    CHECK(contains(store[1], "[bindings]") &&
          contains(store[1], "invert_y = 1")); // input file seeded from the defaults, complete
    CHECK(contains(store[1], "toggle_key = F10"));
    SettingsStore st3(p, io);
    st3.load();
    CHECK(st3.loaded().video.vsync == false && st3.loaded().controls.invertY);
    // saving again without changes does not rewrite
    int writes = 0;
    io.write = [&](const std::string&, const std::string&) {
        ++writes;
        return true;
    };
    SettingsStore st4(p, io);
    st4.load();
    CHECK(st4.save(st4.loaded()));
    CHECK_EQ(writes, 0);
    io.write = [&](const std::string&, const std::string&) { return false; };
    SettingsStore st5(p, io);
    st5.load();
    Settings z = st5.loaded();
    z.audio.volume = 5;
    CHECK(!st5.save(z, &err));
}

static std::string slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testRealFiles(int argc, char** argv) {
    if (argc > 1) {
        const std::string t = slurp(argv[1]);
        if (!t.empty()) {
            SettingsStore st;
            st.loadFromText("", t);
            const Settings& s = st.loaded();
            std::printf("real input ini %s: %zu game bindings, %zu menu bindings\n", argv[1],
                        s.controls.game.lines.size(), s.controls.menu.lines.size());
            CHECK(s.controls.game.lines.size() > 10);
            std::string a, b;
            st.render(s, a, b);
            CHECK(b == t); // identity on a real file
            // change + reload keeps everything else
            Settings c = s;
            c.controls.game.unbind("TRIANGLE", "SPACE");
            c.controls.game.bind("TRIANGLE", "G");
            st.render(c, a, b);
            SettingsStore st2;
            st2.loadFromText("", b);
            CHECK(st2.loaded().controls.game == c.controls.game);
            CHECK(countOf(b, "\n") + 0 >= countOf(t, "\n") - 1);
        }
    }
    if (argc > 2) {
        const std::string t = slurp(argv[2]);
        if (!t.empty()) {
            SettingsStore st;
            st.loadFromText(t, "");
            std::string a, b;
            st.render(st.loaded(), a, b);
            CHECK(a == t);
            std::printf("real drakan.ini %s: round trip identical\n", argv[2]);
        }
    }
}

static void testInventoryScreen() {
    SettingsStore st;
    CHECK(st.defaults().general.inventoryScreen == InventoryScreen::Native);
    st.loadFromText("[general]\ninventory_screen = Modern\n", "");
    CHECK(st.loaded().general.inventoryScreen == InventoryScreen::Modern);
    st.loadFromText("[general]\ninventory_screen = sidebar\n", ""); // unknown value -> default
    CHECK(st.loaded().general.inventoryScreen == InventoryScreen::Native);
    auto edited = st.loaded();
    edited.general.inventoryScreen = InventoryScreen::Modern;
    const auto keys = changedKeys(st.loaded(), edited);
    CHECK((keys == std::vector<std::string>{"general.inventory_screen"}));
    CHECK_EQ(settingLabel("general.inventory_screen"), "Inventory screen");
    std::string a, b;
    st.render(edited, a, b);
    CHECK(contains(a, "inventory_screen = modern"));
    SettingsStore reread;
    reread.loadFromText(a, b);
    CHECK(reread.loaded().general.inventoryScreen == InventoryScreen::Modern);
}

static void testPauseMenu() {
    SettingsStore st;
    CHECK(st.defaults().general.pauseMenu == PauseMenu::Native);
    st.loadFromText("[general]\npause_menu = modern\n", "");
    CHECK(st.loaded().general.pauseMenu == PauseMenu::Modern);
    st.loadFromText("[general]\npause_menu = overlay\n", ""); // unknown value -> default
    CHECK(st.loaded().general.pauseMenu == PauseMenu::Native);
    auto edited = st.loaded();
    edited.general.pauseMenu = PauseMenu::Modern;
    CHECK((changedKeys(st.loaded(), edited) == std::vector<std::string>{"general.pause_menu"}));
    CHECK_EQ(settingLabel("general.pause_menu"), "Pause menu");
    std::string a, b;
    st.render(edited, a, b);
    CHECK(contains(a, "pause_menu = modern"));
    SettingsStore reread;
    reread.loadFromText(a, b);
    CHECK(reread.loaded().general.pauseMenu == PauseMenu::Modern);
}

static void testDualWield() {
    SettingsStore st;
    CHECK(!st.defaults().general.dualWield);
    st.loadFromText("[general]\ndual_wield = 1\n", "");
    CHECK(st.loaded().general.dualWield);
    st.loadFromText("[general]\ndual_wield = 0\n", "");
    CHECK(!st.loaded().general.dualWield);
    auto edited = st.loaded();
    edited.general.dualWield = true;
    CHECK((changedKeys(st.loaded(), edited) == std::vector<std::string>{"general.dual_wield"}));
    CHECK_EQ(settingLabel("general.dual_wield"), "Dual wielding");
    std::string a, b;
    st.render(edited, a, b);
    CHECK(contains(a, "dual_wield = 1"));
    SettingsStore reread;
    reread.loadFromText(a, b);
    CHECK(reread.loaded().general.dualWield);
}

static void testSaveAndTitleMenus() {
    SettingsStore st;
    CHECK(st.defaults().general.saveMenu == SaveMenu::Native);
    CHECK(st.defaults().general.titleMenu == TitleMenu::Native);
    st.loadFromText("[general]\nsave_menu = Modern\ntitle_menu = modern\n", "");
    CHECK(st.loaded().general.saveMenu == SaveMenu::Modern);
    CHECK(st.loaded().general.titleMenu == TitleMenu::Modern);
    st.loadFromText("[general]\nsave_menu = grid\ntitle_menu = 1\n", ""); // unknown values -> defaults
    CHECK(st.loaded().general.saveMenu == SaveMenu::Native);
    CHECK(st.loaded().general.titleMenu == TitleMenu::Native);
    auto edited = st.loaded();
    edited.general.saveMenu = SaveMenu::Modern;
    CHECK((changedKeys(st.loaded(), edited) == std::vector<std::string>{"general.save_menu"}));
    edited.general.titleMenu = TitleMenu::Modern;
    CHECK((changedKeys(st.loaded(), edited) == std::vector<std::string>{"general.save_menu", "general.title_menu"}));
    CHECK_EQ(settingLabel("general.save_menu"), "Save / load screens");
    CHECK_EQ(settingLabel("general.title_menu"), "Title menu");
    std::string a, b;
    st.render(edited, a, b);
    CHECK(contains(a, "save_menu = modern"));
    CHECK(contains(a, "title_menu = modern"));
    SettingsStore reread;
    reread.loadFromText(a, b);
    CHECK(reread.loaded().general.saveMenu == SaveMenu::Modern);
    CHECK(reread.loaded().general.titleMenu == TitleMenu::Modern);
    // Defaults are written as native.
    std::string d1, d2;
    st.render(st.defaults(), d1, d2);
    SettingsStore back;
    back.loadFromText(d1, d2);
    CHECK(back.loaded().general.saveMenu == SaveMenu::Native && back.loaded().general.titleMenu == TitleMenu::Native);
}

static void testCombatAndFovSettings() {
    SettingsStore st;
    st.loadFromText("[video]\nfov=100\n", "[mouse]\nfree_combat=0\n");
    CHECK(st.loaded().video.fieldOfView == 100);
    CHECK(!st.loaded().controls.freeCombat);
    auto edited = st.loaded();
    edited.video.fieldOfView = 110;
    edited.controls.freeCombat = true;
    std::string a, b;
    st.render(edited, a, b);
    SettingsStore reread;
    reread.loadFromText(a, b);
    CHECK(reread.loaded().video.fieldOfView == 110 && reread.loaded().controls.freeCombat);
    const auto changes = changedKeys(st.loaded(), edited);
    CHECK(std::find(changes.begin(), changes.end(), "video.fov") != changes.end());
    CHECK(std::find(changes.begin(), changes.end(), "input.mouse.free_combat") != changes.end());
    for (auto value : {"nan", "inf", "-inf", "junk"}) {
        st.loadFromText(std::string("[video]\nfov=") + value, "");
        CHECK(st.loaded().video.fieldOfView == 80);
    }
    st.loadFromText("[video]\nfov=999", "");
    CHECK(st.loaded().video.fieldOfView == 120);
    st.loadFromText("[video]\nfov=-999", "");
    CHECK(st.loaded().video.fieldOfView == 60);
}

int main(int argc, char** argv) {
    {
        SettingsStore st;
        st.loadFromText("[video]\nmodern_water=1\nray_reflections=0\nray_shadows=1\n", "");
        CHECK(st.loaded().video.modernWater && !st.loaded().video.rayReflections && st.loaded().video.rayShadows);
        auto n = st.loaded();
        n.video.modernWater = false;
        n.video.rayReflections = true;
        n.video.rayShadows = false;
        auto keys = changedKeys(st.loaded(), n);
        CHECK(std::find(keys.begin(), keys.end(), "video.modern_water") != keys.end());
        CHECK(std::find(keys.begin(), keys.end(), "video.ray_reflections") != keys.end());
        std::string a, b;
        st.render(n, a, b);
        SettingsStore next;
        next.loadFromText(a, b);
        CHECK(!next.loaded().video.modernWater && next.loaded().video.rayReflections &&
              !next.loaded().video.rayShadows);
        CHECK(std::find(keys.begin(), keys.end(), "video.ray_shadows") != keys.end());
    }
    testCombatAndFovSettings();
    testInventoryScreen();
    testPauseMenu();
    testDualWield();
    testSaveAndTitleMenus();
    testIniDoc();
    testDefaults();
    testLoad();
    testWorldRate();
    testSaveMinimalDiff();
    testBindings();
    testFileIo();
    testRealFiles(argc, argv);
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
