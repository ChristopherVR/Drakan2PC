// Drakan co-op character roster and clip mapping (drakan_coop_character.h). Portable: no guest access.
#include "drakan_coop_character.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <set>

namespace drakan_coop_character {
namespace {

struct RynnClip {
    uint32_t id;
    Act act;
    int durMs;
};
struct Cell {
    uint32_t id;
    int durMs;
};
struct CharacterRow {
    uint16_t classId;
    const char* name;
    uint32_t model;
    bool usable;
    Cell clips[static_cast<size_t>(Act::Count)];
};

#include "drakan_coop_character_table.inc"

constexpr uint32_t kRynnRecord = 0x20a50u, kRynnModel = 3877u;

std::atomic<int> g_selected{-1}; // -1: not read from the environment yet

const RynnClip* findRynn(uint32_t clip) {
    for (const RynnClip& r : kRynnClips)
        if ((r.id & 0xffffu) == (clip & 0xffffu))
            return &r;
    return nullptr;
}

const CharacterRow* findRow(uint16_t id) {
    for (const CharacterRow& r : kCharacterRows)
        if (r.classId == id)
            return &r;
    return nullptr;
}

bool loops(Act a) {
    switch (a) {
        case Act::Idle:
        case Act::Walk:
        case Act::Run:
        case Act::RunBack:
        case Act::StrafeL:
        case Act::StrafeR:
        case Act::TurnL:
        case Act::TurnR:
        case Act::InAir:
        case Act::Crouch:
        case Act::Block:
            return true;
        default:
            return false;
    }
}

// Fallback chain for an activity the character has no clip for.
Cell pick(const CharacterRow& row, Act a) {
    static const Act chain[][4] = {
        /*Idle*/ {Act::Idle, Act::Idle, Act::Idle, Act::Idle},
        /*Walk*/ {Act::Walk, Act::Run, Act::Idle, Act::Idle},
        /*Run*/ {Act::Run, Act::Walk, Act::Idle, Act::Idle},
        /*RunBack*/ {Act::RunBack, Act::Walk, Act::Run, Act::Idle},
        /*StrafeL*/ {Act::StrafeL, Act::Walk, Act::Run, Act::Idle},
        /*StrafeR*/ {Act::StrafeR, Act::Walk, Act::Run, Act::Idle},
        /*TurnL*/ {Act::TurnL, Act::Idle, Act::Idle, Act::Idle},
        /*TurnR*/ {Act::TurnR, Act::Idle, Act::Idle, Act::Idle},
        /*Jump*/ {Act::Jump, Act::Run, Act::Idle, Act::Idle},
        /*InAir*/ {Act::InAir, Act::Run, Act::Idle, Act::Idle},
        /*Land*/ {Act::Land, Act::Idle, Act::Idle, Act::Idle},
        /*Crouch*/ {Act::Crouch, Act::Idle, Act::Idle, Act::Idle},
        /*Attack*/ {Act::Attack, Act::Idle, Act::Idle, Act::Idle},
        /*Hit*/ {Act::Hit, Act::Idle, Act::Idle, Act::Idle},
        /*Block*/ {Act::Block, Act::Idle, Act::Idle, Act::Idle},
        /*Draw*/ {Act::Draw, Act::Attack, Act::Idle, Act::Idle},
        /*Death*/ {Act::Death, Act::Idle, Act::Idle, Act::Idle},
        /*Spell*/ {Act::Spell, Act::Attack, Act::Idle, Act::Idle},
        /*Roll*/ {Act::Roll, Act::Run, Act::Idle, Act::Idle},
        /*Other*/ {Act::Other, Act::Idle, Act::Idle, Act::Idle},
    };
    for (Act c : chain[static_cast<size_t>(a)]) {
        const Cell& cell = row.clips[static_cast<size_t>(c)];
        if (cell.id)
            return cell;
    }
    return Cell{0, 0};
}

} // namespace

const std::vector<Entry>& roster() {
    static const std::vector<Entry> list = [] {
        std::vector<Entry> v;
        v.push_back({0, kRynnRecord, "Rynn", kRynnModel});
        std::set<uint32_t> models;
        for (const CharacterRow& r : kCharacterRows) {
            if (!r.usable || !models.insert(r.model).second)
                continue;
            v.push_back({r.classId, 0x50000u | r.classId, r.name, r.model});
        }
        return v;
    }();
    return list;
}

bool known(uint16_t id) {
    const auto& r = roster();
    return std::any_of(r.begin(), r.end(), [&](const Entry& e) { return e.id == id; });
}

const Entry& entry(uint16_t id) {
    const auto& r = roster();
    for (const Entry& e : r)
        if (e.id == id)
            return e;
    return r.front();
}

uint16_t selected() {
    int v = g_selected.load();
    if (v < 0) {
        v = 0;
        if (const char* e = std::getenv("DRAKAN_COOP_CHARACTER"))
            v = static_cast<int>(std::strtoul(e, nullptr, 16) & 0xffffu);
        if (!known(static_cast<uint16_t>(v)))
            v = 0;
        g_selected.store(v);
    }
    return static_cast<uint16_t>(v);
}

void setSelected(uint16_t id) {
    g_selected.store(known(id) ? id : 0);
}

Act activityOf(uint32_t rynnClip) {
    const RynnClip* r = findRynn(rynnClip);
    return r ? r->act : Act::Other;
}

Mapped mapClip(uint16_t id, uint32_t rynnClip, float time) {
    Mapped m;
    const RynnClip* r = findRynn(rynnClip);
    const CharacterRow* row = findRow(id);
    if (!row)
        return m;
    m.act = r ? r->act : Act::Idle;
    const Cell c = pick(*row, m.act);
    if (!c.id)
        return m;
    m.clip = c.id;
    float t = time;
    if (r && r->durMs > 0 && c.durMs > 0)
        t = time * static_cast<float>(c.durMs) / static_cast<float>(r->durMs);
    const float dur = static_cast<float>(c.durMs) / 1000.0f;
    if (dur > 0.0f)
        t = loops(m.act) ? std::fmod(std::max(t, 0.0f), dur) : std::min(std::max(t, 0.0f), dur * 0.999f);
    m.time = t;
    return m;
}

} // namespace drakan_coop_character
