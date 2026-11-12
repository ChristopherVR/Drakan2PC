// override_test: self-contained test of the resource override layer (no ISO needed; builds a tiny folder game tree in a
// temp dir).
#include <algorithm>
#include "hle/RsrcIop.h"
#include <cstdio>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace hle;
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

struct FakeMem : IEeMemory {
    std::vector<u8> ram = std::vector<u8>(8u << 20);
    bool read(u32 a, void* d, size_t n) override {
        a &= 0x1FFFFFFF;
        if (a + n > ram.size())
            return false;
        memcpy(d, &ram[a], n);
        return true;
    }
    bool write(u32 a, const void* s, size_t n) override {
        a &= 0x1FFFFFFF;
        if (a + n > ram.size())
            return false;
        memcpy(&ram[a], s, n);
        return true;
    }
};
struct FakeEe : IEeRpcPoster {
    int post(u32, u32, const void*, u32, void*, u32, bool) override { return 0; }
};
struct FakeSpu : ISpu2 {
    void ramWrite(u32, const void*, u32) override {}
    void ramRead(u32, void*, u32) override {}
    void setParam(u16, u16) override {}
    u16 getParam(u16) override { return 0; }
    void setSwitch(u16, u32) override {}
    u32 getSwitch(u16) override { return 0; }
    void setAddr(u16, u32) override {}
    u32 getAddr(u16) override { return 0; }
    void setCoreAttr(u16, u16) override {}
    u16 getCoreAttr(u16) override { return 0; }
};

struct Ent {
    u16 type, id, sub;
    std::vector<u8> data;
};
static std::vector<u8> makeSrsc(const std::vector<Ent>& es) {
    std::vector<u8> b = {'S', 'R', 'S', 'C', 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<std::array<u32, 3>> loc;
    for (auto& e : es) {
        while (b.size() & 7)
            b.push_back(0);
        loc.push_back({(u32)b.size(), (u32)e.data.size(), 0});
        b.insert(b.end(), e.data.begin(), e.data.end());
    }
    u32 idx = (u32)b.size(), cnt = (u32)es.size();
    memcpy(&b[6], &idx, 4);
    memcpy(&b[10], &cnt, 4);
    for (size_t i = 0; i < es.size(); ++i) {
        u8 ent[14];
        memcpy(ent, &es[i].type, 2);
        memcpy(ent + 2, &es[i].id, 2);
        memcpy(ent + 4, &es[i].sub, 2);
        memcpy(ent + 6, &loc[i][0], 4);
        memcpy(ent + 10, &loc[i][1], 4);
        b.insert(b.end(), ent, ent + 14);
    }
    return b;
}
static void writeFile(const fs::path& p, const std::vector<u8>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream o(p, std::ios::binary);
    o.write((const char*)d.data(), d.size());
}
static std::vector<u8> bytes(const std::string& s) {
    return std::vector<u8>(s.begin(), s.end());
}

int main() {
    setVerbose(false);
    fs::path root = fs::temp_directory_path() / "drakan_override_test";
    fs::remove_all(root);
    fs::path game = root / "game", ovr = root / "overrides";

    // text entries: id 1 = "Jump" + translations, id 2 = "Press \033 to continue.", id 3 = not a text layout
    auto text = [](std::vector<std::string> s) { return xdb::encode(s); };
    std::vector<u8> t1 = text({"Jump", "Springen", "Sauter", "Salta", "Saltar"});
    std::vector<u8> t2 = text({"Press \\033 to continue.", "Weiter mit \\033", "", "", ""});
    std::vector<u8> bad = {1, 2, 3, 4, 5, 6, 7};
    std::vector<u8> tex(100, 0x11);
    writeFile(game / "ENGLISH/COMMON/T/T.XDB", makeSrsc({{0x400, 1, 2, t1}, {0x400, 2, 2, t2}, {0x400, 3, 2, bad}}));
    writeFile(game / "ENGLISH/COMMON/T/T.TXD", makeSrsc({{0x40, 5, 0, tex}, {0x40, 6, 0, tex}}));

    // ---- codec
    {
        std::vector<std::string> s;
        CHECK(xdb::decode(t1, s) && s.size() == 8 && s[0] == "Jump" && s[4] == "Saltar" && s[5].empty());
        CHECK(xdb::encode(s) == t1);
        CHECK(!xdb::decode(bad, s));
        std::vector<u8> t = t1;
        t.push_back(0);
        CHECK(!xdb::decode(t, s)); // trailing garbage
        // length rule: 10 chars -> len 10 (no NUL), 11 chars -> 14
        auto e = xdb::encode({"Loading...", "Cargando..."});
        CHECK(e[0] == 10 && e[1] == 0 && e[2 + 10] == 14);
        CHECK(xdb::langFromName("de") == 1 && xdb::langFromName("*") == -1 && xdb::langFromName("xx") == -2);
    }

    // ---- override folder files
    writeFile(ovr / "T.TXD/0040_5_0.bin", std::vector<u8>(300, 0x77)); // replace tex 5 by a bigger blob
    writeFile(ovr / "t.xdb/text.ini",
              bytes("; comment\r\n1 = Alle\r\n1.en = Leap\r\n2.de = Weiter mit Leertaste\\n2\r\nbogus line\r\n"));
    DirectoryOverride dir;
    CHECK(dir.load((ovr).string()) == 1);
    auto xt = std::make_shared<XdbTextOverride>();
    CHECK(xt->loadDirectory(ovr.string()) == 3);
    xt->addRule([](const ResourceId& id, int lang, std::string& t) {
        if (id.id != 2 || lang != 0)
            return false;
        size_t p = t.find("\\033");
        if (p == std::string::npos)
            return false;
        t.replace(p, 4, "Enter");
        return true;
    });

    FakeMem mem;
    FakeEe ee;
    FakeSpu spu;
    IsoFs ifs;
    std::string err;
    CHECK(ifs.openFolder(game.string(), &err));
    ifs.setLanguageRoot("\\ENGLISH");
    RsrcIop iop(ifs, mem, ee, spu);
    iop.res.setDelivery(ResourceService::Delivery::Direct);
    auto open = [&](const char* path) {
        u8 req[0x10c] = {};
        strncpy((char*)req + 8, path, 0xff);
        s32 r[2] = {};
        iop.call(SID_FILE, 0, req, sizeof req, r, 8);
        return r[0];
    };
    auto request = [&](s32 h, u16 type, u16 id, u16 sub, u32 off, u32 len, u32 dest) {
        ResourceRequestWire q{};
        q.cookie = 1;
        q.type = type;
        q.id = id;
        q.sub = sub;
        q.offset = off;
        q.length = len;
        q.dest = dest;
        iop.call(SID_RESOURCE, h, &q, 28, nullptr, 0);
    };
    s32 hx = open("COMMON\\T\\T.XDB"), ht = open("COMMON\\T\\T.TXD");
    CHECK(hx >= 0 && ht >= 0);
    auto readBack = [&](u32 addr, size_t n) {
        std::vector<u8> v(n);
        mem.read(addr, v.data(), n);
        return v;
    };

    // no override registered: untouched
    request(hx, 0x400, 1, 2, 0, 0xFFFFFFFF, 0x100000);
    CHECK(readBack(0x100000, t1.size()) == t1);

    iop.res.addOverride(std::make_shared<DirectoryOverride>(dir));
    iop.res.addOverride(xt);

    // whole-entry replacement (bigger than the original)
    request(ht, 0x40, 5, 0, 0, 0xFFFFFFFF, 0x110000);
    CHECK(readBack(0x110000, 300) == std::vector<u8>(300, 0x77));
    // offset / length are applied to the replacement
    request(ht, 0x40, 5, 0, 290, 100, 0x120000);
    CHECK(readBack(0x120000, 10) == std::vector<u8>(10, 0x77) && mem.ram[0x120000 + 10] == 0);
    // wildcard sub resolves to the entry's own sub
    request(ht, 0x40, 5, 0xFFFF, 0, 4, 0x130000);
    CHECK(readBack(0x130000, 4) == std::vector<u8>(4, 0x77));
    // other texture not overridden
    request(ht, 0x40, 6, 0, 0, 0xFFFFFFFF, 0x140000);
    CHECK(readBack(0x140000, 100) == tex);

    // XDB: file patch + rule
    {
        request(hx, 0x400, 1, 2, 0, 0xFFFFFFFF, 0x150000);
        std::vector<std::string> s;
        std::vector<u8> got = readBack(0x150000, xdb::encode({"Leap", "Alle", "Alle", "Alle", "Alle"}).size());
        CHECK(xdb::decode(got, s) && s[0] == "Leap" && s[1] == "Alle" && s[4] == "Alle");
        request(hx, 0x400, 2, 2, 0, 0xFFFFFFFF, 0x160000);
        got =
            readBack(0x160000, xdb::encode({"Press Enter to continue.", "Weiter mit Leertaste\n2", "", "", ""}).size());
        CHECK(xdb::decode(got, s) && s[0] == "Press Enter to continue." && s[1] == "Weiter mit Leertaste\n2" &&
              s[2].empty());
        // untouched non-text layout stays byte identical
        request(hx, 0x400, 3, 2, 0, 0xFFFFFFFF, 0x170000);
        CHECK(readBack(0x170000, bad.size()) == bad);
    }

    // ---- start-level redirect (DRAKAN_START_LEVEL): folder and file stem, any case, other paths untouched
    {
        writeFile(game / "ENGLISH/LEVELS/LEVEL4LD/LEVEL4LD.LVL", std::vector<u8>(32, 0x44));
        IsoFs lfs;
        CHECK(lfs.openFolder(game.string()));
        lfs.setLanguageRoot("\\ENGLISH");
        FakeMem lmem;
        FileService files(lfs, lmem);
        const std::string surdana = "cdrom0:/English/Levels/Level 1A - Surdana/Level 1A - Surdana.lvl";
        CHECK(files.redirectPath(surdana) == surdana);
        files.setLevelRedirect("Level 1A - Surdana", "Level 4A - Ravenshold");
        CHECK(files.redirectPath(surdana) == "cdrom0:/English/Levels/Level 4A - Ravenshold/Level 4A - Ravenshold.lvl");
        CHECK(files.redirectPath("cdrom0:/ENGLISH/LEVELS/LEVEL 1A - SURDANA/x.db") ==
              "cdrom0:/ENGLISH/LEVELS/Level 4A - Ravenshold/x.db");
        CHECK(files.redirectPath("cdrom0:/English/Levels/Level 0 - Intro/Level 0 - Intro.lvl") ==
              "cdrom0:/English/Levels/Level 0 - Intro/Level 0 - Intro.lvl");
        CHECK(files.redirectPath("cdrom0:\\English\\Levels\\Level 1A - Surdana\\Level 1A - Surdana_splats.db") ==
              "cdrom0:\\English\\Levels\\Level 4A - Ravenshold\\Level 4A - Ravenshold_splats.db");
        // a database named after the redirected level in another level's folder (intro-template packs) stays
        CHECK(files.redirectPath("cdrom0:/English/Levels/Level 4A - Ravenshold/Level 1A - Surdana.odb") ==
              "cdrom0:/English/Levels/Level 4A - Ravenshold/Level 1A - Surdana.odb");
        CHECK(files.redirectPath("cdrom0:/English/Levels/Level 1A - Surdana.lvl") ==
              "cdrom0:/English/Levels/Level 1A - Surdana.lvl");
        // the open itself resolves the redirected (mangled) file
        std::vector<u8> req(8 + surdana.size() + 1, 0);
        memcpy(&req[8], surdana.data(), surdana.size());
        s32 reply[2] = {-99, 0};
        CHECK(files.handle(0, req.data(), (u32)req.size(), reply, sizeof reply) == 8 && reply[0] >= 0 &&
              reply[1] == 32);
        files.setLevelRedirect("", "");
        CHECK(files.redirectPath(surdana) == surdana);
    }

    fs::remove_all(root);
    // ---- campaign credits: served only while the pack's campaign is the current game, only for the two credits
    // entries
    {
        std::string active;
        CampaignCreditsOverride cc;
        cc.add("ootf-levels", "=Drakan: Order of the Flame\r\nTest\r\n");
        cc.setActive([&] { return active; });
        ResourceId first{"DRAKAN2.RRC", "ENGLISH\\DRAKAN2.RRC", 0x401, 8807, 0};
        ResourceId second = first;
        second.id = 8808;
        ResourceId other = first;
        other.id = 8809;
        ResourceId wrongFile = first;
        wrongFile.file = "INTERFCE.XDB";
        CHECK(!cc.wants(first)); // the disc game: nothing served
        active = "someone-else";
        CHECK(!cc.wants(first));
        active = "ootf-levels";
        CHECK(cc.wants(first) && cc.wants(second) && !cc.wants(other) && !cc.wants(wrongFile));
        std::vector<u8> d(10, 1);
        CHECK(cc.apply(first, d) && std::string(d.begin(), d.end()) == "=Drakan: Order of the Flame\r\nTest\r\n");
        CHECK(cc.apply(second, d) && std::string(d.begin(), d.end()) == "\r\n");
        active = "";
        CHECK(!cc.wants(first));
    }
    // ---- campaign title music: the disc's title track is served from the campaign's track while it is the current
    // game; the choice is latched at the descriptor so one play never mixes the two tracks
    {
        std::string active;
        CampaignMusicRedirect mr;
        const std::string music = "ENGLISH\\MUSIC\\PS2MUSIC\\PS2MUSIC.SDB";
        constexpr u16 disc = CampaignMusicRedirect::kDiscTitleMusic;
        CHECK(mr.map(music, 0x302, disc) == disc); // no campaign track registered
        mr.add("ootf-levels", 4);
        mr.setActive([&] { return active; });
        CHECK(mr.map(music, 0x302, disc) == disc && mr.map(music, 0x304, disc) == disc); // the disc game
        active = "ootf-levels";
        CHECK(mr.map(music, 0x304, disc) == 4); // a disc play switches to the campaign's track at its next chunk
        CHECK(mr.map(music, 0x302, disc) == 4 && mr.map(music, 0x304, disc) == 4 && mr.map(music, 0x305, disc) == disc);
        CHECK(mr.map(music, 0x304, 88) == 88);                                    // other tracks untouched
        CHECK(mr.map("ENGLISH\\COMMON\\SOUNDS\\SOUND.SDB", 0x302, disc) == disc); // other files untouched
        CHECK(mr.map(music, 0x400, disc) == disc);                                // other entry types untouched
        active = "";
        CHECK(mr.map(music, 0x304, disc) == 4); // still the campaign's play until the next descriptor read
        CHECK(mr.map(music, 0x302, disc) == disc && mr.map(music, 0x304, disc) == disc);
        CHECK(mr.wanted() == disc);
    }
    // a campaign's music cue replaces the playing loop: the older track's chunks are served silent
    {
        bool on = false;
        CampaignMusicDucker md;
        md.setActive([&] { return on; });
        const std::string music = "ENGLISH\MUSIC\PS2MUSIC\PS2MUSIC.SDB";
        CHECK(!md.silence(music, 0x302, 8, 0) && !md.silence(music, 0x304, 8, 0)); // Drakan 2: nothing changes
        on = true;
        CHECK(!md.silence(music, 0x302, 8, 0) && !md.silence(music, 0x304, 8, 0) &&
              !md.silence(music, 0x304, 8, 163840));
        CHECK(!md.silence(music, 0x302, 6, 0)); // the cue's descriptor: the loop is replaced
        CHECK(md.silence(music, 0x304, 8, 245760) && !md.silence(music, 0x304, 6, 0) &&
              !md.silence(music, 0x304, 6, 163840));
        CHECK(md.silence(music, 0x304, 8, 0));                              // a wrapping loop with no gap stays silent
        CHECK(!md.silence("ENGLISH\COMMON\SOUNDS\SOUND.SDB", 0x304, 8, 0)); // other files untouched
        std::vector<u8> blocks(32, 0xff);
        CampaignMusicDucker::silenceAdpcm(blocks);
        CHECK(blocks[0] == 0 && blocks[1] == 0xff && blocks[2] == 0 && blocks[15] == 0 && blocks[16] == 0 &&
              blocks[17] == 0xff);
    }
    // ---- campaign credits through plain file reads: the overlay rewrites the two entries inside the opened RRC, at
    // read time
    {
        std::vector<u8> first(120, 'a'), second(60, 'b'), other(40, 'c');
        writeFile(game / "ENGLISH/DRAKAN2.RRC",
                  makeSrsc({{0x401, 8807, 0, first}, {0x401, 8808, 0, second}, {0x401, 9000, 0, other}}));
        IsoFs rrc;
        std::string e2;
        CHECK(rrc.openFolder(game.string(), &e2));
        rrc.setLanguageRoot("\\ENGLISH");
        IsoFs::Node node;
        CHECK(rrc.lookup("\\ENGLISH\\DRAKAN2.RRC", node) || rrc.lookup("ENGLISH\\DRAKAN2.RRC", node));
        std::string active;
        CampaignCreditsOverride cc;
        cc.add("ootf-levels", "=Drakan: Order of the Flame\r\nTest\r\n");
        cc.setActive([&] { return active; });
        std::vector<u8> all(node.size);
        CHECK(rrc.read(node, 0, all.data(), all.size()) == all.size());
        const std::vector<u8> orig = all;
        cc.patchRead(rrc, node, 0, all.data(), all.size());
        CHECK(all == orig); // the disc game: untouched
        active = "ootf-levels";
        cc.patchRead(rrc, node, 0, all.data(), all.size());
        auto find = [&](const std::vector<u8>& v, u8 c) { return std::find(v.begin(), v.end(), c) - v.begin(); };
        const size_t at = find(orig, 'a'), at2 = find(orig, 'b'), at3 = find(orig, 'c');
        CHECK(std::string(all.begin() + at, all.begin() + at + 7) == "=Drakan");
        CHECK(all[at + 119] == 10 && all[at + 118] == 13); // padded with spaces, ends with a line break
        CHECK(all[at2] == ' ' && all[at2 + 59] == 10);     // the second entry is one blank line
        CHECK(std::vector<u8>(all.begin() + at3, all.begin() + at3 + 40) == other); // other entries untouched
        // a read that covers only the middle of the first entry gets the matching bytes
        std::vector<u8> part(orig.begin() + at + 10, orig.begin() + at + 30);
        cc.patchRead(rrc, node, at + 10, part.data(), part.size());
        CHECK(part == std::vector<u8>(all.begin() + at + 10, all.begin() + at + 30));
    }
    // --- character LOD fix: the fat villagers' last LOD is never selected, other records stay as they are -------
    {
        // A 0x0208 record as far as the fix reads it: 64 bytes of bounds, the LOD count, filler standing in for the
        // skeleton, then `count` 30-byte LOD entries.
        auto record = [](const std::vector<std::array<u32, 2>>& lods, const std::vector<float>& dist, size_t filler) {
            std::vector<u8> d(66 + filler, 0xEE);
            d[64] = (u8)lods.size();
            d[65] = 0;
            u32 fv = 0, fp = 0;
            for (size_t l = 0; l < lods.size(); ++l) {
                u8 e[30] = {1, 0};
                const u32 usage = 0, node = (u32)l;
                memcpy(e + 2, &dist[l], 4);
                memcpy(e + 6, &usage, 4);
                memcpy(e + 10, &node, 4);
                memcpy(e + 14, &fv, 4);
                memcpy(e + 18, &lods[l][0], 4);
                memcpy(e + 22, &fp, 4);
                memcpy(e + 26, &lods[l][1], 4);
                d.insert(d.end(), e, e + 30);
                fv += lods[l][0];
                fp += lods[l][1];
            }
            d.insert(d.end(), 40, 0x77); // clip list and spheres follow the table
            return d;
        };
        auto distance = [](const std::vector<u8>& d, size_t table, int lod) {
            float f;
            memcpy(&f, &d[table + lod * 30 + 2], 4);
            return f;
        };
        CharacterLodFixOverride fix;
        ResourceId rid;
        rid.file = "NPCS.MOD";
        rid.type = 0x0208;
        rid.id = 4893;
        CHECK(fix.wants(rid));
        ResourceId other = rid;
        other.file = "SYSTEM.MOD";
        CHECK(!fix.wants(other));
        other = rid;
        other.type = 0x0204;
        CHECK(!fix.wants(other));

        // the broken mesh (odd filler: the table is unaligned on the disc)
        std::vector<u8> big = record({{996, 1928}, {452, 882}, {220, 440}}, {0.0f, 6000.0f, 12000.0f}, 123);
        const std::vector<u8> bigOrig = big;
        CHECK(fix.apply(rid, big));
        CHECK(big.size() == bigOrig.size());
        CHECK(distance(big, 66 + 123, 0) == 0.0f && distance(big, 66 + 123, 1) == 6000.0f);
        CHECK(distance(big, 66 + 123, 2) == CharacterLodFixOverride::kNever);
        {
            std::vector<u8> expect = bigOrig; // only the four distance bytes of LOD 2 differ
            memcpy(&expect[66 + 123 + 2 * 30 + 2], &CharacterLodFixOverride::kNever, 4);
            CHECK(big == expect);
        }
        CHECK(!fix.apply(rid, big)); // already patched (or patched by a mod pack): left alone

        // a healthy three-LOD villager, a two-LOD model and a truncated record are not touched
        std::vector<u8> medium = record({{1056, 2048}, {424, 842}, {108, 198}}, {0.0f, 6000.0f, 12000.0f}, 77);
        const std::vector<u8> mediumOrig = medium;
        CHECK(!fix.apply(rid, medium) && medium == mediumOrig);
        std::vector<u8> two = record({{996, 1928}, {452, 882}}, {0.0f, 6000.0f}, 50);
        const std::vector<u8> twoOrig = two;
        CHECK(!fix.apply(rid, two) && two == twoOrig);
        std::vector<u8> cut(bigOrig.begin(), bigOrig.begin() + 66 + 123 + 45);
        const std::vector<u8> cutOrig = cut;
        CHECK(!fix.apply(rid, cut) && cut == cutOrig);
        std::vector<u8> tiny(10, 0);
        CHECK(!fix.apply(rid, tiny));
    }
    printf("override_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
