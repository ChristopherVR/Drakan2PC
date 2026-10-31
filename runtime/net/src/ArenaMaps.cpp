#include "net/ArenaMaps.h"

#include "net/Protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace drakan::net {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

bool parseFloat(const std::string& s, float& out) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end || !std::isfinite(v) || std::fabs(v) > 1e9)
        return false;
    out = static_cast<float>(v);
    return true;
}

bool parseUint(const std::string& s, uint32_t& out) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, 0);
    if (end == s.c_str() || *end || v > 0xffffffffull)
        return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 32)
        return false;
    for (char c : id)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
            return false;
    return true;
}

} // namespace

bool ArenaDef::insideBounds(float x, float z) const {
    return !hasBounds || (x >= bounds[0] && x <= bounds[2] && z >= bounds[1] && z <= bounds[3]);
}

int ArenaDef::pickSpawn(const std::vector<std::pair<float, float>>& others, int avoid) const {
    if (spawns.empty())
        return -1;
    int best = -1;
    float bestScore = -1.0f;
    for (size_t i = 0; i < spawns.size(); ++i) {
        if (static_cast<int>(i) == avoid && spawns.size() > 1)
            continue;
        float nearest = 1e30f;
        for (const auto& o : others) {
            const float dx = spawns[i].pos[0] - o.first, dz = spawns[i].pos[2] - o.second;
            nearest = std::min(nearest, dx * dx + dz * dz);
        }
        if (nearest > bestScore) {
            bestScore = nearest;
            best = static_cast<int>(i);
        }
    }
    return best;
}

size_t ArenaCatalog::add(const std::string& text, const std::string& source, std::vector<std::string>* errors,
                         const std::string& defaultGame) {
    size_t added = 0;
    ArenaDef cur;
    bool open = false, bad = false, skipMode = false;
    int sectionLine = 0;
    const auto err = [&](int line, const std::string& msg) {
        if (errors)
            errors->push_back(source + ":" + std::to_string(line) + ": " + msg);
    };
    const auto finish = [&]() {
        if (!open)
            return;
        open = false;
        if (bad)
            return;
        std::string why;
        if (cur.name.empty())
            why = "no name";
        else if (cur.name.size() > kMaxLevelLength)
            why = "name longer than 64 characters";
        else if (cur.level.empty())
            why = "no level";
        else if (cur.spawns.size() < 2)
            why = "needs at least 2 spawn points";
        else if (!cur.deathmatch && !cur.teamDeathmatch)
            why = "no supported mode";
        if (!why.empty()) {
            err(sectionLine, "arena '" + cur.id + "' dropped: " + why);
            return;
        }
        cur.source = source;
        if (cur.game.empty())
            cur.game = defaultGame;
        for (ArenaDef& a : arenas_)
            if (a.id == cur.id) {
                a = cur;
                ++added;
                return;
            }
        arenas_.push_back(cur);
        ++added;
    };

    std::istringstream in(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        const size_t semi = raw.find_first_of(";#");
        const std::string line = trim(semi == std::string::npos ? raw : raw.substr(0, semi));
        if (line.empty())
            continue;
        if (line.front() == '[') {
            finish();
            const size_t close = line.find(']');
            const std::vector<std::string> head =
                close == std::string::npos ? std::vector<std::string>() : words(line.substr(1, close - 1));
            cur = ArenaDef();
            bad = false;
            skipMode = false;
            sectionLine = lineNo;
            open = true;
            if (head.size() == 2 && lower(head[0]) == "mode") {
                open = false; // [mode <id>] sections belong to ModeCatalog (GameModes.h)
                skipMode = true;
            } else if (head.size() == 2 && lower(head[0]) == "arena" && validId(head[1])) {
                cur.id = lower(head[1]);
            } else {
                err(lineNo, "bad section header (expected [arena <id>])");
                bad = true;
            }
            continue;
        }
        if (!open) {
            if (!skipMode)
                err(lineNo, "setting outside a section");
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            err(lineNo, "expected key = value");
            bad = true;
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        const std::vector<std::string> w = words(val);
        if (key == "name") {
            cur.name = val;
        } else if (key == "level") {
            cur.level = val;
        } else if (key == "game") { // which game the map belongs to (menu tag)
            const std::string g = lower(val);
            cur.game = g == "d1" || g == "ootf" || g == "drakan 1" || g == "drakan1"   ? "Drakan 1"
                       : g == "d2" || g == "drakan 2" || g == "drakan2" || g == "drakan" ? "Drakan 2"
                                                                                           : val.substr(0, 16);
        } else if (key == "description") {
            cur.description = val;
        } else if (key == "modes") {
            cur.deathmatch = cur.teamDeathmatch = false;
            std::string v = val;
            std::replace(v.begin(), v.end(), ',', ' ');
            for (const std::string& m : words(v)) {
                const std::string ml = lower(m);
                if (ml == "dm" || ml == "deathmatch") {
                    cur.deathmatch = true;
                } else if (ml == "tdm" || ml == "team" || ml == "teamdeathmatch") {
                    cur.teamDeathmatch = true;
                } else if (validId(m)) {
                    cur.customModes.push_back(ml);
                } else {
                    err(lineNo, "bad mode id '" + m + "'");
                    bad = true;
                }
            }
        } else if (key == "players") {
            uint32_t n = 0;
            if (!parseUint(val, n) || n < 2 || n > kMaxPlayers) {
                err(lineNo, "players must be 2.." + std::to_string(kMaxPlayers));
                bad = true;
            } else {
                cur.maxPlayers = static_cast<uint8_t>(n);
            }
        } else if (key == "health") {
            uint32_t n = 0;
            if (!parseUint(val, n) || n < 1 || n > 1000) {
                err(lineNo, "health must be 1..1000");
                bad = true;
            } else {
                cur.startHealth = static_cast<uint16_t>(n);
            }
        } else if (key == "bounds") {
            float b[4];
            if (w.size() != 4 || !parseFloat(w[0], b[0]) || !parseFloat(w[1], b[1]) || !parseFloat(w[2], b[2]) ||
                !parseFloat(w[3], b[3]) || !(b[0] < b[2]) || !(b[1] < b[3])) {
                err(lineNo, "bounds = x0 z0 x1 z1 with x0 < x1 and z0 < z1");
                bad = true;
            } else {
                cur.hasBounds = true;
                std::copy(b, b + 4, cur.bounds);
            }
        } else if (key == "loadout") {
            for (const std::string& t : w) {
                uint32_t id = 0;
                if (!parseUint(t, id) || id == 0) {
                    err(lineNo, "loadout entries are resource ids (0x20abc)");
                    bad = true;
                    break;
                }
                cur.loadout.push_back(id);
            }
            if (cur.loadout.size() > 8) {
                err(lineNo, "at most 8 loadout items");
                bad = true;
            }
        } else if (key == "spawn") {
            ArenaSpawn s;
            bool ok = w.size() >= 3 && w.size() <= 5 && parseFloat(w[0], s.pos[0]) && parseFloat(w[1], s.pos[1]) &&
                      parseFloat(w[2], s.pos[2]);
            for (size_t i = 3; ok && i < w.size(); ++i) {
                uint32_t t = 0;
                if (w[i].rfind("team=", 0) == 0)
                    ok = parseUint(w[i].substr(5), t) && t >= 1 && t <= 4 && (s.team = static_cast<uint8_t>(t));
                else
                    ok = i == 3 && parseFloat(w[i], s.yawDeg);
            }
            if (!ok) {
                err(lineNo, "spawn = x y z [yawDeg] [team=1..4]");
                bad = true;
            } else if (cur.spawns.size() >= 32) {
                err(lineNo, "at most 32 spawn points");
                bad = true;
            } else {
                cur.spawns.push_back(s);
            }
        } else if (key == "marker") {
            ArenaMarker m;
            const std::string kind = w.empty() ? "" : lower(w[0]);
            bool ok = w.size() >= 4 && (kind == "base" || kind == "flag" || kind == "zone") &&
                      parseFloat(w[1], m.pos[0]) && parseFloat(w[2], m.pos[1]) && parseFloat(w[3], m.pos[2]);
            m.kind = kind == "base"   ? ArenaMarker::Kind::Base
                     : kind == "flag" ? ArenaMarker::Kind::Flag
                                      : ArenaMarker::Kind::Zone;
            m.radius = m.kind == ArenaMarker::Kind::Flag ? 150.0f : 600.0f;
            for (size_t i = 4; ok && i < w.size(); ++i) {
                uint32_t t = 0;
                if (w[i].rfind("radius=", 0) == 0) {
                    ok = parseFloat(w[i].substr(7), m.radius) && m.radius > 0 && m.radius <= 100000;
                } else if (w[i].rfind("team=", 0) == 0) {
                    ok = parseUint(w[i].substr(5), t) && t >= 1 && t <= 4 && (m.team = static_cast<uint8_t>(t));
                } else if (w[i].rfind("name=", 0) == 0) {
                    m.label = trim(val.substr(val.find("name=") + 5));
                    break;
                } else {
                    ok = false;
                }
            }
            if (!ok) {
                err(lineNo, "marker = base|flag|zone x y z [radius=R] [team=N] [name=Label]");
                bad = true;
            } else if (cur.markers.size() >= 32) {
                err(lineNo, "at most 32 markers");
                bad = true;
            } else {
                cur.markers.push_back(std::move(m));
            }
        } else if (key == "keep" || key == "remove") {
            std::array<float, 3> p{};
            if (w.size() != 3 || !parseFloat(w[0], p[0]) || !parseFloat(w[1], p[1]) || !parseFloat(w[2], p[2])) {
                err(lineNo, key + " = x y z");
                bad = true;
            } else if (cur.keepAt.size() + cur.removeAt.size() >= 4096) {
                err(lineNo, "at most 4096 keep / remove entries");
                bad = true;
            } else {
                (key == "keep" ? cur.keepAt : cur.removeAt).push_back(p);
            }
        } else if (key == "pickup") {
            ArenaPickup p;
            bool ok = w.size() >= 4 && parseFloat(w[0], p.pos[0]) && parseFloat(w[1], p.pos[1]) &&
                      parseFloat(w[2], p.pos[2]) && parseUint(w[3], p.respawnSec) && p.respawnSec >= 1 &&
                      p.respawnSec <= 3600;
            for (size_t i = 4; ok && i < w.size(); ++i) {
                if (w[i].rfind("res=", 0) == 0) {
                    ok = parseUint(w[i].substr(4), p.resId) && p.resId != 0;
                } else if (w[i].rfind("name=", 0) == 0) {
                    // the label may hold spaces: take everything after "name="
                    p.label = trim(val.substr(val.find("name=") + 5));
                    break;
                } else {
                    ok = false;
                }
            }
            if (!ok) {
                err(lineNo, "pickup = x y z respawnSec [res=0x...] [name=Label]");
                bad = true;
            } else if (cur.pickups.size() >= 64) {
                err(lineNo, "at most 64 pickups");
                bad = true;
            } else {
                cur.pickups.push_back(std::move(p));
            }
        } else {
            err(lineNo, "unknown key '" + key + "'");
            bad = true;
        }
    }
    finish();
    return added;
}

const ArenaDef* ArenaCatalog::find(const std::string& idOrName) const {
    const std::string k = lower(trim(idOrName));
    for (const ArenaDef& a : arenas_)
        if (a.id == k || lower(a.name) == k)
            return &a;
    return nullptr;
}

} // namespace drakan::net
