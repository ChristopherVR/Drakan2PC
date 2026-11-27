// Drakan port: user settings from drakan.ini (next to the exe). Simple "[section]" / "key = value" lines, ';' or '#'
// comments. Every setting has an environment variable that overrides the file. The file is created with the defaults on
// first run; an existing file (possibly written by other modules, e.g. [general] skip_memcard_warning) is kept and only
// missing keys/sections are appended.
#pragma once
#include <filesystem>
#include <string>

namespace drakan_cfg {
// The play folder: directory of the running executable. One implementation per platform; on Windows it is read with
// the wide GetModuleFileNameW (no MAX_PATH cut, any characters).
// exeDirPath(): the exact path. Prefer it for new code that opens files (std::filesystem / std::ifstream(path)), it
// works for every folder name.
const std::filesystem::path& exeDirPath();
// exeDir(): the same folder with a trailing native separator, as a narrow string in the encoding the narrow file APIs
// read (fopen, std::ifstream(std::string), std::filesystem::path(std::string)): the ANSI code page on Windows, UTF-8
// elsewhere. A folder name the ANSI code page cannot spell gets '?' there, as before; use exeDirPath() for those.
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
// Case-insensitive ASCII comparison of setting values (0 = equal), like _stricmp.
int compareNoCase(const char* a, const char* b);
// True when the value (env or file) is something other than "auto" / empty.
// Opts the process into per-monitor-v2 DPI awareness (call before creating any window) so Windows does not bitmap-scale
// us.
void enableDpiAwareness();
bool isSet(const char* section, const char* key, const char* env);
} // namespace drakan_cfg
