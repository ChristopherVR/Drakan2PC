#include "settings/Schema.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "settings/Bindings.h"
#include "settings/Store.h"

namespace dsettings {

using gameui::SettingChoice;
using gameui::SettingKind;
using gameui::SettingRow;
using gameui::SettingScope;
using gameui::SettingSection;

namespace {

std::string fmtc(const char* f, int a) {
    char b[96];
    std::snprintf(b, sizeof b, f, a);
    return b;
}
std::string fmtc(const char* f, int a, int b2) {
    char b[96];
    std::snprintf(b, sizeof b, f, a, b2);
    return b;
}
std::string fmtc(const char* f, int a, int b2, int c) {
    char b[96];
    std::snprintf(b, sizeof b, f, a, b2, c);
    return b;
}
std::string num(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", static_cast<double>(v));
    return b;
}

// Row builders. `s` gives the current value of field rows (currentValue in the ini spelling).
struct Builder {
    const Settings& s;
    SettingSection* sec = nullptr;

    SettingRow& row(const std::string& key, SettingKind kind, const char* label, std::string help, bool restart,
                    bool enabled) {
        SettingRow r;
        r.key = key;
        r.kind = kind;
        r.label = label;
        r.help = std::move(help);
        r.restart = restart;
        r.enabled = enabled;
        r.currentValue = settingValue(s, key);
        sec->rows.push_back(std::move(r));
        return sec->rows.back();
    }
    SettingRow& toggle(const std::string& key, const char* label, std::string help, bool restart = false,
                       bool enabled = true) {
        SettingRow& r = row(key, SettingKind::Toggle, label, std::move(help), restart, enabled);
        r.on = r.currentValue == r.onValue;
        return r;
    }
    // A choice over fixed values; the current one is the one whose value equals the field's ini spelling.
    SettingRow& choice(const std::string& key, const char* label, std::string help,
                       std::initializer_list<std::pair<const char*, const char*>> items, bool restart = false,
                       bool enabled = true) {
        SettingRow& r = row(key, SettingKind::Choice, label, std::move(help), restart, enabled);
        for (const auto& [v, l] : items)
            r.choices.push_back({v, l, true});
        select(r);
        return r;
    }
    static void select(SettingRow& r) {
        r.current = -1;
        for (size_t i = 0; i < r.choices.size(); ++i)
            if (r.choices[i].value == r.currentValue)
                r.current = static_cast<int>(i);
    }
    SettingRow& slider(const std::string& key, const char* label, std::string help, float value, float mn, float mx,
                       std::string format, float step, bool enabled = true) {
        SettingRow& r = row(key, SettingKind::Slider, label, std::move(help), false, enabled);
        r.value = value;
        r.min = mn;
        r.max = mx;
        r.format = std::move(format);
        r.step = step;
        r.currentValue = num(value);
        return r;
    }
    // Percent presets of a float field (0 = "Auto"): the current one by value, "Custom" when none matches.
    SettingRow& scaleChoice(const std::string& key, const char* label, std::string help, float value,
                            std::initializer_list<std::pair<float, const char*>> items, bool enabled = true) {
        SettingRow& r = row(key, SettingKind::Choice, label, std::move(help), false, enabled);
        r.currentLabel = "Custom";
        for (const auto& [v, l] : items) {
            char b[16];
            std::snprintf(b, sizeof b, "%.2f", static_cast<double>(v));
            r.choices.push_back({v <= 0.0f ? std::string("auto") : std::string(b), l, true});
            if (std::fabs(v - value) < 0.01f) {
                r.current = static_cast<int>(r.choices.size() - 1);
                r.currentLabel = l;
            }
        }
        return r;
    }
};

struct Res {
    int w, h;
};
const Res kResPresets[] = {{800, 600},  {1024, 768}, {1280, 960},  {1440, 1080}, {1600, 1200},
                           {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}};

void displayPage(gameui::SettingPage& pg, const Settings& st, const SchemaEnv& env) {
    const VideoSettings& v = st.video;
    const HostCaps& caps = env.caps;
    const MonitorInfo& mon = env.monitor;
    pg.id = "display";
    pg.title = "Display";
    pg.description = "Window and resolution, frame rate, presentation, textures, ray tracing and NVIDIA DLSS.";
    Builder b{st};

    pg.sections.push_back({"Window and output", {}});
    b.sec = &pg.sections.back();
    {
        SettingRow& r = b.choice(
            "video.window_mode", "Window mode",
            "Windowed, borderless fullscreen (recommended) or exclusive fullscreen. Alt+Enter and "
            "F11 also toggle fullscreen in game.",
            {{"windowed", "Windowed"}, {"borderless", "Borderless fullscreen"}, {"exclusive", "Exclusive fullscreen"}});
        r.choices[2].enabled = caps.exclusiveFullscreen;
        r.confirm = true;
    }
    {
        const bool windowed = v.windowMode == WindowMode::Windowed;
        SettingRow& r = b.row(kKeyWindowSize, SettingKind::Choice, windowed ? "Window size" : "Output resolution",
                              windowed ? "Size of the game window in pixels. Applied immediately; you can also drag "
                                         "the window edges."
                                       : "Borderless fullscreen always uses the desktop resolution of the monitor.",
                              false, true);
        r.currentValue = fmtc("%dx%d", v.windowWidth, v.windowHeight);
        r.currentLabel = windowed ? fmtc("%d x %d", v.windowWidth, v.windowHeight)
                                  : fmtc("Monitor native: %d x %d", mon.nativeWidth, mon.nativeHeight);
        if (windowed) {
            for (const Res& rs : kResPresets) {
                if (rs.w > mon.nativeWidth || rs.h > mon.nativeHeight)
                    continue;
                const bool is43 = rs.w * 3 == rs.h * 4;
                char lb[64];
                std::snprintf(lb, sizeof lb, "%d x %d%s%s", rs.w, rs.h,
                              is43 ? "  (4:3)" : (rs.w * 9 == rs.h * 16 ? "  (16:9)" : ""),
                              (rs.w == mon.nativeWidth && rs.h == mon.nativeHeight) ? "  monitor native" : "");
                r.choices.push_back({fmtc("%dx%d", rs.w, rs.h), lb, true});
            }
        } else if (v.windowMode == WindowMode::Exclusive && !mon.modes.empty()) {
            for (const DisplayMode& m : mon.modes)
                r.choices.push_back(
                    {fmtc("%dx%d", m.width, m.height), fmtc("%d x %d @ %d Hz", m.width, m.height, m.refreshHz), true});
        } else {
            r.emptyText = "Uses the desktop resolution";
        }
        Builder::select(r);
        r.confirm = true;
    }
    if (v.windowMode == WindowMode::Windowed) {
        SettingRow& r = b.row(kKeyWindowCustom, SettingKind::Size, "Custom window size",
                              "Any size between 640x480 and the monitor size.", false, true);
        r.scope = SettingScope::OverlayOnly;
        r.width = v.windowWidth;
        r.height = v.windowHeight;
        r.minWidth = 640;
        r.minHeight = 480;
        r.maxWidth = std::max(640, mon.nativeWidth);
        r.maxHeight = std::max(480, mon.nativeHeight);
        r.currentValue = fmtc("%dx%d", v.windowWidth, v.windowHeight);
        r.confirm = true;
    }
    b.choice("video.aspect", "Aspect ratio",
             "4:3 is the original picture. 16:9, 21:9 and Auto (the shape of the window) widen the 3D view; menus, "
             "movies and loading screens stay 4:3. Native and Stretch only change how the picture is fitted.",
             {{"4:3", "4:3 (original)"},
              {"16:9", "16:9 widescreen"},
              {"21:9", "21:9 ultrawide"},
              {"auto", "Auto (window shape)"},
              {"native", "Native (as set by the game)"},
              {"stretch", "Stretch to window"}});
    b.scaleChoice("video.hud_scale", "HUD size",
                  "Size of the health / mana orbs, in-game text and subtitle bar. Auto keeps them reasonable on large "
                  "and wide outputs; 100% is the original 4:3 size. They shrink towards the nearest screen edge or "
                  "corner.",
                  v.hudScale,
                  {{0.0f, "Auto"},
                   {0.5f, "50%"},
                   {0.6f, "60%"},
                   {0.7f, "70%"},
                   {0.8f, "80%"},
                   {0.9f, "90%"},
                   {1.0f, "100% (original)"},
                   {1.25f, "125%"},
                   {1.5f, "150%"}});
    b.slider("video.fov", "Field of view",
             "How much of the world you can see. Horizontal degrees at 4:3; widescreen adds more at the sides. 80 is "
             "original. Changes the gameplay view without resizing the HUD or menus.",
             v.fieldOfView, 60.0f, 120.0f, "%.0f degrees", 1.0f);
    b.toggle("video.hud_aspect_correct", "HUD aspect correction",
             "In widescreen keep the HUD and in-game panels at their 4:3 proportions (round orbs stay round). Off "
             "stretches them with the picture.");
    {
        SettingRow& r = b.row("video.scale", SettingKind::Choice, "Internal render scale",
                              "Auto follows the visible output size when you resize or enter fullscreen, up to 8x. A "
                              "fixed scale stays unchanged. Higher scales sharpen rendering but cost performance; "
                              "original artwork can remain soft.",
                              true, caps.internalResolution);
        r.choices.push_back({"auto", "Auto", true});
        for (int s = 1; s <= 8; ++s)
            r.choices.push_back({std::to_string(s), fmtc("%dx  (about %d lines)", s, s * 448), true});
        const int idx = v.scaleAuto ? 0 : v.scale;
        r.currentLabel = idx == 0 ? std::string("Auto") : fmtc("%dx  (%d lines)", idx, idx * 512 / 2 * 2);
        Builder::select(r);
        r.confirm = true;
    }

    pg.sections.push_back({"Presentation", {}});
    b.sec = &pg.sections.back();
    b.choice("video.world_rate", "World frame rate",
             "Uncapped is the default and uses the optimized CPU path with no fixed upper limit. Original restores the "
             "game's 30 FPS world cadence; 60 allows up to 60 FPS. CPU improvements remain enabled in every mode. "
             "Takes effect after a restart.",
             {{"uncapped", "Uncapped (default)"}, {"60", "Capped (up to 60 FPS)"}, {"30", "Original (30 FPS)"}}, true);
    b.toggle("video.vsync", "VSync",
             "Synchronise presentation with the monitor refresh. Off gives the lowest latency but can tear.");
    {
        SettingRow& r = b.row("video.frame_limit", SettingKind::Choice, "Presentation limiter",
                              "Caps presented frames per second (useful with VSync off). Off means no presentation "
                              "cap. This does not raise the world update rate; use World frame rate to change it.",
                              false, caps.frameLimiter);
        for (int f : {0, 30, 50, 60, 72, 90, 100, 120, 144, 165, 240})
            r.choices.push_back({std::to_string(f), f == 0 ? std::string("Off") : fmtc("%d fps", f), true});
        r.currentLabel = v.frameLimit == 0 ? std::string("Off") : fmtc("%d fps", v.frameLimit);
        Builder::select(r);
    }
    {
        SettingRow& r = b.choice("video.show_fps", "FPS counter",
                                 "Shows the game's frame rate and the presented frame rate / frame time in a corner. "
                                 "Detailed adds 1% low, CPU load and the GPU. F2 cycles it during play.",
                                 {{"0", "Off"}, {"1", "Simple"}, {"2", "Detailed"}});
        r.current = std::clamp(v.showFps, 0, 2);
    }
    {
        SettingRow& r = b.choice("video.fps_position", "FPS counter position",
                                 "Which corner of the window the FPS counter is drawn in.",
                                 {{"top-left", "Top left"},
                                  {"top-right", "Top right"},
                                  {"bottom-left", "Bottom left"},
                                  {"bottom-right", "Bottom right"}});
        if (r.current < 0)
            r.current = 0;
    }
    b.choice("video.deinterlace", "Deinterlacing",
             "Weave shows the finished frame as stored (sharp). Bob shows one field per refresh. Blend averages lines.",
             {{"weave", "Weave (sharp)"}, {"bob", "Bob"}, {"blend", "Blend"}});
    b.toggle("video.smooth_menu_highlight", "Smooth menu highlight",
             "The highlighted menu entry is a fire effect that is a noisy checkerboard on a PC screen. On softens it "
             "the way a TV did.");
    b.toggle("video.flicker_filter", "Flicker filter",
             "Keep the game's own anti-flicker blend. It softens the picture, so it is off by default.");
    b.toggle("video.sharp_present", "Sharp scaling",
             "Area-average downscale and sharp-bilinear upscale in the final blit. Off = plain bilinear.");

    pg.sections.push_back({"Textures", {}});
    b.sec = &pg.sections.back();
    b.choice("video.filter", "Texture filtering",
             "Game: as each draw asks. Point: force nearest (crisp, pixel look). Bilinear: force smooth.",
             {{"game", "Game default"}, {"point", "Point (nearest)"}, {"bilinear", "Bilinear"}});
    {
        SettingRow& r =
            b.row("video.anisotropic", SettingKind::Choice, "Anisotropic filtering",
                  "Keeps textures sharp at grazing angles (floors, walls in the distance).", false, caps.anisotropic);
        for (int o : {1, 2, 4, 8, 16})
            r.choices.push_back({std::to_string(o), o == 1 ? std::string("Off") : fmtc("%dx", o), true});
        r.currentLabel = v.anisotropic <= 1 ? std::string("Off") : fmtc("%dx", v.anisotropic);
        Builder::select(r);
    }
    {
        SettingRow& r = b.row("video.texture_upscale", SettingKind::Choice, "Texture upscaling",
                              "Enlarges the original low resolution textures with a filter when they are loaded.", true,
                              caps.textureUpscale);
        for (int o : {0, 1, 2, 4})
            r.choices.push_back({o == 0 ? std::string("auto") : std::to_string(o),
                                 o == 0   ? std::string("Auto")
                                 : o == 1 ? std::string("Off")
                                          : fmtc("%dx", o),
                                 true});
        r.currentLabel = v.textureUpscale == 0   ? std::string("Auto")
                         : v.textureUpscale == 1 ? std::string("Off")
                                                 : fmtc("%dx", v.textureUpscale);
        Builder::select(r);
    }
    {
        SettingRow& r =
            b.row("video.texture_upscale_filter", SettingKind::Choice, "Upscale filter",
                  "Algorithm used for texture upscaling.", true, caps.textureUpscale && v.textureUpscale != 1);
        r.needs = "Needs texture upscaling (not Off).";
        for (const auto& f : caps.textureUpscaleFilters)
            r.choices.push_back({f, f, true});
        r.currentLabel = v.textureUpscaleFilter;
        r.emptyText = "none available";
        Builder::select(r);
    }
    b.slider("video.mip_bias", "Mip bias",
             "Shifts which texture detail level is used. Negative = sharper (can shimmer), positive = softer.",
             v.mipBias, -2.0f, 2.0f, "%+.2f", 0.05f, caps.mipBias);
    b.toggle("video.hd_textures", "HD texture replacement",
             "Load replacement textures from the overrides folder instead of the disc textures.", true,
             caps.hdTextures);
    b.toggle("video.texture_dump", "Texture dump",
             "Write every decoded texture to disk. For texture artists; costs performance.", false, caps.textureDump);

    pg.sections.push_back({"Ray tracing", {}});
    b.sec = &pg.sections.back();
    b.toggle("video.ray_shadows", "Ray-traced shadows",
             "Soft ray-traced shadows from native scene lights plus ray-traced ambient occlusion. Includes offscreen "
             "resident props and alpha-tested foliage.",
             false, caps.modernWater || v.rayShadows);
    b.toggle("video.modern_water", "Modern water (experimental)",
             caps.modernWaterStatus.empty() ? "Wave spectrum, refraction, depth absorption, shoreline foam, caustics "
                                              "and Fresnel shading on native water layers."
                                            : caps.modernWaterStatus,
             false, caps.modernWater || v.modernWater);
    b.toggle("video.ray_reflections", "DXR water reflections",
             "Hardware ray tracing includes resident terrain and ceilings outside the view. Object and alpha-tested "
             "material coverage remains partial. Requires modern water.",
             false, caps.modernWater && v.modernWater)
        .needs = "Needs modern water.";
    if (!caps.rayTracingStatus.empty()) {
        SettingRow& r = b.row("video.ray_tracing_status", SettingKind::Info, "Ray tracing status",
                              caps.rayTracingStatus, false, true);
        r.text = caps.rayTracingStatus;
    }

    pg.sections.push_back({"NVIDIA DLSS", {}});
    b.sec = &pg.sections.back();
    if (caps.dlss || caps.dlssNeural) {
        // Attribution required by the NVIDIA RTX SDKs licence wherever the DLSS SDK is in use.
        SettingRow& r = b.row("video.dlss_credit", SettingKind::Info, "Credits", kNvidiaDlssCredit, false, true);
        r.text = kNvidiaDlssCredit;
    }
    // Neural rendering rows only appear for a renderer that reports the capability; this build has no such backend.
    if (caps.dlssNeural)
        b.toggle("video.dlss_neural", "DLSS 5 neural rendering",
                 caps.dlssNeuralStatus.empty() ? "Enhances the 3D scene before the HUD is drawn. Requires a supported "
                                                 "NVIDIA GPU and the signed neural rendering runtime."
                                               : caps.dlssNeuralStatus,
                 false, true);
    if (caps.dlssNeural && !caps.dlssNeuralStatus.empty()) {
        SettingRow& r =
            b.row("video.dlss_neural_status", SettingKind::Info, "Runtime status", caps.dlssNeuralStatus, false, true);
        r.text = caps.dlssNeuralStatus;
    }
    {
        SettingRow& r = b.row("video.dlss", SettingKind::Choice, "DLSS SR (experimental)",
                              caps.dlssStatus.empty()
                                  ? "NVIDIA DLSS Super Resolution for the 3D world only (needs an RTX GPU). World/HUD "
                                    "separation is experimental. DLAA = anti-aliasing at full resolution; Quality / "
                                    "Balanced / Performance render the world at a lower resolution and reconstruct it."
                                  : caps.dlssStatus,
                              false, caps.dlss || v.dlss != "off");
        static const char* ids[] = {"off", "dlaa", "quality", "balanced", "performance"};
        static const char* names[] = {"Off", "DLAA", "Quality", "Balanced", "Performance"};
        int cur = 0;
        for (int i = 0; i < 5; ++i)
            if (v.dlss == ids[i])
                cur = i;
        for (int i = 0; i < 5; ++i)
            if (i == 0 || caps.dlss)
                r.choices.push_back({ids[i], names[i], true});
        r.currentLabel = names[cur];
        Builder::select(r);
    }
    if (caps.dlssNeural)
        b.choice(
             "video.dlss_neural_resolution", "Neural rendering resolution",
             "Where DLSS 5 runs when DLSS SR renders the world at a reduced size. Output: on the upscaled image (best "
             "detail, most expensive). Render: on the reduced image before Super Resolution, which then upscales it "
             "(much cheaper). Same result with DLAA or SR off.",
             {{"output", "Output resolution"}, {"render", "Render resolution"}}, false,
             (caps.dlssNeural || v.dlssNeural) && v.dlssNeural)
            .needs = "Needs DLSS 5 neural rendering.";
    {
        SettingRow& r =
            b.choice("video.dlss_preset", "DLSS model",
                     "Super Resolution preset hint, subject to SDK/runtime support. Auto uses the SDK "
                     "default. Presets do not select DLSS 5 neural rendering.",
                     {{"auto", "Auto"}, {"j", "Preset J"}, {"k", "Preset K"}, {"l", "Preset L"}, {"m", "Preset M"}},
                     false, caps.dlss && v.dlss != "off");
        r.needs = "Needs DLSS SR (not Off).";
        if (r.current < 0)
            r.current = 0;
    }
    b.slider("video.dlss_sharpness", "DLSS sharpness",
             "Extra sharpening after Super Resolution and optional neural rendering, before the HUD (0 = none). "
             "Applies live without resetting temporal history.",
             v.dlssSharpness, 0.0f, 1.0f, "%.2f", 0.05f, caps.dlss && v.dlss != "off")
        .needs = "Needs DLSS SR (not Off).";
}

void audioPage(gameui::SettingPage& pg, const Settings& st, const SchemaEnv& env) {
    const AudioSettings& a = st.audio;
    pg.id = "audio";
    pg.title = "Audio";
    pg.description = "Sound output, the master volume of the whole mix and the output device.";
    pg.note = "Music and sound effect volume have separate sliders in the game's own Options > Audio screen. This "
              "menu only sets the master level of the whole mix.";
    Builder b{st};
    pg.sections.push_back({"Output", {}});
    b.sec = &pg.sections.back();
    b.toggle("audio.enabled", "Sound",
             "Off silences the output; the sound chip is still emulated so the game runs the same.");
    {
        SettingRow& r = b.slider("audio.volume", "Master volume",
                                 "Overall loudness in percent. Values above 100 amplify and can clip.",
                                 static_cast<float>(a.volume), 0.0f, 200.0f, "%d%%", 5.0f, a.enabled);
        r.integer = true;
        r.currentValue = std::to_string(a.volume);
        r.needs = "Needs Sound on.";
    }
    {
        SettingRow& r = b.row("audio.device", SettingKind::Choice, "Output device",
                              "Default follows the Windows default device, also when it changes.", true,
                              env.caps.audioDeviceSelect);
        r.choices.push_back({"default", "System default", true});
        for (const auto& d : env.audioDevices)
            r.choices.push_back({d, d, true});
        r.currentLabel = (a.device.empty() || a.device == "default") ? std::string("System default") : a.device;
        Builder::select(r);
        if (a.device.empty())
            r.current = 0;
    }
}

void controlsPage(gameui::SettingPage& pg, const Settings& st, const SchemaEnv& env) {
    const ControlSettings& c = st.controls;
    pg.id = "controls";
    pg.title = "Controls";
    pg.description = "Mouse look, sensitivity and combat, keyboard movement and the gamepad.";
    Builder b{st};
    pg.sections.push_back({"Mouse", {}});
    b.sec = &pg.sections.back();
    b.choice("input.general.look_mode", "Look mode",
             "Direct writes raw mouse movement into camera pitch and player turn (recommended). Stick emulates the "
             "right stick.",
             {{"direct", "Direct (raw mouse)"}, {"stick", "Right stick emulation"}});
    {
        char fm[48];
        std::snprintf(fm, sizeof fm, "%%.4f   (x%.2f)", static_cast<double>(c.sensX / 0.0020f));
        SettingRow& r = b.slider("input.mouse.sensitivity_x", "Mouse sensitivity",
                                 "Radians per mouse count (default 0.0020).", c.sensX, 0.0003f, 0.0100f, fm, 0.0f);
        r.logarithmic = true;
    }
    {
        SettingRow& r = b.toggle(kKeyLinkSensitivity, "Same vertical sensitivity",
                                 "Vertical (pitch) sensitivity follows the mouse sensitivity. Changing the vertical "
                                 "sensitivity on its own turns this off.");
        r.scope = SettingScope::ModernOnly;
        r.currentValue = env.linkSensitivity ? "1" : "0";
        r.on = env.linkSensitivity;
    }
    {
        char fm[48];
        std::snprintf(fm, sizeof fm, "%%.4f   (x%.2f)", static_cast<double>(c.sensY / 0.0020f));
        SettingRow& r = b.slider("input.mouse.sensitivity_y", "Vertical sensitivity", "Separate pitch sensitivity.",
                                 c.sensY, 0.0003f, 0.0100f, fm, 0.0f);
        r.logarithmic = true;
        r.linkLabel = "Same as horizontal";
        r.linkKey = kKeyLinkSensitivity;
        r.linked = env.linkSensitivity;
    }
    b.toggle("input.mouse.invert_x", "Invert horizontal look",
             "Off: mouse right turns right. On: reverses horizontal mouse movement.");
    b.toggle("input.mouse.invert_y", "Invert vertical look",
             "Off (default): mouse up looks up. On: mouse up looks down.");
    {
        SettingRow& r = b.row("input.mouse.bow_aim", SettingKind::Toggle, "Modern bow aiming",
                              "With the bow out: hold the left button to aim (slight zoom and a crosshair), release "
                              "to shoot at the crosshair. Off: the game's own bow (left click shoots, right click "
                              "opens the sight).",
                              false, true);
        r.onValue = "modern";
        r.offValue = "classic";
        r.on = c.bowAimModern;
    }
    b.toggle("input.mouse.free_combat", "Free mouse combat",
             "Mouse movement turns Rynn freely during combat. F and middle click toggle native target lock; the bow "
             "sight keeps its native aiming camera.");
    {
        SettingRow& r = b.slider("input.mouse.camera_follow", "Camera follow",
                                 "Turning carries the camera round Rynn at once; this sets how quickly it catches up "
                                 "as she moves (higher = stiffer, lower = smoother). 0 = the original console camera, "
                                 "which also swings round about half a second after a turn.",
                                 c.cameraFollow, 0.0f, 30.0f, "%.0f", 1.0f);
        r.zeroLabel = "Original";
    }
    b.toggle("input.mouse.pitch_persist", "Keep camera pitch",
             "The camera stays tilted where the mouse left it. Off: the game re-centres it a moment after you stop.");
    b.toggle("input.menu.inventory_mouse", "Mouse in inventory / map",
             "Moving the pointer steps the selection, the wheel switches sections, left click uses the item, right "
             "click goes back.");
    b.toggle("input.general.capture_mouse", "Capture mouse", "Hide and lock the cursor to the window while playing.");
    b.toggle("input.general.release_on_esc", "Esc releases the mouse",
             "Esc also frees the cursor by hand. Not needed: the cursor is freed automatically in menus.");
    {
        SettingRow& r = b.row("input.general.toggle_key", SettingKind::Choice, "Capture toggle key",
                              "Frees / recaptures the cursor while playing.", false, true);
        r.choices.push_back({"NONE", "None", true});
        for (const auto& k : allKeyNames())
            if (k.size() > 1 && k[0] == 'F' && k.size() <= 3)
                r.choices.push_back({k, sourceDisplayName(k), true});
        r.currentLabel = c.toggleKey.empty() || c.toggleKey == "NONE" ? std::string("None") : c.toggleKey;
        Builder::select(r);
    }

    pg.sections.push_back({"Keyboard movement", {}});
    b.sec = &pg.sections.back();
    b.toggle("input.keyboard.toggle_run", "Toggle run",
             "Press the Walk key (Left Shift by default) to switch between walking and running. Off: hold to walk. "
             "Starts running; opening a menu resets to running.");
    b.slider("input.keyboard.walk_scale", "Walk speed", "Left stick scale while walking.", c.walkScale, 0.1f, 1.0f,
             "%.2f", 0.05f);
    b.toggle("input.keyboard.strafe", "A / D strafe",
             "On foot A and D sidestep and the mouse turns Rynn (Order of the Flame). Off: A / D turn like the stock "
             "PS2 controls.");
    b.toggle("input.keyboard.curve_compensation", "Full speed on keyboard",
             "The game squares stick input; this undoes it so WASD gives full speed.");

    pg.sections.push_back({"Gamepad", {}});
    b.sec = &pg.sections.back();
    b.toggle("input.gamepad.enabled", "Use gamepad", "Merge an XInput pad with keyboard and mouse.", false,
             env.caps.gamepad);
    b.slider("input.gamepad.deadzone", "Stick dead zone", "Ignore small stick movements.", c.gamepadDeadzone, 0.0f,
             0.5f, "%.2f", 0.01f, env.caps.gamepad && c.gamepadEnabled)
        .needs = "Needs Use gamepad.";
}

void gameplayPage(gameui::SettingPage& pg, const Settings& st, const SchemaEnv& env) {
    const GeneralSettings& g = st.general;
    const HostCaps& caps = env.caps;
    pg.id = "gameplay";
    pg.title = "Gameplay";
    pg.description = "Start-up, button prompts, language, the modern menus, dual wielding and the narration subtitles.";
    Builder b{st};
    pg.sections.push_back({"Game", {}});
    b.sec = &pg.sections.back();
    b.toggle("general.skip_memcard_warning", "Skip memory card warning",
             "Dismiss the start-up memory card message automatically. Saves are written to a folder next to the game.");
    b.choice("general.ui_prompts", "Button prompts",
             "Keyboard: the game's text and help pictures name your keys and mouse buttons, following the key "
             "bindings. PS2: original controller icons.",
             {{"keyboard", "Keyboard and mouse"}, {"ps2", "PlayStation 2 icons"}}, true, caps.promptStyle);
    {
        SettingRow& r = b.choice("general.language", "Language",
                                 "Text, voices and menus come from the matching disc language tree. Takes effect "
                                 "after a restart.",
                                 {{"english", "English"},
                                  {"french", "Fran\xC3\xA7"
                                             "ais"},
                                  {"german", "Deutsch"},
                                  {"italian", "Italiano"},
                                  {"spanish", "Espa\xC3\xB1ol"}},
                                 true, caps.languageSelect);
        r.currentLabel = r.current >= 0 ? r.choices[static_cast<size_t>(r.current)].label : std::string("English");
    }
    b.choice("general.inventory_screen", "Inventory screen",
             "Modern: the Tab inventory as a full-screen PC menu over the live world (mouse, keyboard and gamepad). "
             "Original: the game's own screen. The I strip and the M map keep their views. Takes effect the next time "
             "the inventory opens.",
             {{"modern", "Modern"}, {"native", "Original"}});
    b.choice("general.pause_menu", "Pause menu",
             "Modern: the Esc menu as a PC menu over the dimmed world, with your location, objectives and health "
             "(Save, Load, Options and Quit still open the game's own screens). Original: the game's own pause menu. "
             "Takes effect the next time the game is paused.",
             {{"modern", "Modern"}, {"native", "Original"}});
    b.choice("general.save_menu", "Save / load screens",
             "Modern: Save Game and Load Game as a PC slot browser with large previews of each save. Original: the "
             "game's own slot screens. Takes effect the next time a save or load screen opens.",
             {{"modern", "Modern"}, {"native", "Original"}});
    b.choice("general.title_menu", "Title menu",
             "Modern: the main menu (Load Game, New Game, Options, Credits, Quit) as a PC menu over the title art. "
             "Original: the game's own title menu. Takes effect the next time the title menu opens.",
             {{"modern", "Modern"}, {"native", "Original"}});

    pg.sections.push_back({"Combat", {}});
    b.sec = &pg.sections.back();
    b.toggle("general.dual_wield", "Dual wielding",
             "Use a second one-handed weapon while Rynn holds one and it goes to her left hand instead of a shield "
             "(the inventory shows Equip Left Hand). Her strikes then alternate hands: every second strike in a combo "
             "is the same swing done left-handed. Use it again to put it away. Turning this off puts the left-hand "
             "weapon back in inventory when play resumes.");

    pg.sections.push_back({"Subtitles", {}});
    b.sec = &pg.sections.back();
    b.toggle("subtitles.narration", "Narration subtitles",
             "Show subtitles for the intro and ending narration. Spoken dialogue in the levels uses the game's own "
             "Options > Audio > Show Subtitles switch.");
    b.scaleChoice("subtitles.size", "Subtitle size", "Size of the narration subtitles. Auto follows the window height.",
                  st.subtitles.size,
                  {{0.0f, "Auto"}, {0.75f, "75%"}, {1.0f, "100%"}, {1.25f, "125%"}, {1.5f, "150%"}, {2.0f, "200%"}},
                  st.subtitles.narration)
        .needs = "Needs narration subtitles.";

    pg.sections.push_back({"This menu", {}});
    b.sec = &pg.sections.back();
    {
        SettingRow& r = b.scaleChoice(
            "general.settings_ui_scale", "Menu size", "Size of this settings menu. Auto follows the window height.",
            g.uiScale,
            {{0.0f, "Auto"}, {0.75f, "75%"}, {1.0f, "100%"}, {1.25f, "125%"}, {1.5f, "150%"}, {2.0f, "200%"}});
        r.scope = SettingScope::OverlayOnly; // the F1 overlay's own size
    }
}

bool parseSize(const std::string& v, int& w, int& h) {
    const size_t x = v.find_first_of("xX");
    if (x == std::string::npos)
        return false;
    char* e = nullptr;
    const long a = std::strtol(v.c_str(), &e, 10);
    if (e != v.c_str() + x)
        return false;
    const char* p = v.c_str() + x + 1;
    const long b = std::strtol(p, &e, 10);
    if (e == p || *e || a <= 0 || b <= 0)
        return false;
    w = static_cast<int>(a);
    h = static_cast<int>(b);
    return true;
}

} // namespace

std::vector<gameui::BindingLayerView> buildBindingViews(const Settings& s) {
    std::vector<gameui::BindingLayerView> out;
    for (int li = 0; li < 2; ++li) {
        const bool game = li == 0;
        const BindingLayer& layer = game ? s.controls.game : s.controls.menu;
        const auto& defs = game ? gameActions() : menuActions();
        gameui::BindingLayerView v;
        v.id = game ? "game" : "menu";
        v.title = game ? "Gameplay keys" : "Menu keys";
        v.description = game
                            ? "Keys and mouse buttons while playing. Amber: a key used for several actions; red: a key "
                              "the port reserves."
                            : "Active whenever the game is not in gameplay: title, pause, inventory, cutscenes. Esc "
                              "normally gives Circle and Start together (back, and resume from the pause menu).";
        std::map<std::string, std::vector<std::string>> bySource;
        for (const auto& l : layer.lines) {
            bySource[l.source] = l.actions;
            v.sources.push_back({l.source, l.actions});
        }
        auto labelOf = [&](const std::string& id) -> std::string {
            for (const auto& d : defs)
                if (id == d.id)
                    return d.label;
            return id;
        };
        for (const ActionDef& d : defs) {
            if (v.groups.empty() || v.groups.back().title != d.group)
                v.groups.push_back({d.group, {}});
            gameui::BindingAction a;
            a.id = d.id;
            a.label = d.label;
            a.hint = d.hint ? d.hint : "";
            for (const std::string& src : layer.sourcesFor(d.id)) {
                gameui::BindingSlot sl;
                sl.source = src;
                sl.display = sourceDisplayName(src);
                for (const std::string& other : bySource[src])
                    if (other != d.id)
                        sl.alsoBoundTo.push_back(labelOf(other));
                sl.reserved = reservedReason(src, s.controls.toggleKey);
                a.slots.push_back(std::move(sl));
            }
            v.groups.back().actions.push_back(std::move(a));
        }
        out.push_back(std::move(v));
    }
    return out;
}

gameui::SettingsSchema buildSettingsSchema(const Settings& s, const SchemaEnv& env) {
    gameui::SettingsSchema sc;
    sc.pages.resize(4);
    displayPage(sc.pages[0], s, env);
    audioPage(sc.pages[1], s, env);
    controlsPage(sc.pages[2], s, env);
    gameplayPage(sc.pages[3], s, env);
    sc.bindings = buildBindingViews(s);
    sc.bindingSlots = 3;
    for (auto& page : sc.pages)
        for (auto& section : page.sections)
            for (auto& row : section.rows)
                if (env.caps.unavailableSettings.contains(row.key))
                    row.enabled = false;
    return sc;
}

bool applySettingChange(Settings& s, bool& link, const std::string& key, const std::string& value) {
    if (key == kKeyLinkSensitivity) {
        link = value == "1";
        if (link)
            s.controls.sensY = s.controls.sensX;
        return true;
    }
    if (key == kKeyWindowSize || key == kKeyWindowCustom) {
        int w = 0, h = 0;
        if (!parseSize(value, w, h))
            return false;
        s.video.windowWidth = w;
        s.video.windowHeight = h;
        return true;
    }
    if (!setSettingValue(s, key, value))
        return false;
    if (key == "input.mouse.sensitivity_x" && link)
        s.controls.sensY = s.controls.sensX;
    else if (key == "input.mouse.sensitivity_y" && std::fabs(s.controls.sensY - s.controls.sensX) > 1e-6f)
        link = false;
    return true;
}

} // namespace dsettings
