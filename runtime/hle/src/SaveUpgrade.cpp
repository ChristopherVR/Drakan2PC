#include "hle/SaveUpgrade.h"
#include "hle/ModPack.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace hle::mods {
namespace {
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (true) {
        const size_t end = s.find(sep, pos);
        out.push_back(trim(s.substr(pos, end == std::string::npos ? std::string::npos : end - pos)));
        if (end == std::string::npos)
            return out;
        pos = end + 1;
    }
}
std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t a = s.find_first_not_of(" \t", pos);
        if (a == std::string::npos)
            break;
        const size_t b = s.find_first_of(" \t", a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        pos = b == std::string::npos ? s.size() : b;
    }
    return out;
}
bool toU32(const std::string& s, int base, u32& out) {
    if (s.empty() || s[0] == '-' || s[0] == '+')
        return false;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s.c_str(), &end, base);
    if (!end || *end || v > 0xFFFFFFFFull)
        return false;
    out = static_cast<u32>(v);
    return true;
}
bool toFloat(const std::string& s, float& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    out = std::strtof(s.c_str(), &end);
    return end && !*end;
}
u32 rd32(const u8* p) {
    return static_cast<u32>(p[0]) | static_cast<u32>(p[1]) << 8 | static_cast<u32>(p[2]) << 16 |
           static_cast<u32>(p[3]) << 24;
}
u16 rd16(const u8* p) {
    return static_cast<u16>(p[0] | p[1] << 8);
}
void wr32(std::vector<u8>& v, size_t at, u32 x) {
    for (size_t i = 0; i < 4; ++i)
        v[at + i] = static_cast<u8>(x >> (8 * i));
}
bool readDatabases(const std::vector<u8>& data, std::vector<std::string>& databases) {
    if (data.size() < 4)
        return false;
    const u32 count = rd32(data.data());
    size_t pos = 4;
    for (u32 i = 0; i < count; ++i) {
        if (data.size() - pos < 2)
            return false;
        const u16 size = rd16(&data[pos]);
        pos += 2;
        if (size == 0 || size > data.size() - pos)
            return false;
        std::string name(reinterpret_cast<const char*>(&data[pos]), size);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        std::replace(name.begin(), name.end(), '\\', '/');
        databases.push_back(lower(std::move(name)));
        pos += size;
    }
    return pos == data.size();
}
} // namespace

bool parseSaveRevisionRule(const std::string& line, SaveRevisionRule::Kind kind, SaveRevisionRule& out) {
    const std::vector<std::string> f = split(line, '|');
    SaveRevisionRule r;
    r.kind = kind;
    u32 rev = 0;
    if (f.size() < 3 || !toU32(f[0], 10, rev) || rev == 0 || rev > 1000000 || f[1].empty())
        return false;
    r.revision = static_cast<int>(rev);
    r.level = f[1];
    if (kind != SaveRevisionRule::Kind::ReplaceEffects) {
        if (f.size() != 3)
            return false;
        for (const std::string& w : words(f[2])) {
            const size_t at = w.find('@');
            u32 id = 0, need = 0;
            if (!toU32(w.substr(0, at), 10, id) || id == 0 ||
                (at != std::string::npos &&
                 (kind != SaveRevisionRule::Kind::AddObjects || !toU32(w.substr(at + 1), 10, need) || need == 0)))
                return false;
            r.ids.push_back(id);
            r.needs.push_back(need);
        }
        if (r.ids.size() < (kind == SaveRevisionRule::Kind::RelinkPowerUps ? 2u : 1u))
            return false;
    } else {
        const std::vector<std::string> xz = f.size() == 4 ? words(f[3]) : std::vector<std::string>();
        const std::vector<std::string> cls = words(f[2]);
        u32 from = 0, to = 0;
        if (f.size() != 4 || cls.size() != 2 || !toU32(cls[0], 16, from) || !toU32(cls[1], 16, to) || !from || !to ||
            from > 0xFFFF || to > 0xFFFF || from == to || xz.size() != 2 || !toFloat(xz[0], r.x) ||
            !toFloat(xz[1], r.z))
            return false;
        r.fromClass = static_cast<u16>(from);
        r.toClass = static_cast<u16>(to);
    }
    out = std::move(r);
    return true;
}

std::string formatSaveRevisionRule(const SaveRevisionRule& r) {
    std::string s = std::to_string(r.revision) + " | " + r.level + " | ";
    if (r.kind != SaveRevisionRule::Kind::ReplaceEffects) {
        for (size_t i = 0; i < r.ids.size(); ++i) {
            s += (i ? " " : "") + std::to_string(r.ids[i]);
            if (i < r.needs.size() && r.needs[i])
                s += "@" + std::to_string(r.needs[i]);
        }
        return s;
    }
    char b[64];
    std::snprintf(b, sizeof b, "%x %x | %.0f %.0f", r.fromClass, r.toClass, static_cast<double>(r.x),
                  static_cast<double>(r.z));
    return s + b;
}

bool ruleLevelMatches(const std::string& ruleLevel, const std::string& saveStem) {
    const std::string want = lower(trim(saveStem)), have = lower(trim(ruleLevel));
    if (want.empty())
        return false;
    if (want == have)
        return true;
    return saveStem.size() >= 24 && have.compare(0, want.size(), want) == 0;
}

std::vector<const SaveRevisionRule*> rulesFor(const std::vector<SaveRevisionRule>& rules, SaveRevisionRule::Kind kind,
                                              const std::string& saveStem, int saveRevision) {
    std::vector<const SaveRevisionRule*> out;
    for (const SaveRevisionRule& r : rules)
        if (r.kind == kind && r.revision > saveRevision && ruleLevelMatches(r.level, saveStem))
            out.push_back(&r);
    return out;
}

bool upgradeSave(const std::vector<u8>& in, const std::vector<const SaveRevisionRule*>& rules, std::vector<u8>& out,
                 SaveUpgradeReport& report, std::string& err, std::span<const SaveClassRemap> remaps,
                 const SaveVisibilityRepair* visibility) {
    out.clear();
    report = {};
    std::vector<std::pair<u32, u32>> ids; // (id, required id or 0)
    std::vector<const SaveRevisionRule*> replace, relink;
    std::vector<u32> togglers;
    for (const SaveRevisionRule* r : rules) {
        if (r->kind == SaveRevisionRule::Kind::AddObjects)
            for (size_t i = 0; i < r->ids.size(); ++i)
                ids.emplace_back(r->ids[i], i < r->needs.size() ? r->needs[i] : 0u);
        else if (r->kind == SaveRevisionRule::Kind::ResetTogglers)
            togglers.insert(togglers.end(), r->ids.begin(), r->ids.end());
        else
            (r->kind == SaveRevisionRule::Kind::ReplaceEffects ? replace : relink).push_back(r);
    }
    if (in.size() < 16 || std::memcmp(in.data(), "SRSC", 4) != 0) {
        err = "not an SRSC container";
        return false;
    }
    const u32 idx = rd32(&in[6]), cnt = rd32(&in[10]);
    if (idx < 16 || idx > in.size() || static_cast<u64>(cnt) * 14 > in.size() - idx) {
        err = "index out of range";
        return false;
    }
    struct Ent {
        u16 type, id, sub;
        u32 off, size;
    };
    std::vector<Ent> ents;
    size_t records = 0;
    int statics = -1, pairs = -1, dbs = -1;
    for (u32 i = 0; i < cnt; ++i) {
        const u8* q = &in[static_cast<size_t>(idx) + static_cast<size_t>(i) * 14];
        const Ent e{rd16(q), rd16(q + 2), rd16(q + 4), rd32(q + 6), rd32(q + 10)};
        if (static_cast<u64>(e.off) + e.size > idx) {
            err = "entry outside the data region";
            return false;
        }
        if (e.type == 0x102)
            ++records;
        if (e.type == 0x22)
            dbs = static_cast<int>(ents.size());
        if (e.type == 0x24 || e.type == 0x21) {
            int& at = e.type == 0x24 ? statics : pairs;
            if (at >= 0) {
                err = "more than one object table";
                return false;
            }
            at = static_cast<int>(ents.size());
        }
        ents.push_back(e);
    }
    if (statics < 0 || pairs < 0) {
        err = "no object tables (0x24 / 0x21)";
        return false;
    }
    const Ent se = ents[static_cast<size_t>(statics)];
    const Ent pe = ents[static_cast<size_t>(pairs)];
    std::vector<u8> st, pd;
    if (!unzlib(&in[se.off], se.size, st) || !unzlib(&in[pe.off], pe.size, pd)) {
        err = "object tables do not inflate";
        return false;
    }
    if (st.size() < 4 || st.size() != 4 + 4 * static_cast<size_t>(rd32(st.data()))) {
        err = "unexpected 0x24 layout";
        return false;
    }
    const u32 nStatic = rd32(st.data());
    if (pd.size() < 16) {
        err = "unexpected 0x21 layout";
        return false;
    }
    const u32 total = rd32(pd.data()), nPairs = rd32(&pd[12]);
    if (16 + 8 * static_cast<u64>(nPairs) > pd.size() || total != records + nStatic) {
        err = "0x21 does not match the records and the 0x24 table";
        return false;
    }
    std::vector<std::pair<u32, u32>> list;
    list.reserve(nPairs + ids.size());
    std::vector<bool> slotNamed(total, false);
    for (u32 i = 0; i < nPairs; ++i) {
        list.emplace_back(rd32(&pd[16 + 8 * static_cast<size_t>(i)]), rd32(&pd[20 + 8 * static_cast<size_t>(i)]));
        if (list.back().second < total)
            slotNamed[list.back().second] = true;
    }
    const auto named = [&](u32 id) {
        return std::any_of(list.begin(), list.end(), [&](const auto& p) { return p.first == id; });
    };
    for (const auto& [id, need] : ids)
        if (!named(id) && (!need || named(need)) &&
            std::find(report.added.begin(), report.added.end(), id) == report.added.end())
            report.added.push_back(id);
    std::vector<SrscPatch> patches;
    if (!remaps.empty()) {
        std::vector<std::string> databases;
        std::vector<u8> d;
        if (dbs < 0 || !unzlib(&in[ents[dbs].off], ents[dbs].size, d) || !readDatabases(d, databases)) {
            err = "could not read the save's database table (0x22)";
            return false;
        }
        for (const Ent& e : ents) {
            if (e.type != 0x102)
                continue;
            if (!unzlib(&in[e.off], e.size, d) || d.size() < 4) {
                err = "could not read a saved class record (0x102)";
                return false;
            }
            const u32 word = rd32(d.data()), db = word >> 16;
            if (db >= databases.size()) {
                err = "saved class names an unknown database";
                return false;
            }
            for (const SaveClassRemap& remap : remaps) {
                if ((word & 0xFFFF) != remap.from || !databases[db].ends_with(remap.database))
                    continue;
                wr32(d, 0, (word & 0xFFFF0000u) | remap.to);
                patches.push_back({e.type, e.id, e.sub, zlibStored(d), false});
                ++report.reclassified;
                break;
            }
        }
    }
    // Spawned records (no pair names their slot = record id - 1) of a replaced class near the rule's spot.
    if (!replace.empty())
        for (const Ent& e : ents) {
            if (e.type != 0x102 || e.id == 0 || e.id > total || slotNamed[e.id - 1u])
                continue;
            std::vector<u8> d;
            if (!unzlib(&in[e.off], e.size, d) || d.size() < 24)
                continue;
            const u32 word = rd32(d.data());
            float x, z;
            std::memcpy(&x, &d[12], 4);
            std::memcpy(&z, &d[20], 4);
            for (const SaveRevisionRule* r : replace)
                if ((word & 0xFFFF) == r->fromClass && std::fabs(x - r->x) <= kEffectRadius &&
                    std::fabs(z - r->z) <= kEffectRadius) {
                    wr32(d, 0, (word & 0xFFFF0000u) | r->toClass);
                    patches.push_back({e.type, e.id, e.sub, zlibStored(d), false});
                    ++report.replaced;
                    break;
                }
        }
    // Dragons: the slots of the power-ups (named by a pair, or appended above) back into the emptied link list.
    std::map<u32, u32> slotOf;
    for (const auto& [id, slot] : list)
        slotOf.emplace(id, slot);
    for (size_t i = 0; i < report.added.size(); ++i)
        slotOf.emplace(report.added[i], total + static_cast<u32>(i));
    // Dynamic scenery can retain a hidden bit after its startup visibility was corrected in the level.
    if (visibility && dbs >= 0) {
        const auto slot = slotOf.find(visibility->object);
        if (slot != slotOf.end() && slot->second < records) {
            const auto e = std::find_if(ents.begin(), ents.end(),
                                        [&](const Ent& x) { return x.type == 0x102 && x.id == slot->second + 1; });
            const Ent& db = ents[static_cast<size_t>(dbs)];
            std::vector<std::string> databases;
            std::vector<u8> d, names;
            if (e != ents.end() && unzlib(&in[e->off], e->size, d) && d.size() >= 12 &&
                unzlib(&in[db.off], db.size, names) && readDatabases(names, databases)) {
                const u32 resource = rd32(d.data()), database = resource >> 16;
                const u32 flags = rd32(&d[8]);
                if ((resource & 0xffffu) == visibility->classId && database < databases.size() &&
                    databases[database].ends_with(visibility->database) && !(flags & 1)) {
                    wr32(d, 8, flags | 1);
                    patches.push_back({e->type, e->id, e->sub, zlibStored(d), false});
                    report.visibilityRestored = true;
                }
            }
        }
    }
    for (const SaveRevisionRule* r : relink) {
        const auto dragon = slotOf.find(r->ids[0]);
        if (dragon == slotOf.end() || dragon->second >= records)
            continue; // not a saved record: the level file's dragon still has its links
        const auto e = std::find_if(ents.begin(), ents.end(),
                                    [&](const Ent& x) { return x.type == 0x102 && x.id == dragon->second + 1; });
        std::vector<u8> d;
        if (e == ents.end() || !unzlib(&in[e->off], e->size, d) || d.size() < kDragonPowerUpMark + 8 ||
            rd32(&d[kDragonPowerUpMark]) != 1 || rd32(&d[kDragonPowerUpMark + 4]) != 0)
            continue;
        std::vector<u32> slots;
        for (size_t k = 1; k < r->ids.size(); ++k)
            if (const auto s = slotOf.find(r->ids[k]); s != slotOf.end())
                slots.push_back(s->second);
        if (slots.empty())
            continue;
        std::vector<u8> mid(8 + 4 * slots.size());
        wr32(mid, 0, 0);
        wr32(mid, 4, static_cast<u32>(slots.size()));
        for (size_t k = 0; k < slots.size(); ++k)
            wr32(mid, 8 + 4 * k, slots[k]);
        d.erase(d.begin() + kDragonPowerUpMark, d.begin() + kDragonPowerUpMark + 8);
        d.insert(d.begin() + kDragonPowerUpMark, mid.begin(), mid.end());
        patches.push_back({e->type, e->id, e->sub, zlibStored(d), false});
        ++report.relinked;
    }
    // Togglers: their saved state word back to 0.
    for (u32 id : togglers) {
        const auto t = slotOf.find(id);
        if (t == slotOf.end() || t->second >= records)
            continue;
        const auto e = std::find_if(ents.begin(), ents.end(),
                                    [&](const Ent& x) { return x.type == 0x102 && x.id == t->second + 1; });
        std::vector<u8> d;
        if (e == ents.end() || !unzlib(&in[e->off], e->size, d))
            continue;
        size_t at = 0, hits = 0;
        for (size_t o = 0x48; o + 4 <= d.size(); o += 4) {
            const u32 w = rd32(&d[o]);
            if ((w & 0xFFFF) == kTogglerEngine && (w >> 16) >= 1 && (w >> 16) <= 0xFF) {
                at = o;
                ++hits;
            }
        }
        if (hits != 1)
            continue;
        wr32(d, at, kTogglerEngine);
        patches.push_back({e->type, e->id, e->sub, zlibStored(d), false});
        ++report.togglers;
    }
    if (!report.changed()) {
        out = in;
        return true;
    }
    if (!report.added.empty()) {
        std::vector<u8> newSt(st);
        wr32(newSt, 0, nStatic + static_cast<u32>(report.added.size()));
        for (size_t i = 0; i < report.added.size(); ++i) {
            newSt.resize(newSt.size() + 4);
            wr32(newSt, newSt.size() - 4, report.added[i]);
            list.emplace_back(report.added[i], total + static_cast<u32>(i));
        }
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<u8> newPd(16 + 8 * list.size());
        std::memcpy(newPd.data(), pd.data(), 16);
        wr32(newPd, 0, total + static_cast<u32>(report.added.size()));
        wr32(newPd, 12, static_cast<u32>(list.size()));
        for (size_t i = 0; i < list.size(); ++i) {
            wr32(newPd, 16 + 8 * i, list[i].first);
            wr32(newPd, 20 + 8 * i, list[i].second);
        }
        newPd.insert(newPd.end(), pd.begin() + 16 + 8 * static_cast<std::ptrdiff_t>(nPairs), pd.end());
        patches.push_back({se.type, se.id, se.sub, zlibStored(newSt), false});
        patches.push_back({pe.type, pe.id, pe.sub, zlibStored(newPd), false});
    }
    const std::vector<u8> trimmed(in.begin(), in.begin() + idx + static_cast<std::ptrdiff_t>(cnt) * 14);
    if (!patchSrsc(trimmed, patches, out, err)) {
        out.clear();
        report = {};
        return false;
    }
    return true;
}

} // namespace hle::mods
