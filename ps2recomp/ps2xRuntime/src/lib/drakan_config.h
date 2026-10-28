// Drakan port: user settings from drakan.ini (next to the exe). Simple "[section]" / "key = value" lines, ';' or '#'
// comments. Every setting has an environment variable that overrides the file. The file is created with the defaults on
// first run; an existing file (possibly written by other modules, e.g. [general] skip_memcard_warning) is kept and only
// missing keys/sections are appended.
#pragma once
#include <string>

namespace drakan_cfg {
// Directory of the running executable, with a trailing native separator.
const std::string& exeDir();
// Full path of drakan.ini (DRAKAN_INI overrides).
const std::string& iniPath();
// Loads (creating / completing the file on first use). Called implicitly by the getters.
void load();

// Lookup order: environment variable `env` (if non-null and set) -> file -> built-in default (table in
// drakan_config.cpp).
std::string getString(const char* section, const char* key, const char* env, const char* def);
int getInt(const char* section, const char* key, const char* env, int def);
double getFloat(const char* section, const char* key, const char* env, double def);
bool getBool(const char* section, const char* key, const char* env, bool def);
// True when the value (env or file) is something other than "auto" / empty.
// Opts the process into per-monitor-v2 DPI awareness (call before creating any window) so Windows does not bitmap-scale
// us.
void enableDpiAwareness();
bool isSet(const char* section, const char* key, const char* env);
} // namespace drakan_cfg
