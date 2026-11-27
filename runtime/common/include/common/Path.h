// UTF-8 text <-> std::filesystem::path, independent of the Windows ANSI code page.
//
// The runtime passes paths around as UTF-8 std::string (exeDirectory(), ini values, the browser's file names). On
// Windows a std::string handed to std::ifstream / std::filesystem::path is read in the ANSI code page instead, so a
// folder name outside it (Cyrillic, CJK, ...) opens the wrong file or none. Convert at the point of use. Header only.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace drakan::common {

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8FromPath(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}

} // namespace drakan::common
