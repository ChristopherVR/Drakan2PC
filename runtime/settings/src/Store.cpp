#include "settings/Store.h"
#include "settings/Bindings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dsettings {

std::string ISettingsHost::defaultInputIni() const {
    return builtinDefaultInputIni();
}

// ---- enum helpers
// ----------------------------------------------------------------------------------------------------
const char* toIni(WindowMode m) {
    switch (m) {
        case WindowMode::Windowed:
            return "windowed";
        case WindowMode::Borderless:
            return "borderless";
        default:
            return "exclusive";
    }
}
const char* toIni(Aspect a) {
    switch (a) {
        case Aspect::Ratio4x3:
            return "4:3";
        case Aspect::Ratio16x9:
            return "16:9";
        case Aspect::Native:
            return "native";
        case Aspect::Ratio21x9:
            return "21:9";
        case Aspect::Auto:
            return "auto";
        default:
            return "stretch";
    }
}
const char* toIni(Deinterlace d) {
    switch (d) {
        case Deinterlace::Weave:
            return "weave";
        case Deinterlace::Bob:
            return "bob";
        default:
            return "blend";
    }
}
const char* toIni(TexFilter f) {
    switch (f) {
        case TexFilter::Game:
            return "game";
        case TexFilter::Point:
            return "point";
        default:
            return "bilinear";
    }
}
const char* toIni(LookMode m) {
    return m == LookMode::Direct ? "direct" : "stick";
}
const char* toIni(PromptStyle p) {
    return p == PromptStyle::Keyboard ? "keyboard" : "ps2";
}
const char* toIni(InventoryScreen s) {
    return s == InventoryScreen::Modern ? "modern" : "native";
}
const char* toIni(PauseMenu s) {
    return s == PauseMenu::Modern ? "modern" : "native";
}
const char* toIni(SaveMenu s) {
    return s == SaveMenu::Modern ? "modern" : "native";
}
const char* toIni(TitleMenu s) {
    return s == TitleMenu::Modern ? "modern" : "native";
}

namespace {

std::string lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}
std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}
std::string b2s(bool b) {
    return b ? "1" : "0";
}
bool s2b(const std::string& v, bool& out) {
    std::string u = lower(trim(v));
    if (u == "1" || u == "true" || u == "yes" || u == "on") {
        out = true;
        return true;
    }
    if (u == "0" || u == "false" || u == "no" || u == "off") {
        out = false;
        return true;
    }
    return false;
}
bool s2i(const std::string& v, int& out) {
    char* e = nullptr;
    long n = std::strtol(v.c_str(), &e, 0);
    if (!e || e == v.c_str() || *e)
        return false;
    out = static_cast<int>(n);
    return true;
}
bool s2f(const std::string& v, float& out) {
    char* e = nullptr;
    float f = std::strtof(v.c_str(), &e);
    if (!e || e == v.c_str() || *e)
        return false;
    out = f;
    return true;
}

struct Field {
    const char* section;
    const char* key;
    bool input; // true: drakan_input.ini, false: drakan.ini
    std::string (*get)(const Settings&);
    void (*set)(Settings&, const std::string&);
    const char* comment; // written above a key that did not exist yet
};

#define GETB(expr) [](const Settings& s) { return b2s(s.expr); }
#define SETB(expr)                                                                                                     \
    [](Settings& s, const std::string& v) {                                                                            \
        bool b;                                                                                                        \
        if (s2b(v, b))                                                                                                 \
            s.expr = b;                                                                                                \
    }
#define GETI(expr) [](const Settings& s) { return std::to_string(s.expr); }
#define SETI(expr)                                                                                                     \
    [](Settings& s, const std::string& v) {                                                                            \
        int n;                                                                                                         \
        if (s2i(v, n))                                                                                                 \
            s.expr = n;                                                                                                \
    }
#define GETF(expr, f) [](const Settings& s) { return fmt(f, s.expr); }
#define SETF(expr)                                                                                                     \
    [](Settings& s, const std::string& v) {                                                                            \
        float x;                                                                                                       \
        if (s2f(v, x))                                                                                                 \
            s.expr = x;                                                                                                \
    }
#define GETS(expr) [](const Settings& s) { return s.expr; }
#define SETS(expr) [](Settings& s, const std::string& v) { s.expr = v; }

const std::vector<Field>& fields() {
    static const std::vector<Field> f = {
        // ---------------- drakan.ini ----------------
        {"general", "skip_memcard_warning", false, GETB(general.skipMemcardWarning), SETB(general.skipMemcardWarning),
         nullptr},
        {"general", "ui_prompts", false, [](const Settings& s) { return std::string(toIni(s.general.prompts)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "keyboard")
                 s.general.prompts = PromptStyle::Keyboard;
             else if (u == "ps2")
                 s.general.prompts = PromptStyle::Ps2;
         },
         "keyboard = key/mouse prompts in the game's text, ps2 = original PlayStation 2 button icons (restart)"},
        {"general", "language", false, GETS(general.language),
         [](Settings& s, const std::string& v) { s.general.language = lower(trim(v)); },
         "english | french | german | italian | spanish: which disc language tree to load (restart)"},
        {"general", "settings_ui_scale", false,
         [](const Settings& s) {
             return s.general.uiScale <= 0.0f ? std::string("auto") : fmt("%.2f", s.general.uiScale);
         },
         [](Settings& s, const std::string& v) {
             float x;
             if (lower(trim(v)) == "auto")
                 s.general.uiScale = 0.0f;
             else if (s2f(v, x))
                 s.general.uiScale = x;
         },
         "size of the F1 settings menu: auto (by window height) or 0.75 .. 2.5"},
        {"general", "inventory_screen", false,
         [](const Settings& s) { return std::string(toIni(s.general.inventoryScreen)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "native")
                 s.general.inventoryScreen = InventoryScreen::Native;
             else if (u == "modern")
                 s.general.inventoryScreen = InventoryScreen::Modern;
         },
         "native = the original Tab screen, modern = the full-screen PC inventory over the live world"},
        {"general", "pause_menu", false, [](const Settings& s) { return std::string(toIni(s.general.pauseMenu)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "native")
                 s.general.pauseMenu = PauseMenu::Native;
             else if (u == "modern")
                 s.general.pauseMenu = PauseMenu::Modern;
         },
         "native = the game's own pause menu, modern = the PC pause menu over the dimmed world (env "
         "DRAKAN_PAUSE_MENU)"},
        {"general", "dual_wield", false, GETB(general.dualWield), SETB(general.dualWield),
         "1 = a second one-handed weapon goes to Rynn's left hand and strikes with the right hand's attacks (env "
         "DRAKAN_DUAL_WIELD)"},
        {"general", "save_menu", false, [](const Settings& s) { return std::string(toIni(s.general.saveMenu)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "native")
                 s.general.saveMenu = SaveMenu::Native;
             else if (u == "modern")
                 s.general.saveMenu = SaveMenu::Modern;
         },
         "native = the game's own Save Game / Load Game screens, modern = the PC slot browser with large previews"},
        {"general", "title_menu", false, [](const Settings& s) { return std::string(toIni(s.general.titleMenu)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "native")
                 s.general.titleMenu = TitleMenu::Native;
             else if (u == "modern")
                 s.general.titleMenu = TitleMenu::Modern;
         },
         "native = the game's own title menu, modern = the PC main menu over the title art"},

        {"video", "fullscreen", false,
         [](const Settings& s) { return b2s(s.video.windowMode != WindowMode::Windowed); },
         [](Settings& s, const std::string& v) {
             bool b;
             if (s2b(v, b))
                 s.video.windowMode = b ? WindowMode::Borderless : WindowMode::Windowed;
         },
         nullptr},
        {"video", "window_mode", false, [](const Settings& s) { return std::string(toIni(s.video.windowMode)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "windowed")
                 s.video.windowMode = WindowMode::Windowed;
             else if (u == "borderless")
                 s.video.windowMode = WindowMode::Borderless;
             else if (u == "exclusive")
                 s.video.windowMode = WindowMode::Exclusive;
         },
         "windowed | borderless | exclusive (overrides 'fullscreen'; the settings menu writes both)"},
        {"video", "window_width", false, GETI(video.windowWidth), SETI(video.windowWidth), nullptr},
        {"video", "window_height", false, GETI(video.windowHeight), SETI(video.windowHeight), nullptr},
        {"video", "scale", false,
         [](const Settings& s) { return s.video.scaleAuto ? std::string("auto") : std::to_string(s.video.scale); },
         [](Settings& s, const std::string& v) {
             int n;
             if (lower(trim(v)) == "auto")
                 s.video.scaleAuto = true;
             else if (s2i(v, n) && n >= 1 && n <= 8) {
                 s.video.scaleAuto = false;
                 s.video.scale = n;
             }
         },
         nullptr},
        {"video", "aspect", false, [](const Settings& s) { return std::string(toIni(s.video.aspect)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "4:3")
                 s.video.aspect = Aspect::Ratio4x3;
             else if (u == "16:9")
                 s.video.aspect = Aspect::Ratio16x9;
             else if (u == "native")
                 s.video.aspect = Aspect::Native;
             else if (u == "stretch")
                 s.video.aspect = Aspect::Stretch;
             else if (u == "21:9")
                 s.video.aspect = Aspect::Ratio21x9;
             else if (u == "auto")
                 s.video.aspect = Aspect::Auto;
         },
         "4:3 (original) | 16:9 | 21:9 (widescreen: the 3D view gets a wider field of view) | auto (= window shape) | "
         "native | stretch"},
        {"video", "fov", false, GETF(video.fieldOfView, "%.0f"),
         [](Settings& s, const std::string& v) {
             float x;
             if (s2f(v, x) && std::isfinite(x))
                 s.video.fieldOfView = std::clamp(x, 60.0f, 120.0f);
         },
         "field of view in horizontal degrees at 4:3, 60 .. 120 (80 original); widescreen extends horizontally; live"},
        {"video", "vsync", false, GETB(video.vsync), SETB(video.vsync), nullptr},
        {"video", "filter", false, [](const Settings& s) { return std::string(toIni(s.video.filter)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "game")
                 s.video.filter = TexFilter::Game;
             else if (u == "point")
                 s.video.filter = TexFilter::Point;
             else if (u == "bilinear")
                 s.video.filter = TexFilter::Bilinear;
         },
         nullptr},
        {"video", "deinterlace", false, [](const Settings& s) { return std::string(toIni(s.video.deinterlace)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "weave")
                 s.video.deinterlace = Deinterlace::Weave;
             else if (u == "bob")
                 s.video.deinterlace = Deinterlace::Bob;
             else if (u == "blend")
                 s.video.deinterlace = Deinterlace::Blend;
         },
         nullptr},
        {"video", "flicker_filter", false, GETB(video.flickerFilter), SETB(video.flickerFilter), nullptr},
        {"video", "sharp_present", false, GETB(video.sharpPresent), SETB(video.sharpPresent), nullptr},
        {"video", "renderer", false, [](const Settings& s) { return s.video.renderer; },
         [](Settings& s, const std::string& v) {
             const std::string u = lower(trim(v));
             if (u == "webgpu" || u == "d3d11" || u == "sw")
                 s.video.renderer = u;
         },
         nullptr},
        {"video", "world_rate", false,
         [](const Settings& s) {
             return s.video.worldRate == 0 ? std::string("uncapped")
                                           : std::to_string(s.video.worldRate == 60 ? 60 : 30);
         },
         [](Settings& s, const std::string& v) {
             const std::string u = lower(trim(v));
             if (u == "30")
                 s.video.worldRate = 30;
             else if (u == "60")
                 s.video.worldRate = 60;
             else if (u == "uncapped")
                 s.video.worldRate = 0;
             else
                 s.video.worldRate = 30;
         },
         "world updates: uncapped (default, no fixed upper limit), 60 (up to 60 FPS), or 30 (original cadence); "
         "restart required"},
        {"video", "frame_limit", false, GETI(video.frameLimit), SETI(video.frameLimit),
         "presentation frame rate cap in fps, 0 = off; does not raise the world update rate"},
        {"video", "anisotropic", false, GETI(video.anisotropic), SETI(video.anisotropic),
         "anisotropic filtering: 1 (off), 2, 4, 8, 16"},
        {"video", "texture_upscale", false,
         [](const Settings& s) {
             return s.video.textureUpscale == 0 ? std::string("auto") : std::to_string(s.video.textureUpscale);
         },
         [](Settings& s, const std::string& v) {
             int n;
             if (lower(trim(v)) == "auto")
                 s.video.textureUpscale = 0;
             else if (s2i(v, n) && n >= 1)
                 s.video.textureUpscale = n;
         },
         "texture upscaling factor: auto, 1 (off), 2, 4"},
        {"video", "texture_upscale_filter", false, GETS(video.textureUpscaleFilter),
         [](Settings& s, const std::string& v) { s.video.textureUpscaleFilter = trim(v); },
         "algorithm used for texture upscaling (names depend on the build)"},
        {"video", "mip_bias", false, GETF(video.mipBias, "%.2f"), SETF(video.mipBias),
         "texture LOD bias, negative = sharper, positive = softer"},
        {"video", "hd_textures", false, GETB(video.hdTextures), SETB(video.hdTextures),
         "1 = use replacement textures from the overrides folder"},
        {"video", "texture_dump", false, GETB(video.textureDump), SETB(video.textureDump),
         "1 = write decoded textures to disk (for making replacements)"},
        {"video", "hud_scale", false,
         [](const Settings& s) {
             return s.video.hudScale <= 0.0f ? std::string("auto") : fmt("%.2f", s.video.hudScale);
         },
         [](Settings& s, const std::string& v) {
             float x;
             if (lower(trim(v)) == "auto")
                 s.video.hudScale = 0.0f;
             else if (s2f(v, x))
                 s.video.hudScale = std::min(1.5f, std::max(0.5f, x));
         },
         "size of the HUD, in-game text and subtitle bar: auto (follows the output size) or 0.5 .. 1.5 (1.0 = the "
         "original 4:3 proportion)"},
        {"video", "hud_aspect_correct", false, GETB(video.hudAspectCorrect), SETB(video.hudAspectCorrect),
         "1 = HUD and menu panels keep their 4:3 proportions in widescreen (full-screen fades stay full width)"},
        {"video", "smooth_menu_highlight", false, GETB(video.smoothMenuHighlight), SETB(video.smoothMenuHighlight),
         "1 = soften the checkerboard fire effect of the highlighted menu entry the way a CRT showed it"},
        {"video", "show_fps", false, GETI(video.showFps),
         [](Settings& s, const std::string& v) {
             int n;
             if (s2i(v, n))
                 s.video.showFps = n < 0 ? 0 : n > 2 ? 2 : n;
         },
         "FPS counter: 0 = off, 1 = simple (game fps, presented fps, frame time), 2 = detailed (+ 1% low, CPU load, "
         "GPU). F2 cycles it in game"},
        {"video", "fps_position", false, GETS(video.fpsPosition),
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "top-left" || u == "top-right" || u == "bottom-left" || u == "bottom-right")
                 s.video.fpsPosition = u;
         },
         "corner of the FPS counter: top-left | top-right | bottom-left | bottom-right"},
        {"video", "dlss", false, GETS(video.dlss),
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "off" || u == "dlaa" || u == "quality" || u == "balanced" || u == "performance")
                 s.video.dlss = u;
             else if (u == "on")
                 s.video.dlss = "quality";
         },
         "NVIDIA DLSS Super Resolution for the 3D world (the HUD stays native): off | dlaa | quality | balanced | "
         "performance. Needs an RTX GPU and nvngx_dlss.dll next to the exe"},
        {"video", "dlss_preset", false, GETS(video.dlssPreset),
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "auto" || u == "j" || u == "k" || u == "l" || u == "m")
                 s.video.dlssPreset = u;
         },
         "DLSS model preset: auto (SDK default for the mode) | j | k | l | m (SR hints; runtime support varies; not "
         "DLSS 5)"},
        {"video", "dlss_sharpness", false, GETF(video.dlssSharpness, "%.2f"), SETF(video.dlssSharpness),
         "optional extra sharpening of the DLSS image, 0 .. 1"},
        {"video", "modern_water", false, GETB(video.modernWater), SETB(video.modernWater),
         "water shading on native water layers; requires DXR 1.1"},
        {"video", "ray_reflections", false, GETB(video.rayReflections), SETB(video.rayReflections),
         "hardware DXR reflections including resident offscreen geometry; requires modern_water"},
        {"video", "ray_shadows", false, GETB(video.rayShadows), SETB(video.rayShadows),
         "hardware ray-traced soft shadows from native lights and ambient occlusion, including alpha-tested foliage"},
        {"video", "dlss_neural", false, GETB(video.dlssNeural), SETB(video.dlssNeural),
         "DLSS 5 neural rendering: 0 off, 1 on; independent of Super Resolution; requires a supported NVIDIA GPU and "
         "matching signed runtime bundle"},
        {"video", "dlss_neural_resolution", false, GETS(video.dlssNeuralResolution),
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "output" || u == "render")
                 s.video.dlssNeuralResolution = u;
         },
         "where DLSS 5 neural rendering runs with a reduced-resolution DLSS SR mode: output (on the upscaled image) | "
         "render (on the reduced image before SR; about half the NR cost)"},
        {"subtitles", "narration", false, GETB(subtitles.narration), SETB(subtitles.narration),
         "1 = show subtitles for the intro / outro narration (text files in the subtitles folder)"},
        {"subtitles", "size", false,
         [](const Settings& s) {
             return s.subtitles.size <= 0.0f ? std::string("auto") : fmt("%.2f", s.subtitles.size);
         },
         [](Settings& s, const std::string& v) {
             float x;
             if (lower(trim(v)) == "auto")
                 s.subtitles.size = 0.0f;
             else if (s2f(v, x))
                 s.subtitles.size = x;
         },
         "size of the narration subtitles: auto (by window height) or 0.5 .. 3.0"},

        {"audio", "enabled", false, GETB(audio.enabled), SETB(audio.enabled), nullptr},
        {"audio", "volume", false, GETI(audio.volume), SETI(audio.volume), nullptr},
        {"audio", "device", false, GETS(audio.device),
         [](Settings& s, const std::string& v) { s.audio.device = trim(v); }, nullptr},

        // ---------------- drakan_input.ini ----------------
        {"general", "capture_mouse", true, GETB(controls.captureMouse), SETB(controls.captureMouse), nullptr},
        {"general", "release_on_esc", true, GETB(controls.releaseOnEsc), SETB(controls.releaseOnEsc), nullptr},
        {"general", "toggle_key", true, GETS(controls.toggleKey),
         [](Settings& s, const std::string& v) { s.controls.toggleKey = upperCopy(trim(v)); }, nullptr},
        {"general", "look_mode", true, [](const Settings& s) { return std::string(toIni(s.controls.lookMode)); },
         [](Settings& s, const std::string& v) {
             std::string u = lower(trim(v));
             if (u == "direct")
                 s.controls.lookMode = LookMode::Direct;
             else if (u == "stick")
                 s.controls.lookMode = LookMode::Stick;
         },
         nullptr},
        {"mouse", "sensitivity_x", true, GETF(controls.sensX, "%.4f"), SETF(controls.sensX), nullptr},
        {"mouse", "sensitivity_y", true, GETF(controls.sensY, "%.4f"), SETF(controls.sensY), nullptr},
        {"mouse", "invert_x", true, GETB(controls.invertX), SETB(controls.invertX), nullptr},
        {"mouse", "invert_y", true, GETB(controls.invertY), SETB(controls.invertY), nullptr},
        {"menu", "inventory_mouse", true, GETB(controls.inventoryMouse), SETB(controls.inventoryMouse), nullptr},
        {"mouse", "pitch_persist", true, GETB(controls.pitchPersist), SETB(controls.pitchPersist), nullptr},
        {"mouse", "camera_follow", true, GETF(controls.cameraFollow, "%.0f"), SETF(controls.cameraFollow),
         "how quickly the camera catches up as Rynn moves; turning stays locked (0 = the game's own rubber band)"},
        {"mouse", "bow_aim", true,
         [](const Settings& s) { return std::string(s.controls.bowAimModern ? "modern" : "classic"); },
         [](Settings& s, const std::string& v) {
             const std::string u = lower(trim(v));
             if (u == "modern")
                 s.controls.bowAimModern = true;
             else if (u == "classic")
                 s.controls.bowAimModern = false;
         },
         "modern: hold the left button to aim the bow (zoom, crosshair), release to shoot; classic: native bow"},
        {"mouse", "free_combat", true, GETB(controls.freeCombat), SETB(controls.freeCombat),
         "free mouse combat: no controller target lock; mouse turns Rynn through attack animations"},
        {"keyboard", "strafe", true, GETB(controls.strafeKeys), SETB(controls.strafeKeys), nullptr},
        {"keyboard", "walk_scale", true, GETF(controls.walkScale, "%.2f"), SETF(controls.walkScale), nullptr},
        {"keyboard", "toggle_run", true, GETB(controls.toggleRun), SETB(controls.toggleRun), nullptr},
        {"keyboard", "curve_compensation", true, GETB(controls.curveCompensation), SETB(controls.curveCompensation),
         nullptr},
        {"gamepad", "enabled", true, GETB(controls.gamepadEnabled), SETB(controls.gamepadEnabled), nullptr},
        {"gamepad", "deadzone", true, GETF(controls.gamepadDeadzone, "%.2f"), SETF(controls.gamepadDeadzone), nullptr},
    };
    return f;
}

std::string joinActions(const std::vector<std::string>& a) {
    std::string s;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i)
            s += " | ";
        s += a[i];
    }
    return s;
}

// Canonical action token: upper case, but ACT:/CMD: ids keep a lower case "0x" prefix and hex digits ("ACT:0x0e") like
// the shipped ini.
std::string normToken(const std::string& in) {
    std::string t = upperCopy(trim(in));
    if (t.rfind("ACT:", 0) == 0 || t.rfind("CMD:", 0) == 0) {
        for (size_t i = 4; i < t.size(); ++i)
            t[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
    }
    return t;
}

BindingLayer parseLayer(const IniDoc& doc, const std::string& section) {
    BindingLayer l;
    for (const auto& e : doc.entries(section)) {
        BindingLine b;
        b.source = upperCopy(e.key);
        b.docLine = e.line;
        std::stringstream ss(e.value);
        std::string a;
        while (std::getline(ss, a, '|')) {
            a = normToken(a);
            if (a.empty() || a == "NONE")
                continue;
            // The old slot actions ACT:0x02..0x06 are quick slots 1..5 (input/Config.cpp Config::migrate).
            if (section == "bindings" && a.size() == 8 && a.rfind("ACT:0x0", 0) == 0 && a[7] >= '2' && a[7] <= '6')
                a = std::string("QUICK") + char(a[7] - 1);
            if (std::find(b.actions.begin(), b.actions.end(), a) == b.actions.end())
                b.actions.push_back(a);
        }
        if (!b.actions.empty())
            l.lines.push_back(std::move(b));
    }
    if (section == "bindings") {
        // Match the input module's implicit shortcuts so an older ini shows its effective F5/F8 bindings.
        for (const auto& [key, action] : {std::pair{"F5", "QUICKSAVE"}, std::pair{"F8", "QUICKLOAD"}})
            if (!doc.has(section, key) && l.sourcesFor(action).empty())
                l.bind(action, key);
    }
    return l;
}

void writeLayer(const BindingLayer& before, const BindingLayer& requested, IniDoc& doc, const std::string& section) {
    BindingLayer after = requested;
    if (section == "bindings")
        for (const auto& [key, action] : {std::pair{"F5", "QUICKSAVE"}, std::pair{"F8", "QUICKLOAD"}})
            if (!before.sourcesFor(action).empty() && requested.sourcesFor(action).empty() &&
                requested.actionsFor(key).empty())
                after.bind("NONE", key);
    if (section == "bindings")
        for (const auto& line : before.lines)
            if (after.actionsFor(line.source).empty() &&
                (std::find(line.actions.begin(), line.actions.end(), "QUICKSAVE") != line.actions.end() ||
                 std::find(line.actions.begin(), line.actions.end(), "QUICKLOAD") != line.actions.end()))
                after.bind("NONE", line.source); // preserve a deliberate unbind across input-config migration
    if (before == after)
        return;
    if (!doc.hasSection(section)) {
        std::vector<std::pair<std::string, std::string>> kv;
        for (const auto& l : after.lines)
            kv.emplace_back(l.source, joinActions(l.actions));
        doc.replaceEntries(section, kv);
        return;
    }
    // existing lines: delete the ones that vanished, rewrite the changed ones
    for (const auto& ol : before.lines) {
        if (ol.docLine < 0)
            continue;
        const BindingLine* cur = nullptr;
        for (const auto& nl : after.lines)
            if (nl.docLine == ol.docLine) {
                cur = &nl;
                break;
            }
        if (!cur)
            doc.deleteLine(ol.docLine);
        else if (cur->actions != ol.actions)
            doc.setLineValue(ol.docLine, joinActions(cur->actions));
    }
    for (const auto& nl : after.lines)
        if (nl.docLine < 0)
            doc.appendEntry(section, nl.source, joinActions(nl.actions));
}

} // namespace

// ---- BindingLayer text defaults / pure functions
// ---------------------------------------------------------------------------
Settings parseSettings(const IniDoc& drakan, const IniDoc& input, const IniDoc& defaultInput) {
    Settings s;
    for (const Field& f : fields()) {
        const IniDoc& d = f.input ? input : drakan;
        if (d.has(f.section, f.key))
            f.set(s, d.get(f.section, f.key));
        else if (f.input && defaultInput.has(f.section, f.key))
            f.set(s, defaultInput.get(f.section, f.key));
    }
    // A bindings section replaces the built-in set as soon as it appears (same rule as input/Config.cpp).
    s.controls.game =
        input.hasSection("bindings") ? parseLayer(input, "bindings") : parseLayer(defaultInput, "bindings");
    s.controls.menu = input.hasSection("menu_bindings") ? parseLayer(input, "menu_bindings")
                                                        : parseLayer(defaultInput, "menu_bindings");
    if (!input.hasSection("bindings"))
        for (auto& l : s.controls.game.lines)
            l.docLine = -1;
    if (!input.hasSection("menu_bindings"))
        for (auto& l : s.controls.menu.lines)
            l.docLine = -1;
    return s;
}

void writeSettings(const Settings& before, const Settings& after, IniDoc& drakan, IniDoc& input) {
    for (const Field& f : fields()) {
        const std::string a = f.get(before), b = f.get(after);
        IniDoc& d = f.input ? input : drakan;
        if (a == b)
            continue;
        d.set(f.section, f.key, b, f.comment ? f.comment : "");
    }
    // The window mode is stored in two keys; keep them consistent even when only the mode changed.
    if (before.video.windowMode != after.video.windowMode) {
        drakan.set("video", "fullscreen", b2s(after.video.windowMode != WindowMode::Windowed));
        drakan.set("video", "window_mode", toIni(after.video.windowMode));
    }
    writeLayer(before.controls.game, after.controls.game, input, "bindings");
    writeLayer(before.controls.menu, after.controls.menu, input, "menu_bindings");
}

std::vector<std::string> changedKeys(const Settings& a, const Settings& b) {
    std::vector<std::string> out;
    for (const Field& f : fields())
        if (f.get(a) != f.get(b))
            out.push_back(std::string(f.input ? "input." : "") + f.section + "." + f.key);
    if (!(a.controls.game == b.controls.game))
        out.push_back("input.bindings");
    if (!(a.controls.menu == b.controls.menu))
        out.push_back("input.menu_bindings");
    return out;
}

namespace {
const Field* findField(const std::string& key) {
    for (const Field& f : fields())
        if (key == std::string(f.input ? "input." : "") + f.section + "." + f.key)
            return &f;
    return nullptr;
}
} // namespace

bool setSettingValue(Settings& s, const std::string& key, const std::string& value) {
    const Field* f = findField(key);
    if (!f)
        return false;
    f->set(s, value);
    return true;
}

std::string settingValue(const Settings& s, const std::string& key) {
    const Field* f = findField(key);
    return f ? f->get(s) : std::string();
}

std::vector<std::string> settingKeys() {
    std::vector<std::string> v;
    for (const Field& f : fields())
        v.push_back(std::string(f.input ? "input." : "") + f.section + "." + f.key);
    return v;
}

std::string settingLabel(const std::string& key) {
    if (key == "video.fov")
        return "Field of view";
    if (key == "input.mouse.free_combat")
        return "Free mouse combat";
    if (key == "input.mouse.camera_follow")
        return "Camera follow";
    if (key == "input.mouse.bow_aim")
        return "Modern bow aiming";
    static const struct {
        const char* k;
        const char* v;
    } t[] = {
        {"video.fullscreen", "Window mode"},
        {"video.window_mode", "Window mode"},
        {"video.window_width", "Window size"},
        {"video.window_height", "Window size"},
        {"video.renderer", "Renderer"},
        {"video.scale", "Internal render scale"},
        {"video.aspect", "Aspect ratio"},
        {"video.vsync", "VSync"},
        {"video.filter", "Texture filtering"},
        {"video.deinterlace", "Deinterlacing"},
        {"video.flicker_filter", "Flicker filter"},
        {"video.sharp_present", "Sharp scaling"},
        {"video.world_rate", "World frame rate"},
        {"video.frame_limit", "Presentation limiter"},
        {"video.show_fps", "FPS counter"},
        {"video.fps_position", "FPS counter position"},
        {"video.anisotropic", "Anisotropic filtering"},
        {"video.texture_upscale", "Texture upscaling"},
        {"video.texture_upscale_filter", "Texture upscale filter"},
        {"video.mip_bias", "Mip bias"},
        {"video.hd_textures", "HD texture replacement"},
        {"video.texture_dump", "Texture dump"},
        {"video.modern_water", "Modern water"},
        {"video.ray_reflections", "DXR water reflections"},
        {"video.ray_shadows", "Ray-traced shadows"},
        {"video.dlss", "DLSS"},
        {"video.dlss_preset", "DLSS model"},
        {"video.dlss_sharpness", "DLSS sharpness"},
        {"video.hud_aspect_correct", "HUD aspect correction"},
        {"video.hud_scale", "HUD size"},
        {"video.smooth_menu_highlight", "Smooth menu highlight"},
        {"subtitles.narration", "Narration subtitles"},
        {"subtitles.size", "Subtitle size"},
        {"audio.enabled", "Sound"},
        {"audio.volume", "Master volume"},
        {"audio.device", "Output device"},
        {"general.skip_memcard_warning", "Skip memory card warning"},
        {"general.ui_prompts", "Button prompts"},
        {"general.language", "Language"},
        {"general.settings_ui_scale", "Settings menu size"},
        {"general.inventory_screen", "Inventory screen"},
        {"general.pause_menu", "Pause menu"},
        {"general.dual_wield", "Dual wielding"},
        {"general.save_menu", "Save / load screens"},
        {"general.title_menu", "Title menu"},
    };
    for (const auto& e : t)
        if (key == e.k)
            return e.v;
    if (key == "input.bindings")
        return "Key bindings";
    if (key == "input.menu_bindings")
        return "Menu key bindings";
    if (key.rfind("input.", 0) == 0)
        return "Controls";
    return key;
}

// ---- FileIO / store
// ----------------------------------------------------------------------------------------------------------
FileIO FileIO::native() {
    FileIO io;
    io.read = [](const std::string& path, std::string& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f)
            return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    };
    io.write = [](const std::string& path, const std::string& data) {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!f)
                return false;
        }
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) { // some filesystems refuse to replace: fall back to copy
            std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(tmp, ec);
            return !ec;
        }
        return true;
    };
    return io;
}

SettingsStore::SettingsStore(Paths paths, FileIO io, std::string defaultInputIni)
    : paths_(std::move(paths)), io_(std::move(io)), defaultInput_(std::move(defaultInputIni)) {
    if (defaultInput_.empty())
        defaultInput_ = builtinDefaultInputIni();
    defaults_ = parseSettings(IniDoc(";"), IniDoc(";#"), IniDoc::parse(defaultInput_, ";#"));
    // drakan.ini defaults are the member initialisers of Settings (they match drakan_config.cpp kEntries).
    for (auto& l : defaults_.controls.game.lines)
        l.docLine = -1;
    for (auto& l : defaults_.controls.menu.lines)
        l.docLine = -1;
    loaded_ = defaults_;
}

void SettingsStore::loadFromText(const std::string& drakanText, const std::string& inputText, bool inputExisted) {
    drakan_ = IniDoc::parse(drakanText, ";");
    input_ = IniDoc::parse((inputExisted || !inputText.empty()) ? inputText : defaultInput_, ";#");
    rebuild();
}

void SettingsStore::rebuild() {
    loaded_ = parseSettings(drakan_, input_, IniDoc::parse(defaultInput_, ";#"));
}

bool SettingsStore::load(std::string* warnings) {
    std::string a, b;
    bool okA = io_.read(paths_.drakanIni, a);
    bool okB = io_.read(paths_.inputIni, b);
    if (!okA && warnings)
        *warnings += paths_.drakanIni + " not found, using defaults\n";
    if (!okB && warnings)
        *warnings += paths_.inputIni + " not found, using defaults\n";
    loadFromText(a, b, okB);
    return true;
}

void SettingsStore::render(const Settings& s, std::string& drakanText, std::string& inputText) const {
    IniDoc d = drakan_, i = input_;
    writeSettings(loaded_, s, d, i);
    drakanText = d.serialize();
    inputText = i.serialize();
}

bool SettingsStore::save(const Settings& s, std::string* error) {
    std::string a, b;
    render(s, a, b);
    std::string curA = drakan_.serialize(), curB = input_.serialize();
    bool ok = true;
    if (a != curA) {
        if (!io_.write(paths_.drakanIni, a)) {
            ok = false;
            if (error)
                *error += "cannot write " + paths_.drakanIni + "\n";
        }
    }
    if (b != curB) {
        if (!io_.write(paths_.inputIni, b)) {
            ok = false;
            if (error)
                *error += "cannot write " + paths_.inputIni + "\n";
        }
    }
    if (ok)
        loadFromText(a, b, true);
    return ok;
}

} // namespace dsettings
