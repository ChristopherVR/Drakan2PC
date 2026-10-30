// world::readSnapshot over SYNTHETIC memory images built here (no game bytes). Layout facts:
// docs/reference/world-model.md.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "world/Exchange.h"
#include "world/Json.h"
#include "world/Reader.h"

namespace {

int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

using namespace world;

constexpr uint32_t kCtx = 0x4de5e0, kTable = 0x1741170, kRes = 0x4db4c0, kRecs = 0x881c10, kKey = 0x739acd45;
constexpr uint32_t kHandles = 0xa5b000, kInst = 0xc30000, kLists = 0xa17000;
constexpr uint32_t kCapacity = 64;

struct Image {
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x2000000, 0);
    void w32(uint32_t a, uint32_t v) { std::memcpy(&ram[a], &v, 4); }
    void w16(uint32_t a, uint16_t v) { std::memcpy(&ram[a], &v, 2); }
    void wf(uint32_t a, float f) { std::memcpy(&ram[a], &f, 4); }
    void str(uint32_t a, const char* s) { std::memcpy(&ram[a], s, std::strlen(s) + 1); }
    gameui::FlatGuestMemory mem() const { return gameui::FlatGuestMemory(ram.data(), ram.size()); }
};

uint32_t enc(uint32_t v) {
    return ((v << 8) | (v >> 24)) ^ kKey;
}

uint32_t handleAddr(uint32_t slot) {
    return kHandles + slot * 0x120u;
}

void addRecord(Image& im, uint32_t index, uint32_t id, uint32_t type, uint32_t cls, uint32_t model) {
    const uint32_t r = kRecs + index * 0x24;
    im.w32(r, id);
    im.w32(r + 4, type);
    im.w32(r + 8, cls);
    im.w32(r + 0x10, model);
}

// Registers a live handle in `slot`.
uint32_t addHandle(Image& im, uint32_t slot, uint32_t type, uint32_t resIndex, float x, float y, float z, float yaw,
                   uint32_t model = 0x1234) {
    const Globals g;
    const uint32_t h = handleAddr(slot);
    im.w32(kTable + slot * 8, h);
    im.w32(kTable + slot * 8 + 4, 1);
    im.w32(h, g.handleVtable);
    im.w32(h + 8, model);
    im.w32(h + 0xc, type);
    im.w16(h + 0x10, static_cast<uint16_t>(resIndex));
    im.w16(h + 0x12, static_cast<uint16_t>(slot + 1));
    im.wf(h + 0x14, x);
    im.wf(h + 0x18, y);
    im.wf(h + 0x1c, z);
    // rotation about Y: rows (c, 0, s), (0, 1, 0), (-s, 0, c)
    const float c = std::cos(yaw), s = std::sin(yaw);
    const float rot[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    for (int i = 0; i < 9; ++i)
        im.wf(h + 0x20 + 4 * i, rot[i]);
    return h;
}

void setInstance(Image& im, uint32_t h, uint32_t inst, int32_t cur, int32_t mx) {
    im.w32(h + 0xfc, inst + 0x100);
    im.w32(inst + 0x100 + 0x10, inst);
    im.w32(inst + 0x14, enc(static_cast<uint32_t>(cur)));
    im.w32(inst + 0x18, enc(static_cast<uint32_t>(mx)));
}

Image makeWorld() {
    const Globals g;
    Image im;
    im.w32(g.contextPtr, kCtx);
    im.w32(g.hpKey, kKey);
    im.w32(kCtx + 0x7c, kTable);
    im.w32(kCtx + 0x80, kCapacity);
    im.w32(kCtx + 0x84, 9); // slots 0..9 in use
    im.str(kCtx + 0x98, "cdrom0:/English/Levels/Test/Test.lvl");
    im.str(kCtx + 0x298, "Testland");
    im.w32(kCtx + 0x320, kRes);
    im.w32(kRes + 8, kRecs);
    im.w32(kRes + 0x10, 5);
    addRecord(im, 0, 0x20002, 4, kClassCamera, 0);
    addRecord(im, 1, 0x20a50, 3, kClassRynn, 0x20f25);
    addRecord(im, 2, 0x31f30, 10, 0x1000e, 0x31751);
    addRecord(im, 3, 0x40e8c, 13, 0x10006, 0x40e8c);

    addHandle(im, 0, 8, 3, 10, 20, 30, 0.0f); // slot 0: a prop
    const uint32_t player = addHandle(im, 1, 3, 1, 100, 200, 300, 1.0f);
    const uint32_t cam = addHandle(im, 2, 4, 0, 101, 202, 303, 0.5f);
    const uint32_t sword = addHandle(im, 3, 10, 2, 5, 6, 7, 0.0f);
    setInstance(im, sword, kInst, 144, 150);
    addHandle(im, 5, 13, 3, 400, 0, 0, 0.0f); // slot 4 stays empty (free)
    // slot 6: a stale pointer (free-list link): not a handle
    im.w32(kTable + 6 * 8, 0x00b00010);
    // slot 7: carries the vtable but a wrong own ref
    {
        const uint32_t h = addHandle(im, 7, 8, 3, 1, 1, 1, 0);
        im.w16(h + 0x12, 99);
    }
    // slot 8: resource index out of range -> no class
    addHandle(im, 8, 8, 4000, 2, 2, 2, 0);

    im.w32(kCtx + 0x4b0, player);
    im.w32(kCtx + 0x4b4, cam);
    // player stats block: HP 84 / 100
    im.w32(g.playerStats + 0xc, enc(84));
    im.w32(g.playerStats + 0x10, enc(100));
    // secondary list 0 (0x44c) contains the player and the sword
    im.w32(kCtx + 0x44c, kLists);
    im.w32(kCtx + 0x450, 64);
    im.w32(kCtx + 0x454, 2);
    im.w32(kLists, player);
    im.w32(kLists + 4, sword);
    // secondary list 3 (0x470) contains the sword only
    im.w32(kCtx + 0x470, kLists + 0x100);
    im.w32(kCtx + 0x474, 64);
    im.w32(kCtx + 0x478, 1);
    im.w32(kLists + 0x100, sword);
    return im;
}

void testEnumeration() {
    const Image im = makeWorld();
    const auto mem = im.mem();
    ReaderState st;
    Snapshot s;
    CHECK(readSnapshot(mem, Globals{}, st, s) == ReadStatus::Ok);
    CHECK(s.levelName == "Testland");
    CHECK(s.levelPath == "cdrom0:/English/Levels/Test/Test.lvl");
    CHECK(s.playerHandle == handleAddr(1));
    CHECK(s.cameraHandle == handleAddr(2));
    CHECK(s.registryCapacity == kCapacity && s.registryHighWater == 9);
    // live: slots 0,1,2,3,5,8. stale: 6 (bad pointer) and 7 (wrong own ref). 4 and 9 are empty.
    CHECK(s.entities.size() == 6);
    CHECK(s.staleSlots == 2);
    const EntitySnapshot* p = s.player();
    CHECK(p != nullptr);
    if (!p)
        return;
    CHECK(p->ref == 2 && p->type == 3 && p->classId == kClassRynn && p->resourceId == 0x20a50);
    CHECK(p->position.x == 100 && p->position.y == 200 && p->position.z == 300);
    CHECK(std::fabs(p->yaw - 1.0f) < 1e-5f);
    CHECK(p->hpKind == HpKind::PlayerHp && p->hp == 84 && p->hpMax == 100);
    CHECK(p->listMask == 1);
    CHECK(p->refCount == 1);
    const EntitySnapshot* sw = s.find(handleAddr(3));
    CHECK(sw != nullptr);
    if (sw) {
        CHECK(sw->classId == 0x1000e && sw->modelId == 0x1234);
        CHECK(sw->hpKind == HpKind::Durability && sw->hp == 144 && sw->hpMax == 150);
        CHECK(sw->listMask == (1 | 8));
    }
    const EntitySnapshot* far = s.find(handleAddr(8));
    CHECK(far != nullptr && far->classId == 0 && far->resourceIndex == 4000);
    CHECK(s.find(handleAddr(7)) == nullptr);
    CHECK(s.find(0) == nullptr);
    CHECK(std::fabs(distance(s.entities[0].position, p->position) - std::sqrt(90.f * 90 + 180 * 180 + 270 * 270)) <
          0.01f);
}

void testIndestructibleAndBadHp() {
    Image im = makeWorld();
    const uint32_t sword = handleAddr(3);
    setInstance(im, sword, kInst, -1, -1);
    {
        const auto mem = im.mem();
        ReaderState st;
        Snapshot s;
        readSnapshot(mem, Globals{}, st, s);
        const EntitySnapshot* e = s.find(sword);
        CHECK(e && e->hpKind == HpKind::Durability && e->hp == -1 && e->hpMax == -1);
    }
    setInstance(im, sword, kInst, 500, 150); // current > max: implausible, not reported
    {
        const auto mem = im.mem();
        ReaderState st;
        Snapshot s;
        readSnapshot(mem, Globals{}, st, s);
        const EntitySnapshot* e = s.find(sword);
        CHECK(e && !e->hasHp());
    }
}

void testFailures() {
    Image im;
    ReaderState st;
    Snapshot s;
    auto mem = im.mem();
    CHECK(readSnapshot(mem, Globals{}, st, s) == ReadStatus::NoContext);
    CHECK(s.entities.empty());
    im.w32(Globals{}.contextPtr, kCtx);
    mem = im.mem();
    CHECK(readSnapshot(mem, Globals{}, st, s) == ReadStatus::NoRegistry);
    // A memory image that is too small must not crash and reads as zeros.
    std::vector<uint8_t> tiny(16, 0);
    gameui::FlatGuestMemory tm(tiny.data(), tiny.size());
    CHECK(readSnapshot(tm, Globals{}, st, s) == ReadStatus::NoContext);
}

void testReuseAndSequence() {
    const Image im = makeWorld();
    const auto mem = im.mem();
    ReaderState st;
    Snapshot s;
    readSnapshot(mem, Globals{}, st, s);
    const uint64_t first = s.sequence;
    readSnapshot(mem, Globals{}, st, s);
    CHECK(s.sequence == first + 1);
    CHECK(s.entities.size() == 6); // replaced, not appended
}

void testRegionGlobals() {
    // A region with other absolute addresses: only Globals change.
    Image im = makeWorld();
    Globals g;
    g.contextPtr = 0x600000;
    g.hpKey = 0x600010;
    g.playerStats = 0x600100;
    g.handleVtable = 0x52b8b0;
    im.w32(g.contextPtr, kCtx);
    im.w32(g.hpKey, kKey);
    im.w32(g.playerStats + 0xc, enc(7));
    im.w32(g.playerStats + 0x10, enc(9));
    for (uint32_t slot : {0u, 1u, 2u, 3u, 5u, 8u})
        im.w32(handleAddr(slot), g.handleVtable);
    const auto mem = im.mem();
    ReaderState st;
    Snapshot s;
    CHECK(readSnapshot(mem, g, st, s) == ReadStatus::Ok);
    CHECK(s.entities.size() == 6);
    CHECK(s.player() && s.player()->hp == 7 && s.player()->hpMax == 9);
}

void testExchange() {
    SnapshotExchange x;
    CHECK(x.acquire() == nullptr);
    x.back().levelName = "a";
    x.publish();
    auto held = x.acquire();
    CHECK(held && held->levelName == "a");
    x.back().levelName = "b"; // must not touch what the reader holds
    x.publish();
    CHECK(held->levelName == "a");
    CHECK(x.acquire()->levelName == "b");
    CHECK(x.published() == 2);
}

void testJson() {
    const Image im = makeWorld();
    const auto mem = im.mem();
    ReaderState st;
    Snapshot s;
    readSnapshot(mem, Globals{}, st, s);
    s.levelName = "Say \"hi\"\n";
    const std::string j = toJson(s);
    CHECK(j.find("\"levelName\": \"Say \\\"hi\\\"\\n\"") != std::string::npos);
    CHECK(j.find("\"player\": \"0xa5b120\"") != std::string::npos);
    CHECK(j.find("\"hp\": 84, \"hpMax\": 100, \"hpKind\": \"player\"") != std::string::npos);
    CHECK(j.find("\"class\": \"0x1000e\"") != std::string::npos);
    Snapshot empty;
    CHECK(toJson(empty).find("\"entities\": []") != std::string::npos);
}

} // namespace

int main() {
    testEnumeration();
    testIndestructibleAndBadHp();
    testFailures();
    testReuseAndSequence();
    testRegionGlobals();
    testExchange();
    testJson();
    if (g_failed == 0)
        std::puts("world_test: ok");
    return g_failed ? 1 : 0;
}
