// Drakan co-op character roster and clip mapping (drakan_coop_character.h). Portable: no guest access.
#include "drakan_coop_character.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>

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
    bool humanoid;
    Cell clips[static_cast<size_t>(Act::Count)];
};

struct VoiceSound {
    uint32_t id;
    int kind; // 0 damage cry, 1 death cry, 2 any other Rynn line (generated table)
};

struct HitVoiceRow {
    uint16_t classId;
    unsigned count;
    uint32_t sounds[8];
};

#include "drakan_coop_character_table.inc"

constexpr uint32_t kRynnRecord = 0x20a50u, kRynnModel = 3877u;

std::atomic<bool> g_retargetedMotion{false};
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
        /*Jump*/ {Act::Jump, Act::Crouch, Act::Block, Act::Idle},
        /*InAir*/ {Act::InAir, Act::Crouch, Act::Block, Act::Idle},
        /*Land*/ {Act::Land, Act::Idle, Act::Idle, Act::Idle},
        /*Crouch*/ {Act::Crouch, Act::Idle, Act::Idle, Act::Idle},
        /*Attack*/ {Act::Attack, Act::Idle, Act::Idle, Act::Idle},
        /*Hit*/ {Act::Hit, Act::Idle, Act::Idle, Act::Idle},
        /*Block*/ {Act::Block, Act::Idle, Act::Idle, Act::Idle},
        /*Draw*/ {Act::Draw, Act::Attack, Act::Idle, Act::Idle},
        /*Death*/ {Act::Death, Act::Idle, Act::Idle, Act::Idle},
        /*Spell*/ {Act::Spell, Act::Attack, Act::Idle, Act::Idle},
        /*Roll*/ {Act::Roll, Act::Crouch, Act::Block, Act::Idle},
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
        for (uint16_t id : kSelectableCharacters)
            if (id)
                if (const CharacterRow* r = findRow(id); r && r->usable && r->humanoid)
                    v.push_back({id, 0x50000u | id, r->name, r->model, true});
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

uint32_t mapVoiceSound(uint16_t id, uint32_t sound) {
    if (!id)
        return sound;
    const auto* voice = std::find_if(std::begin(kRynnVoiceSounds), std::end(kRynnVoiceSounds),
                                     [&](const VoiceSound& v) { return v.id == sound; });
    if (voice == std::end(kRynnVoiceSounds))
        return sound;
    const HitVoiceRow* chosen = nullptr;
    const HitVoiceRow* maleFallback = nullptr;
    for (const HitVoiceRow& row : kHitVoices) {
        if (row.classId == id)
            chosen = &row;
        if (row.classId == 0x177f)
            maleFallback = &row; // original Nichols nonverbal male damage reactions, shared when no own voice exists
    }
    if (!chosen)
        return sound; // not a roster character: keep the original
    // Jump / hoist / attack grunts and spoken remarks have no original male equivalent: never play Rynn's voice.
    if (voice->kind == 2)
        return 0;
    if (!chosen->count)
        chosen = maleFallback;
    if (!chosen || !chosen->count)
        return 0;
    // Damage and death cries pick a stable sample from the character's damage set.
    const auto* hit = std::find(std::begin(kRynnHitSounds), std::end(kRynnHitSounds), sound);
    const unsigned index = hit != std::end(kRynnHitSounds)
                               ? static_cast<unsigned>(hit - std::begin(kRynnHitSounds))
                               : static_cast<unsigned>(voice - std::begin(kRynnVoiceSounds));
    return chosen->sounds[index % chosen->count];
}

bool nodeUsesLower(uint16_t id, uint32_t targetNode) {
    const uint32_t model = entry(id).modelId;
    const bool warrior = model == 4402 || model == 4544 || model == 4705 || model == 4706;
    // Torso node 2 owns upper-body attacks; the lowerbody subtree starts at 44 / 45 on the two curated rigs.
    return targetNode == 0 || targetNode >= (warrior ? 45u : 44u);
}

void setRetargetedMotion(bool enabled) {
    g_retargetedMotion.store(enabled);
}

Act activityOf(uint32_t rynnClip) {
    const RynnClip* r = findRynn(rynnClip);
    return r ? r->act : Act::Other;
}

uint32_t sampleClip(Act a, float* durationSec) {
    for (const RynnClip& r : kRynnClips)
        if (r.act == a) {
            if (durationSec)
                *durationSec = static_cast<float>(r.durMs) / 1000.0f;
            return r.id;
        }
    return 0;
}

bool actByName(const std::string& name, Act& out) {
    static const char* const names[] = {"idle",  "walk", "run",   "runback", "strafel", "strafer", "turnl",
                                        "turnr", "jump", "inair", "land",    "crouch",  "attack",  "hit",
                                        "block", "draw", "death", "spell",   "roll",    "other"};
    for (size_t i = 0; i < sizeof names / sizeof *names; ++i)
        if (name == names[i]) {
            out = static_cast<Act>(i);
            return true;
        }
    return false;
}

Mapped mapClip(uint16_t id, uint32_t rynnClip, float time) {
    Mapped m;
    const RynnClip* r = findRynn(rynnClip);
    const CharacterRow* row = findRow(id);
    if (!row)
        return m;
    m.act = r ? r->act : Act::Idle;
    if (r && known(id) && id && g_retargetedMotion.load()) {
        // Generator allocates 256 ids per distinct model in ascending order, with source clips in table order.
        // Exact source clips retain left/right evasion, weapon grip and jump phases, locally and over the network.
        static const std::vector<uint32_t> models = [] {
            std::vector<uint32_t> result;
            for (const Entry& e : roster())
                if (e.id)
                    result.push_back(e.modelId);
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            return result;
        }();
        const auto model = std::find(models.begin(), models.end(), row->model);
        if (model != models.end()) {
            m.clip = 0x58000u + static_cast<uint32_t>(model - models.begin()) * 256u +
                     static_cast<uint32_t>(r - std::begin(kRynnClips));
            m.time = std::isfinite(time) ? std::max(time, 0.0f) : 0.0f;
            return m;
        }
    }
    const Cell c = pick(*row, m.act);
    if (!c.id)
        return m;
    m.clip = c.id;
    float t = std::isfinite(time) ? std::max(time, 0.0f) : 0.0f;
    if (r && r->durMs > 0 && c.durMs > 0)
        t = t * static_cast<float>(c.durMs) / static_cast<float>(r->durMs);
    const float dur = static_cast<float>(c.durMs) / 1000.0f;
    if (dur > 0.0f)
        t = loops(m.act) ? std::fmod(std::max(t, 0.0f), dur) : std::min(std::max(t, 0.0f), dur * 0.999f);
    // The shipped selectable NPCs have no airborne / evasion clips. Hold their own ready/cower pose instead of
    // cycling a run while the controller jumps. Never send Rynn's incompatible joint tracks to an NPC skeleton.
    const bool heldFallback =
        (m.act == Act::Jump || m.act == Act::InAir || m.act == Act::Roll) && !row->clips[static_cast<size_t>(m.act)].id;
    m.time = heldFallback ? dur * 0.5f : t;
    return m;
}

} // namespace drakan_coop_character
