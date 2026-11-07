// DRAKAN_PLAYTEST_WATCH helpers (world/Watch.h): parsing, the filter and the line text. Synthetic values only.
#include <cstdio>
#include <string>

#include "world/Watch.h"

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

} // namespace

int main() {
    watch::Spec s;
    CHECK(!watch::parse(nullptr, s));
    CHECK(!watch::parse("", s));
    CHECK(!watch::parse("words=10", s)); // no filter key at all
    CHECK(watch::parse("class=10013,near=30000,words=8,data=4,max=2,every=0.5", s));
    CHECK(s.hasClass && s.classId == 0x10013 && !s.hasRef);
    CHECK(s.near == 30000.0f && s.entityWords == 8 && s.dataWords == 4 && s.maxLines == 2 && s.everySeconds == 0.5);
    CHECK(watch::parse("ref=0x4f2", s) && s.hasRef && s.ref == 0x4f2 && !s.hasClass);
    CHECK(watch::parse("ref=1234,words=9999", s) && s.ref == 1234 && s.entityWords == 256); // clamped

    EntitySnapshot e;
    e.ref = 1234;
    e.handle = 0xda8ab0;
    e.classId = 0x10013;
    e.type = 4;
    e.position = {100.0f, 200.0f, 300.0f};
    e.yaw = 1.5f;
    const Vec3 player{100.0f, 200.0f, 20300.0f};
    watch::Spec by;
    CHECK(watch::parse("class=10013", by) && watch::matches(by, e, player));
    CHECK(watch::parse("class=1000d", by) && !watch::matches(by, e, player));
    CHECK(watch::parse("ref=1234,near=25000", by) && watch::matches(by, e, player));
    CHECK(watch::parse("ref=1234,near=15000", by) && !watch::matches(by, e, player));
    CHECK(watch::parse("ref=1235", by) && !watch::matches(by, e, player));

    CHECK(watch::parse("player=1", by) && by.withPlayer && watch::matches(by, e, player, true));
    CHECK(!watch::matches(by, e, player, false) || by.near <= 0.0f); // player=1 alone adds no other object by itself
    CHECK(watch::parse("class=10013", by) && !watch::matches(by, e, player, true));

    const std::string line = watch::format(12.5, e, {0, 0, 0, 0, 0x4, 0, 0xff}, {0, 0, 0, 0, 1});
    CHECK(line ==
          "[watch 12.50] ref=1234 h=00da8ab0 cls=10013 type=4 pos=100,200,300 yaw=1.500 | e+10=4 18=ff | d+10=1");
    const std::string bare = watch::format(0.0, e, {0, 0x4}, {});
    CHECK(bare.find("| d+") == std::string::npos);
    CHECK(bare.find("| e+4=4") != std::string::npos);
    if (g_failed == 0)
        std::printf("world_watch_test: ok\n");
    return g_failed ? 1 : 0;
}
