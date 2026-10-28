#include "vu/ps2_vu1_flag_batch.h"
#include <algorithm>
#include <vector>
#include <random>
#include <cstdio>
struct State {
    uint32_t mac, status, clip;
};
struct Event {
    int cycle, kind;
    uint32_t mac, status, extra;
};
void apply(State& s, const Event& e) {
    if (e.kind == 0) {
        s.mac = e.mac;
        const uint32_t current = e.status & 15u;
        s.status = (s.status & 0xff0u) | current | ((current | e.extra) << 6);
    }
    if (e.kind == 1)
        s.status = (s.status & 0x3fu) | (e.status & 0xfc0u);
    if (e.kind == 2)
        s.clip = e.mac;
    if (e.kind == 3)
        s.status = (s.status & 0xfcfu) | (e.status & 0x30u) | ((e.status & 0x30u) << 6);
}
bool equal(const State& a, const State& b) {
    return a.mac == b.mac && a.status == b.status && a.clip == b.clip;
}
int main() {
    std::mt19937 rng(0x21fa391);
    uint64_t checks = 0;
    for (unsigned trial = 0; trial < 200000; ++trial) {
        State expected{rng(), rng(), rng()}, actual = expected;
        std::vector<Event> events;
        for (unsigned n = 0, count = rng() % 9; n < count; ++n)
            events.push_back({int(rng() % 8) - 4, int(rng() % 3), rng(), rng(), rng() & 15u});
        const int end = 4 + rng() % 64;
        // Q/DI may retire among incoming entries or between new FMAC results.
        events.push_back({int(rng() % (end + 8)), 3, 0, rng() & 0x30u, 0});
        Vu1FlagBatch batch;
        unsigned early = 0;
        for (int issue = 0; issue < end; issue += 1 + rng() % 4) {
            Event next{issue + 4, 0, rng(), rng() & 15u, rng() & 15u};
            events.push_back(next);
            if (next.cycle <= end) {
                batch.add(next.mac, next.status, next.extra);
                ++early;
            }
        }
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            if (a.cycle != b.cycle)
                return a.cycle < b.cycle;
            return a.kind != 3 && b.kind == 3; // flags ready at Q's cycle retire first
        });
        for (const auto& e : events)
            if (e.cycle <= end)
                apply(expected, e);
        // Apply only incoming flags and Q; publish the early FMAC aggregate once.
        for (const auto& e : events)
            if (e.cycle <= end && (e.cycle < 4 || e.kind != 0))
                apply(actual, e);
        if (early)
            batch.apply(actual.mac, actual.status);
        if (!equal(expected, actual)) {
            std::printf("batch boundary mismatch %u\n", trial);
            return 1;
        }
        ++checks;
        for (int cycle = end + 1; cycle <= end + 8; ++cycle) {
            for (const auto& e : events)
                if (e.cycle == cycle) {
                    apply(expected, e);
                    apply(actual, e);
                }
            if (!equal(expected, actual))
                return 2;
            ++checks;
        }
    }
    std::printf("%llu exact block-boundary and future flag-state comparisons\n", (unsigned long long)checks);
}
