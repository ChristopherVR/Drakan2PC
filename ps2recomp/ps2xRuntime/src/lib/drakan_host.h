#pragma once

#include <string_view>

namespace drakan_host {
// Configuration tokens are ASCII. Avoid host CRT names and locale-dependent folding.
constexpr bool asciiEqual(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    const auto lower = [](char value) { return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value; };
    for (size_t i = 0; i < left.size(); ++i)
        if (lower(left[i]) != lower(right[i]))
            return false;
    return true;
}
} // namespace drakan_host
