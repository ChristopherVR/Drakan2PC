// A synthetic settings schema for the SettingsPages tests and the offline preview (the real one comes from
// dsettings::buildSettingsSchema; runtime/settings/tests/settings_pages_preview.cpp previews that).
#pragma once
#include <cstdio>
#include <string>

#include "gameui/SettingsSchema.h"

namespace gameui::test {

inline SettingRow toggleRow(const char* key, const char* label, bool on, const char* help, bool enabled = true) {
    SettingRow r;
    r.key = key;
    r.kind = SettingKind::Toggle;
    r.label = label;
    r.help = help;
    r.on = on;
    r.enabled = enabled;
    r.currentValue = on ? "1" : "0";
    return r;
}

inline SettingRow choiceRow(const char* key, const char* label, std::vector<SettingChoice> choices, int current,
                            const char* help) {
    SettingRow r;
    r.key = key;
    r.kind = SettingKind::Choice;
    r.label = label;
    r.help = help;
    r.choices = std::move(choices);
    r.current = current;
    r.currentValue = current >= 0 ? r.choices[static_cast<size_t>(current)].value : std::string("?");
    return r;
}

inline SettingRow sliderRow(const char* key, const char* label, float v, float mn, float mx, const char* fmt,
                            float step, const char* help) {
    SettingRow r;
    r.key = key;
    r.kind = SettingKind::Slider;
    r.label = label;
    r.help = help;
    r.value = v;
    r.min = mn;
    r.max = mx;
    r.format = fmt;
    r.step = step;
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", static_cast<double>(v));
    r.currentValue = b;
    return r;
}

struct SettingsWorld {
    SettingsSchema schema;
    SettingsWorld() {
        SettingPage d;
        d.id = "display";
        d.title = "Display";
        d.description = "Window, frame rate and textures.";
        SettingSection w{"Window and output", {}};
        {
            SettingRow r = choiceRow("video.window_mode", "Window mode",
                                     {{"windowed", "Windowed", true},
                                      {"borderless", "Borderless fullscreen", true},
                                      {"exclusive", "Exclusive fullscreen", false}},
                                     0, "Windowed, borderless fullscreen (recommended) or exclusive fullscreen.");
            r.confirm = true;
            w.rows.push_back(r);
        }
        {
            SettingRow r = choiceRow("video.window_size", "Window size",
                                     {{"800x600", "800 x 600  (4:3)", true},
                                      {"1024x768", "1024 x 768  (4:3)", true},
                                      {"1280x960", "1280 x 960  (4:3)", true},
                                      {"1440x1080", "1440 x 1080  (4:3)", true},
                                      {"1280x720", "1280 x 720  (16:9)", true},
                                      {"1600x900", "1600 x 900  (16:9)", true},
                                      {"1920x1080", "1920 x 1080  (16:9)  monitor native", true},
                                      {"2560x1440", "2560 x 1440  (16:9)", true},
                                      {"3840x2160", "3840 x 2160  (16:9)", true}},
                                     2, "Size of the game window in pixels.");
            r.currentLabel = "1280 x 960";
            r.confirm = true;
            w.rows.push_back(r);
        }
        {
            SettingRow r;
            r.key = "video.window_custom";
            r.kind = SettingKind::Size;
            r.scope = SettingScope::OverlayOnly;
            r.label = "Custom window size";
            w.rows.push_back(r);
        }
        w.rows.push_back(sliderRow("video.fov", "Field of view", 80, 60, 120, "%.0f degrees", 1,
                                   "How much of the world you can see. 80 is original."));
        {
            SettingRow r = choiceRow("video.scale", "Internal render scale",
                                     {{"auto", "Auto", true}, {"1", "1x", true}, {"2", "2x", true}, {"3", "3x", true}},
                                     0, "Higher scales sharpen rendering but cost performance.");
            r.restart = true;
            w.rows.push_back(r);
        }
        d.sections.push_back(w);
        SettingSection pr{"Presentation", {}};
        pr.rows.push_back(toggleRow("video.vsync", "VSync", true, "Synchronise presentation with the monitor."));
        pr.rows.push_back(toggleRow("video.flicker_filter", "Flicker filter", false, "The game's anti-flicker blend."));
        pr.rows.push_back(toggleRow("video.sharp_present", "Sharp scaling", true, "Sharp-bilinear upscale."));
        pr.rows.push_back(
            toggleRow("video.texture_dump", "Texture dump", false, "Write every decoded texture.", false));
        pr.rows.push_back(sliderRow("video.mip_bias", "Mip bias", 0, -2, 2, "%+.2f", 0.05f,
                                    "Negative = sharper (can shimmer), positive = softer."));
        {
            SettingRow r;
            r.key = "video.ray_tracing_status";
            r.kind = SettingKind::Info;
            r.label = "Ray tracing status";
            r.text = "DXR 1.1 available";
            pr.rows.push_back(r);
        }
        pr.rows.push_back(toggleRow("video.hd_textures", "HD texture replacement", true, "Replacement textures."));
        pr.rows.push_back(toggleRow("video.smooth_menu_highlight", "Smooth menu highlight", true, "Softer flames."));
        d.sections.push_back(pr);
        schema.pages.push_back(d);

        SettingPage a;
        a.id = "audio";
        a.title = "Audio";
        a.description = "Sound output and the master volume.";
        a.note = "Music and sound effect volume have separate sliders in the game's own Audio page.";
        SettingSection o{"Output", {}};
        o.rows.push_back(toggleRow("audio.enabled", "Sound", true, "Off silences the output."));
        {
            SettingRow r = sliderRow("audio.volume", "Master volume", 100, 0, 200, "%d%%", 5, "Overall loudness.");
            r.integer = true;
            o.rows.push_back(r);
        }
        {
            SettingRow r = choiceRow("audio.device", "Output device",
                                     {{"default", "System default", true}, {"Speakers", "Speakers", true}}, 0,
                                     "Default follows the Windows default device.");
            r.restart = true;
            o.rows.push_back(r);
        }
        a.sections.push_back(o);
        schema.pages.push_back(a);

        auto slot = [](const char* src, const char* disp) {
            BindingSlot s;
            s.source = src;
            s.display = disp;
            return s;
        };
        BindingLayerView g;
        g.id = "game";
        g.title = "Gameplay keys";
        g.description = "Keys and mouse buttons while playing.";
        BindingGroup mv{"Movement", {}};
        mv.actions.push_back({"LSTICK_UP", "Forward", "Left stick up", {slot("W", "W"), slot("UP", "Up")}});
        mv.actions.push_back({"LSTICK_DOWN", "Back", "Left stick down", {slot("S", "S")}});
        {
            BindingAction j{"TRIANGLE", "Jump", "Triangle on the PS2 pad", {slot("SPACE", "Space")}};
            j.slots[0].alsoBoundTo = {"Use"};
            mv.actions.push_back(j);
        }
        BindingGroup cb{"Combat", {}};
        {
            BindingAction u{"CROSS", "Use", "Cross on the PS2 pad", {slot("SPACE", "Space"), slot("E", "E")}};
            u.slots[0].alsoBoundTo = {"Jump"};
            cb.actions.push_back(u);
        }
        {
            BindingAction s{"SQUARE", "Attack", "Square", {slot("MOUSE1", "Left Mouse"), slot("F1", "F1")}};
            s.slots[1].reserved = "F1 opens the PC settings";
            cb.actions.push_back(s);
        }
        g.groups = {mv, cb};
        g.sources = {
            {"W", {"LSTICK_UP"}}, {"UP", {"LSTICK_UP"}},  {"S", {"LSTICK_DOWN"}}, {"SPACE", {"TRIANGLE", "CROSS"}},
            {"E", {"CROSS"}},     {"MOUSE1", {"SQUARE"}}, {"F1", {"SQUARE"}}};
        schema.bindings.push_back(g);
        BindingLayerView m;
        m.id = "menu";
        m.title = "Menu keys";
        m.description = "Active whenever the game is not in gameplay.";
        BindingGroup nav{"Navigation", {}};
        nav.actions.push_back({"DPAD_UP", "Up", "D-pad up", {slot("UP", "Up")}});
        m.groups = {nav};
        m.sources = {{"UP", {"DPAD_UP"}}};
        schema.bindings.push_back(m);
    }
    // Apply a Change the way the glue would (value, current, on), so the next frame shows it.
    void apply(const std::string& key, const std::string& value) {
        for (SettingPage& p : schema.pages)
            for (SettingSection& s : p.sections)
                for (SettingRow& r : s.rows)
                    if (r.key == key) {
                        r.currentValue = value;
                        r.on = value == r.onValue;
                        r.currentLabel.clear();
                        for (size_t i = 0; i < r.choices.size(); ++i)
                            if (r.choices[i].value == value)
                                r.current = static_cast<int>(i);
                        if (r.kind == SettingKind::Slider)
                            r.value = std::stof(value);
                    }
        ++schema.revision;
    }
};

} // namespace gameui::test
