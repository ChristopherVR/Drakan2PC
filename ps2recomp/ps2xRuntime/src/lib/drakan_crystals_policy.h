// Drakan: Magic Crystal table (`gameplay/crystals.ini` of a converted Order of the Flame pack, written by the editor's
// ootfcrystal.ts): converted crystal class -> the effect its use spawns at Rynn. Pure parsing and the use test, no
// guest access (drakan_crystals.cpp uses it; scripts/tests crystals_policy covers it).
//
//   format = 1
//   [crystal 0x34200]      ; the crystal's resource id (level dependency << 16 | class id)
//   effect = 0x20c07       ; the resource id of the effect class
//   kind = fire            ; for the log
//   name = Fire Crystal    ; for the log
#pragma once

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

namespace drakan_crystals {

struct Crystal {
    uint32_t effect = 0;
    std::string kind, name;
};

// The engine message a usable item gets when it is used (the mana power-up's handler 392aa0 adds its Mana Boost only
// for this id, with a non-zero user in the message's first word).
inline constexpr uint16_t kUseMessage = 0x10cb;

inline bool isUseMessage(uint16_t id, uint32_t user) {
    return id == kUseMessage && user != 0;
}

inline std::string trimmed(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline bool parseNumber(const std::string& text, uint32_t& out) {
    const std::string t = trimmed(text);
    if (t.empty())
        return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(t.c_str(), &end, 0);
    if (!end || *end != '\0' || v > 0xffffffffUL)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// Adds the rows of one file to `out` (a later file wins for the same crystal). Returns the number of rows read; rows
// without a valid crystal id or effect are skipped, unknown keys ignored, `;` / `#` start a comment.
inline size_t parseCrystalTable(const std::string& text, std::map<uint32_t, Crystal>& out) {
    size_t rows = 0;
    uint32_t current = 0;
    bool inCrystal = false;
    Crystal pending;
    auto flush = [&]() {
        if (inCrystal && current && pending.effect) {
            out[current] = pending;
            ++rows;
        }
        inCrystal = false;
        current = 0;
        pending = Crystal{};
    };
    size_t at = 0;
    while (at <= text.size()) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos)
            nl = text.size();
        std::string line = text.substr(at, nl - at);
        at = nl + 1;
        const size_t comment = line.find_first_of(";#");
        if (comment != std::string::npos)
            line.resize(comment);
        line = trimmed(line);
        if (line.empty())
            continue;
        if (line.front() == '[') {
            flush();
            const size_t close = line.find(']');
            const std::string head =
                trimmed(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            if (head.rfind("crystal ", 0) == 0 && parseNumber(head.substr(8), current) && current)
                inCrystal = true;
            continue;
        }
        const size_t eq = line.find('=');
        if (!inCrystal || eq == std::string::npos)
            continue;
        const std::string key = trimmed(line.substr(0, eq)), value = trimmed(line.substr(eq + 1));
        if (key == "effect") {
            uint32_t v = 0;
            if (parseNumber(value, v))
                pending.effect = v;
        } else if (key == "kind") {
            pending.kind = value;
        } else if (key == "name") {
            pending.name = value;
        }
    }
    flush();
    return rows;
}

} // namespace drakan_crystals
