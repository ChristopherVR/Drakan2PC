// The drakan_input.ini schema (input/IniSchema.h): the default file agrees with the key table, every key reaches
// Config, and the value rules accept / reject what they promise. Pure logic, no files.
#include "input/Config.h"
#include "input/FrameInput.h"
#include "input/IniSchema.h"

#include "common/Path.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

using namespace drakan_input;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

// section.key -> value of the scalar sections of an ini text (comments stripped the way Config::parse does).
std::map<std::string, std::string> scalarValues(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line, section;
    auto trim = [](std::string s) {
        const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    };
    while (std::getline(in, line)) {
        for (size_t i = 0; i < line.size(); ++i)
            if ((line[i] == ';' || line[i] == '#') && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
                line.resize(i);
                break;
            }
        line = trim(line);
        if (line.empty())
            continue;
        if (line.front() == '[') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        const size_t eq = line.find('=');
        if (eq != std::string::npos && ini::isScalarSection(section))
            out[section + "." + trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return out;
}

bool accepts(const char* section, const char* key, const std::string& text, ini::Value* out = nullptr) {
    const ini::KeySpec* spec = ini::findKey(section, key);
    ini::Value v;
    const bool ok = spec && ini::parseValue(*spec, text, v);
    if (ok && out)
        *out = v;
    return ok;
}

std::string canonical(const char* section, const char* key, const std::string& text) {
    std::string out = "<rejected>";
    ini::canonicalize(*ini::findKey(section, key), text, out);
    return out;
}

// A valid value of the key that differs from its default (to prove the key reaches Config).
std::string otherValue(const ini::KeySpec& k) {
    switch (k.type) {
        case ini::Type::Bool:
            return std::string(k.defaultText) == "1" ? "0" : "1";
        case ini::Type::Float:
            return "7.5";
        case ini::Type::Int:
            return "2";
        case ini::Type::Choice: {
            ini::Value def;
            ini::parseValue(k, k.defaultText, def);
            for (const auto& c : k.choices)
                if (c.value != def.choice)
                    return c.name;
            return k.choices.front().name;
        }
        case ini::Type::Key:
            return "G";
        case ini::Type::PadCombo:
            return "SELECT+START";
        case ini::Type::Ignored:
            return "anything";
    }
    return {};
}

void testDefaultsAgree() {
    // The default file parses without a single warning and every scalar key in it is in the schema.
    Config c;
    std::string warnings;
    Config::parse(ini::defaultIniText(), c, &warnings);
    CHECK(warnings.empty());
    if (!warnings.empty())
        std::printf("%s", warnings.c_str());
    CHECK(std::string(Config::defaultIniText()) == ini::defaultIniText());

    const auto values = scalarValues(ini::defaultIniText());
    for (const auto& [name, value] : values) {
        const size_t dot = name.find('.');
        const ini::KeySpec* spec = ini::findKey(name.substr(0, dot), name.substr(dot + 1));
        CHECK(spec != nullptr);
        if (spec && std::string(spec->defaultText) != value) {
            std::printf("default of %s: schema '%s', file '%s'\n", name.c_str(), spec->defaultText, value.c_str());
            CHECK(false);
        }
    }
    // ... and every schema default is written by the file and is a valid value of its own key.
    for (const auto& k : ini::keys()) {
        const std::string def = k.defaultText;
        if (def.empty())
            continue;
        CHECK(values.count(std::string(k.section) + "." + k.key) == 1);
        CHECK(accepts(k.section, k.key, def));
    }
    // The defaults of Config's member initialisers that the file restates.
    const Config d = Config::defaults();
    CHECK(d.toggleKeyDik == 0x44 && d.debugMenuCombo == (Pad::L3 | Pad::R3) && d.controlsVersion == 3);
    CHECK(d.gamepadLayout == GamepadLayout::Auto && d.directionalAttack == DirectionalAttackMode::Modifier);
    CHECK(d.addrPadMgr == 0x4e90c0 && d.addrStickMap == 0x4e9558);
}

void testEveryKeyReachesConfig() {
    for (const auto& k : ini::keys()) {
        const std::string text = std::string("[") + k.section + "]\n" + k.key + " = " + otherValue(k) + "\n";
        Config c;
        std::string warnings;
        Config::parse(text, c, &warnings);
        if (!warnings.empty())
            std::printf("%s.%s: %s", k.section, k.key, warnings.c_str());
        CHECK(warnings.empty());
    }
    // Spot checks that the value lands in the right member.
    Config c;
    std::string w;
    Config::parse("[gamepad]\nlayout = TWIN_STICK\ndebug_menu_combo = select+start\n[mouse]\ndragon_pitch_down = 0.5\n"
                  "dragon_pitch_up = -0.25\nbow_aim = ps2\n[advanced]\naddr_padmgr = 0x10\nturn_call_abi = gpr\n"
                  "[general]\ntoggle_key = none\nlook_mode = Stick\n",
                  c, &w);
    CHECK(w.empty());
    CHECK(c.gamepadLayout == GamepadLayout::TwinStick);
    CHECK(c.debugMenuCombo == (Pad::SELECT | Pad::START));
    CHECK(c.dragonPitchMin == -0.5f && c.dragonPitchMax == 0.25f);
    CHECK(c.bowAimNative && !c.bowAimModern);
    CHECK(c.addrPadMgr == 0x10 && c.turnCallAbi == CallAbi::GprBits);
    CHECK(c.toggleKeyDik == -1 && c.lookMode == LookMode::Stick);
}

void testRejectedValuesKeepThePrevious() {
    Config c = Config::defaults();
    std::string w;
    Config::parse("[keyboard]\nwalk_scale = fast\ndirectional_attack = none\n[gamepad]\nlayout = sideways\n"
                  "[advanced]\naddr_padmgr = -1\n[general]\ntoggle_key = F13\nfoo = 1\n[nowhere]\nbar = 1\n",
                  c, &w);
    CHECK(c.walkScale == 0.5f);
    CHECK(c.directionalAttack == DirectionalAttackMode::Modifier);
    CHECK(c.gamepadLayout == GamepadLayout::Auto);
    CHECK(c.addrPadMgr == 0x4e90c0);
    CHECK(c.toggleKeyDik == 0x44);
    CHECK(w.find("line 2: bad number for walk_scale") != std::string::npos);
    CHECK(w.find("line 3: directional_attack must be modifier|tap|always|off") != std::string::npos);
    CHECK(w.find("line 5: layout must be auto|native|twin_stick") != std::string::npos);
    CHECK(w.find("line 7: bad number for addr_padmgr") != std::string::npos);
    CHECK(w.find("line 9: unknown key F13") != std::string::npos);
    CHECK(w.find("line 10: unknown key foo") != std::string::npos);
    CHECK(w.find("line 12: key outside a known section: bar") != std::string::npos);
}

void testValueRules() {
    // Bool
    for (const char* yes : {"1", "true", "YES", "On"})
        CHECK(accepts("general", "capture_mouse", yes));
    for (const char* bad : {"", "2", "enabled", "y"})
        CHECK(!accepts("general", "capture_mouse", bad));
    // Float: finite, in float range, the whole text.
    ini::Value v;
    CHECK(accepts("mouse", "sensitivity_x", "0.0020", &v) && v.f == 0.0020f);
    CHECK(accepts("mouse", "yaw_sign", "-1", &v) && v.f == -1.0f);
    for (const char* bad : {"", "abc", "1.0x", "nan", "inf", "-inf", "1e999", "0.5 ms"})
        CHECK(!accepts("mouse", "sensitivity_x", bad));
    // Int: guest addresses are 32 bit and not negative; controls_version is an int.
    CHECK(accepts("advanced", "addr_stick_map", "0xFFFFFFFF", &v) && v.i == 0xFFFFFFFFll);
    CHECK(accepts("advanced", "gui_vtable_offset", "16", &v) && v.i == 16);
    for (const char* bad : {"-1", "0x100000000", "0x", "12abc", ""})
        CHECK(!accepts("advanced", "addr_stick_map", bad));
    CHECK(accepts("general", "controls_version", "2"));
    CHECK(!accepts("general", "controls_version", "99999999999"));
    // Choice: any case, aliases written back as their main spelling.
    CHECK(canonical("mouse", "bow_aim", "Classic") == "ootf");
    CHECK(canonical("mouse", "bow_aim", "PS2") == "native");
    CHECK(canonical("mouse", "dragon_mode", "direct") == "mouse");
    CHECK(canonical("gamepad", "layout", "TWIN_STICK") == "twin_stick");
    CHECK(canonical("keyboard", "directional_attack", "Tap") == "tap");
    for (const char* bad : {"none", "0", "sometimes"})
        CHECK(!accepts("keyboard", "directional_attack", bad));
    // Key
    CHECK(canonical("general", "toggle_key", "f10") == "F10");
    CHECK(canonical("general", "toggle_key", "none") == "NONE");
    CHECK(!accepts("general", "toggle_key", "F13"));
    // PadCombo: names joined by '+', written in mask order.
    CHECK(canonical("gamepad", "debug_menu_combo", "r3+l3") == "L3+R3");
    CHECK(canonical("gamepad", "debug_menu_combo", "L3 + R3") == "L3+R3");
    CHECK(canonical("gamepad", "debug_menu_combo", "L3+") == "L3");
    CHECK(canonical("gamepad", "debug_menu_combo", "off") == "NONE");
    for (const char* bad : {"+L3", "L4", "L3++R3"})
        CHECK(!accepts("gamepad", "debug_menu_combo", bad));
    // Ignored keys take anything.
    CHECK(accepts("menu", "inventory_step", "whatever"));
    // Lookups ignore case.
    CHECK(ini::findKey("MOUSE", "Sensitivity_X") == ini::findKey("mouse", "sensitivity_x"));
    CHECK(ini::findKey("mouse", "no_such_key") == nullptr);
    CHECK(ini::isScalarSection("Advanced") && !ini::isScalarSection("bindings"));
}

void testNameTables() {
    int dik = 0;
    CHECK(keyFromName("space", &dik) && dik == 0x39);
    CHECK(std::string(nameOfKey(0x9C)) == "NUMPADENTER");
    CHECK(nameOfKey(0xFF) == nullptr);
    CHECK(allKeyNames().size() == 97 && allKeyNames().front() == "ESC" && allKeyNames().back() == "DELETE");
    for (const auto& name : allKeyNames())
        CHECK(keyFromName(name, &dik) && std::string(nameOfKey(dik)) == name);
    uint16_t mask = 0;
    CHECK(padButtonFromName("dpad_left", &mask) && mask == Pad::LEFT);
    CHECK(padComboToText(Pad::START | Pad::L2) == "L2+START" && padComboToText(0) == "NONE");
    CHECK(ini::isBindingSource("MOUSE5") && ini::isBindingSource("wheelup") && ini::isBindingSource("F12"));
    CHECK(!ini::isBindingSource("MOUSE6") && !ini::isBindingSource("MOUSE0") && !ini::isBindingSource("JOY1"));
    DirectionalAttackMode m = DirectionalAttackMode::Off;
    CHECK(directionalAttackModeFromName(" Always ", &m) && m == DirectionalAttackMode::Always);
    CHECK(!directionalAttackModeFromName("none", &m) && m == DirectionalAttackMode::Always);
}

// loadOrCreate takes a UTF-8 path (exeDirectory() + name), also outside the Windows ANSI code page.
void testUtf8Path() {
    namespace fs = std::filesystem;
    using drakan::common::pathFromUtf8;
    using drakan::common::utf8FromPath;
    const std::string dirUtf8 = utf8FromPath(fs::temp_directory_path()) + "/drakan_ini_test_" +
                                "\xD0\x94\xD1\x80\xD0\xB0\xD0\xBA\xD0\xB0\xD0\xBD_\xE9\xBE\x8D";
    std::error_code ec;
    fs::remove_all(pathFromUtf8(dirUtf8), ec);
    CHECK(fs::create_directories(pathFromUtf8(dirUtf8), ec));
    const std::string iniUtf8 = dirUtf8 + "/drakan_input.ini";
    std::string warnings;
    Config::loadOrCreate(iniUtf8, &warnings); // no file yet: writes the default one
    CHECK(warnings.empty());
    CHECK(fs::exists(pathFromUtf8(iniUtf8)));
    {
        std::ofstream o(pathFromUtf8(iniUtf8), std::ios::binary | std::ios::trunc);
        o << "[general]\ncontrols_version = 3\n[keyboard]\nwalk_scale = 0.25\n";
    }
    const Config c = Config::loadOrCreate(iniUtf8, &warnings); // the existing file is read, not replaced
    CHECK(warnings.empty() && c.walkScale == 0.25f);
    fs::remove_all(pathFromUtf8(dirUtf8), ec);
}

} // namespace

int main() {
    testDefaultsAgree();
    testEveryKeyReachesConfig();
    testRejectedValuesKeepThePrevious();
    testValueRules();
    testNameTables();
    testUtf8Path();
    std::printf("input_ini_schema_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
