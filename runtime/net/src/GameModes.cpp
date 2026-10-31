#include "net/GameModes.h"

#include <cctype>
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

bool parseInt(const std::string& s, long long lo, long long hi, long long& out) {
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 0);
    if (end == s.c_str() || *end || v < lo || v > hi)
        return false;
    out = v;
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

bool parseIds(const std::vector<std::string>& w, std::vector<uint32_t>& out) {
    for (const std::string& t : w) {
        long long v = 0;
        if (!parseInt(t, 1, 0xffffffffll, v))
            return false;
        out.push_back(static_cast<uint32_t>(v));
    }
    return out.size() <= 16;
}

} // namespace

size_t ModeCatalog::add(const std::string& text, const std::string& source, std::vector<std::string>* errors) {
    size_t added = 0;
    ModeDef cur;
    bool open = false, bad = false;
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
        else if (cur.name.size() > 40)
            why = "name longer than 40 characters";
        else if (!cur.teamBase && cur.teams != 0)
            why = "teams needs base = tdm";
        if (!why.empty()) {
            err(sectionLine, "mode '" + cur.id + "' dropped: " + why);
            return;
        }
        if (cur.teamBase && cur.teams == 0)
            cur.teams = 2;
        cur.source = source;
        for (ModeDef& m : modes_)
            if (m.id == cur.id) {
                m = cur;
                ++added;
                return;
            }
        modes_.push_back(cur);
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
            open = false;
            if (head.size() == 2 && lower(head[0]) == "mode") {
                cur = ModeDef();
                bad = false;
                sectionLine = lineNo;
                open = true;
                if (validId(head[1])) {
                    cur.id = lower(head[1]);
                } else {
                    err(lineNo, "bad mode id");
                    bad = true;
                }
            } // other sections (arenas) are not ours
            continue;
        }
        if (!open)
            continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            err(lineNo, "expected key = value");
            bad = true;
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        const std::vector<std::string> w = words(val);
        long long n = 0;
        const auto num = [&](long long lo, long long hi) {
            if (parseInt(val, lo, hi, n))
                return true;
            err(lineNo, key + " must be " + std::to_string(lo) + ".." + std::to_string(hi));
            bad = true;
            return false;
        };
        if (key == "name") {
            cur.name = val;
        } else if (key == "description") {
            cur.description = val;
        } else if (key == "base") {
            const std::string b = lower(val);
            if (b == "dm" || b == "tdm") {
                cur.teamBase = b == "tdm";
            } else {
                err(lineNo, "base must be dm or tdm");
                bad = true;
            }
        } else if (key == "teams") {
            if (num(0, 4)) {
                if (n == 1) {
                    err(lineNo, "teams must be 0 or 2..4");
                    bad = true;
                } else {
                    cur.teams = static_cast<uint8_t>(n);
                }
            }
        } else if (key == "score_kill") {
            if (num(-1000, 1000))
                cur.scoreKill = static_cast<int32_t>(n);
        } else if (key == "score_death") {
            if (num(-1000, 1000))
                cur.scoreDeath = static_cast<int32_t>(n);
        } else if (key == "score_objective") {
            if (num(-1000, 1000))
                cur.scoreObjective = static_cast<int32_t>(n);
        } else if (key == "frag_limit") {
            if (num(0, 1000))
                cur.fragLimit = static_cast<uint16_t>(n);
        } else if (key == "time_limit") {
            if (num(0, 7200))
                cur.timeLimitSec = static_cast<uint16_t>(n);
        } else if (key == "respawn_delay") {
            if (num(0, 60))
                cur.respawnDelaySec = static_cast<uint8_t>(n);
        } else if (key == "friendly_fire") {
            const std::string b = lower(val);
            if (b == "on" || b == "off" || b == "true" || b == "false" || b == "1" || b == "0") {
                cur.friendlyFire = b == "on" || b == "true" || b == "1";
            } else {
                err(lineNo, "friendly_fire must be on or off");
                bad = true;
            }
        } else if (key == "start_health") {
            if (num(0, 1000))
                cur.startHealth = static_cast<uint16_t>(n);
        } else if (key == "loadout" || key == "weapons") {
            std::vector<uint32_t>& dst = key == "loadout" ? cur.loadout : cur.weapons;
            dst.clear();
            if (!parseIds(w, dst)) {
                err(lineNo, key + " entries are resource ids (0x20abc), at most 16");
                bad = true;
            }
        } else if (key == "objective") {
            ModeObjective o;
            const std::string kind = w.empty() ? "" : lower(w[0]);
            bool ok = kind == "capture" || kind == "hold";
            o.kind = kind == "hold" ? ModeObjective::Kind::Hold : ModeObjective::Kind::Capture;
            for (size_t i = 1; ok && i < w.size(); ++i) {
                long long v = 0;
                if (w[i].rfind("points=", 0) == 0) {
                    ok = parseInt(w[i].substr(7), 0, 1000, v);
                    o.points = static_cast<uint32_t>(v);
                } else if (w[i].rfind("seconds=", 0) == 0) {
                    ok = parseInt(w[i].substr(8), 0, 3600, v);
                    o.seconds = static_cast<uint32_t>(v);
                } else if (w[i].rfind("radius=", 0) == 0) {
                    ok = parseInt(w[i].substr(7), 1, 100000, v);
                    o.radius = static_cast<float>(v);
                } else {
                    ok = false;
                }
            }
            if (ok && o.kind == ModeObjective::Kind::Hold && o.seconds == 0)
                ok = false;
            if (!ok || cur.objectives.size() >= 8) {
                err(lineNo, "objective = capture|hold [points=N] [seconds=N (hold needs it)] [radius=N], at most 8");
                bad = true;
            } else {
                cur.objectives.push_back(o);
            }
        } else {
            err(lineNo, "unknown key '" + key + "'");
            bad = true;
        }
    }
    finish();
    return added;
}

const ModeDef* ModeCatalog::find(const std::string& idOrName) const {
    const std::string k = lower(trim(idOrName));
    for (const ModeDef& m : modes_)
        if (m.id == k || lower(m.name) == k)
            return &m;
    return nullptr;
}

void applyModeToConfig(const ModeDef& mode, MatchConfigData& config) {
    config.mode = mode.teamBase ? MatchMode::TeamDeathmatch : MatchMode::Deathmatch;
    config.fragLimit = mode.fragLimit;
    config.timeLimitSec = mode.timeLimitSec;
    config.respawnDelayMs = static_cast<uint16_t>(mode.respawnDelaySec * 1000u);
    if (mode.startHealth)
        config.startHealth = mode.startHealth;
}

} // namespace drakan::net
