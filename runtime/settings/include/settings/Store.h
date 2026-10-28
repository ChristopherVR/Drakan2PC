// SettingsStore: drakan.ini + drakan_input.ini <-> Settings. Comment/unknown-key preserving, minimal-diff writer.
// Platform free.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "settings/Host.h"
#include "settings/IniDoc.h"
#include "settings/Types.h"

namespace dsettings {

// File access hooks (default: std::ifstream/ofstream with write-to-temp + rename). Replace for OPFS/IndexedDB in the
// browser build or for tests.
struct FileIO {
    std::function<bool(const std::string& path, std::string& out)> read;
    std::function<bool(const std::string& path, const std::string& data)> write;
    static FileIO native();
};

class SettingsStore {
public:
    explicit SettingsStore(Paths paths = {}, FileIO io = FileIO::native(), std::string defaultInputIni = {});

    // Reads both files; missing files / keys give defaults. Returns false only when a file exists but cannot be read.
    bool load(std::string* warnings = nullptr);
    // Same from text (tests): `inputText` empty -> defaults.
    void loadFromText(const std::string& drakanText, const std::string& inputText, bool inputFileExisted = true);

    const Settings& loaded() const { return loaded_; } // state of the files after load()/the last save()
    const Settings& defaults() const { return defaults_; }

    // Writes only the keys whose value differs from loaded() (new keys get a comment), then re-reads. drakan.ini and
    // drakan_input.ini are written only when their text actually changed.
    bool save(const Settings& s, std::string* error = nullptr);
    // Text form of what save() would write (tests, preview).
    void render(const Settings& s, std::string& drakanText, std::string& inputText) const;

    const Paths& paths() const { return paths_; }

private:
    Paths paths_;
    FileIO io_;
    std::string defaultInput_;
    IniDoc drakan_{";"}, input_{";#"};
    Settings loaded_, defaults_;
    void rebuild();
};

// Pure functions (used by the store and the tests).
Settings parseSettings(const IniDoc& drakan, const IniDoc& input, const IniDoc& defaultInput);
void writeSettings(const Settings& before, const Settings& after, IniDoc& drakan, IniDoc& input);

// Keys whose value differs: "video.vsync" (drakan.ini), "input.mouse.sensitivity_x" (drakan_input.ini),
// "input.bindings", "input.menu_bindings".
std::vector<std::string> changedKeys(const Settings& a, const Settings& b);
// Human readable name of a changed key ("video.scale" -> "Internal render scale").
std::string settingLabel(const std::string& key);

// One setting by its changedKeys() name, in the ini spelling the files use ("video.window_mode" -> "borderless",
// "input.mouse.sensitivity_x" -> "0.0020"). False / empty for an unknown key. The set accepts what the loader accepts.
bool setSettingValue(Settings& s, const std::string& key, const std::string& value);
std::string settingValue(const Settings& s, const std::string& key);
// Every key of the field table (drakan.ini and drakan_input.ini, no bindings), in table order.
std::vector<std::string> settingKeys();

} // namespace dsettings
