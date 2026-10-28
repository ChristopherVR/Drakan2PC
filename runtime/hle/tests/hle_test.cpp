// hle_test <iso> <expected.txt>   (expected.txt from tests/gen_expected.py)
#include "hle/RsrcIop.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

using namespace hle;

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
#define CHECKM(c, ...)                                                                                                 \
    do {                                                                                                               \
        if (c)                                                                                                         \
            ++g_pass;                                                                                                  \
        else {                                                                                                         \
            ++g_fail;                                                                                                  \
            printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c);                                                    \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

// ---- tiny SHA1
static std::string sha1(const u8* d, size_t n) {
    u32 h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::vector<u8> m(d, d + n);
    m.push_back(0x80);
    while (m.size() % 64 != 56)
        m.push_back(0);
    u64 bits = (u64)n * 8;
    for (int i = 7; i >= 0; --i)
        m.push_back((u8)(bits >> (i * 8)));
    auto rol = [](u32 x, int s) { return (x << s) | (x >> (32 - s)); };
    for (size_t o = 0; o < m.size(); o += 64) {
        u32 w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (m[o + i * 4] << 24) | (m[o + i * 4 + 1] << 16) | (m[o + i * 4 + 2] << 8) | m[o + i * 4 + 3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        u32 a = h[0], b = h[1], c = h[2], dd = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            u32 f, k;
            if (i < 20) {
                f = (b & c) | (~b & dd);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ dd;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & dd) | (c & dd);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ dd;
                k = 0xCA62C1D6;
            }
            u32 t = rol(a, 5) + f + e + k + w[i];
            e = dd;
            dd = c;
            c = rol(b, 30);
            b = a;
            a = t;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += dd;
        h[4] += e;
    }
    char buf[48];
    snprintf(buf, sizeof buf, "%08x%08x%08x%08x%08x", h[0], h[1], h[2], h[3], h[4]);
    return buf;
}

// ---- fakes
struct FakeMem : IEeMemory {
    std::vector<u8> ram = std::vector<u8>(32u << 20);
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
struct Post {
    u32 sid, fno;
    std::vector<u8> payload;
    bool blocking;
};
struct FakeEe : IEeRpcPoster {
    std::vector<Post> posts;
    u32 allocAddr = 0x01000000;
    int post(u32 sid, u32 fno, const void* p, u32 n, void* reply, u32 cap, bool blocking) override {
        posts.push_back({sid, fno, std::vector<u8>((const u8*)p, (const u8*)p + n), blocking});
        if (sid == SID_EE_ALLOC && reply && cap >= 16) {
            u32 r[4] = {0, allocAddr, 0, 0};
            memcpy(reply, r, 16);
            return 16;
        }
        return 0;
    }
};
struct FakeSpu : ISpu2 {
    std::vector<u8> ram = std::vector<u8>(2u << 20);
    std::vector<std::string> calls;
    void ramWrite(u32 a, const void* s, u32 n) override { memcpy(&ram[a], s, n); }
    void ramRead(u32 a, void* d, u32 n) override { memcpy(d, &ram[a], n); }
    void setParam(u16 e, u16 v) override {
        char b[64];
        snprintf(b, 64, "P%04x=%04x", e, v);
        calls.push_back(b);
    }
    u16 getParam(u16) override { return 0; }
    void setSwitch(u16 e, u32 v) override {
        char b[64];
        snprintf(b, 64, "S%04x=%08x", e, v);
        calls.push_back(b);
    }
    u32 getSwitch(u16) override { return 0; }
    void setAddr(u16 e, u32 v) override {
        char b[64];
        snprintf(b, 64, "A%04x=%08x", e, v);
        calls.push_back(b);
    }
    u32 getAddr(u16) override { return 0; }
    void setCoreAttr(u16 e, u16 v) override {
        char b[64];
        snprintf(b, 64, "C%04x=%04x", e, v);
        calls.push_back(b);
    }
    u16 getCoreAttr(u16) override { return 0; }
};

static std::vector<std::string> split(const std::string& s, char c) {
    std::vector<std::string> v;
    std::string cur;
    std::istringstream is(s);
    while (std::getline(is, cur, c))
        v.push_back(cur);
    return v;
}

struct Rig {
    FakeMem mem;
    FakeEe ee;
    FakeSpu spu;
    IsoFs fs;
    RsrcIop* iop = nullptr;
    ~Rig() { delete iop; }
    s32 open(const std::string& path, u32* size = nullptr) {
        u8 req[0x10c] = {};
        strncpy((char*)req + 8, path.c_str(), 0xff);
        s32 r[2] = {};
        int n = iop->call(SID_FILE, 0, req, sizeof req, r, 8);
        CHECK(n == 8);
        if (size)
            *size = (u32)r[1];
        return r[0];
    }
    s32 simple(u32 fno, u32 a, u32 b, u32 c, u32 sz = 12) {
        u32 req[3] = {a, b, c};
        s32 r = 0;
        int n = iop->call(SID_FILE, fno, req, sz, &r, 4);
        CHECK(n == 4);
        return r;
    }
};

int main(int argc, char** argv) {
    const char* envIso = std::getenv("DRAKAN_ISO");
    std::string isoPath = argc > 1 ? argv[1] : envIso ? envIso : "";
    // HLE_TEST_EXPECTED is set by CMake to tests/expected.txt, so the CWD does not matter.
    std::string expPath = argc > 2 ? argv[2] : HLE_TEST_EXPECTED;
    if (isoPath.empty() || !std::ifstream(isoPath, std::ios::binary)) {
        printf("SKIP: no disc image (pass <iso> or set DRAKAN_ISO)\n");
        return 77; // ctest SKIP_RETURN_CODE
    }
    setVerbose(false);

    Rig R;
    std::string err;
    if (!R.fs.openIso(isoPath, &err)) {
        printf("cannot open ISO: %s\n", err.c_str());
        return 2;
    }
    R.fs.setLanguageRoot("\\ENGLISH");
    R.iop = new RsrcIop(R.fs, R.mem, R.ee, R.spu);
    R.iop->res.setDelivery(ResourceService::Delivery::Direct); // the library default is Rpc1301; most checks below read
                                                               // the data back directly
    printf("ISO entries: %zu\n", R.fs.entries().size());
    CHECK(R.fs.entries().size() > 1000); // 926 files + 177 dirs ~ 1103

    std::ifstream ef(expPath);
    if (!ef) {
        printf("cannot open %s\n", expPath.c_str());
        return 2;
    }
    std::string line;
    std::vector<std::vector<std::string>> exp;
    while (std::getline(ef, line)) {
        if (!line.empty())
            exp.push_back(split(line, '\t'));
    }

    // ---- 1. mangling vs Python
    printf("[mangle]\n");
    int nm = 0;
    for (auto& e : exp)
        if (e[0] == "MANGLE" && e[2] != "SKIP") {
            std::string key = R.fs.resolveKey(e[1]);
            std::string want = e[2];
            if (want.rfind("ENGLISH\\", 0) != 0)
                want = "ENGLISH\\" + want;
            CHECKM(key == want, "%s -> %s (want %s)", e[1].c_str(), key.c_str(), want.c_str());
            ++nm;
        }
    CHECK(mangleComponent("Level 5 - Shiverbane") == "LEVEL5NE");
    CHECK(mangleComponent("Interface") == "INTERFCE");
    CHECK(mangleComponent("Mountain World Common") == "MOUNTAON");
    CHECK(mangleComponent("Drakan 2.rrc") == "DRAKAN2.RRC");
    // every LEVELLST entry resolves to a real ISO file
    {
        IsoFs::Node n;
        CHECK(R.fs.lookup("LevelList.txt", n));
        auto txt = R.fs.readAll(n);
        std::istringstream is(std::string(txt.begin(), txt.end()));
        int cnt = 0;
        while (std::getline(is, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty())
                continue;
            IsoFs::Node f;
            bool ok = R.fs.lookup(line, f);
            CHECKM(ok && !f.dir, "LEVELLST path not found: %s -> %s", line.c_str(), R.fs.resolveKey(line).c_str());
            ++cnt;
        }
        printf("  %d LEVELLST paths resolved, %d mangle vectors\n", cnt, nm);
        CHECK(cnt == 14);
    }
    // path forms
    {
        IsoFs::Node a, b, c, d;
        CHECK(R.fs.lookup("cdrom0:\\COMMON\\INTERFCE\\INTERFCE.TXD;1", a));
        CHECK(R.fs.lookup("\\Common\\Interface\\Interface.txd", b));
        CHECK(R.fs.lookup("ENGLISH\\COMMON\\INTERFCE\\INTERFCE.TXD", c));
        CHECK(R.fs.lookup("/common/interface/interface.txd", d));
        CHECK(a.lba == b.lba && b.lba == c.lba && c.lba == d.lba && a.size == 5607812);
        IsoFs::Node ioprp;
        CHECK(R.fs.lookup("\\IOP\\IOPRP23.IMG", ioprp) && ioprp.size == 230329); // language-less fallback
        IsoFs::Node dep;                                                         // DB dependency style with ..
        CHECK(R.fs.lookup("\\Levels\\Level 5 - Shiverbane\\..\\..\\Common\\Interface\\Interface.txd", dep));
    }

    // ---- 2. File RPC vs Python hashes
    printf("[file rpc]\n");
    for (auto& e : exp)
        if (e[0] == "FILE") {
            u32 size = 0;
            s32 h = R.open(e[1], &size);
            CHECKM(h >= 0, "open %s -> %d", e[1].c_str(), h);
            if (h < 0)
                continue;
            CHECKM(size == (u32)std::stoul(e[2]), "size %u vs %s", size, e[2].c_str());
            u32 want = std::min<u32>(size, 4u << 20);
            u32 got = 0;
            std::vector<u8> all;
            while (got < want) {
                u32 n = std::min<u32>(want - got, 0x10000);
                s32 r = R.simple(2, h, 0x00100000, n); // always dest 0x100000, like a streaming buffer
                if (r <= 0)
                    break;
                std::vector<u8> t(r);
                R.mem.read(0x00100000, t.data(), r);
                all.insert(all.end(), t.begin(), t.end());
                got += r;
            }
            CHECKM(sha1(all.data(), all.size()) == e[3], "sha1 mismatch %s", e[1].c_str());
            // lseek semantic + read at end
            CHECK(R.simple(4, h, 0, 0) == 0);
            CHECK(R.simple(4, h, 100, 0) == 100);
            CHECK(R.simple(4, h, 20, 1) == 120);
            CHECKM(R.simple(4, h, 16, 2) == (s32)size - 16, "whence 2 quirk");
            CHECK(R.simple(4, h, 0, 2) == (s32)size);
            CHECK(R.simple(2, h, 0x100000, 100) == 0); // at EOF
            CHECK(R.simple(1, h, 0, 0, 4) == 0);
            CHECK(R.simple(1, h, 0, 0, 4) == -4); // double close
        }
    CHECK(R.open("\\Common\\Nope\\Nothing.txd") == -1); // measured: not found is -1
    { // short open record (8 + strlen + 1 bytes) with a 4-byte reply (what the game's resource opener sends)
        const char* p = "cdrom0:/English/Common/Interface/Interface.odb";
        std::vector<u8> req(8 + strlen(p) + 1, 0);
        memcpy(&req[8], p, strlen(p));
        s32 hnd = -99;
        int n = R.iop->call(SID_FILE, 0, req.data(), (u32)req.size(), &hnd, 4);
        CHECK(n == 4 && hnd >= 0);
        if (hnd >= 0)
            R.simple(1, (u32)hnd, 0, 0, 4);
    }
    CHECK(R.simple(2, 99, 0x100000, 16) == -4);
    // handle exhaustion
    {
        std::vector<s32> hs;
        s32 h;
        while ((h = R.open("LevelList.txt")) >= 0) {
            hs.push_back(h);
            if (hs.size() > 100)
                break;
        }
        CHECK(hs.size() == 64 && h == -7);
        for (auto x : hs)
            R.simple(1, x, 0, 0, 4);
    }

    // ---- 3. SRSC index + Resource Request
    printf("[resource rpc]\n");
    std::set<std::string> srscDone;
    for (auto& e : exp)
        if (e[0] == "SRSC") {
            s32 h = R.open(e[1]);
            CHECK(h >= 0);
            FileService::Open o;
            CHECK(R.iop->file.get(h, o));
            CHECK(o.srsc.valid && o.srsc.indexOffset == (u32)std::stoul(e[2]) && o.srsc.count == (u32)std::stoul(e[3]));
            std::vector<u8> idx;
            for (auto& s : o.srsc.entries) {
                u8 b[14];
                memcpy(b, &s.type, 2);
                memcpy(b + 2, &s.id, 2);
                memcpy(b + 4, &s.sub, 2);
                memcpy(b + 6, &s.off, 4);
                memcpy(b + 10, &s.size, 4);
                idx.insert(idx.end(), b, b + 14);
            }
            CHECKM(sha1(idx.data(), idx.size()) == e[4], "index sha1 %s", e[1].c_str());
            printf("  %s: %u index entries OK\n", e[1].c_str(), o.srsc.count);
            // resource requests for this file's RES lines: 3 in one batch, dest type 0 at distinct EE addrs
            std::vector<std::vector<std::string>> rs;
            for (auto& r : exp)
                if (r[0] == "RES" && r[1] == e[1])
                    rs.push_back(r);
            R.ee.posts.clear();
            std::vector<ResourceRequestWire> w;
            for (size_t i = 0; i < rs.size(); ++i) {
                ResourceRequestWire q{};
                q.cookie = 0x00500000 + (u32)i * 16;
                q.flags = 0;
                q.type = (u16)std::stoul(rs[i][2]);
                q.id = (u16)std::stoul(rs[i][3]);
                q.sub = (u16)std::stoi(rs[i][4]);
                q.offset = 0;
                q.length = 0x40000;
                q.dest = 0x00200000 + (u32)i * 0x100000;
                w.push_back(q);
            }
            R.iop->call(SID_RESOURCE, h, w.data(), (u32)(w.size() * 28), nullptr, 0);
            CHECK(R.ee.posts.size() == 1 && R.ee.posts[0].sid == SID_EE_DONE && R.ee.posts[0].fno == w.size() &&
                  R.ee.posts[0].payload.size() == w.size() * 8);
            for (size_t i = 0; i < rs.size(); ++i) {
                u32 cookie, st;
                memcpy(&cookie, &R.ee.posts[0].payload[i * 8], 4);
                memcpy(&st, &R.ee.posts[0].payload[i * 8 + 4], 4);
                u32 sz = (u32)std::stoul(rs[i][5]);
                CHECKM(cookie == w[i].cookie && st == 0, "completion %zu cookie %08x st %d", i, cookie, (int)st);
                u32 n = std::min<u32>(sz, 65536);
                std::vector<u8> t(n);
                R.mem.read(w[i].dest, t.data(), n);
                CHECKM(sha1(t.data(), n) == rs[i][6], "resource data sha1 %s %s/%s", e[1].c_str(), rs[i][2].c_str(),
                       rs[i][3].c_str());
            }
            // error statuses: not found; bad handle; too big (if any entry > MAX_LEN)
            R.ee.posts.clear();
            ResourceRequestWire bad = w[0];
            bad.type = 0x7fff;
            bad.id = 0x7fff;
            R.iop->call(SID_RESOURCE, h, &bad, 28, nullptr, 0);
            {
                u32 st;
                memcpy(&st, &R.ee.posts[0].payload[4], 4);
                CHECK((s32)st == -1);
            }
            R.ee.posts.clear();
            R.iop->call(SID_RESOURCE, 77, &bad, 28, nullptr, 0);
            {
                u32 st;
                memcpy(&st, &R.ee.posts[0].payload[4], 4);
                CHECK((s32)st == -4);
            }
            for (auto& s : o.srsc.entries)
                if (s.size > ResourceService::MAX_LEN) {
                    ResourceRequestWire big = w[0];
                    big.type = s.type;
                    big.id = s.id;
                    big.sub = s.sub;
                    big.length = s.size;
                    R.ee.posts.clear();
                    R.iop->call(SID_RESOURCE, h, &big, 28, nullptr, 0);
                    u32 st;
                    memcpy(&st, &R.ee.posts[0].payload[4], 4);
                    CHECK((s32)st == -9);
                    printf("  too-big (-9) verified on %04x/%u (%u bytes)\n", s.type, s.id, s.size);
                    break;
                }
            // partial: offset + length inside an entry
            if (!rs.empty() && std::stoul(rs[1 % rs.size()][5]) > 300) {
                auto& r = rs[1 % rs.size()];
                ResourceRequestWire q{};
                q.cookie = 0x123450;
                q.type = (u16)std::stoul(r[2]);
                q.id = (u16)std::stoul(r[3]);
                q.sub = (u16)std::stoi(r[4]);
                q.offset = 100;
                q.length = 200;
                q.dest = 0x00900000;
                R.ee.posts.clear();
                R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
                // compare with full-read of same entry via the file service
                const SrscEntry* se = o.srsc.find(q.type, q.id, q.sub);
                std::vector<u8> ref(200);
                R.fs.read(o.node, (u64)se->off + 100, ref.data(), 200);
                std::vector<u8> got(200);
                R.mem.read(0x00900000, got.data(), 200);
                CHECK(ref == got);
                u8 guard;
                R.mem.read(0x00900000 + 200, &guard, 1);
                CHECK(guard == 0);
            }
            // 0x80 flag: EE allocates (0x1303), data lands at the returned address
            {
                ResourceRequestWire q = w[0];
                q.flags = 0x80;
                q.dest = 0;
                q.cookie = 0x00ABCDE0;
                R.ee.posts.clear();
                R.ee.allocAddr = 0x01100000;
                R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
                CHECK(R.ee.posts.size() == 2 && R.ee.posts[0].sid == SID_EE_ALLOC && R.ee.posts[0].fno == q.cookie &&
                      R.ee.posts[0].blocking);
                const SrscEntry* se = o.srsc.find(q.type, q.id, q.sub);
                u32 want = std::min<u32>(se->size, q.length);
                u32 p0;
                memcpy(&p0, R.ee.posts[0].payload.data(), 4);
                CHECK(p0 == ((want + 3) & ~3u));
                std::vector<u8> ref(want), got(want);
                R.fs.read(o.node, se->off, ref.data(), want);
                R.mem.read(0x01100000, got.data(), want);
                CHECK(ref == got);
            }
            // alloc flag with a non-zero dest: the allocated EE address is stored at dest (measured: movie frame slot
            // descriptors)
            {
                ResourceRequestWire q = w[0];
                q.flags = 0x80;
                q.dest = 0x00C00000;
                q.cookie = 0x00ABCDF0;
                R.ee.posts.clear();
                R.ee.allocAddr = 0x01200000;
                R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
                u32 stored = 0;
                R.mem.read(0x00C00000, &stored, 4);
                CHECK(stored == 0x01200000);
            }
            // sub == 0xFFFF means "any instance" and id/type are in the measured wire order (id at +8, type at +0x0a)
            {
                ResourceRequestWire q = w[0];
                q.sub = 0xFFFF;
                q.cookie = 0x00ABCE00;
                q.flags = 0;
                q.dest = 0x00D00000;
                R.ee.posts.clear();
                R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
                u8 raw[28];
                memcpy(raw, &q, 28);
                u16 idAt8, typeAtA;
                memcpy(&idAt8, raw + 8, 2);
                memcpy(&typeAtA, raw + 10, 2);
                CHECK(idAt8 == q.id && typeAtA == q.type);
                CHECK(R.ee.posts.size() == 1 && R.ee.posts[0].sid == SID_EE_DONE); // completion posted, status 0
                u32 st = 1;
                memcpy(&st, R.ee.posts[0].payload.data() + 4, 4);
                CHECK(st == 0);
            }
            // SPU destination
            {
                ResourceRequestWire q = w[0];
                q.flags = 1 | 0x20 | 0x10;
                q.dest = 0x5000;
                q.cookie = 0x77;
                R.ee.posts.clear();
                R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
                const SrscEntry* se = o.srsc.find(q.type, q.id, q.sub);
                u32 want = std::min<u32>(se->size, q.length);
                std::vector<u8> ref(want);
                R.fs.read(o.node, se->off, ref.data(), want);
                ResourceService::adpcmFixup(ref, q.flags);
                ref.resize((want + 63) & ~63u);
                CHECK(memcmp(&R.spu.ram[0x5000], ref.data(), ref.size()) == 0);
                CHECK(R.ee.posts.size() == 1 && R.ee.posts[0].sid == SID_EE_DONE);
            }
            R.simple(1, h, 0, 0, 4);
            srscDone.insert(e[1]);
        }
    CHECK(srscDone.size() >= 3);

    // >64 completions are split into batches of 64
    {
        s32 h = R.open("COMMON\\SOUNDS\\SOUND.SDB");
        FileService::Open o;
        R.iop->file.get(h, o);
        std::vector<ResourceRequestWire> w;
        for (int i = 0; i < 150; ++i) {
            ResourceRequestWire q{};
            q.cookie = 0x300000 + i * 16;
            q.type = o.srsc.entries[i].type;
            q.id = o.srsc.entries[i].id;
            q.sub = o.srsc.entries[i].sub;
            q.length = 64;
            q.dest = 0x00A00000;
            w.push_back(q);
        }
        R.ee.posts.clear();
        R.iop->call(SID_RESOURCE, h, w.data(), (u32)(w.size() * 28), nullptr, 0);
        CHECK(R.ee.posts.size() == 3 && R.ee.posts[0].fno == 64 && R.ee.posts[1].fno == 64 && R.ee.posts[2].fno == 22);
        R.simple(1, h, 0, 0, 4);
    }

    // Rpc1301 delivery mode
    {
        R.iop->res.setDelivery(ResourceService::Delivery::Rpc1301);
        s32 h = R.open("COMMON\\INTERFCE\\INTERFCE.TXD");
        FileService::Open o;
        R.iop->file.get(h, o);
        ResourceRequestWire q{};
        q.cookie = 0x4444;
        q.type = o.srsc.entries[0].type;
        q.id = o.srsc.entries[0].id;
        q.sub = o.srsc.entries[0].sub;
        q.length = 4;
        q.dest = 0x00B00000;
        R.ee.posts.clear();
        R.iop->call(SID_RESOURCE, h, &q, 28, nullptr, 0);
        CHECK(R.ee.posts.size() == 1 && R.ee.posts[0].sid == SID_EE_STREAM && R.ee.posts[0].fno == 0x4444 &&
              R.ee.posts[0].payload.size() == 16 + 4);
        R.iop->res.setDelivery(ResourceService::Delivery::Direct);
        R.simple(1, h, 0, 0, 4);
    }

    // ---- 4. ADPCM fixups
    printf("[adpcm]\n");
    {
        std::vector<u8> d(64, 0);
        ResourceService::adpcmFixup(d, 0x20);
        CHECK(d[1] == 0x06 && d[0] == 0);
        std::fill(d.begin(), d.end(), 0);
        ResourceService::adpcmFixup(d, 0x10);
        CHECK(d[48 + 1] == 0x03 && d[1] == 0);
        // with 0x7777 sentinel block at the end: loop-end applies to the block before it
        std::fill(d.begin(), d.end(), 0);
        d[62] = 0x77;
        d[63] = 0x77;
        ResourceService::adpcmFixup(d, 0x10);
        CHECK(d[32 + 1] == 0x03 && d[48 + 1] == 0);
        // 0x40: clear bits, zero sentinel
        std::fill(d.begin(), d.end(), 0xff);
        ResourceService::adpcmFixup(d, 0x40); // no sentinel (ends ff ff)
        CHECK(d[48 + 1] == 0xfc && d[49 - 1 + 0] == 0xff);
        std::fill(d.begin(), d.end(), 0xff);
        d[62] = d[63] = 0x77;
        ResourceService::adpcmFixup(d, 0x40);
        CHECK(d[32 + 1] == 0xfc && d[48] == 0 && d[63] == 0 && d[47] == 0xff);
    }

    // ---- 5. Sound service
    printf("[sound]\n");
    {
        SdBatch b[] = {{SD_BATCH_SETPARAM, 0x0200, 0x1234},
                       {SD_BATCH_SETSWITCH, 0x1500, 0x00000003},
                       {SD_BATCH_SETADDR, 0x2040, 0x00012340},
                       {SD_BATCH_SETCOREATTR, 0x4001, 0x8000},
                       {SD_BATCH_GETPARAM, 0x0200, 0}};
        s32 rep = -1;
        int n = R.iop->call(SID_SOUND, 0, b, sizeof b, &rep, 4);
        CHECK(n == 4 && rep == 0 && R.spu.calls.size() == 4);
        CHECK(R.spu.calls[0] == "P0200=1234" && R.spu.calls[1] == "S1500=00000003" &&
              R.spu.calls[2] == "A2040=00012340" && R.spu.calls[3] == "C4001=8000");
        for (int i = 0; i < 64; ++i)
            R.spu.ram[0x1000 + i] = (u8)i;
        u32 rb[3] = {0x1000, 0x00300000, 64};
        CHECK(R.iop->call(SID_SOUND, 1, rb, 12, &rep, 4) == 4 && rep == 0);
        u8 t[64];
        R.mem.read(0x00300000, t, 64);
        CHECK(t[63] == 63 && t[5] == 5);
        u32 cp[3] = {0x1000, 0x2000, 64};
        CHECK(R.iop->call(SID_SOUND, 2, cp, 12, &rep, 4) == 4 && rep == 0);
        CHECK(R.spu.ram[0x2000 + 10] == 10);
    }
    CHECK(R.iop->call(0x9999, 0, nullptr, 0, nullptr, 0) < 0);

    // ---- 6. folder backend (re-uses an extracted tree if given as argv[3])
    if (argc > 3) {
        printf("[folder backend %s]\n", argv[3]);
        IsoFs ff;
        std::string e2;
        CHECK(ff.openFolder(argv[3], &e2));
        ff.setLanguageRoot("\\ENGLISH");
        IsoFs::Node n;
        CHECK(ff.lookup("LevelList.txt", n));
        auto v = ff.readAll(n);
        CHECK(v.size() == 834);
    }

    printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
