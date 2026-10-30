// Data-dependent check of the world reader over local RAM dumps (game-derived, under build/, never committed).
// Exits 77 (ctest: skipped) when no dump is present.
//   world_ram_test                  the default dumps below, relative to the repository root
//   world_ram_test <dump.ram> ...   the same invariants for any other gameplay dump
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "world/Reader.h"

#ifndef WORLD_REPO_ROOT
#define WORLD_REPO_ROOT "."
#endif

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        ++g_checks;                                                                                                    \
        if (!(c)) {                                                                                                    \
            ++g_fail;                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                   \
        }                                                                                                              \
    } while (0)

namespace {

bool load(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return out.size() >= 0x2000000;
}

void check(const std::string& name, const std::vector<uint8_t>& ram) {
    const world::FlatGuestMemory mem(ram.data(), ram.size());
    world::ReaderState st;
    world::Snapshot s;
    const world::ReadStatus rs = world::readSnapshot(mem, world::Globals{}, st, s);
    std::printf("%s: %s, level '%s', %zu entities, %u stale\n", name.c_str(), world::readStatusName(rs),
                s.levelName.c_str(), s.entities.size(), s.staleSlots);
    CHECK(rs == world::ReadStatus::Ok);
    if (rs != world::ReadStatus::Ok)
        return;
    CHECK(!s.levelName.empty());
    CHECK(s.entities.size() > 1000);
    const world::EntitySnapshot* p = s.player();
    CHECK(p != nullptr);
    if (p) {
        CHECK(p->classId == world::kClassRynn || p->classId == world::kClassDragon);
        CHECK(std::isfinite(p->position.x) && std::isfinite(p->position.y) && std::isfinite(p->position.z));
        CHECK(p->hasHp() && p->hp <= p->hpMax);
    }
    const world::EntitySnapshot* c = s.find(s.cameraHandle);
    CHECK(c != nullptr && c->classId == world::kClassCamera);
    for (const world::EntitySnapshot& e : s.entities) {
        CHECK(e.ref >= 1 && e.ref <= s.registryHighWater + 1);
        if (e.type == world::type::kWeapon || e.type == world::type::kUsable)
            CHECK(e.hasHp());
    }
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i)
        paths.push_back(argv[i]);
    if (paths.empty()) {
        const std::string root = std::string(WORLD_REPO_ROOT) + "/build/";
        paths = {root + "combined-build06-private/t/build06-final-native32/ram_cat_baseline.ram",
                 root + "harness/magic5/ram_k0.ram"};
    }
    int used = 0;
    for (const std::string& p : paths) {
        std::vector<uint8_t> ram;
        if (!load(p, ram))
            continue;
        ++used;
        check(p, ram);
    }
    if (!used) {
        std::puts("world_ram_test: no RAM dump present, skipped");
        return 77;
    }
    std::printf("world_ram_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
