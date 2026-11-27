// Small ASCII text helpers shared by the Drakan glue (ini values, level names, file names). std only, no locale:
// the "C" locale rules the std::tolower / std::isspace copies they replace used, for any byte value.
#pragma once

#include <string>
#include <string_view>

namespace drakan_text {

constexpr char lowerAscii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
// Space, tab, newline, vertical tab, form feed, carriage return (std::isspace in the "C" locale).
constexpr bool isSpace(char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

inline void lowerInPlace(std::string& s) {
    for (char& c : s)
        c = lowerAscii(c);
}
inline std::string lower(std::string s) {
    lowerInPlace(s);
    return s;
}

// `s` without the leading and trailing white space.
inline std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a]))
        ++a;
    while (b > a && isSpace(s[b - 1]))
        --b;
    return std::string(s.substr(a, b - a));
}
inline std::string trimLower(std::string_view s) {
    return lower(trim(s));
}

inline bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lowerAscii(a[i]) != lowerAscii(b[i]))
            return false;
    return true;
}

} // namespace drakan_text
