#pragma once

#include <string>
#include <string_view>

#ifndef DRAKAN_REPORT_BUILD
#define DRAKAN_REPORT_BUILD ""
#endif
#ifndef DRAKAN_REPORT_DISC
#define DRAKAN_REPORT_DISC ""
#endif

namespace drakan_issue {

struct SystemDetails {
    std::string os, cpu, ram, gpu;
};

inline std::string compactContext(std::string_view value) {
    constexpr size_t kMaxBytes = 96;
    if (value.size() > kMaxBytes) {
        size_t length = kMaxBytes;
        while (length && (static_cast<unsigned char>(value[length]) & 0xc0) == 0x80)
            --length;
        value = value.substr(0, length);
    }
    std::string clean(value);
    for (char& c : clean)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    return clean;
}

inline void appendQuery(std::string& url, std::string_view field, std::string_view value) {
    if (value.empty())
        return;
    url += '&';
    url += field;
    url += '=';
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            url += static_cast<char>(c);
        } else {
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
}

inline std::string formUrl(bool crash, std::string_view platform, std::string_view screen = {},
                           std::string_view campaign = {}, std::string_view level = {},
                           const SystemDetails& system = {}) {
    std::string url = "https://github.com/ChristopherVR/Drakan2PC/issues/new?template=";
    url += crash ? "crash-report.yml" : "bug-report.yml";
    const std::string location = compactContext(level);
    const std::string screenName = compactContext(screen);
    appendQuery(url, "title",
                crash ? "[Crash] Drakan on " + std::string(platform)
                      : "[Bug] " + (!location.empty()     ? location
                                    : !screenName.empty() ? screenName
                                                          : "Drakan"));
    appendQuery(url, "build", DRAKAN_REPORT_BUILD);

    std::string environment = "Platform: " + std::string(platform);
    auto describe = [&](std::string_view label, std::string_view detail) {
        if (!detail.empty()) {
            environment += '\n';
            environment += label;
            environment += detail;
        }
    };
    describe("Disc: ", DRAKAN_REPORT_DISC);
    describe("Screen: ", screenName);
    describe("Campaign: ", compactContext(campaign));
    describe("Location: ", location);
    describe("OS: ", compactContext(system.os));
    describe("CPU: ", compactContext(system.cpu));
    describe("RAM: ", compactContext(system.ram));
    describe("GPU: ", compactContext(system.gpu));
    appendQuery(url, "environment", environment);
    return url;
}

} // namespace drakan_issue
