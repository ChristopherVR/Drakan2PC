#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <string>

namespace drakan_language {
// Published once before guest startup; native labels and automatic narration follow this
// effective language until restart, including profile/absent-tree fallbacks.
inline std::atomic<uint32_t> g_effectiveOsdLanguage{1};
inline const char* effectiveName() {
    switch (g_effectiveOsdLanguage.load(std::memory_order_relaxed)) {
        case 2:
            return "french";
        case 3:
            return "spanish";
        case 4:
            return "german";
        case 5:
            return "italian";
        default:
            return "english";
    }
}
inline void setEffectiveLanguage(uint32_t language) {
    g_effectiveOsdLanguage.store(language, std::memory_order_relaxed);
}

struct Selection {
    const char* root;
    uint32_t osdLanguage;
    bool fallback;
};

// An explicit environment value wins, including an empty/invalid value (English fallback).
inline Selection select(const std::string& configured, const char* environment, uint32_t languageCount) {
    std::string name = environment ? environment : configured;
    const auto first = name.find_first_not_of(" \t/\\");
    const auto last = name.find_last_not_of(" \t/\\");
    name = first == std::string::npos ? "" : name.substr(first, last - first + 1);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return (char)std::toupper(ch); });
    const char* roots[] = {"\\ENGLISH", "\\GERMAN", "\\FRENCH", "\\ITALIAN", "\\SPANISH"};
    const uint32_t osd[] = {1, 4, 2, 5, 3};
    for (uint32_t i = 0; i < 5; ++i) {
        if (name == roots[i] + 1)
            return i < languageCount ? Selection{roots[i], osd[i], false} : Selection{roots[0], osd[0], true};
    }
    return {roots[0], osd[0], true};
}
} // namespace drakan_language
