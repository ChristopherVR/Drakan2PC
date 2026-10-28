// override_test: self-contained test of the resource override layer (no ISO needed; builds a tiny folder game tree in a
// temp dir).
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
    printf("override_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
