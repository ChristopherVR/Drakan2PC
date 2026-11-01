#include "gameui/UiSkin.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "gameui/Layout.h"
#include "gameui/PauseScreen.h"
#include "gameui/TitleScreen.h"

namespace gameui {

namespace {

std::atomic<const UiSkin*> g_skin{nullptr};
thread_local bool g_noSkin = false; // themeColorNames(): read the compiled-in defaults

const UiSkin& emptySkin() {
    static const UiSkin e;
    return e;
}

struct ColorField {
    const char* name;
    Color Theme::* member;
};
const ColorField kColors[] = {
    {"backdrop", &Theme::backdrop},
    {"backdrop_edge", &Theme::backdropEdge},
    {"panel", &Theme::panel},
    {"panel_raised", &Theme::panelRaised},
    {"inset", &Theme::inset},
    {"brass", &Theme::brass},
    {"brass_dim", &Theme::brassDim},
    {"text", &Theme::text},
    {"text_muted", &Theme::textMuted},
    {"text_disabled", &Theme::textDisabled},
    {"heading", &Theme::heading},
    {"ember", &Theme::ember},
    {"ember_glow", &Theme::emberGlow},
    {"select", &Theme::select},
    {"grid_line", &Theme::gridLine},
    {"grid_light", &Theme::gridLight},
    {"gilt", &Theme::gilt},
    {"ivory", &Theme::ivory},
    {"good", &Theme::good},
    {"bad", &Theme::bad},
    {"hp", &Theme::hp},
    {"mana", &Theme::mana},
    {"xp", &Theme::xp},
    {"parchment", &Theme::parchment},
    {"ink", &Theme::ink},
};
struct SizeField {
    const char* name;
    float Theme::* member;
};
const SizeField kSizes[] = {
    {"size_title", &Theme::sizeTitle}, {"size_heading", &Theme::sizeHeading}, {"size_body", &Theme::sizeBody},
    {"size_small", &Theme::sizeSmall}, {"size_numeric", &Theme::sizeNumeric},
};
struct LayoutField {
    const char* name;
    float def;
};
// Defaults equal the constants in TitleScreen.cpp / PauseScreen.cpp.
const LayoutField kLayout[] = {
    {"title_column_w", 520}, {"title_row_h", 56},  {"title_row_step", 64},
    {"title_centre_y", 650}, {"pause_menu_x", 96}, {"pause_menu_w", 560},
    {"pause_panel_y", 160},  {"pause_row_h", 88},  {"pause_row_step", 104},
};
const char* const kPages[] = {"title.main", "pause.root", "pause.options", "pause.audio", "pause.controller"};

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r'))
        --b;
    return std::string(s.substr(a, b - a));
}

bool parseFloat(const std::string& s, float& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !(v == v) || v < -100000 || v > 100000)
        return false;
    out = v;
    return true;
}

const ColorField* findColor(const std::string& k) {
    for (const ColorField& f : kColors)
        if (k == f.name)
            return &f;
    return nullptr;
}
const SizeField* findSize(const std::string& k) {
    for (const SizeField& f : kSizes)
        if (k == f.name)
            return &f;
    return nullptr;
}
const LayoutField* findLayout(const std::string& k) {
    for (const LayoutField& f : kLayout)
        if (k == f.name)
            return &f;
    return nullptr;
}

} // namespace

bool parseSkinColor(std::string_view in, Color& out) {
    std::string s = trim(in);
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6 && s.size() != 8)
        return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    const uint32_t v = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
    out = s.size() == 6 ? ((v << 8) | 0xffu) : v;
    return true;
}

std::string formatSkinColor(Color c) {
    char b[16];
    if ((c & 0xff) == 0xff)
        std::snprintf(b, sizeof b, "#%06X", static_cast<unsigned>(c >> 8));
    else
        std::snprintf(b, sizeof b, "#%08X", static_cast<unsigned>(c));
    return b;
}

size_t parseUiSkin(std::string_view text, UiSkin& into, std::vector<std::string>* warnings) {
    auto warn = [&](int line, const std::string& m) {
        if (warnings)
            warnings->push_back("line " + std::to_string(line) + ": " + m);
    };
    size_t accepted = 0;
    std::string section;
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos)
            nl = text.size();
        const std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == ';' || line[0] == '#' || line.rfind("//", 0) == 0)
            continue;
        if (line[0] == '[') {
            const size_t e = line.find(']');
            if (e == std::string::npos) {
                warn(lineNo, "unterminated section header");
                section = "?";
                continue;
            }
            section = trim(std::string_view(line).substr(1, e - 1));
            std::transform(section.begin(), section.end(), section.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (section != "theme" && section != "hud" && section != "layout" && section != "rows" &&
                section != "strings") {
                warn(lineNo, "unknown section [" + section + "]");
                section = "?";
            }
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn(lineNo, "expected key = value");
            continue;
        }
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string val = trim(std::string_view(line).substr(eq + 1));
        if (section.empty()) {
            warn(lineNo, "key outside a section");
            continue;
        }
        if (section == "?")
            continue;
        if (section == "strings") {
            if (key.empty()) {
                warn(lineNo, "empty string key");
                continue;
            }
            std::string v;
            for (size_t i = 0; i < val.size(); ++i) { // "\n" and "\\" escapes
                if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == 'n') {
                    v += '\n';
                    ++i;
                } else if (val[i] == '\\' && i + 1 < val.size() && val[i + 1] == '\\') {
                    v += '\\';
                    ++i;
                } else
                    v += val[i];
            }
            into.strings[key] = v;
            ++accepted;
        } else if (section == "rows") {
            std::vector<std::string> ids;
            size_t a = 0;
            while (a <= val.size()) {
                size_t c = val.find(',', a);
                if (c == std::string::npos)
                    c = val.size();
                const std::string id = trim(std::string_view(val).substr(a, c - a));
                if (!id.empty())
                    ids.push_back(id);
                a = c + 1;
            }
            bool known = false;
            for (const char* p : kPages)
                known = known || key == p;
            if (!known) {
                warn(lineNo, "unknown row page '" + key + "'");
                continue;
            }
            into.rows[key] = std::move(ids);
            ++accepted;
        } else if (section == "theme" || section == "hud") {
            const bool hud = section == "hud";
            if (!hud && findSize(key)) {
                float v;
                if (!parseFloat(val, v) || v < 4 || v > 400) {
                    warn(lineNo, "bad size for " + key);
                    continue;
                }
                into.numbers[key] = v;
                ++accepted;
                continue;
            }
            Color c;
            if (!parseSkinColor(val, c)) {
                warn(lineNo, "bad colour '" + val + "' for " + key);
                continue;
            }
            if (hud && key == "accent")
                into.colors["hud_accent"] = c;
            else if (!hud && (key == "accent" || findColor(key)))
                into.colors[key] = c;
            else {
                warn(lineNo, "unknown key '" + key + "' in [" + section + "]");
                continue;
            }
            ++accepted;
        } else if (section == "layout") {
            float v;
            if (!findLayout(key)) {
                warn(lineNo, "unknown layout key '" + key + "'");
                continue;
            }
            if (!parseFloat(val, v) || v < 0 || v > 4000) {
                warn(lineNo, "bad number for " + key);
                continue;
            }
            into.numbers[key] = v;
            ++accepted;
        }
    }
    return accepted;
}

std::string writeUiSkin(const UiSkin& s) {
    std::string o;
    auto sectionOf = [&](const char* name, auto&& body) {
        std::string b;
        body(b);
        if (!b.empty())
            o += std::string("[") + name + "]\n" + b + "\n";
    };
    auto num = [](float v) {
        char b[32];
        std::snprintf(b, sizeof b, "%g", static_cast<double>(v));
        return std::string(b);
    };
    sectionOf("theme", [&](std::string& b) {
        for (const auto& [k, v] : s.colors)
            if (k != "hud_accent")
                b += k + " = " + formatSkinColor(v) + "\n";
        for (const auto& [k, v] : s.numbers)
            if (findSize(k))
                b += k + " = " + num(v) + "\n";
    });
    sectionOf("hud", [&](std::string& b) {
        if (auto it = s.colors.find("hud_accent"); it != s.colors.end())
            b += "accent = " + formatSkinColor(it->second) + "\n";
    });
    sectionOf("layout", [&](std::string& b) {
        for (const auto& [k, v] : s.numbers)
            if (findLayout(k))
                b += k + " = " + num(v) + "\n";
    });
    sectionOf("rows", [&](std::string& b) {
        for (const auto& [k, v] : s.rows) {
            b += k + " = ";
            for (size_t i = 0; i < v.size(); ++i)
                b += (i ? ", " : "") + v[i];
            b += "\n";
        }
    });
    sectionOf("strings", [&](std::string& b) {
        for (const auto& [k, v] : s.strings) {
            std::string e;
            for (char c : v)
                e += c == '\n' ? std::string("\\n") : c == '\\' ? std::string("\\\\") : std::string(1, c);
            b += k + " = " + e + "\n";
        }
    });
    return o;
}

void setUiSkin(UiSkin skin) {
    g_skin.store(new UiSkin(std::move(skin))); // the old one is leaked on purpose: references stay valid
}

const UiSkin& activeUiSkin() {
    const UiSkin* s = g_skin.load();
    return s ? *s : emptySkin();
}

const char* skinText(const std::string& key, const char* def) {
    const UiSkin& s = activeUiSkin();
    if (s.strings.empty())
        return def;
    auto it = s.strings.find(key);
    return it == s.strings.end() ? def : it->second.c_str();
}

float skinNumber(const char* key, float def) {
    const UiSkin& s = activeUiSkin();
    if (s.numbers.empty())
        return def;
    auto it = s.numbers.find(key);
    return it == s.numbers.end() ? def : it->second;
}

const std::vector<std::string>* skinRows(const char* page) {
    const UiSkin& s = activeUiSkin();
    if (s.rows.empty())
        return nullptr;
    auto it = s.rows.find(page);
    return it == s.rows.end() ? nullptr : &it->second;
}

void applyUiSkin(Theme& t, const UiSkin& s) {
    if (s.empty())
        return;
    if (auto it = s.colors.find("accent"); it != s.colors.end()) {
        t.ember = it->second;
        t.emberGlow = withAlpha(it->second, static_cast<uint8_t>(t.emberGlow & 0xff)); // soft glow in the accent
    }
    for (const ColorField& f : kColors)
        if (auto it = s.colors.find(f.name); it != s.colors.end())
            t.*(f.member) = it->second;
    if (auto it = s.colors.find("hud_accent"); it != s.colors.end())
        t.hudAccent = it->second;
    for (const SizeField& f : kSizes)
        if (auto it = s.numbers.find(f.name); it != s.numbers.end())
            t.*(f.member) = it->second;
}

Theme::Theme() {
    if (!g_noSkin)
        applyUiSkin(*this, activeUiSkin());
}

std::vector<SkinKeyInfo> themeColorNames() {
    g_noSkin = true;
    const Theme d;
    g_noSkin = false;
    std::vector<SkinKeyInfo> v;
    v.push_back({"accent", formatSkinColor(d.ember)});
    for (const ColorField& f : kColors)
        v.push_back({f.name, formatSkinColor(d.*(f.member))});
    v.push_back({"hud_accent", formatSkinColor(d.hudAccent)});
    return v;
}

std::vector<SkinKeyInfo> themeSizeNames() {
    g_noSkin = true;
    const Theme d;
    g_noSkin = false;
    std::vector<SkinKeyInfo> v;
    for (const SizeField& f : kSizes)
        v.push_back({f.name, std::to_string(static_cast<int>(d.*(f.member)))});
    return v;
}

std::vector<SkinKeyInfo> layoutNames() {
    std::vector<SkinKeyInfo> v;
    for (const LayoutField& f : kLayout)
        v.push_back({f.name, std::to_string(static_cast<int>(f.def))});
    return v;
}

std::vector<SkinKeyInfo> stringKeys() {
    std::vector<SkinKeyInfo> v;
    for (int i = 0; i <= static_cast<int>(TitleRow::NewGameCoop); ++i) {
        const TitleRow r = static_cast<TitleRow>(i);
        v.push_back({std::string("title.row.") + titleRowId(r), titleRowDefaultLabel(r)});
    }
    for (int i = 0; i <= static_cast<int>(TitleRow::NewGameCoop); ++i) {
        const TitleRow r = static_cast<TitleRow>(i);
        if (const char* d = titleRowDefaultDescription(r))
            v.push_back({std::string("title.desc.") + titleRowId(r), d});
    }
    v.push_back({"title.quit.title", "Quit the game?"});
    v.push_back({"title.quit.body", "Drakan closes and returns to the desktop."});
    for (int i = 0; i <= static_cast<int>(PauseRow::Cancel); ++i) {
        const PauseRow r = static_cast<PauseRow>(i);
        v.push_back({std::string("pause.row.") + pauseRowId(r), pauseRowDefaultLabel(r)});
    }
    return v;
}

std::vector<SkinKeyInfo> rowPages() {
    return {
        {"title.main", "continue, load_game, new_game, multiplayer, options, credits, pc_settings, quit"},
        {"pause.root", "resume, save, load, options, quit"},
        {"pause.options", "controller, audio, pc_settings, defaults, back"},
        {"pause.audio", "master_volume, music_volume, subtitles, stereo, back"},
        {"pause.controller", "scheme, vibration, reverse_pitch, key_bindings, back"},
    };
}

} // namespace gameui
