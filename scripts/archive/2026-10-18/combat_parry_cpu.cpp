// CPU checks for the observer, optionally against existing private RAM snapshots.
#include "drakan_parry_audit.h"
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace drakan_parry_audit;
static int checks = 0;
static void check(bool v, const char* label) {
    if (!v)
        throw std::runtime_error(label);
    ++checks;
}
static uint64_t hash(const std::vector<uint8_t>& v) {
    uint64_t h = 14695981039346656037ull;
    for (auto c : v)
        h = (h ^ c) * 1099511628211ull;
    return h;
}
int main(int argc, char** argv) {
    std::vector<uint8_t> b(0x2000000);
    auto put = [&](uint32_t a, uint32_t x) { std::memcpy(b.data() + a, &x, 4); };
    Ram r{b.data()};
    check(!r.valid(0xfffff), "reject low pointer");
    check(r.valid(0x1fffffc) && !r.valid(0x1fffffd), "end bounds");
    check(r.valid(0x80100000), "EE alias");
    check(r.data(0) == 0 && r.resource(0) == 0 && r.property(0, 0x159) == 0, "unavailable memory");
    put(0x5565d4, 0x600000);
    put(0x600320, 0x610000);
    put(0x610008, 0x620000);
    put(0x610010, 1);
    put(0x700010, 1);
    put(0x620024 + 8, 0x10009);
    check(r.resource(0x700000) == 0x10009, "inclusive descriptor maximum");
    put(0x700010, 0xffff);
    check(r.resource(0x700000) == 0, "descriptor sentinel");
    put(0x700010, 2);
    check(r.resource(0x700000) == 0, "descriptor upper bound");
    put(0x700054, 0x710000);
    put(0x710000, 0x15a);
    put(0x710004, 0x720000);
    put(0x710008, 0x730000);
    put(0x73001c, 0x192e48);
    put(0x720000 + 0x159 * 4, 100);
    check(r.u(r.property(0x700000, 0x159)) == 100, "known native property getter");
    check(r.property(0x700000, 0x15a) == 0, "property bounds");
    put(0x73001c, 0x192e50);
    check(r.property(0x700000, 0x159) == 0, "unknown getter rejected");
    put(0x4fbad4, 100u << 8);
    put(0x740000, 0);
    PendingReduction pending{1, 0x700000, 0x750000, 0x780000, 10, 0};
    auto matches = [&](uint32_t ra = 0x314ed0, uint32_t stats = 0x4fbac8, int hp = 100, uint32_t player = 0x700000,
                       uint32_t stack = 0x780000, int amount = 0, uint32_t damage = 0x740000) {
        return hpUpdateMatches(r, pending, ra, stats, hp, player, stack, amount, damage);
    };
    check(matches(), "matching native HP setter");
    check(!matches(0x314ed8), "unrelated HP caller");
    check(!matches(0x314ed0, 0x4fbac4), "wrong stats object");
    check(!matches(0x314ed0, 0x4fbac8, 99), "wrong new HP");
    check(!matches(0x314ed0, 0x4fbac8, 100, 0x700004), "different Rynn");
    check(!matches(0x314ed0, 0x4fbac8, 100, 0x700000, 0x780010), "different guest call stack");
    check(!matches(0x314ed0, 0x4fbac8, 100, 0x700000, 0x780000, 10), "wrong native s1 damage");
    put(0x740000, 10);
    check(!matches(), "wrong native damage pointer value");
    put(0x740000, 0);
    check(!matches(0x314ed0, 0x4fbac8, 100, 0x700000, 0x780000, 0, 0), "invalid native damage pointer");
    pending.id = 0;
    check(!matches(), "no pending reducer call");
    for (int i = 1; i < argc; ++i) {
        std::ifstream f(argv[i], std::ios::binary);
        f.read(reinterpret_cast<char*>(b.data()), b.size());
        check(f.gcount() == static_cast<std::streamsize>(b.size()), "32MiB RAM required");
        const auto before = hash(b);
        const auto c = r.u(0x5565d4), own = r.u(c + 0x4b0), p = r.data(own);
        check(r.resource(own) == 0x10009 && r.valid(p, 0x1108), "Rynn identity");
        check(r.resource(r.reference(r.h(p + 0xed0))) == 0x1000e, "existing dagger");
        check(r.f(r.property(own, 0x155)) == 0 && std::abs(r.f(r.property(own, 0x156)) - .8f) < .00001f,
              "native parry interval");
        check(r.u(r.property(own, 0x159)) == 100, "native reduction percentage");
        check(r.animation(p) != 0, "native animation slot resolves");
        check(before == hash(b), "observer leaves RAM unchanged");
        std::cout << argv[i] << " hp=" << r.hp() << " b28=" << r.u(p + 0xb28)
                  << " animTime=" << r.f(r.animation(p) + 0x58) << "\n";
    }
    std::cout << checks << " CPU checks passed; no game launch or gameplay verdict.\n";
}
