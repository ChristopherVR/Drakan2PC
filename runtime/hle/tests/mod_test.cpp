// mod_test: synthetic mod packs (folder and zip): manifest parsing, merge order, conflicts, profile mismatch,
// dependencies, oversize rejection, text.ini, mods.ini handling, zip stored/deflate, zip-slip. No game data needed.
#include "hle/IsoFs.h"
#include "hle/ModPack.h"
#include "hle/Srsc.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace hle;
using namespace hle::mods;
namespace fs = std::filesystem;

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

static std::vector<std::string> g_log;
static void sink(const std::string& line) {
    g_log.push_back(line);
}
static bool logHas(const char* needle) {
    for (auto& l : g_log)
        if (l.find(needle) != std::string::npos)
            return true;
    return false;
}

static void writeFile(const fs::path& p, const std::vector<u8>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o.write((const char*)d.data(), (std::streamsize)d.size());
}
static void writeText(const fs::path& p, const std::string& s) {
    writeFile(p, std::vector<u8>(s.begin(), s.end()));
}
static std::vector<u8> fill(size_t n, u8 v) {
    return std::vector<u8>(n, v);
}

// ---- tiny zip writer (stored or pre-deflated payloads)
static u32 crcOf(const std::vector<u8>& d) {
    u32 c = 0xFFFFFFFFu;
    for (u8 b : d) {
        c ^= b;
        for (int k = 0; k < 8; ++k)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}
struct ZEnt {
    std::string name;
    std::vector<u8> plain, packed; // packed = bytes as written (deflated when method 8)
    int method;
    u32 crc;
};
static void p16(std::vector<u8>& v, u32 x) {
    v.push_back((u8)x);
    v.push_back((u8)(x >> 8));
}
static void p32(std::vector<u8>& v, u32 x) {
    p16(v, x & 0xFFFF);
    p16(v, x >> 16);
}
static void writeZip(const fs::path& p, const std::vector<ZEnt>& es, bool flagEncrypted = false) {
    std::vector<u8> out, cd;
    for (auto& e : es) {
        u32 lho = (u32)out.size();
        p32(out, 0x04034b50);
        p16(out, 20);
        p16(out, flagEncrypted ? 1 : 0);
        p16(out, (u32)e.method);
        p32(out, 0);
        p32(out, e.crc);
        p32(out, (u32)e.packed.size());
        p32(out, (u32)e.plain.size());
        p16(out, (u32)e.name.size());
        p16(out, 0);
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), e.packed.begin(), e.packed.end());
        p32(cd, 0x02014b50);
        p16(cd, 20);
        p16(cd, 20);
        p16(cd, flagEncrypted ? 1 : 0);
        p16(cd, (u32)e.method);
        p32(cd, 0);
        p32(cd, e.crc);
        p32(cd, (u32)e.packed.size());
        p32(cd, (u32)e.plain.size());
        p16(cd, (u32)e.name.size());
        p16(cd, 0);
        p16(cd, 0);
        p16(cd, 0);
        p16(cd, 0);
        p32(cd, 0);
        p32(cd, lho);
        cd.insert(cd.end(), e.name.begin(), e.name.end());
    }
    u32 cdOff = (u32)out.size();
    out.insert(out.end(), cd.begin(), cd.end());
    p32(out, 0x06054b50);
    p16(out, 0);
    p16(out, 0);
    p16(out, (u32)es.size());
    p16(out, (u32)es.size());
    p32(out, (u32)cd.size());
    p32(out, cdOff);
    p16(out, 0);
    writeFile(p, out);
}
static ZEnt stored(const std::string& n, const std::vector<u8>& d) {
    return {n, d, d, 0, crcOf(d)};
}
static ZEnt stored(const std::string& n, const std::string& s) {
    return stored(n, std::vector<u8>(s.begin(), s.end()));
}

// kDeflated = raw deflate (zlib level 9, dynamic Huffman) of plainBlob(), generated with python zlib
static std::vector<u8> plainBlob() {
    std::string s;
    for (int i = 0; i < 3; ++i)
        s += "The quick brown fox jumps over the lazy dog. ";
    for (int i = 0; i < 200; ++i)
        s += (char)i;
    for (int i = 0; i < 5; ++i)
        s += "Drakan mod pack inflate test ";
    return std::vector<u8>(s.begin(), s.end());
}
static const u8 kDeflated[] = {
    11,  201, 72,  85,  40,  44,  205, 76,  206, 86,  72,  42,  202, 47,  207, 83,  72,  203, 175, 80,  200, 42,  205,
    45,  40,  86,  200, 47,  75,  45,  82,  40,  1,   74,  231, 36,  86,  85,  42,  164, 228, 167, 235, 41,  132, 208,
    76,  49,  3,   35,  19,  51,  11,  43,  27,  59,  7,   39,  23,  55,  15,  47,  31,  191, 128, 160, 144, 176, 136,
    168, 152, 184, 132, 164, 148, 180, 140, 172, 156, 188, 130, 162, 146, 178, 138, 170, 154, 186, 134, 166, 150, 182,
    142, 174, 158, 190, 129, 161, 145, 177, 137, 169, 153, 185, 133, 165, 149, 181, 141, 173, 157, 189, 131, 163, 147,
    179, 139, 171, 155, 187, 135, 167, 151, 183, 143, 175, 159, 127, 64,  96,  80,  112, 72,  104, 88,  120, 68,  100,
    84,  116, 76,  108, 92,  124, 66,  98,  82,  114, 74,  106, 90,  122, 70,  102, 86,  118, 78,  110, 94,  126, 65,
    97,  81,  113, 73,  105, 89,  121, 69,  101, 85,  117, 77,  109, 93,  125, 67,  99,  83,  115, 75,  107, 91,  123,
    71,  103, 87,  119, 79,  111, 95,  255, 132, 137, 147, 38,  79,  153, 58,  109, 250, 140, 153, 179, 102, 207, 153,
    59,  111, 254, 130, 133, 139, 22,  47,  89,  186, 108, 249, 138, 149, 171, 86,  175, 89,  187, 110, 253, 134, 141,
    155, 54,  111, 217, 186, 109, 251, 142, 157, 187, 118, 239, 217, 187, 111, 255, 129, 131, 135, 14,  31,  57,  122,
    236, 184, 75,  81,  98,  118, 98,  158, 66,  110, 126, 138, 66,  65,  34,  208, 235, 153, 121, 105, 57,  137, 37,
    169, 10,  37,  169, 197, 37,  10,  3,   33,  9,   0};

static ResourceId rid(const char* file, u16 type, u16 id, u16 sub) {
    ResourceId r;
    r.file = file;
    r.path = std::string("ENGLISH\\") + file;
    r.type = type;
    r.id = id;
    r.sub = sub;
    return r;
}
// Applies the merged override to an "original" payload of `origSize` 0xEE bytes; returns the result.
static std::vector<u8> applyTo(const std::shared_ptr<IResourceOverride>& o, const ResourceId& id, size_t origSize,
                               bool* changed = nullptr) {
    std::vector<u8> d(origSize, 0xEE);
    bool c = o && o->wants(id) && o->apply(id, d);
    if (changed)
        *changed = c;
    return d;
}

static std::string toml(const std::string& id, const std::string& ver = "1.0", const std::string& top = "",
                        const std::string& target = "profile = \"pal\"\n") {
    return top + "id = \"" + id + "\"\nname = \"Pack " + id + "\"\nversion = \"" + ver + "\"\n[target]\n" + target;
}

// ---- whole files, patched containers, new entries, levels (against a folder-backed IsoFs)
static std::vector<u8> makeSrsc(const std::vector<std::pair<SrscEntry, std::vector<u8>>>& es) {
    std::vector<u8> v(16, 0);
    memcpy(v.data(), "SRSC", 4);
    v[4] = 1;
    v[5] = 1;
    std::vector<SrscEntry> idx;
    for (auto& [e, d] : es) {
        SrscEntry x = e;
        x.off = (u32)v.size();
        x.size = (u32)d.size();
        v.insert(v.end(), d.begin(), d.end());
        while (v.size() % 8)
            v.push_back(0);
        idx.push_back(x);
    }
    u32 io = (u32)v.size(), n = (u32)idx.size();
    memcpy(&v[6], &io, 4);
    memcpy(&v[10], &n, 4);
    for (auto& x : idx) {
        u8 b[14];
        memcpy(b, &x.type, 2);
        memcpy(b + 2, &x.id, 2);
        memcpy(b + 4, &x.sub, 2);
        memcpy(b + 6, &x.off, 4);
        memcpy(b + 10, &x.size, 4);
        v.insert(v.end(), b, b + 14);
    }
    return v;
}
static bool entryBytes(IsoFs& f, const std::string& path, u16 t, u16 i, u16 s, std::vector<u8>& out) {
    IsoFs::Node n;
    if (!f.lookup(path, n))
        return false;
    std::vector<u8> all = f.readAll(n);
    if (all.size() < 16)
        return false;
    u32 idx, cnt;
    memcpy(&idx, &all[6], 4);
    memcpy(&cnt, &all[10], 4);
    for (u32 k = 0; k < cnt; ++k) {
        SrscEntry e;
        memcpy(&e.type, &all[idx + k * 14], 2);
        memcpy(&e.id, &all[idx + k * 14 + 2], 2);
        memcpy(&e.sub, &all[idx + k * 14 + 4], 2);
        memcpy(&e.off, &all[idx + k * 14 + 6], 4);
        memcpy(&e.size, &all[idx + k * 14 + 10], 4);
        if (e.type == t && e.id == i && e.sub == s) {
            out.assign(all.begin() + e.off, all.begin() + e.off + e.size);
            return true;
        }
    }
    return false;
}

static void filesTests(const fs::path& root, const DiscProfile& pal) {
    const fs::path disc = root / "disc";
    writeFile(disc / "ENGLISH/LEVELS/LEVEL1NA/LEVEL1NA.LVL", makeSrsc({{{0x20, 1, 0, 0, 0}, fill(100, 0x11)},
                                                                       {{0x20, 2, 0, 0, 0}, fill(40, 0x22)},
                                                                       {{0x30, 3, 0, 0, 0}, fill(8, 0x33)}}));
    const std::vector<u8> frLvl = makeSrsc({{{0x20, 2, 0, 0, 0}, fill(40, 0x22)}});
    writeFile(disc / "FRENCH/LEVELS/LEVEL1NA/LEVEL1NA.LVL", frLvl);
    writeFile(disc / "ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(64, 0x44)}}));
    writeText(disc / "ENGLISH/LEVELLST.TXT", "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n");
    IsoFs f;
    std::string err;
    CHECK(f.openFolder(disc.string(), &err));
    f.setLanguageRoot("\\English");
    IsoFs::Node n;
    CHECK(f.lookup("\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", n));

    // memory overlay
    f.addMemoryFile("\\Common\\Weapons\\Extra.txt", {'h', 'i'});
    CHECK(f.lookup("ENGLISH\\COMMON\\WEAPONS\\EXTRA.TXT", n) && n.size == 2 && f.readAll(n).size() == 2);

    // patchSrsc: in place (shorter), appended (longer), new, wildcard, unmatched, bad input
    {
        std::vector<u8> base =
            makeSrsc({{{1, 1, 0, 0, 0}, fill(32, 1)}, {{1, 1, 5, 0, 0}, fill(32, 2)}, {{1, 2, 0, 0, 0}, fill(16, 3)}});
        std::vector<SrscPatch> ps(4);
        ps[0] = {1, 1, 0xFFFF, fill(8, 9), false};
        ps[1] = {1, 2, 0, fill(64, 8), false};
        ps[2] = {9, 9, 1, fill(5, 7), true};
        ps[3] = {5, 5, 5, fill(5, 7), false}; // no such entry: skipped
        std::vector<u8> out;
        CHECK(patchSrsc(base, ps, out, err));
        u32 cnt;
        memcpy(&cnt, &out[10], 4);
        CHECK(cnt == 4 && memcmp(out.data(), "SRSC", 4) == 0 && out.size() > base.size());
        std::vector<u8> bad(base.begin(), base.begin() + 12);
        CHECK(!patchSrsc(bad, ps, out, err) && !err.empty());
        std::vector<SrscPatch> noSub(1);
        noSub[0] = {9, 9, 0xFFFF, fill(1, 1), true};
        CHECK(!patchSrsc(base, noSub, out, err));
    }

    // ODB header record 0x0100: the u16 class and group counts follow the added 0x0102 / 0x0101 records (the guest
    // sizes its class table from them; 3 records past the declared count corrupted the heap), but a pack's own counts
    // stay
    {
        auto hdr = [](u16 c, u16 g) {
            std::vector<u8> h(8, 0);
            memcpy(&h[0], &c, 2);
            memcpy(&h[2], &g, 2);
            return h;
        };
        auto countsOf = [](const std::vector<u8>& b, u16& c, u16& g) {
            u32 idx, n;
            memcpy(&idx, &b[6], 4);
            memcpy(&n, &b[10], 4);
            for (u32 i = 0; i < n; ++i) {
                u16 t;
                u32 off;
                memcpy(&t, &b[idx + i * 14], 2);
                if (t != 0x100)
                    continue;
                memcpy(&off, &b[idx + i * 14 + 6], 4);
                memcpy(&c, &b[off], 2);
                memcpy(&g, &b[off + 2], 2);
            }
        };
        std::vector<u8> odb = makeSrsc({{{0x100, 0, 0, 0, 0}, hdr(2, 1)},
                                        {{0x101, 1, 0, 0, 0}, fill(8, 1)},
                                        {{0x102, 5, 1, 0, 0}, fill(24, 2)},
                                        {{0x102, 9, 1, 0, 0}, fill(24, 3)}});
        std::vector<SrscPatch> add(3);
        for (u16 k = 0; k < 3; ++k)
            add[k] = {0x102, (u16)(20 + k), 1, fill(24, 4), true};
        std::vector<u8> out;
        u16 c = 0, g = 0;
        CHECK(patchSrsc(odb, add, out, err));
        countsOf(out, c, g);
        CHECK(c == 5 && g == 1);
        // a patch that only replaces a record leaves the counts alone
        std::vector<SrscPatch> rep(1);
        rep[0] = {0x102, 5, 1, fill(8, 6), false};
        CHECK(patchSrsc(odb, rep, out, err));
        countsOf(out, c, g);
        CHECK(c == 2 && g == 1);
        // a pack that wrote its own header counts keeps them
        std::vector<SrscPatch> own = add;
        own.push_back({0x100, 0, 0, hdr(40, 7), false});
        CHECK(patchSrsc(odb, own, out, err));
        countsOf(out, c, g);
        CHECK(c == 40 && g == 7);
        // a container without the header record (or with other counts there) is not touched
        std::vector<u8> plain = makeSrsc({{{0x102, 5, 1, 0, 0}, fill(24, 2)}});
        CHECK(patchSrsc(plain, {add[0]}, out, err));
        u32 plainCount;
        memcpy(&plainCount, &out[10], 4);
        CHECK(plainCount == 2);
    }

    // Imported-bow sidecars are bounded, enabled-pack-only, and preserve mod load order.
    {
        const fs::path mods = root / "mods_bow_metadata";
        for (const auto* id : {"first", "last", "off"}) {
            writeText(mods / id / "mod.toml", toml(id));
            writeText(mods / id / "gameplay/bows.ini", std::string("format=1\n;") + id);
        }
        writeText(mods / "first/gameplay/deeper/bows.ini", "ignored");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"first", true}, {"off", false}, {"last", true}}) == 2);
        CHECK(mm.bowAimFiles().size() == 2);
        CHECK(mm.bowAimFiles()[0].second == "format=1\n;first");
        CHECK(mm.bowAimFiles()[1].second == "format=1\n;last");
        CHECK(mm.load(mods.string(), {{"first", false}, {"off", false}, {"last", false}}) == 0);
        CHECK(mm.bowAimFiles().empty());
    }

    // Portable English pack files also serve the running language. Explicit disc
    // lookups keep their original language, and localized files retain load precedence.
    {
        const fs::path mods = root / "mods_localized";
        writeText(disc / "GERMAN/LEVELLST.TXT", "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n");
        writeFile(disc / "GERMAN/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(12, 0x21)}}));
        writeZip(
            mods / "first.zip",
            {stored("mod.toml", toml("first", "1.0", "levels = [\"Level 9X - Clone Test\"]\n")),
             stored("files/English/Levels/Level9st/Level9st.lvl", makeSrsc({{{0x20, 1, 0, 0, 0}, fill(10, 0x55)}})),
             stored("files/English/Common/Weap/Weap.txd", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x61)}})),
             stored("files/German/Common/Weap/Weap.txd", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x62)}}))});
        writeText(mods / "last/mod.toml", toml("last"));
        writeFile(mods / "last/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 0x63)}}));
        IsoFs localized;
        CHECK(localized.openFolder(disc.string(), &err));
        localized.setLanguageRoot("German");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"first", true}, {"last", false}}) == 1);
        CHECK(mm.installFiles(localized) >= 4);
        CHECK(mm.extraLevels().size() == 1);
        std::vector<u8> b;
        CHECK(entryBytes(localized, "\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl", 0x20, 1, 0, b) &&
              b[0] == 0x55);
        CHECK(entryBytes(localized, "GERMAN\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x62);
        CHECK(entryBytes(localized, "ENGLISH\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x61);
        CHECK(localized.findKey("FRENCH\\LEVELS\\LEVEL1NA\\LEVEL1NA.LVL", n) && !n.mem);
        CHECK(mm.load(mods.string(), {{"first", true}, {"last", true}}) == 2);
        CHECK(mm.installFiles(localized) >= 4);
        CHECK(entryBytes(localized, "GERMAN\\COMMON\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b[0] == 0x63);
    }

    // pack with files/, LVL override, new/ entry, levels
    {
        const fs::path mods = root / "mods_files";
        writeText(mods / "pk/mod.toml", toml("pk", "1.0",
                                             "levels = [\"Level 9X - Clone Test\", \"Bad/Name\"]\n"
                                             "level_copies = [\"Level 9X - Clone Test = Level 1A - Surdana\"]\n"));
        writeFile(mods / "pk/files/ENGLISH/LEVELS/LEVEL9ST/LEVEL9ST.LVL",
                  makeSrsc({{{0x20, 1, 0, 0, 0}, fill(10, 0x55)}}));
        writeFile(mods / "pk/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(70, 0x66)}}));
        writeFile(mods / "pk/files/ENGLISH/COMMON/BAD/BAD.TXD", fill(40, 0)); // not SRSC: rejected
        writeFile(mods / "pk/overrides/LEVEL1NA.LVL/0020_2_x.bin", fill(30, 0x77));
        writeFile(mods / "pk/new/WEAP.TXD/0040_900_0.bin", fill(24, 0x88));
        writeText(mods / "pk/maps/clone_arena.ini",
                  "[arena clone]\nname = Clone Arena\nlevel = Level 9X - Clone Test\n");
        writeFile(mods / "pk/previews/Clone_Arena.png", fill(20, 0x42));
        writeText(mods / "pk/previews/deeper/x.png", "no");
        writeText(mods / "pk/maps/notes.txt", "not a map file");
        writeText(mods / "pk/maps/deeper/ignored.ini", "[arena no]\n");
        writeFile(mods / "late/overrides/LEVEL1NA.LVL/0020_2_0.bin", fill(12, 0x99));
        writeText(mods / "late/mod.toml", toml("late"));
        ModManager mm(pal);
        g_log.clear();
        CHECK(mm.load(mods.string(), {{"pk", true}, {"late", true}}) == 2);
        CHECK(mm.status()[0].files == 3 && mm.status()[0].added == 1 && mm.status()[0].levels == 1 &&
              mm.status()[0].rejected == 1);
        CHECK(logHas("ignoring level 'Bad/Name'"));
        const size_t served = mm.installFiles(f);
        CHECK(served >= 4);
        CHECK(logHas("is not a valid SRSC container"));
        std::vector<u8> b;
        CHECK(entryBytes(f, "\\Common\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b.size() == 70 && b[0] == 0x66);
        CHECK(entryBytes(f, "\\Common\\WEAP\\WEAP.TXD", 0x40, 900, 0, b) && b.size() == 24 && b[0] == 0x88);
        // LVL: the later pack's exact override wins over the earlier x
        CHECK(entryBytes(f, "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", 0x20, 2, 0, b) && b.size() == 12 &&
              b[0] == 0x99);
        CHECK(entryBytes(f, "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl", 0x20, 1, 0, b) && b.size() == 100);
        // other language trees are not patched
        CHECK(f.findKey("FRENCH\\LEVELS\\LEVEL1NA\\LEVEL1NA.LVL", n) && n.size == frLvl.size() && !n.mem);
        // new level file reachable by long name and listed
        CHECK(f.lookup("\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl", n) && n.size > 0);
        // multiplayer maps: only maps/*.ini directly in the folder of a loaded pack
        CHECK(mm.mapFiles().size() == 1 && mm.mapFiles()[0].first.find("mod pk") == 0 &&
              mm.mapFiles()[0].second.find("[arena clone]") == 0);
        CHECK(mm.previewFiles().size() == 1 && mm.previewFiles()[0].first == "Clone_Arena" &&
              mm.previewFiles()[0].second.size() == 20);
        CHECK(mm.extraLevels().size() == 1 && mm.extraLevels()[0].name == "Level 9X - Clone Test" &&
              mm.extraLevels()[0].listIndex == 1 && mm.extraLevels()[0].modId == "pk" &&
              mm.extraLevels()[0].copyOf == "Level 1A - Surdana");
        CHECK(f.lookup("LEVELLST.TXT", n));
        std::vector<u8> lst = f.readAll(n);
        std::string txt(lst.begin(), lst.end());
        CHECK(txt == "\\Levels\\Level 1A - Surdana\\Level 1A - Surdana.lvl\r\n"
                     "\\Levels\\Level 9X - Clone Test\\Level 9X - Clone Test.lvl\r\n");
    }

    // guest limits: a replacement whose entry crosses the whole-entry limit where the original did not is rejected
    {
        const fs::path mods = root / "mods_files2";
        writeText(mods / "big/mod.toml", toml("big"));
        writeFile(mods / "big/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(0x41000, 1)}}));
        IsoFs f2;
        CHECK(f2.openFolder(disc.string(), &err));
        f2.setLanguageRoot("\\English");
        ModManager mm(pal);
        mm.load(mods.string(), {{"big", true}});
        g_log.clear();
        mm.installFiles(f2);
        CHECK(logHas("past the 264192 byte whole-entry limit") && logHas("file rejected"));
        IsoFs::Node n2;
        CHECK(f2.lookup("\\Common\\WEAP\\WEAP.TXD", n2) && n2.size < 0x1000);
        // a pack file over the whole-file cap is rejected at load
        ModLimits lim;
        lim.maxWholeFile = 100;
        ModManager small(pal, lim);
        small.load(mods.string(), {{"big", true}});
        CHECK(small.status()[0].rejected == 1 && small.status()[0].files == 0);
    }

    // a later pack replaces an earlier pack's file: conflict reported, later wins
    {
        const fs::path mods = root / "mods_files3";
        writeText(mods / "a/mod.toml", toml("a"));
        writeText(mods / "b/mod.toml", toml("b"));
        writeFile(mods / "a/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(10, 1)}}));
        writeFile(mods / "b/files/ENGLISH/COMMON/WEAP/WEAP.TXD", makeSrsc({{{0x40, 7, 0, 0, 0}, fill(11, 2)}}));
        IsoFs f3;
        CHECK(f3.openFolder(disc.string(), &err));
        f3.setLanguageRoot("\\English");
        ModManager mm(pal);
        mm.load(mods.string(), {{"a", true}, {"b", true}});
        CHECK(mm.conflicts().size() == 1 && mm.conflicts()[0].winner == "b");
        mm.installFiles(f3);
        std::vector<u8> b;
        CHECK(entryBytes(f3, "\\Common\\WEAP\\WEAP.TXD", 0x40, 7, 0, b) && b.size() == 11);
    }

    // content files (story editing): entries of ODB / XDB / SDB / SSD are replaced per request (0x1202 override layer)
    // and added with new/ (container rebuilt); both together, a wildcard sub, and a pack that only adds
    {
        const fs::path d2 = root / "disc_story";
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.ODB",
                  makeSrsc({{{0x102, 4167, 3055, 0, 0}, fill(86, 0x10)}, {{0x103, 0, 0, 0, 0}, fill(40, 0x11)}}));
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.XDB",
                  makeSrsc({{{0x400, 22, 8, 0, 0}, fill(64, 0x20)}, {{0x400, 23, 8, 0, 0}, fill(64, 0x21)}}));
        writeFile(d2 / "ENGLISH/COMMON/SYSTEM/SYSTEM.SDB",
                  makeSrsc({{{0x302, 5, 7, 0, 0}, fill(48, 0x30)}, {{0x304, 5, 7, 0, 0}, fill(200, 0x31)}}));
        writeFile(d2 / "ENGLISH/LEVELS/LEVEL1ON/LEVEL1ON.SSD", makeSrsc({{{0x311, 9, 0, 0, 0}, fill(120, 0x40)}}));
        const fs::path mods = root / "mods_story";
        writeText(mods / "story/mod.toml", toml("story"));
        writeFile(mods / "story/overrides/SYSTEM.ODB/0102_4167_3055.bin", fill(90, 0xA1));
        writeFile(mods / "story/overrides/SYSTEM.XDB/0400_22_x.bin", fill(80, 0xA2));
        writeFile(mods / "story/overrides/SYSTEM.SDB/0302_5_7.bin", fill(52, 0xA3));
        writeFile(mods / "story/overrides/LEVEL1ON.SSD/0311_9_0.bin", fill(100, 0xA4));
        writeFile(mods / "story/new/SYSTEM.XDB/0400_114_8.bin", fill(40, 0xB1));
        writeFile(mods / "story/new/SYSTEM.ODB/0102_4200_3055.bin", fill(88, 0xB2));
        writeFile(mods / "story/new/SYSTEM.ODB/0103_0_0.bin", fill(60, 0xB3)); // replaces the existing 0x103 (same key)
        writeFile(mods / "story/new/LEVEL1ON.SSD/0311_10_0.bin", fill(30, 0xB4));
        writeText(mods / "adds/mod.toml", toml("adds"));
        writeFile(mods / "adds/new/SYSTEM.XDB/0400_115_8.bin", fill(44, 0xB5));
        IsoFs f4;
        std::string err4;
        CHECK(f4.openFolder(d2.string(), &err4));
        f4.setLanguageRoot("\\English");
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"story", true}, {"adds", true}}) == 2);
        CHECK(mm.status()[0].added == 4 && mm.status()[0].entries == 4 && mm.status()[1].added == 1);
        // per-request overrides: sizes may differ from the original (86 -> 90, 64 -> 80, 48 -> 52, 120 -> 100)
        CHECK(applyTo(mm.entries(), rid("SYSTEM.ODB", 0x102, 4167, 3055), 86) == fill(90, 0xA1));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 22, 8), 64) == fill(80, 0xA2));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 22, 3), 64) == fill(80, 0xA2)); // x sub
        CHECK(applyTo(mm.entries(), rid("SYSTEM.XDB", 0x400, 23, 8), 64) == fill(64, 0xEE)); // untouched string
        CHECK(applyTo(mm.entries(), rid("SYSTEM.SDB", 0x302, 5, 7), 48) == fill(52, 0xA3));
        CHECK(applyTo(mm.entries(), rid("SYSTEM.SDB", 0x304, 5, 7), 200) == fill(200, 0xEE)); // audio data untouched
        CHECK(applyTo(mm.entries(), rid("LEVEL1ON.SSD", 0x311, 9, 0), 120) == fill(100, 0xA4));
        g_log.clear();
        const size_t served4 = mm.installFiles(f4);
        CHECK(served4 == 3); // SYSTEM.XDB, SYSTEM.ODB, LEVEL1ON.SSD rebuilt; SYSTEM.SDB untouched
        std::vector<u8> b;
        const std::string xdbPath = "\\Common\\System\\SYSTEM.XDB", odbPath = "\\Common\\System\\SYSTEM.ODB";
        CHECK(entryBytes(f4, xdbPath, 0x400, 114, 8, b) && b == fill(40, 0xB1));
        CHECK(entryBytes(f4, xdbPath, 0x400, 115, 8, b) && b == fill(44, 0xB5));
        CHECK(entryBytes(f4, xdbPath, 0x400, 23, 8, b) && b == fill(64, 0x21));
        CHECK(entryBytes(f4, odbPath, 0x102, 4200, 3055, b) && b == fill(88, 0xB2));
        CHECK(entryBytes(f4, odbPath, 0x103, 0, 0, b) && b == fill(60, 0xB3));
        // ODB is plain-read by the game: an `overrides/` entry of it is patched into the rebuilt file as well
        CHECK(entryBytes(f4, odbPath, 0x102, 4167, 3055, b) && b == fill(90, 0xA1));
        IsoFs::Node sn;
        CHECK(f4.lookup("\\Levels\\LEVEL1ON\\LEVEL1ON.SSD", sn) && sn.mem);
        CHECK(f4.lookup("\\Common\\System\\SYSTEM.SDB", sn) && !sn.mem);
    }
}

int main() {
    setLogSink(sink);
    setVerbose(false);
    fs::path root = fs::temp_directory_path() / "drakan_mod_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // ---- manifest parsing
    {
        Manifest m;
        std::string err;
        CHECK(parseManifest("# c\nformat = 1\nid = \"a.b-c_1\"\nname = 'X'  # trailing\nversion = \"1.2.3\"\n"
                            "author = \"me\"\ndescription = \"d \\\"q\\\"\"\n[target]\nprofile = [\"pal\", \"usa\"]\n"
                            "hash = \"00ff\"\n[load]\nafter = [\n  \"a\",\n  \"b\", # c\n]\nbefore = [\"z\"]\n"
                            "dependencies = [\"core>=1.2\"]\n",
                            false, m, err));
        CHECK(m.id == "a.b-c_1" && m.name == "X" && m.version == "1.2.3" && m.author == "me" &&
              m.description == "d \"q\"");
        CHECK(m.profiles.size() == 2 && m.profiles[1] == "usa" && m.profileHashes.size() == 1);
        CHECK(m.loadAfter.size() == 2 && m.loadAfter[1] == "b" && m.loadBefore[0] == "z" &&
              m.dependencies[0] == "core>=1.2");
        CHECK(parseManifest("{\"id\":\"j1\",\"name\":\"J\",\"version\":\"2\",\"target\":{\"profile\":\"pal\","
                            "\"hash\":\"ABCDEF\"},\"load\":{\"after\":[\"x\"]},\"dependencies\":[\"y\",\"z>=2\"],"
                            "\"extra\":[{\"a\":1},[1,2]],\"n\":{\"k\":[]}}",
                            true, m, err));
        CHECK(m.id == "j1" && m.profiles[0] == "pal" && m.profileHashes[0] == "ABCDEF" && m.loadAfter[0] == "x" &&
              m.dependencies.size() == 2);
        // A custom campaign (docs/reference/modding.md "Campaign packs").
        CHECK(
            parseManifest("id = \"c1\"\nlevels = [\"A Copy\", \"B Copy\"]\ncampaign = \"test-valley\"\n"
                          "campaign_name = \"The Test Valley\"\ncampaign_description = \"Two levels.\"\n"
                          "campaign_start = \"A Copy\"\ncampaign_saves = \"testvalley\"\n[target]\nprofile = \"pal\"\n",
                          false, m, err));
        CHECK(m.campaign == "test-valley" && m.campaignName == "The Test Valley" &&
              m.campaignDescription == "Two levels." && m.campaignStart == "A Copy" && m.campaignSaves == "testvalley");
        CHECK(!parseManifest("name = \"x\"\n", false, m, err) && err.find("'id'") != std::string::npos);
        CHECK(!parseManifest("id = \"a b\"\n", false, m, err));
        CHECK(!parseManifest("id = \"a\"\nname = \"unterminated\n", false, m, err));
        CHECK(!parseManifest("{\"id\": ", true, m, err));
        CHECK(!parseManifest("[1]", true, m, err));
        CHECK(compareVersions("1.10", "1.9") > 0 && compareVersions("1.0", "1") == 0 &&
              compareVersions("0.9", "1") < 0);
        CHECK(profileHash("SCES-50006", "BESCES-50006") != profileHash("SCUS-97128", "BASCUS-97128"));
    }

    // ---- inflate / zip
    {
        std::vector<u8> plain = plainBlob(), out;
        CHECK(inflateRaw(kDeflated, sizeof kDeflated, out, plain.size()) && out == plain);
        CHECK(!inflateRaw(kDeflated, sizeof kDeflated / 2, out, plain.size()));
        CHECK(!inflateRaw(kDeflated, sizeof kDeflated, out, plain.size() - 1)); // output limit
        writeZip(root / "z.zip",
                 {stored("a.txt", "hello"),
                  {"b.bin", plain, std::vector<u8>(kDeflated, kDeflated + sizeof kDeflated), 8, crcOf(plain)},
                  stored("../evil.bin", "x")});
        std::string err;
        auto z = openArchive((root / "z.zip").string(), err);
        CHECK(z != nullptr);
        std::vector<u8> r;
        CHECK(z && z->read("a.txt", r) && std::string(r.begin(), r.end()) == "hello");
        CHECK(z && z->read("b.bin", r) && r == plain);
        CHECK(z && !z->read("../evil.bin", r) && z->list().size() == 2); // zip-slip entry dropped
        CHECK(z && !z->read("missing", r));
        auto bad = stored("c.bin", "abc");
        bad.crc ^= 1;
        writeZip(root / "bad.zip", {bad});
        auto zb = openArchive((root / "bad.zip").string(), err);
        CHECK(zb && !zb->read("c.bin", r));
        writeZip(root / "enc.zip", {stored("a", "b")}, true);
        CHECK(!openArchive((root / "enc.zip").string(), err) && err.find("encrypted") != std::string::npos);
        writeText(root / "notzip.zip", "this is not a zip file at all, just text padding.......");
        CHECK(!openArchive((root / "notzip.zip").string(), err));
        CHECK(!openArchive((root / "nope").string(), err));
    }

    const DiscProfile pal{"pal", profileHash("SCES-50006", "BESCES-50006")};
    const DiscProfile usa{"usa", profileHash("SCUS-97128", "BASCUS-97128")};

    // ---- merge order, conflicts, wildcard, text, zip + folder
    {
        fs::path mods = root / "mods1";
        writeText(mods / "alpha/mod.toml", toml("alpha"));
        writeFile(mods / "alpha/overrides/interfce.txd/0040_97_0.bin", fill(100, 0xA1));
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/0040_98_x.bin", fill(50, 0xA2));
        writeFile(mods / "alpha/overrides/INTERFCE.XDB/0400_5_2.bin", fill(10, 0xA3));
        writeText(mods / "alpha/overrides/INTERFCE.XDB/text.ini", "156 = Alpha\n157.en = A157\n");
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/bogus.bin", fill(4, 1));
        writeFile(mods / "alpha/overrides/INTERFCE.TXD/0040_1_0.bin", {}); // empty: rejected
        // zip pack B: nested single top-level folder, mixed case
        writeZip(mods / "beta.zip", {stored("Beta/MOD.TOML", toml("beta", "2.0")),
                                     stored("Beta/overrides/INTERFCE.TXD/0040_97_0.bin", fill(120, 0xB1)),
                                     stored("Beta/overrides/INTERFCE.TXD/0040_98_3.bin", fill(60, 0xB2)),
                                     stored("Beta/overrides/INTERFCE.XDB/text.ini", "156 = Beta\n")});
        writeText(mods / "off/mod.toml", toml("off"));
        writeText(mods / "usaonly/mod.toml", toml("usaonly", "1.0", "", "profile = \"usa\"\n"));
        writeFile(mods / "usaonly/overrides/X.TXD/0040_1_0.bin", fill(8, 1));
        writeText(mods / "nomanifest/readme.txt", "hi");
        g_log.clear();
        ModManager mm(pal);
        mm.load(mods.string(), {{"alpha", true},
                                {"beta", true},
                                {"off", false},
                                {"usaonly", true},
                                {"nomanifest", true},
                                {"ghost", true}});
        CHECK(mm.status().size() == 6);
        size_t loaded = 0;
        for (auto& s : mm.status())
            loaded += s.state == ModState::Loaded;
        CHECK(loaded == 2);
        auto st = [&](const char* k) -> const ModStatus* {
            for (auto& s : mm.status())
                if (s.key == k)
                    return &s;
            return nullptr;
        };
        CHECK(st("alpha")->state == ModState::Loaded && st("alpha")->entries == 3 && st("alpha")->texts == 2 &&
              st("alpha")->rejected == 2);
        CHECK(st("beta")->state == ModState::Loaded && st("beta")->entries == 2 && st("beta")->texts == 1);
        CHECK(st("off")->state == ModState::Disabled);
        CHECK(st("usaonly")->state == ModState::Skipped && st("usaonly")->message.find("usa") != std::string::npos);
        CHECK(logHas("targets disc profile usa but this build is pal"));
        CHECK(st("nomanifest")->state == ModState::Skipped &&
              st("nomanifest")->message.find("mod.toml") != std::string::npos);
        CHECK(st("ghost")->state == ModState::Skipped && st("ghost")->message.find("not found") != std::string::npos);
        CHECK(mm.entryCount() == 4); // 97/0, 98/x, XDB 5/2, 98/3
        auto e = mm.entries();
        CHECK(e != nullptr);
        // the later pack (beta) wins the same entry; alpha-only entries survive
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 97, 0), 100) == fill(120, 0xB1));
        CHECK(applyTo(e, rid("INTERFCE.XDB", 0x400, 5, 2), 10) == fill(10, 0xA3));
        // wildcard (alpha, earlier) vs specific (beta, later) for 98/3: beta wins; other subs use the wildcard
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 98, 3), 60) == fill(60, 0xB2));
        CHECK(applyTo(e, rid("INTERFCE.TXD", 0x40, 98, 0), 50) == fill(50, 0xA2));
        bool ch = true;
        applyTo(e, rid("INTERFCE.TXD", 0x40, 99, 0), 10, &ch);
        CHECK(!ch);
        CHECK(!e->wants(rid("X.TXD", 0x40, 1, 0))); // skipped pack contributes nothing
        bool c97 = false, c98 = false, c156 = false;
        for (auto& c : mm.conflicts()) {
            c97 |= c.what.find("0040/97/0") != std::string::npos && c.winner == "beta" && c.loser == "alpha";
            c98 |= c.what.find("0040/98/3") != std::string::npos && c.winner == "beta";
            c156 |= c.what.find("156.*") != std::string::npos && c.winner == "beta";
        }
        CHECK(c97 && c98 && c156 && mm.conflicts().size() == 3);
        CHECK(logHas("conflict:"));
        // text: the later pack wins 156, alpha's 157.en survives
        auto t = mm.texts();
        CHECK(t != nullptr && mm.textCount() == 3);
        auto txt = xdb::encode({"orig", "o", "o", "o", "o"});
        ResourceId tid = rid("INTERFCE.XDB", 0x400, 156, 2);
        std::vector<u8> d = txt;
        std::vector<std::string> sl;
        CHECK(t->wants(tid) && t->apply(tid, d) && xdb::decode(d, sl) && sl[0] == "Beta" && sl[4] == "Beta");
        tid.id = 157;
        d = txt;
        CHECK(t->apply(tid, d) && xdb::decode(d, sl) && sl[0] == "A157" && sl[1] == "o");
        CHECK(!mm.report().empty());
    }

    // ---- reversed order flips the winner
    {
        ModManager mm(pal);
        mm.load((root / "mods1").string(), {{"beta", true}, {"alpha", true}});
        CHECK(applyTo(mm.entries(), rid("INTERFCE.TXD", 0x40, 97, 0), 100) == fill(100, 0xA1));
        // alpha's wildcard 98/x is now the later pack: it beats beta's specific 98/3
        CHECK(applyTo(mm.entries(), rid("INTERFCE.TXD", 0x40, 98, 3), 60) == fill(50, 0xA2));
    }

    // ---- mods.ini: unknown packs are appended as "new" (off, ModStatus::isNew); order and enabled flags honoured
    {
        fs::path mods = root / "mods2";
        writeText(mods / "one/mod.toml", toml("one"));
        writeFile(mods / "one/overrides/A.TXD/0040_1_0.bin", fill(8, 1));
        writeText(mods / "two/mod.toml", toml("two"));
        writeFile(mods / "two/overrides/A.TXD/0040_1_0.bin", fill(8, 2));
        ModManager first(pal);
        CHECK(first.load(mods.string()) == 0); // nothing enabled yet
        std::ifstream in(mods / "mods.ini");
        std::string ini((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        CHECK(ini.find("one = new") != std::string::npos && ini.find("two = new") != std::string::npos);
        CHECK(first.status().size() == 2 && first.status()[0].isNew && first.status()[1].isNew);
        CHECK(first.status()[0].state == ModState::Disabled && !first.status()[0].enabled);
        {
            // `new` survives the next start (still undecided) and loads nothing; `off` is a decision.
            ModManager again(pal);
            CHECK(again.load(mods.string()) == 0);
            CHECK(again.status().size() == 2 && again.status()[0].isNew && again.status()[1].isNew);
            writeText(mods / "mods.ini", "[mods]\none = off\ntwo = new\n");
            ModManager decided(pal);
            CHECK(decided.load(mods.string()) == 0);
            CHECK(!decided.status()[0].isNew && decided.status()[1].isNew);
        }
        writeText(mods / "mods.ini", "[mods]\n; comment\ntwo = on\none = on\n");
        ModManager m2(pal);
        CHECK(m2.load(mods.string()) == 2);
        CHECK(applyTo(m2.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1)); // one is later
        writeText(mods / "mods.ini", "one = on\ntwo = off\n");
        ModManager m3(pal);
        CHECK(m3.load(mods.string()) == 1);
        CHECK(applyTo(m3.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1));
        CHECK(fs::file_size(mods / "mods.ini") == 19); // untouched when nothing is new
        CHECK(!m3.status()[0].isNew && !m3.status()[1].isNew);
    }

    // ---- profile hash, dependencies, duplicate ids, format, hints
    {
        fs::path mods = root / "mods3";
        const std::string h = hex64(pal.hash);
        writeText(mods / "hashok/mod.toml", "id = \"hashok\"\n[target]\nhash = \"" + h + "\"\n");
        writeText(mods / "hashbad/mod.toml", "id = \"hashbad\"\n[target]\nprofile = \"pal\"\nhash = \"deadbeef\"\n");
        writeText(mods / "notarget/mod.toml", "id = \"notarget\"\n");
        writeText(mods / "any/mod.toml", "id = \"any\"\nversion = \"1.5\"\n[target]\nprofile = \"*\"\n");
        writeText(mods / "needs/mod.json",
                  "{\"id\":\"needs\",\"target\":{\"profile\":\"pal\"},\"dependencies\":[\"any>=1.2\"],"
                  "\"load\":{\"after\":[\"any\"]}}");
        writeText(mods / "needsnew/mod.toml", "dependencies = [\"any>=2.0\"]\n" + toml("needsnew"));
        writeText(mods / "needsmissing/mod.toml", "dependencies = [\"nothere\"]\n" + toml("needsmissing"));
        writeText(mods / "chain/mod.toml", "dependencies = [\"needsmissing\"]\n" + toml("chain"));
        writeText(mods / "dup1/mod.toml", toml("same"));
        writeText(mods / "dup2/mod.toml", toml("same"));
        writeText(mods / "future/mod.toml", "format = 9\n" + toml("future"));
        g_log.clear();
        ModManager mm(pal);
        mm.load(mods.string(), {{"hashok", true},
                                {"hashbad", true},
                                {"notarget", true},
                                {"needs", true},
                                {"any", true},
                                {"needsnew", true},
                                {"needsmissing", true},
                                {"chain", true},
                                {"dup1", true},
                                {"dup2", true},
                                {"future", true}});
        auto state = [&](const char* k) {
            for (auto& s : mm.status())
                if (s.key == k)
                    return s.state;
            return ModState::Disabled;
        };
        CHECK(state("hashok") == ModState::Loaded && state("hashbad") == ModState::Skipped);
        CHECK(state("notarget") == ModState::Skipped && logHas("no target profile"));
        CHECK(state("any") == ModState::Loaded && state("needs") == ModState::Loaded);
        CHECK(state("needsnew") == ModState::Skipped && logHas("needs 'any' >= 2.0 but 1.5"));
        CHECK(state("needsmissing") == ModState::Skipped && state("chain") == ModState::Skipped); // transitive
        CHECK(state("dup1") == ModState::Loaded && state("dup2") == ModState::Skipped &&
              logHas("already used by 'dup1'"));
        CHECK(state("future") == ModState::Skipped && logHas("needs mod format 9"));
        CHECK(logHas("'needs' asks to load after 'any'")); // hint violated: "any" is listed later
        // the USA build rejects the PAL-pinned pack by hash, and accepts profile = "*"
        ModManager mu(usa);
        mu.load(mods.string(), {{"hashok", true}, {"any", true}});
        CHECK(mu.status()[0].state == ModState::Skipped && mu.status()[1].state == ModState::Loaded);
    }

    // ---- oversize: growth past the whole-entry limit is rejected; already-large entries and plain growth are not
    {
        fs::path mods = root / "mods4";
        writeText(mods / "big/mod.toml", toml("big"));
        writeFile(mods / "big/overrides/A.TXD/0040_1_0.bin", fill(0x40801, 7)); // just over
        writeFile(mods / "big/overrides/A.TXD/0040_2_0.bin", fill(0x40800, 8)); // exactly at the limit
        writeFile(mods / "big/overrides/A.TXD/0040_3_0.bin", fill(0x60000, 9)); // original already larger
        writeFile(mods / "big/overrides/A.TXD/0040_4_0.bin", fill(5000, 6));    // plain growth
        g_log.clear();
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"big", true}}) == 1);
        auto e = mm.entries();
        bool ch = true;
        auto r = applyTo(e, rid("A.TXD", 0x40, 1, 0), 1000, &ch);
        CHECK(!ch && r == fill(1000, 0xEE) && logHas("whole-entry limit"));
        r = applyTo(e, rid("A.TXD", 0x40, 2, 0), 1000, &ch);
        CHECK(ch && r.size() == 0x40800);
        r = applyTo(e, rid("A.TXD", 0x40, 3, 0), 0x50000, &ch);
        CHECK(ch && r.size() == 0x60000);
        r = applyTo(e, rid("A.TXD", 0x40, 3, 0), 0x100, &ch);
        CHECK(!ch);
        g_log.clear();
        r = applyTo(e, rid("A.TXD", 0x40, 4, 0), 100, &ch);
        CHECK(ch && r.size() == 5000 && logHas("grows 100 -> 5000"));
        ModLimits lim;
        lim.maxFileBytes = 4000; // per-file sanity cap at load
        ModManager small(pal, lim);
        small.load(mods.string(), {{"big", true}});
        CHECK(small.entryCount() == 0 && small.status()[0].rejected == 4);
    }

    // ---- a corrupt zip is skipped with a message, other packs still load
    {
        fs::path mods = root / "mods5";
        writeText(mods / "broken.zip", "PK but nothing else here, definitely not a valid archive........");
        writeText(mods / "fine/mod.toml", toml("fine"));
        writeFile(mods / "fine/overrides/A.TXD/0040_1_0.bin", fill(8, 1));
        ModManager mm(pal);
        CHECK(mm.load(mods.string(), {{"broken", true}, {"fine", true}}) == 1);
        CHECK(mm.status()[0].state == ModState::Skipped && mm.status()[0].message.find("invalid pack") == 0);
    }

    filesTests(root, pal);

    setLogSink(nullptr);
    fs::remove_all(root);
    printf("mod_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
