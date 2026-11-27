// Small ASCII text helpers shared by the runtime libraries (ini keys, file names, command words).
//
// ASCII only on purpose: the files they read are ASCII keywords with UTF-8 or Windows-1252 text in the values, and
// std::tolower / std::isspace would change bytes >= 0x80 as soon as a host calls setlocale(). Header only.
#pragma once
#include <string>
#include <string_view>

namespace drakan::common {

constexpr bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}
constexpr char asciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
constexpr char asciiUpper(char c) {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}

inline std::string asciiLower(std::string s) {
    for (char& c : s)
        c = asciiLower(c);
    return s;
}
inline std::string asciiUpper(std::string s) {
    for (char& c : s)
        c = asciiUpper(c);
    return s;
}

// The view without leading / trailing whitespace (space, tab, CR, LF, VT, FF).
constexpr std::string_view trimView(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isAsciiSpace(s[a]))
        ++a;
    while (b > a && isAsciiSpace(s[b - 1]))
        --b;
    return s.substr(a, b - a);
}
inline std::string trim(std::string_view s) {
    return std::string(trimView(s));
}

// Case-insensitive (ASCII) equality: iequals("Mouse", "MOUSE").
constexpr bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (asciiLower(a[i]) != asciiLower(b[i]))
            return false;
    return true;
}

constexpr bool startsWith(std::string_view s, std::string_view prefix) {
    return s.substr(0, prefix.size()) == prefix;
}
constexpr bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace drakan::common
