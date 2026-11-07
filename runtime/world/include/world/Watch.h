// Play-test watch (DRAKAN_PLAYTEST_WATCH): prints one line per second for the entities that match a filter, so a
// harness run measures what a puzzle does to an object (door open state, lever flags) instead of judging screenshots.
// Pure helpers: parsing of the setting, the filter and the text of one line. The adapter that reads guest memory is
// ps2xRuntime/src/lib/drakan_world.cpp.
//
//   DRAKAN_PLAYTEST_WATCH=class=10013,near=30000        every door within 30000 units of the player
//   DRAKAN_PLAYTEST_WATCH=ref=1234,words=64,data=24     one object by registry ref (decimal or 0x hex)
//
// Keys: player=1 (the player's own line too: her position shows whether a door blocks her), class=<hex class id>,
// ref=<registry ref>, near=<units from the player, 0 = any>, words=<entity words printed, from +0, default 64 = 0x100
// bytes>, data=<instance data words, default 24 = 0x60 bytes>, dataoff=<hex byte offset where they start, default 0>,
// max=<lines per tick, 8>, every=<seconds between lines, default 1>.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "world/Snapshot.h"

namespace world::watch {

struct Spec {
    bool hasClass = false, hasRef = false, withPlayer = false; // withPlayer: also the player's own line
    uint32_t classId = 0, ref = 0;
    float near = 0.0f; // 0 = no distance limit
    uint32_t entityWords = 64, dataWords = 24, maxLines = 8;
    uint32_t dataOffset = 0; // byte offset of the first instance data word printed
    double everySeconds = 1.0;
};

// Parses the setting; false when it is empty or has no filter key (class, ref or near).
inline bool parse(const char* text, Spec& out) {
    if (!text || !*text)
        return false;
    Spec s;
    bool any = false;
    const char* p = text;
    while (*p) {
        const char* end = std::strchr(p, ',');
        const std::string item = end ? std::string(p, end) : std::string(p);
        p = end ? end + 1 : p + item.size();
        const size_t eq = item.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string k = item.substr(0, eq), v = item.substr(eq + 1);
        char* e = nullptr;
        if (k == "class") {
            s.classId = static_cast<uint32_t>(std::strtoul(v.c_str(), &e, 16));
            if (e != v.c_str())
                s.hasClass = any = true;
        } else if (k == "ref") {
            s.ref = static_cast<uint32_t>(std::strtoul(v.c_str(), &e, 0));
            if (e != v.c_str())
                s.hasRef = any = true;
        } else if (k == "player") {
            s.withPlayer = v != "0";
            any = any || s.withPlayer;
        } else if (k == "near") {
            s.near = std::strtof(v.c_str(), nullptr);
            any = true;
        } else if (k == "words") {
            s.entityWords = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            if (s.entityWords > 256)
                s.entityWords = 256;
        } else if (k == "data") {
            s.dataWords = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
            if (s.dataWords > 256)
                s.dataWords = 256;
        } else if (k == "dataoff") {
            s.dataOffset = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 16));
        } else if (k == "max") {
            s.maxLines = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 0));
        } else if (k == "every") {
            const double d = std::strtod(v.c_str(), nullptr);
            if (d > 0.0)
                s.everySeconds = d;
        }
    }
    if (!any)
        return false;
    out = s;
    return true;
}

inline bool matches(const Spec& s, const EntitySnapshot& e, const Vec3& player, bool isPlayer = false) {
    if (isPlayer)
        return s.withPlayer;
    if (s.hasClass && e.classId != s.classId)
        return false;
    if (s.hasRef && e.ref != s.ref)
        return false;
    return s.near <= 0.0f || distance(e.position, player) <= s.near;
}

// One line: time, identity, position, yaw, then every non-zero word of the entity (`e+off=hex`, off in hex bytes) and
// of its instance data (`d+off=hex`). Zero words are left out so that two lines diff by eye. `entity[i]` is the word at
// byte 4*i of the entity record, `data[i]` the word at 4*i of the instance data (empty when there is none).
inline std::string format(double seconds, const EntitySnapshot& e, const std::vector<uint32_t>& entity,
                          const std::vector<uint32_t>& data, uint32_t dataOffset = 0) {
    char b[200];
    std::snprintf(b, sizeof b, "[watch %.2f] ref=%u h=%08x cls=%05x type=%u pos=%.0f,%.0f,%.0f yaw=%.3f", seconds,
                  e.ref, e.handle, e.classId, e.type, static_cast<double>(e.position.x),
                  static_cast<double>(e.position.y), static_cast<double>(e.position.z), static_cast<double>(e.yaw));
    std::string o = b;
    const auto words = [&o](const char* tag, const std::vector<uint32_t>& w, uint32_t base) {
        o += tag;
        bool first = true;
        for (size_t i = 0; i < w.size(); ++i) {
            if (!w[i])
                continue;
            char t[32];
            std::snprintf(t, sizeof t, "%s%x=%x", first ? "" : " ", static_cast<unsigned>(base + i * 4), w[i]);
            o += t;
            first = false;
        }
    };
    words(" | e+", entity, 0);
    if (!data.empty())
        words(" | d+", data, dataOffset);
    return o;
}

} // namespace world::watch
