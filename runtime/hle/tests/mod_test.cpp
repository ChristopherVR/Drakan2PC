// mod_test: synthetic mod packs (folder and zip): manifest parsing, merge order, conflicts, profile mismatch,
// dependencies, oversize rejection, text.ini, mods.ini handling, zip stored/deflate, zip-slip. No game data needed.
#include "hle/ModPack.h"
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

    // ---- mods.ini: unknown packs are appended as "off"; order and enabled flags honoured
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
        CHECK(ini.find("one = off") != std::string::npos && ini.find("two = off") != std::string::npos);
        writeText(mods / "mods.ini", "[mods]\n; comment\ntwo = on\none = on\n");
        ModManager m2(pal);
        CHECK(m2.load(mods.string()) == 2);
        CHECK(applyTo(m2.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1)); // one is later
        writeText(mods / "mods.ini", "one = on\ntwo = off\n");
        ModManager m3(pal);
        CHECK(m3.load(mods.string()) == 1);
        CHECK(applyTo(m3.entries(), rid("A.TXD", 0x40, 1, 0), 8) == fill(8, 1));
        CHECK(fs::file_size(mods / "mods.ini") == 19); // untouched when nothing is new
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

    setLogSink(nullptr);
    fs::remove_all(root);
    printf("mod_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
