// resource_sequence_test: the EE side of a resource batch as a call sequence (IEeCallSequence). No disc needed: a
// small SRSC container served from memory. Checks the calls of a mixed batch (allocated destination, inline data, SPU2
// destination, failures), their order and payloads, the memory writes between them, and that a host which runs the
// sequence elsewhere (another thread, all calls in one go) sees exactly the calls of the call-by-call default.
#include "hle/RsrcIop.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

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
    u32 word(u32 a) const {
        u32 v;
        memcpy(&v, &ram[a], 4);
        return v;
    }
};
struct Post {
    u32 sid, fno;
    std::vector<u8> payload;
    bool blocking;
    u32 slotAtCall; // the word at kSlot when the call was made (what the guest handler would see)
    bool operator==(const Post& o) const {
        return sid == o.sid && fno == o.fno && payload == o.payload && blocking == o.blocking &&
               slotAtCall == o.slotAtCall;
    }
};
constexpr u32 kSlot = 0x2000, kAlloc = 0x100000, kAllocOffset = 0x40;
struct FakeEe : IEeRpcPoster {
    FakeMem& mem;
    std::vector<Post> posts;
    bool failAlloc = false;
    explicit FakeEe(FakeMem& m) : mem(m) {}
    int post(u32 sid, u32 fno, const void* p, u32 n, void* reply, u32 cap, bool blocking) override {
        posts.push_back({sid, fno, std::vector<u8>((const u8*)p, (const u8*)p + n), blocking, mem.word(kSlot)});
        if (sid == SID_EE_ALLOC && reply && cap >= 16) {
            const u32 r[4] = {0, failAlloc ? 0 : kAlloc, kAllocOffset, 0};
            memcpy(reply, r, 16);
            return 16;
        }
        return 0;
    }
};
// A host that runs every sequence on another thread, in one go, like the game's runtime does on its EE thread.
struct ElsewhereEe final : FakeEe {
    using FakeEe::FakeEe;
    int sequences = 0;
    void run(IEeCallSequence& sequence) override {
        ++sequences;
        std::thread([&] { FakeEe::run(sequence); }).join();
    }
};
struct FakeSpu : ISpu2 {
    std::vector<std::pair<u32, std::vector<u8>>> writes;
    void ramWrite(u32 a, const void* s, u32 n) override {
        writes.emplace_back(a, std::vector<u8>((const u8*)s, (const u8*)s + n));
    }
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
    std::vector<std::array<u32, 2>> loc;
    for (auto& e : es) {
        while (b.size() & 7)
            b.push_back(0);
        loc.push_back({(u32)b.size(), (u32)e.data.size()});
        b.insert(b.end(), e.data.begin(), e.data.end());
    }
    const u32 idx = (u32)b.size(), cnt = (u32)es.size();
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
static std::vector<u8> pattern(size_t n, u8 seed) {
    std::vector<u8> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = (u8)(seed + i * 7);
    return v;
}
static ResourceRequestWire request(u32 cookie, u16 flags, u16 type, u16 id, u32 dest) {
    ResourceRequestWire r{};
    r.cookie = cookie;
    r.flags = flags;
    r.type = type;
    r.id = id;
    r.sub = 0;
    r.length = 0xFFFFFFFFu;
    r.dest = dest;
    return r;
}
static u32 word(const std::vector<u8>& b, size_t at) {
    u32 v = 0;
    if (at + 4 <= b.size())
        memcpy(&v, &b[at], 4);
    return v;
}

// One service over the container, with its own memory and SPU2; `batch` runs a batch and returns the posts.
struct Rig {
    IsoFs fs;
    FakeMem mem;
    FakeSpu spu;
    s32 handle = -1;
    Rig() {
        fs.addMemoryFile("\\Common\\T\\T.TXD",
                         makeSrsc({{0x40, 1, 0, pattern(300, 1)}, {0x40, 2, 0, pattern(70, 9)}, {0x300, 3, 0, {}}}));
    }
    template <class Ee>
    std::vector<Post> batch(Ee& ee, const std::vector<ResourceRequestWire>& requests, bool skipFailures = false) {
        RsrcIop iop(fs, mem, ee, spu);
        iop.res.skipFailureCompletions = skipFailures;
        u8 open[0x10c] = {};
        const char* path = "\\Common\\T\\T.TXD";
        memcpy(open + 8, path, strlen(path) + 1);
        u8 reply[8] = {};
        iop.call(SID_FILE, 0, open, sizeof open, reply, sizeof reply);
        memcpy(&handle, reply, 4);
        iop.call(SID_RESOURCE, (u32)handle, requests.data(), (u32)(requests.size() * sizeof(ResourceRequestWire)),
                 nullptr, 0);
        return ee.posts;
    }
};

int main() {
    setVerbose(false);
    setLogSink([](const std::string&) {});
    const std::vector<u8> a = pattern(300, 1), b = pattern(70, 9);
    // 0x80: the EE allocates the destination, the allocated address goes to `dest`; 0: data inline in the 0x1301
    // call; 1: SPU2 RAM (padded to 64); a missing entry and an empty one fail.
    const std::vector<ResourceRequestWire> mixed = {
        request(0xA1, 0x80, 0x40, 1, kSlot), request(0xA2, 0x00, 0x40, 2, 0),  request(0xA3, 0x01, 0x40, 2, 0x5000),
        request(0xA4, 0x00, 0x40, 77, 0),    request(0xA5, 0x00, 0x300, 3, 0), request(0xA6, 0x80, 0x40, 2, 0),
    };

    // ---- the calls of a mixed batch, in the loader's order
    {
        Rig rig;
        FakeEe ee(rig.mem);
        const std::vector<Post> posts = rig.batch(ee, mixed);
        CHECK(rig.handle >= 0);
        CHECK(posts.size() == 7);
        if (posts.size() == 7) {
            // A1: allocation (blocking, length rounded up to 4), then the header-only delivery
            CHECK(posts[0].sid == SID_EE_ALLOC && posts[0].fno == 0xA1 && posts[0].blocking &&
                  posts[0].payload.size() == 16 && word(posts[0].payload, 0) == 300 && posts[0].slotAtCall == 0);
            CHECK(posts[1].sid == SID_EE_STREAM && posts[1].fno == 0xA1 && posts[1].payload.size() == 16 &&
                  word(posts[1].payload, 0) == kAllocOffset && word(posts[1].payload, 4) == 300 &&
                  word(posts[1].payload, 8) == kAllocOffset);
            // the allocated address is in the requester's slot and the data in the buffer before the delivery call
            CHECK(posts[1].slotAtCall == kAlloc);
            // A2: inline data behind a 16 byte header
            CHECK(posts[2].sid == SID_EE_STREAM && posts[2].fno == 0xA2 && posts[2].payload.size() == 16 + 70 &&
                  word(posts[2].payload, 0) == 0x10 && word(posts[2].payload, 4) == 70 &&
                  std::equal(b.begin(), b.end(), posts[2].payload.begin() + 16));
            // A3: SPU2 upload, then its header
            CHECK(posts[3].sid == SID_EE_STREAM && posts[3].fno == 0xA3 && posts[3].payload.size() == 16 &&
                  word(posts[3].payload, 0) == 0x10 && word(posts[3].payload, 4) == 70);
            // A6: allocation with no slot to store it in, header-only delivery
            CHECK(posts[4].sid == SID_EE_ALLOC && posts[4].fno == 0xA6 && word(posts[4].payload, 0) == 72);
            CHECK(posts[5].sid == SID_EE_STREAM && posts[5].fno == 0xA6 && word(posts[5].payload, 4) == 70);
            // the two failures are completed in one 0x1302 call, in submission order
            CHECK(posts[6].sid == SID_EE_DONE && posts[6].fno == 2 && posts[6].blocking &&
                  posts[6].payload.size() == 16 && word(posts[6].payload, 0) == 0xA4 &&
                  (s32)word(posts[6].payload, 4) == ResourceService::ST_NOT_FOUND &&
                  word(posts[6].payload, 8) == 0xA5 && (s32)word(posts[6].payload, 12) == ResourceService::ST_ZERO);
        }
        CHECK(rig.mem.word(kSlot) == kAlloc);
        CHECK(memcmp(&rig.mem.ram[kAlloc], b.data(), b.size()) == 0); // A6 came last into the same fake buffer
        CHECK(rig.spu.writes.size() == 1 && rig.spu.writes[0].first == 0x5000 &&
              rig.spu.writes[0].second.size() == 128 &&
              std::equal(b.begin(), b.end(), rig.spu.writes[0].second.begin()));
    }

    // ---- a host that runs the sequence elsewhere makes the same calls
    {
        Rig plain, moved;
        FakeEe ee(plain.mem);
        ElsewhereEe elsewhere(moved.mem);
        const std::vector<Post> expected = plain.batch(ee, mixed);
        const std::vector<Post> got = moved.batch(elsewhere, mixed);
        CHECK(elsewhere.sequences == 1);
        CHECK(got == expected);
        CHECK(plain.mem.ram == moved.mem.ram);
        CHECK(plain.spu.writes == moved.spu.writes);
    }

    // ---- a failed allocation is reported, the rest of the batch is delivered
    {
        Rig rig;
        FakeEe ee(rig.mem);
        ee.failAlloc = true;
        const std::vector<Post> posts =
            rig.batch(ee, {request(0xB1, 0x80, 0x40, 1, kSlot), request(0xB2, 0, 0x40, 2, 0)});
        CHECK(posts.size() == 3);
        if (posts.size() == 3) {
            CHECK(posts[0].sid == SID_EE_ALLOC && posts[1].sid == SID_EE_STREAM && posts[1].fno == 0xB2);
            CHECK(posts[2].sid == SID_EE_DONE && posts[2].fno == 1 && word(posts[2].payload, 0) == 0xB1 &&
                  (s32)word(posts[2].payload, 4) == ResourceService::ST_ALLOC);
        }
        CHECK(rig.mem.word(kSlot) == 0);
    }

    // ---- completions go out 64 at a time; a bad file handle fails every request; failures can be left unreported
    {
        Rig rig;
        FakeEe ee(rig.mem);
        std::vector<ResourceRequestWire> many;
        for (u32 i = 0; i < 70; ++i)
            many.push_back(request(0x1000 + i, 0, 0x40, 500, 0));
        const std::vector<Post> posts = rig.batch(ee, many);
        CHECK(posts.size() == 2 && posts[0].sid == SID_EE_DONE && posts[0].fno == 64 &&
              posts[0].payload.size() == 512 && posts[1].fno == 6 && word(posts[1].payload, 0) == 0x1000 + 64);
        Rig quiet;
        FakeEe none(quiet.mem);
        CHECK(quiet.batch(none, many, true).empty());
        Rig bad;
        FakeEe ee2(bad.mem);
        RsrcIop iop(bad.fs, bad.mem, ee2, bad.spu);
        const ResourceRequestWire one = request(0xC1, 0, 0x40, 1, 0);
        iop.call(SID_RESOURCE, 55, &one, sizeof one, nullptr, 0);
        CHECK(ee2.posts.size() == 1 && ee2.posts[0].sid == SID_EE_DONE &&
              (s32)word(ee2.posts[0].payload, 4) == ResourceService::ST_BAD_HANDLE);
    }

    setLogSink(nullptr);
    printf("resource_sequence_test: %d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
