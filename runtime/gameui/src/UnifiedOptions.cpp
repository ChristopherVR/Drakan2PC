#include "gameui/UnifiedOptions.h"

#include <string>

namespace gameui {

namespace {

constexpr PauseOption kAll[] = {PauseOption::MasterVolume, PauseOption::MusicVolume, PauseOption::Subtitles,
                                PauseOption::Stereo,       PauseOption::Scheme,      PauseOption::Vibration,
                                PauseOption::ReversePitch};

const char* keyName(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
            return "master_volume";
        case PauseOption::MusicVolume:
            return "music_volume";
        case PauseOption::Subtitles:
            return "subtitles";
        case PauseOption::Stereo:
            return "stereo";
        case PauseOption::Scheme:
            return "scheme";
        case PauseOption::Vibration:
            return "vibration";
        case PauseOption::ReversePitch:
            return "reverse_pitch";
    }
    return "?";
}

SettingRow gameRow(PauseOption o, const PauseOptionsState& st) {
    SettingRow r;
    r.key = gameOptionKey(o);
    r.enabled = st.valid;
    r.unavailable = "The game's options are not available right now.";
    const int32_t v = st.get(o);
    r.currentValue = std::to_string(v);
    switch (o) {
        case PauseOption::MasterVolume:
        case PauseOption::MusicVolume:
            r.kind = SettingKind::Slider;
            r.label = o == PauseOption::MasterVolume ? "Game volume" : "Music volume";
            r.help = o == PauseOption::MasterVolume
                         ? "The game's own volume of everything it plays (its Master Volume option)."
                         : "The game's music volume (its Music Volume option).";
            r.min = 0;
            r.max = 100;
            r.step = 5;
            r.integer = true;
            r.value = static_cast<float>(v);
            r.format = "%d%%";
            break;
        case PauseOption::Subtitles:
            r.kind = SettingKind::Toggle;
            r.label = "Show subtitles";
            r.help = "Subtitles for the spoken dialogue in cut scenes (the game's own option).";
            r.on = v != 0;
            break;
        case PauseOption::Stereo:
            r.kind = SettingKind::Toggle;
            r.label = "Stereo sound";
            r.help = "Stereo output; off mixes the game's sound to mono.";
            r.on = v != 0;
            break;
        case PauseOption::Scheme:
            r.kind = SettingKind::Choice;
            r.label = "Control scheme";
            r.help = "The game's controller layout A, B or C (the buttons the gamepad and the key bindings map to).";
            r.choices = {{"0", "A", true}, {"1", "B", true}, {"2", "C", true}};
            r.current = v >= 0 && v <= 2 ? v : -1;
            r.currentLabel = r.current >= 0 ? r.choices[static_cast<size_t>(r.current)].label : std::string();
            break;
        case PauseOption::Vibration:
            r.kind = SettingKind::Toggle;
            r.label = "Vibration";
            r.help = "Controller vibration.";
            r.on = v != 0;
            break;
        case PauseOption::ReversePitch:
            r.kind = SettingKind::Toggle;
            r.label = "Reverse pitch";
            r.help = "The game's own up / down inversion for the camera and flying (its Reverse Pitch option).";
            r.on = v != 0;
            break;
    }
    return r;
}

} // namespace

std::string gameOptionKey(PauseOption o) {
    return std::string(kGameOptionPrefix) + keyName(o);
}

std::optional<PauseOption> gameOptionFromKey(const std::string& key) {
    for (PauseOption o : kAll)
        if (key == gameOptionKey(o))
            return o;
    return std::nullopt;
}

const char* gameOptionPage(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
        case PauseOption::MusicVolume:
        case PauseOption::Subtitles:
        case PauseOption::Stereo:
            return "audio";
        default:
            return "controls";
    }
}

int32_t gameOptionDefault(PauseOption o) {
    switch (o) {
        case PauseOption::MasterVolume:
            return 100;
        case PauseOption::MusicVolume:
            return 80;
        case PauseOption::Subtitles:
        case PauseOption::Stereo:
        case PauseOption::Vibration:
            return 1;
        default:
            return 0;
    }
}

void foldGameOptions(const SettingsSchema& pc, const PauseOptionsState& o, SettingsSchema& out) {
    out = pc;
    uint32_t h = 2166136261u;
    for (int32_t v : o.values)
        h = (h ^ static_cast<uint32_t>(v)) * 16777619u;
    out.revision = pc.revision * 31u + h + (o.valid ? 1u : 0u);
    for (SettingPage& pg : out.pages) {
        const bool audio = pg.id == "audio", controls = pg.id == "controls";
        if (!audio && !controls)
            continue;
        SettingSection game{audio ? "Game sound" : "Game controller", {}};
        for (PauseOption opt : kAll)
            if (gameOptionPage(opt) == pg.id)
                game.rows.push_back(gameRow(opt, o));
        pg.sections.insert(pg.sections.begin(), std::move(game));
        if (audio) {
            pg.description = "The game's volumes, subtitles and stereo, the output volume of the whole mix and the "
                             "output device.";
            pg.note = "Output volume scales the whole mix after the game's own volumes.";
            for (SettingSection& s : pg.sections)
                for (SettingRow& r : s.rows)
                    if (r.key == "audio.volume")
                        r.label = "Output volume";
        } else {
            pg.description = "The game's control scheme, vibration and pitch, mouse look, keyboard movement and the "
                             "gamepad.";
        }
    }
}

} // namespace gameui
