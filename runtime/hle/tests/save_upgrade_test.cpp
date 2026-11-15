// save_upgrade_test: save revisions of a content pack (SaveUpgrade.h): manifest rules, the record's revision key and
// the object-table append on synthetic SRSC saves. No game data.
#include "hle/ModPack.h"
#include "hle/SaveRecord.h"
#include "hle/SaveUpgrade.h"
#include <cstdio>
#include <cstring>
#include <deque>

using namespace hle;
using namespace hle::mods;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                      \
        }                                                                                                              \
    } while (0)

static void p16(std::vector<u8>& v, u32 x) {
    v.push_back(static_cast<u8>(x));
    v.push_back(static_cast<u8>(x >> 8));
}
static void p32(std::vector<u8>& v, u32 x) {
    p16(v, x & 0xFFFF);
    p16(v, x >> 16);
}
static u32 r32(const std::vector<u8>& v, size_t at) {
    return v[at] | v[at + 1] << 8 | v[at + 2] << 16 | static_cast<u32>(v[at + 3]) << 24;
}

struct Entry {
    u16 type, id, sub;
    std::vector<u8> data; // inflated
};
// A save container: header, zlib payloads in order, index at the end, then `trailing` stale bytes.
static std::vector<u8> container(const std::vector<Entry>& es, size_t trailing = 0) {
    std::vector<u8> out(16, 0);
    std::vector<std::pair<u32, u32>> where;
    for (const Entry& e : es) {
        const std::vector<u8> z = zlibStored(e.data);
        where.emplace_back(static_cast<u32>(out.size()), static_cast<u32>(z.size()));
        out.insert(out.end(), z.begin(), z.end());
    }
    const u32 idx = static_cast<u32>(out.size());
    for (size_t i = 0; i < es.size(); ++i) {
        p16(out, es[i].type);
        p16(out, es[i].id);
        p16(out, es[i].sub);
        p32(out, where[i].first);
        p32(out, where[i].second);
    }
    std::memcpy(out.data(), "SRSC", 4);
    out[4] = 1;
    out[5] = 1;
    for (int i = 0; i < 4; ++i) {
        out[6 + i] = static_cast<u8>(idx >> (8 * i));
        out[10 + i] = static_cast<u8>(es.size() >> (8 * i));
    }
    out.insert(out.end(), trailing, 0x5a);
    return out;
}
// Reads every entry back (inflated), by index order.
static bool entries(const std::vector<u8>& f, std::vector<Entry>& out) {
    out.clear();
    if (f.size() < 16 || std::memcmp(f.data(), "SRSC", 4) != 0)
        return false;
    const u32 idx = r32(f, 6), cnt = r32(f, 10);
    if (idx + cnt * 14 != f.size())
        return false;
    for (u32 i = 0; i < cnt; ++i) {
        const size_t q = idx + i * 14;
        Entry e{static_cast<u16>(f[q] | f[q + 1] << 8),
                static_cast<u16>(f[q + 2] | f[q + 3] << 8),
                static_cast<u16>(f[q + 4] | f[q + 5] << 8),
                {}};
        const u32 off = r32(f, q + 6), size = r32(f, q + 10);
        if (!unzlib(&f[off], size, e.data))
            return false;
        out.push_back(std::move(e));
    }
    return true;
}
// AddObjects rules for `ids` (kept alive for the test), followed by `more`.
static std::vector<const SaveRevisionRule*> rules(std::vector<u32> ids,
                                                  std::vector<const SaveRevisionRule*> more = {}) {
    static std::deque<SaveRevisionRule> keep;
    std::vector<const SaveRevisionRule*> out;
    if (!ids.empty()) {
        keep.push_back({});
        keep.back().kind = SaveRevisionRule::Kind::AddObjects;
        keep.back().revision = 1;
        keep.back().level = "L";
        keep.back().ids = std::move(ids);
        out.push_back(&keep.back());
    }
    out.insert(out.end(), more.begin(), more.end());
    return out;
}
static const Entry* find(const std::vector<Entry>& es, u16 type) {
    for (const Entry& e : es)
        if (e.type == type)
            return &e;
    return nullptr;
}

// A level save: `records` saved records (two of them level objects 10 and 30, one spawned without an id), untouched
// objects 20, 40, 50 in the 0x24 table, the 0x21 pairs and four trailing counted slot arrays (kept verbatim).
static std::vector<Entry> levelSave() {
    std::vector<Entry> es;
    // Records 1 (level object 10) and 3 (spawned, no id) are of class 0x3a2b in database 1 at x 100, z 200; record 2
    // (level object 30) is another class at the same spot.
    for (u16 i = 1; i <= 3; ++i) {
        std::vector<u8> d;
        p32(d, i == 2 ? 0x10500u : 0x13a2bu);
        p32(d, 0x16090000u);
        p32(d, 0x08101045u);
        for (float f : {100.0f, 5.0f, 200.0f}) {
            u32 w;
            std::memcpy(&w, &f, 4);
            p32(d, w);
        }
        d.resize(0x80, static_cast<u8>(i));
        if (i == 1) { // level object 10 is a dragon that took his power-ups: mark 1, empty list
            std::memset(&d[kDragonPowerUpMark], 0, 8);
            d[kDragonPowerUpMark] = 1;
        }
        if (i == 2) { // level object 30 is a toggler whose state word says hidden (2)
            std::memset(&d[0x48], 0, d.size() - 0x48);
            const u32 w = (2u << 16) | kTogglerEngine;
            std::memcpy(&d[0x70], &w, 4);
        }
        es.push_back({0x102, i, 0, d});
    }
    std::vector<u8> st;
    p32(st, 3);
    for (u32 id : {20u, 40u, 50u})
        p32(st, id);
    es.push_back({0x24, 0, 0, st});
    std::vector<u8> pd;
    p32(pd, 6); // total slots = 3 records + 3 untouched
    p32(pd, 2);
    p32(pd, 7);
    p32(pd, 5); // pairs: 10 -> 0, 20 -> 3, 30 -> 1, 40 -> 4, 50 -> 5 (record 2 has no id)
    for (auto [id, slot] : {std::pair{10u, 0u}, {20u, 3u}, {30u, 1u}, {40u, 4u}, {50u, 5u}}) {
        p32(pd, id);
        p32(pd, slot);
    }
    for (u32 w : {2u, 0u, 4u, 0u, 0u, 0u, 1u, 3u, 0xabcdu})
        p32(pd, w);
    es.push_back({0x21, 0, 0, pd});
    es.push_back({0x401, 893, 0, std::vector<u8>(12, 7)});
    return es;
}

int main() {
    setVerbose(false);

    // ---- manifest rules
    {
        SaveRevisionRule r;
        CHECK(parseSaveRevisionRule("1 | OotF 01 - Ruined Village | 3353", SaveRevisionRule::Kind::AddObjects, r));
        CHECK(r.revision == 1 && r.level == "OotF 01 - Ruined Village" && r.ids == std::vector<u32>{3353});
        CHECK(parseSaveRevisionRule(" 2|Level X| 5 6  7 ", SaveRevisionRule::Kind::AddObjects, r));
        CHECK(r.revision == 2 && r.level == "Level X" && r.ids == (std::vector<u32>{5, 6, 7}));
        CHECK(formatSaveRevisionRule(r) == "2 | Level X | 5 6 7");
        CHECK(parseSaveRevisionRule("1 | OotF 01 - Ruined Village | 3a2b 4b07 | 321687 197611",
                                    SaveRevisionRule::Kind::ReplaceEffects, r));
        CHECK(r.fromClass == 0x3a2b && r.toClass == 0x4b07 && r.x == 321687.0f && r.z == 197611.0f);
        CHECK(formatSaveRevisionRule(r) == "1 | OotF 01 - Ruined Village | 3a2b 4b07 | 321687 197611");
        SaveRevisionRule keep;
        keep.level = "unchanged";
        for (const char* bad :
             {"", "1 | L", "0 | L | 5", "x | L | 5", "1 |  | 5", "1 | L | 5 x", "1 | L | -5", "1 | L | 5 | 6"})
            CHECK(!parseSaveRevisionRule(bad, SaveRevisionRule::Kind::AddObjects, keep) && keep.level == "unchanged");
        for (const char* bad : {"1 | L | 3a2b 4b07", "1 | L | zz 1 | 1 2", "1 | L | 0 5 | 1 2", "1 | L | 3a2b | 1 2",
                                "1 | L | 3a2b 3a2b | 1 2", "1 | L | 13a2b 5 | 1 2", "1 | L | 3a2b 4b07 | 1"})
            CHECK(!parseSaveRevisionRule(bad, SaveRevisionRule::Kind::ReplaceEffects, keep) &&
                  keep.level == "unchanged");

        CHECK(ruleLevelMatches("OotF 01 - Ruined Village", "ootf 01 - ruined village"));
        CHECK(!ruleLevelMatches("OotF 01 - Ruined Village", "OotF 02 - Ruined Village "));
        CHECK(ruleLevelMatches("OotF 02 - Ruined Village Book", "OotF 02 - Ruined Village ")); // a cut save name
        CHECK(!ruleLevelMatches("Level 1 - Long Name", "Level 1"));                            // short: exact only
        CHECK(!ruleLevelMatches("Level 1", ""));

        std::vector<SaveRevisionRule> rules(3);
        parseSaveRevisionRule("1 | A | 1", SaveRevisionRule::Kind::AddObjects, rules[0]);
        parseSaveRevisionRule("2 | A | 2", SaveRevisionRule::Kind::AddObjects, rules[1]);
        parseSaveRevisionRule("2 | A | 3 4 | 0 0", SaveRevisionRule::Kind::ReplaceEffects, rules[2]);
        CHECK(rulesFor(rules, SaveRevisionRule::Kind::AddObjects, "A", 0).size() == 2);
        CHECK(rulesFor(rules, SaveRevisionRule::Kind::AddObjects, "A", 1).size() == 1);
        CHECK(rulesFor(rules, SaveRevisionRule::Kind::AddObjects, "A", 2).empty());
        CHECK(rulesFor(rules, SaveRevisionRule::Kind::AddObjects, "B", 0).empty());
        CHECK(rulesFor(rules, SaveRevisionRule::Kind::ReplaceEffects, "A", 1).size() == 1);
    }

    // ---- manifest keys and the record's revision
    {
        Manifest m;
        std::string err;
        CHECK(
            parseManifest("id = \"p\"\nbuild = \"b1\"\nsave_revision = 2\nsave_added_objects = [\"1 | L | 5\", \"2 | L "
                          "| 6\"]\nsave_replaced_effects = [\"1 | L | 3a2b 4b07 | 1 2\"]\n",
                          false, m, err));
        CHECK(m.saveRevision == 2 && m.saveAddedObjects.size() == 2 && m.saveReplacedEffects.size() == 1);
        CHECK(parseManifest("id = \"p\"\n", false, m, err) && m.saveRevision == 0 && m.saveAddedObjects.empty());

        PackStamp a{"p", "P", "1", "b1"};
        a.revision = 3;
        std::vector<PackStamp> back;
        const std::string text = formatSaveRecord({a, {"q", "Q", "1", "b2"}});
        CHECK(text.find("revision = 3") != std::string::npos);
        CHECK(text.find("revision = 0") == std::string::npos); // 0 is not written
        CHECK(parseSaveRecord(text, back) && back.size() == 2 && back[0].revision == 3 && back[1].revision == 0);
        // the build check ignores the revision: an older revision of the same build loads (and is upgraded)
        ActivePacks act;
        act.packs = {{"p", "P", "1", "b1"}};
        act.packs[0].revision = 5;
        SaveCheckInput in;
        in.hasRecord = true;
        in.record = {back[0]};
        in.level = "L";
        CHECK(checkSave(in, act).ok());
        CHECK(recordFor(act, "L").at(0).revision == 5);
    }

    // ---- the object-table append
    {
        const std::vector<Entry> src = levelSave();
        const std::vector<u8> file = container(src);
        std::vector<u8> out;
        SaveUpgradeReport rep;
        std::string err;
        CHECK(upgradeSave(file, rules({60, 20, 70, 60}), out, rep, err));
        CHECK(rep.added == (std::vector<u32>{60, 70}) && rep.replaced == 0); // 20 is named already, 60 once
        std::vector<Entry> es;
        CHECK(entries(out, es) && es.size() == src.size());
        const Entry* st = find(es, 0x24);
        const Entry* pd = find(es, 0x21);
        CHECK(st && st->data.size() == 4 + 4 * 5 && r32(st->data, 0) == 5);
        CHECK(st && r32(st->data, 4) == 20 && r32(st->data, 12) == 50 && r32(st->data, 16) == 60 &&
              r32(st->data, 20) == 70);
        CHECK(pd && r32(pd->data, 0) == 8 && r32(pd->data, 4) == 2 && r32(pd->data, 8) == 7 && r32(pd->data, 12) == 7);
        if (pd && pd->data.size() >= 16 + 8 * 7) {
            const u32 want[7][2] = {{10, 0}, {20, 3}, {30, 1}, {40, 4}, {50, 5}, {60, 6}, {70, 7}};
            bool same = true;
            for (int i = 0; i < 7; ++i)
                same = same && r32(pd->data, 16 + 8 * i) == want[i][0] && r32(pd->data, 20 + 8 * i) == want[i][1];
            CHECK(same);
            // the counted slot arrays after the pairs are kept as they were
            const std::vector<u8>& old = find(src, 0x21)->data;
            CHECK(std::vector<u8>(pd->data.begin() + 16 + 8 * 7, pd->data.end()) ==
                  std::vector<u8>(old.begin() + 16 + 8 * 5, old.end()));
        }
        for (size_t i = 0; i < src.size(); ++i) // every other entry unchanged, same index order
            if (src[i].type != 0x24 && src[i].type != 0x21)
                CHECK(es[i].type == src[i].type && es[i].id == src[i].id && es[i].data == src[i].data);

        // nothing to add: the same bytes back
        CHECK(upgradeSave(file, rules({10, 50}), out, rep, err) && !rep.changed() && out == file);
        // stale bytes after the index (an overwritten longer save) are dropped
        CHECK(upgradeSave(container(src, 37), rules({60}), out, rep, err) && rep.added.size() == 1 && entries(out, es));
        // a save whose tables do not agree is left alone
        std::vector<Entry> broken = src;
        broken.erase(broken.begin()); // one record fewer than the 0x21 total counts
        CHECK(!upgradeSave(container(broken), rules({60}), out, rep, err) && out.empty() && !rep.changed());
        std::vector<Entry> noTables = {src[0], src[5]};
        CHECK(!upgradeSave(container(noTables), rules({60}), out, rep, err));
        CHECK(!upgradeSave(std::vector<u8>(40, 0), rules({60}), out, rep, err));
        std::vector<u8> cut = file;
        cut.resize(cut.size() - 3); // index runs past the end
        CHECK(!upgradeSave(cut, rules({60}), out, rep, err));
    }

    // ---- spawned records get the inert class
    {
        const std::vector<Entry> src = levelSave();
        const std::vector<u8> file = container(src);
        SaveRevisionRule near, far, other;
        CHECK(parseSaveRevisionRule("1 | L | 3a2b 4b07 | 130 170", SaveRevisionRule::Kind::ReplaceEffects, near));
        CHECK(parseSaveRevisionRule("1 | L | 3a2b 4b07 | 300 200", SaveRevisionRule::Kind::ReplaceEffects, far));
        CHECK(parseSaveRevisionRule("1 | L | 1234 4b07 | 100 200", SaveRevisionRule::Kind::ReplaceEffects, other));
        std::vector<u8> out;
        SaveUpgradeReport rep;
        std::string err;
        CHECK(upgradeSave(file, rules({}, {&far, &other}), out, rep, err) && !rep.changed() && out == file);
        CHECK(upgradeSave(file, rules({60}, {&near}), out, rep, err) && rep.replaced == 1 && rep.added.size() == 1);
        std::vector<Entry> es;
        CHECK(entries(out, es) && es.size() == src.size());
        // only the record without a level id changed, and only its class (database kept)
        CHECK(r32(es[2].data, 0) == 0x14b07);
        CHECK(std::vector<u8>(es[2].data.begin() + 4, es[2].data.end()) ==
              std::vector<u8>(src[2].data.begin() + 4, src[2].data.end()));
        CHECK(es[0].data == src[0].data && es[1].data == src[1].data); // level object 10 keeps its class
        CHECK(find(es, 0x24) && r32(find(es, 0x24)->data, 0) == 4);
    }

    // ---- a dragon gets his power-ups linked again
    {
        const std::vector<Entry> src = levelSave();
        const std::vector<u8> file = container(src);
        SaveRevisionRule link, other, absent;
        CHECK(parseSaveRevisionRule("1 | L | 10 40 60", SaveRevisionRule::Kind::RelinkPowerUps, link));
        CHECK(link.ids == (std::vector<u32>{10, 40, 60}));
        CHECK(!parseSaveRevisionRule("1 | L | 10", SaveRevisionRule::Kind::RelinkPowerUps, other));
        CHECK(parseSaveRevisionRule("1 | L | 30 40", SaveRevisionRule::Kind::RelinkPowerUps, other));  // no mark
        CHECK(parseSaveRevisionRule("1 | L | 20 40", SaveRevisionRule::Kind::RelinkPowerUps, absent)); // untouched
        std::vector<u8> out;
        SaveUpgradeReport rep;
        std::string err;
        CHECK(upgradeSave(file, rules({}, {&other, &absent}), out, rep, err) && !rep.changed() && out == file);
        // power-up 40 is untouched (slot 4), 60 is appended by the same upgrade (slot 6)
        CHECK(upgradeSave(file, rules({60}, {&link}), out, rep, err) && rep.relinked == 1 && rep.added.size() == 1);
        std::vector<Entry> es;
        CHECK(entries(out, es));
        const std::vector<u8>& d = es[0].data;
        CHECK(d.size() == src[0].data.size() + 8);
        CHECK(r32(d, kDragonPowerUpMark) == 0 && r32(d, kDragonPowerUpMark + 4) == 2 &&
              r32(d, kDragonPowerUpMark + 8) == 4 && r32(d, kDragonPowerUpMark + 12) == 6);
        CHECK(std::vector<u8>(d.begin(), d.begin() + kDragonPowerUpMark) ==
              std::vector<u8>(src[0].data.begin(), src[0].data.begin() + kDragonPowerUpMark));
        CHECK(std::vector<u8>(d.begin() + kDragonPowerUpMark + 16, d.end()) ==
              std::vector<u8>(src[0].data.begin() + kDragonPowerUpMark + 8, src[0].data.end()));
        // once relinked the mark is 0: a second upgrade leaves it
        std::vector<u8> again;
        CHECK(upgradeSave(out, rules({}, {&link}), again, rep, err) && rep.relinked == 0 && again == out);
    }

    // ---- a toggler's saved state is cleared
    {
        const std::vector<Entry> src = levelSave();
        const std::vector<u8> file = container(src);
        SaveRevisionRule tog, wrong;
        CHECK(parseSaveRevisionRule("1 | L | 30", SaveRevisionRule::Kind::ResetTogglers, tog));
        CHECK(parseSaveRevisionRule("1 | L | 10 20", SaveRevisionRule::Kind::ResetTogglers, wrong)); // no state word
        std::vector<u8> out;
        SaveUpgradeReport rep;
        std::string err;
        CHECK(upgradeSave(file, rules({}, {&wrong}), out, rep, err) && !rep.changed() && out == file);
        CHECK(upgradeSave(file, rules({}, {&tog}), out, rep, err) && rep.togglers == 1);
        std::vector<Entry> es;
        CHECK(entries(out, es) && r32(es[1].data, 0x70) == kTogglerEngine && es[1].data.size() == src[1].data.size());
        std::vector<u8> again;
        CHECK(upgradeSave(out, rules({}, {&tog}), again, rep, err) && rep.togglers == 0 && again == out);
    }

    printf("save_upgrade_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
