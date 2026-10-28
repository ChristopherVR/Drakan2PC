// Settings data model (platform free, std only). Field names mirror drakan.ini / drakan_input.ini keys.
#pragma once
#include <string>
#include <vector>

namespace dsettings {

enum class WindowMode { Windowed, Borderless, Exclusive };
enum class Aspect {
    Ratio4x3,
    Ratio16x9,
    Native,
    Stretch,
    Ratio21x9,
    Auto
}; // ini: 4:3 | 16:9 | native | stretch | 21:9 | auto (= window aspect)
enum class Deinterlace { Weave, Bob, Blend };
enum class TexFilter { Game, Point, Bilinear };
enum class LookMode { Direct, Stick };
enum class PromptStyle { Keyboard, Ps2 };      // ini [general] ui_prompts = keyboard | ps2
enum class InventoryScreen { Native, Modern }; // ini [general] inventory_screen = native | modern
enum class PauseMenu { Native, Modern };       // ini [general] pause_menu = native | modern
enum class SaveMenu { Native, Modern };        // ini [general] save_menu = native | modern
enum class TitleMenu { Native, Modern };       // ini [general] title_menu = native | modern

struct VideoSettings {
    // ---- keys that exist in drakan.ini today ----
    WindowMode windowMode = WindowMode::Windowed; // fullscreen=0/1 (+ window_mode, see README)
    int windowWidth = 1280, windowHeight = 960;   // window_width / window_height
    bool scaleAuto = true;                        // scale = auto | 1..8
    int scale = 3;                                // used when !scaleAuto
    Aspect aspect = Aspect::Ratio4x3;
    float fieldOfView = 80.0f; // fov: horizontal degrees at 4:3; wider aspects extend horizontally
    bool vsync = true;
    TexFilter filter = TexFilter::Game;
    Deinterlace deinterlace = Deinterlace::Weave;
    bool flickerFilter = false;
    bool sharpPresent = true;
    // ---- new keys (added to drakan.ini by this library; the runtime owner / gs owner consumes them) ----
    int worldRate = 0;      // world_rate: 0 = uncapped (default), 30 original, 60 capped (restart required)
    int frameLimit = 0;     // frame_limit: presentation fps cap, 0 = off; does not raise world update rate
    int anisotropic = 16;   // anisotropic: 1 (off) 2 4 8 16
    int textureUpscale = 0; // texture_upscale: 0 (auto) 1 (off) 2 4
    std::string textureUpscaleFilter = "default"; // texture_upscale_filter: names come from HostCaps
    float mipBias = 0.0f;                         // mip_bias: -2..+2
    bool hdTextures = true;                       // hd_textures: use replacement textures from overrides/
    bool textureDump = false;                     // texture_dump: write decoded textures to disk
    float hudScale = 0.0f;           // hud_scale: 0 = auto (by output size), else 0.5 .. 1.5 (1 = original 4:3 size)
    bool hudAspectCorrect = true;    // hud_aspect_correct: HUD / menu sprites keep their 4:3 proportions in widescreen
    bool smoothMenuHighlight = true; // smooth_menu_highlight: soften the fire-automaton highlight of the menu entries
    int showFps = 0;                 // show_fps: 0 off, 1 simple, 2 detailed (F2 cycles it for the session)
    std::string fpsPosition = "top-left"; // fps_position: top-left | top-right | bottom-left | bottom-right
    std::string dlss = "off"; // dlss: off | dlaa | quality | balanced | performance (NVIDIA DLSS Super Resolution
                              // applied to the 3D world only; the HUD stays native)
    std::string dlssPreset = "auto"; // dlss_preset: auto | j | k | l | m (transformer model presets of the DLSS SDK;
                                     // auto = the SDK default for the mode)
    float dlssSharpness = 0.0f;      // dlss_sharpness: 0 .. 1 optional post sharpen of the upscaled 3D image
    bool modernWater = false;
    bool rayReflections = true;
    bool rayShadows = false;
    bool dlssNeural = false; // dlss_neural: DLSS 5 neural rendering, independent of Super Resolution
    std::string dlssNeuralResolution = "output"; // dlss_neural_resolution: output | render (NR on the SR output, or
                                                 // on the reduced raster before SR; only differs with a reduced mode)
    bool operator==(const VideoSettings&) const = default;
};

struct AudioSettings {
    bool enabled = true;
    int volume = 100;               // percent, 0..200
    std::string device = "default"; // "default" or part of a device name
    bool operator==(const AudioSettings&) const = default;
};

struct GeneralSettings {
    bool skipMemcardWarning = true;
    PromptStyle prompts = PromptStyle::Keyboard; // ui_prompts
    std::string language = "english";            // language: english | french | german | italian | spanish (restart)
    float uiScale = 0.0f;                        // settings_ui_scale: 0 = auto (by window height), else 0.75 .. 2.5
    // inventory_screen: the Tab screen. native = the original screen; modern = the host-drawn full-screen menu
    // (runtime/gameui, drawn by the overlay; applied live through ISettingsHost::applyGeneral).
    InventoryScreen inventoryScreen = InventoryScreen::Native;
    // pause_menu: the Esc menu. native = the game's pause page; modern = the host-drawn pause screen (gameui
    // PauseScreen; Save, Load, Options and Quit still open the game's own screens). Applies from the next Esc.
    PauseMenu pauseMenu = PauseMenu::Native;
    // dual_wield: a second one-handed weapon goes to Rynn's left hand (the shield slot); attacks alternate hands
    // Applied live through ISettingsHost::applyGeneral; disabling stows the off-hand on the next game update.
    bool dualWield = false;
    // save_menu: the Save Game / Load Game screens. native = the game's slot grid (with the PC pager and previews);
    // modern = the host-drawn slot browser (gameui SaveLoadScreen). Not wired into the game yet.
    SaveMenu saveMenu = SaveMenu::Native;
    // title_menu: the title (main) menu. native = the game's menu; modern = the host-drawn main menu over the title
    // art (gameui TitleScreen). Not wired into the game yet.
    TitleMenu titleMenu = TitleMenu::Native;
    bool operator==(const GeneralSettings&) const = default;
};

// One line of [bindings] / [menu_bindings]:  <source> = <action> [| <action> ...]
struct BindingLine {
    std::string source;               // upper-case input name: W, SPACE, LCONTROL, MOUSE1, WHEELUP ...
    std::vector<std::string> actions; // upper-case action tokens: TRIANGLE, LSTICK_UP, ACT:0x02 ...
    int docLine = -1;                 // line index in the loaded ini (-1 = new)
    bool operator==(const BindingLine& o) const { return source == o.source && actions == o.actions; }
};
struct BindingLayer {
    std::vector<BindingLine> lines;
    bool operator==(const BindingLayer& o) const { return lines == o.lines; }

    // ---- editing helpers (action-centric view of the source-centric file) ----
    std::vector<std::string> sourcesFor(const std::string& action) const; // in file order
    void bind(const std::string& action,
              const std::string& source); // appends action to the source's line (new line when needed)
    void unbind(const std::string& action, const std::string& source);
    void clearAction(const std::string& action);
    // Removes `source` from every action (used by "steal this key").
    void clearSource(const std::string& source);
    std::vector<std::string> actionsFor(const std::string& source) const;
};

struct ControlSettings {
    bool freeCombat = true;                 // [mouse] free_combat: unrestricted combat mouse, no target lock
    bool bowAimModern = true;               // [mouse] bow_aim: modern (hold to aim, release to shoot) | classic
    LookMode lookMode = LookMode::Direct;   // [general] look_mode
    bool captureMouse = true;               // [general] capture_mouse
    bool releaseOnEsc = false;              // [general] release_on_esc
    std::string toggleKey = "F10";          // [general] toggle_key (NONE = disabled)
    float sensX = 0.0020f, sensY = 0.0020f; // [mouse] sensitivity_x / sensitivity_y (radians per count)
    float cameraFollow = 5.0f; // [mouse] camera_follow (1/s the camera travel eases behind Rynn; 0 = original)
    bool invertX = false, invertY = false; // [mouse] invert_x / invert_y
    float walkScale = 0.5f;                // [keyboard] walk_scale
    bool toggleRun = false;                // [keyboard] toggle_run
    bool strafeKeys =
        true; // [keyboard] strafe (A / D strafe like Order of the Flame; off = the game's stock mapping, A / D turn)
    bool inventoryMouse =
        true;                 // [menu] inventory_mouse (inventory / map: pointer steps the selection, wheel = sections)
    bool pitchPersist = true; // [mouse] pitch_persist (camera pitch stays where the mouse left it)
    bool curveCompensation = true; // [keyboard] curve_compensation
    bool gamepadEnabled = true;    // [gamepad] enabled
    float gamepadDeadzone = 0.15f; // [gamepad] deadzone
    BindingLayer game;             // [bindings]
    BindingLayer menu;             // [menu_bindings]
    bool operator==(const ControlSettings&) const = default;
};

struct SubtitleSettings {
    bool narration = true; // [subtitles] narration: intro / outro narration subtitles (SRT files in subtitles/)
    float size = 0.0f;     // [subtitles] size: 0 = auto (by window height), else 0.5 .. 3.0
    bool operator==(const SubtitleSettings&) const = default;
};

struct Settings {
    VideoSettings video;
    SubtitleSettings subtitles;
    AudioSettings audio;
    GeneralSettings general;
    ControlSettings controls;
    bool operator==(const Settings&) const = default;
};

// ---- string <-> enum helpers (ini spelling) ----
const char* toIni(WindowMode);
const char* toIni(Aspect);
const char* toIni(Deinterlace);
const char* toIni(TexFilter);
const char* toIni(LookMode);
const char* toIni(PromptStyle);
const char* toIni(InventoryScreen);
const char* toIni(PauseMenu);
const char* toIni(SaveMenu);
const char* toIni(TitleMenu);

} // namespace dsettings
