#include "drakan_config.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace drakan_cfg {
namespace {
struct Entry {
    const char *section, *key, *def, *comment;
};

// Defaults written to a fresh drakan.ini (and appended to an existing one when missing). Order = file order.
const Entry kEntries[] = {
    {"video", "fov", "80", "field of view: horizontal degrees at 4:3, 60 .. 120; 80 original; live (env DRAKAN_FOV)"},
    {"general", "skip_memcard_warning", "1",
     "auto-dismiss the memory card warning (Press X to continue) at boot; card emulation is untouched"},
    {"general", "iso", "",
     "path of the game disc image (.iso). Empty = look for 'Drakan*.iso' next to the exe, then in the parent folders "
     "(env DRAKAN_ISO)"},
    {"general", "elf", "",
     "path of the extracted SCES_500.06 (optional). Empty = read it straight from the disc image (env DRAKAN_ELF)"},
    {"general", "ui_prompts", "keyboard",
     "button prompts in the game's text: keyboard (keys and mouse) or ps2 (PlayStation 2 icons); needs a restart (env "
     "DRAKAN_UI_PROMPTS=0|1)"},
    {"general", "language", "english",
     "english | french | german | italian | spanish: language tree of the disc to load; needs a restart (env "
     "DRAKAN_LANG)"},
    {"general", "pause_when_unfocused", "0",
     "1 = pause the game and its audio while the window is not in the foreground (or minimised); 0 = keep running at "
     "normal speed (env DRAKAN_PAUSE_UNFOCUSED)"},
    {"general", "console", "0",
     "1 = open a console window with the log (QuickEdit disabled); 0 = no console, the log goes to drakan.log next to "
     "the exe (env DRAKAN_CONSOLE)"},
    {"general", "quit_exits", "1",
     "1 = Quit in the pause menu (after the confirmation) closes the game and returns to the desktop; 0 = the original "
     "behaviour, back to the title menu (env DRAKAN_QUIT_EXITS)"},
    {"general", "resume_on_close", "0",
     "F1 during gameplay pauses through the game's own pause menu and opens the settings over it; 1 = closing the "
     "settings also resumes the game, 0 = leave the pause menu open (env DRAKAN_RESUME_ON_CLOSE)"},
    {"general", "save_pages", "4",
     "Load Game / Save Game: number of pages of four slots (1 = the original four slots, 4 = 16 slots); Next / Back "
     "buttons, PgUp / PgDn and L1 / R1 switch pages; page 1 = the original save folders (env DRAKAN_SAVE_PAGES)"},
    {"general", "title_quit", "1",
     "1 = the title menu gets a Quit entry below Credits (asks for confirmation, then closes the game); 0 = the "
     "original four entries (env DRAKAN_TITLE_QUIT)"},
    {"general", "settings_ui_scale", "auto", "size of the F1 settings menu: auto (by window height) or 0.75 .. 2.5"},
    {"general", "dual_wield", "0",
     "1 = using a second one-handed weapon while Rynn holds one puts it in her left hand (instead of a shield); her "
     "strikes then alternate hands. 0 = original, one weapon (env DRAKAN_DUAL_WIELD)"},

    {"video", "renderer", "d3d11",
     "d3d11 = GPU renderer (default), sw = slow reference software renderer (env PS2X_GS)"},
    {"video", "scale", "auto",
     "internal resolution multiplier 1..8; auto follows fitted output size, including width for the native 512-square "
     "display (env PS2X_GS_SCALE)"},
    {"video", "aspect", "4:3",
     "4:3 (original) | 16:9 | 21:9 | auto (= the window's shape): the 3D world gets a wider field of view, menus / "
     "movies / loading screens stay 4:3 | native | stretch (env DRAKAN_WS_ASPECT / PS2X_ASPECT)"},
    {"video", "hud_scale", "auto",
     "size of the HUD, in-game text and subtitle bar: auto (follows the output size) or 0.5 .. 1.5 (1.0 = the original "
     "4:3 size); clusters shrink towards their nearest screen edge (env DRAKAN_HUD_SCALE)"},
    {"video", "hud_aspect_correct", "1",
     "1 = HUD and in-game panels keep their 4:3 proportions in widescreen (full-width fades stay full width), 0 = they "
     "stretch with the picture (env DRAKAN_HUD_ASPECT)"},
    {"video", "smooth_menu_highlight", "1",
     "1 = soften the checkerboard fire effect of the highlighted menu entry (New Game, OK ...) like a TV did, 0 = raw "
     "PS2 output (env DRAKAN_SMOOTH_HIGHLIGHT)"},
    {"video", "window_width", "1280", "initial window size in pixels (windowed mode)"},
    {"video", "window_height", "960", ""},
    {"video", "fullscreen", "0",
     "1 = start in borderless fullscreen; Alt+Enter or F11 toggles at runtime (env DRAKAN_FULLSCREEN)"},
    {"video", "vsync", "1", "1 = synchronise presentation with the display refresh (env DRAKAN_VSYNC)"},
    {"video", "filter", "game",
     "texture filtering: game = as the TEX1 register of each draw asks, point = force nearest, bilinear = force linear "
     "(env DRAKAN_FILTER)"},
    {"video", "deinterlace", "weave",
     "weave = show the finished frame as stored (sharp, default), bob = one field per vsync, blend = vertical 2-tap "
     "(env DRAKAN_DEINTERLACE)"},
    {"video", "flicker_filter", "0",
     "1 = keep the anti-flicker blend of the game (blurs the image), 0 = off (env DRAKAN_FLICKER_FILTER)"},
    {"video", "sharp_present", "1",
     "1 = area-average downscale / sharp-bilinear upscale in the final blit, 0 = plain bilinear (env "
     "DRAKAN_SHARP_PRESENT)"},

    {"video", "show_fps", "0",
     "FPS counter: 0 = off, 1 = simple (game fps, presented fps, frame time), 2 = detailed (+ 1% low, worst frame, CPU "
     "load, GPU); F2 cycles it in game (env DRAKAN_SHOW_FPS)"},
    {"video", "fps_position", "top-left",
     "corner of the FPS counter: top-left | top-right | bottom-left | bottom-right (env DRAKAN_FPS_POSITION)"},
    {"video", "frame_limit", "0",
     "cap of the presented frame rate in fps, 0 = off (the game runs at its own pace; only useful with vsync off)"},
    {"video", "world_rate", "uncapped",
     "uncapped = default (no fixed world FPS limit), 60 = up to 60 FPS, 30 = original cadence; CPU optimizations apply "
     "to all modes; restart required (env DRAKAN_WORLD_RATE)"},
    {"video", "tick_snap", "1",
     "1 = with vsync, advance the world by whole display refreshes per frame (smooth motion; total game time "
     "unchanged); restart required (env DRAKAN_TICK_SNAP)"},
    {"video", "anisotropic", "16", "anisotropic filtering: 1 = off, 2, 4, 8, 16 (env GSR_ANISO)"},
    {"video", "texture_upscale", "auto",
     "texture upscaling: 1 = off, 2, 4, 8, auto (internal scale 2-3 -> 2x, 4+ -> 4x) (env GSR_TEXUP)"},
    {"video", "texture_upscale_filter", "default",
     "default (xBRZ for pixel art / UI, smooth for photographic) | xbrz | smooth (env GSR_TEXUP_FILTER)"},
    {"video", "mip_bias", "auto",
     "texture LOD bias, negative = sharper; auto = -log2(internal scale) (env GSR_MIPBIAS)"},
    {"video", "hd_textures", "1",
     "1 = use replacement textures from texture_dir (also accepted: texture_replace) (env GSR_TEXREPLACE)"},
    {"video", "texture_dump", "0",
     "1 = write every decoded texture to <texture_dir>/dump/<hash>.png (for making replacements) (env GSR_TEXDUMP)"},
    {"video", "texture_dir", "textures", "texture pack / dump folder, relative to the exe folder (env GSR_TEXDIR)"},
    {"video", "hd_movies", "1",
     "1 = play the logo / intro / outro movies from the HD movie pack in the movies folder when it exists "
     "(analysis/scripts/fmv_hd_pack.py), 0 = the original 512x512 frames (env DRAKAN_HD_MOVIES)"},
    {"video", "texture_budget_mb", "0",
     "texture cache budget in MB, 0 = auto (40 % of the dedicated VRAM) (env GSR_TEXBUDGET)"},

    {"subtitles", "narration", "1",
     "1 = subtitles for the intro / ending narration from subtitles/intro_<language>.srt, outro_<language>.srt; the "
     "game's own Options > Audio > Show Subtitles also switches them (env DRAKAN_SUBTITLES)"},
    {"subtitles", "size", "auto", "size of the narration subtitles: auto (by window height) or 0.5 .. 3.0"},

    {"audio", "enabled", "1", "0 = no sound output (the SPU2 is still emulated silently) (env DRAKAN_AUDIO)"},
    {"audio", "volume", "100", "master volume in percent, 0..200 (env DRAKAN_VOLUME)"},
    {"audio", "device", "default",
     "default = follow the Windows default output device (also when it changes), or part of a device name, e.g. "
     "Speakers (env DRAKAN_AUDIO_DEVICE)"},
};

std::mutex g_mutex;
bool g_loaded = false;
std::map<std::string, std::string> g_values; // "section.key" (lower case) -> value

std::string lower(std::string s) {
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a]))
        ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1]))
        --b;
    return s.substr(a, b - a);
}

std::string readFile(const std::string& path, bool& ok) {
    ok = false;
    std::string data;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            data.append(buf, n);
        std::fclose(f);
        ok = true;
    }
    return data;
}

// Parses ini text into `out` ("section.key" -> value).
void parseInto(const std::string& text, std::map<std::string, std::string>& out) {
    std::string section;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos)
            e = text.size();
        std::string line = trim(text.substr(pos, e - pos));
        pos = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[') {
            const size_t r = line.find(']');
            section = lower(trim(line.substr(1, r == std::string::npos ? std::string::npos : r - 1)));
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = lower(trim(line.substr(0, eq)));
        std::string v = line.substr(eq + 1);
        const size_t sc = v.find(" ;"); // trailing comment
        if (sc != std::string::npos)
            v = v.substr(0, sc);
        out[section + "." + k] = trim(v);
    }
}

// Offset just after the "[sec]" header line, or npos.
size_t sectionBodyStart(const std::string& text, const std::string& sec) {
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t e = text.find('\n', pos);
        const size_t next = e == std::string::npos ? text.size() : e + 1;
        const std::string line = trim(text.substr(pos, (e == std::string::npos ? text.size() : e) - pos));
        if (!line.empty() && line[0] == '[') {
            const size_t r = line.find(']');
            if (lower(trim(line.substr(1, r == std::string::npos ? std::string::npos : r - 1))) == sec)
                return next;
        }
        pos = next;
    }
    return std::string::npos;
}

// Appends the keys/sections of kEntries that are missing from `text`. Returns true when text changed.
bool complete(std::string& text, const std::string& nl) {
    std::map<std::string, std::string> have;
    parseInto(text, have);
    bool changed = false;
    if (text.empty()) {
        text = "; drakan.ini - Drakan port settings. Environment variables (named in the comments) override these "
               "values." +
               nl;
        changed = true;
    }
    std::vector<std::string> sections;
    for (const Entry& en : kEntries)
        if (std::find(sections.begin(), sections.end(), en.section) == sections.end())
            sections.push_back(en.section);
    for (const std::string& sec : sections) {
        std::string add;
        for (const Entry& en : kEntries) {
            if (sec != en.section || have.count(sec + "." + en.key))
                continue;
            if (en.comment && en.comment[0])
                add += "; " + std::string(en.comment) + nl;
            add += std::string(en.key) + " = " + en.def + nl;
        }
        if (add.empty())
            continue;
        changed = true;
        size_t at = sectionBodyStart(text, sec);
        if (at == std::string::npos) {
            if (!text.empty() && text.back() != '\n')
                text += nl;
            text += nl + "[" + sec + "]" + nl + add;
        } else {
            if (at == text.size() && (text.empty() || text.back() != '\n'))
                add = nl + add;
            text.insert(at, add);
        }
    }
    return changed;
}

const Entry* findEntry(const char* section, const char* key) {
    for (const Entry& e : kEntries)
        if (!std::strcmp(e.section, section) && !std::strcmp(e.key, key))
            return &e;
    return nullptr;
}
} // namespace

const std::string& iniPath() {
    static std::string p = [] {
        if (const char* e = std::getenv("DRAKAN_INI"))
            if (*e)
                return std::string(e);
        return exeDir() + "drakan.ini";
    }();
    return p;
}

void load() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loaded)
        return;
    g_loaded = true;
    bool ok = false;
    std::string text = readFile(iniPath(), ok);
    const std::string nl = (!ok || text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    if (complete(text, nl)) {
        if (std::FILE* f = std::fopen(iniPath().c_str(), "wb")) {
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
        }
    }
    parseInto(text, g_values);
}

std::string getString(const char* section, const char* key, const char* env, const char* def) {
    load();
    if (env && *env)
        if (const char* e = std::getenv(env))
            if (*e)
                return e;
    auto it = g_values.find(lower(section) + "." + lower(key));
    if (it != g_values.end())
        return it->second;
    if (!def)
        if (const Entry* en = findEntry(section, key))
            def = en->def;
    return def ? def : "";
}

int getInt(const char* section, const char* key, const char* env, int def) {
    const std::string s = getString(section, key, env, nullptr);
    if (s.empty() || lower(s) == "auto")
        return def;
    return std::atoi(s.c_str());
}

double getFloat(const char* section, const char* key, const char* env, double def) {
    const std::string s = getString(section, key, env, nullptr);
    if (s.empty() || lower(s) == "auto")
        return def;
    return std::atof(s.c_str());
}

bool getBool(const char* section, const char* key, const char* env, bool def) {
    const std::string s = lower(getString(section, key, env, nullptr));
    if (s.empty() || s == "auto")
        return def;
    return !(s == "0" || s == "false" || s == "no" || s == "off");
}

bool isSet(const char* section, const char* key, const char* env) {
    const std::string s = lower(getString(section, key, env, nullptr));
    return !s.empty() && s != "auto";
}
} // namespace drakan_cfg
