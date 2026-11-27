// EE invocation stack slots (runtime/ee_invocation_stacks.h): a deleted thread's slots are reused, a live slot never
// is. Before the free list, about 64 distinct thread ids exhausted the 1 MB pool and killed the game thread.
#include "../../ps2recomp/ps2xRuntime/include/runtime/ee_invocation_stacks.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);                         \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (0)

namespace {
// Same shape as PS2Runtime::reserveAsyncCallbackStack: a bump allocator down from 0x2000000 to 0x1F00000 (64 slots of
// 16 KB) that returns the slot top minus 16 and 0 once the pool is empty.
struct BumpPool {
    uint32_t top = 0x2000000u;
    uint32_t floor = 0x1F00000u;
    int reservations = 0;
    uint32_t operator()(uint32_t size) {
        if (top - floor < size)
            return 0u;
        const uint32_t slotTop = top;
        top -= size;
        ++reservations;
        return slotTop - 0x10u;
    }
};
} // namespace

int main() {
    // Same key, same slot; different depth or thread, different slot.
    {
        EeInvocationStacks stacks;
        BumpPool pool;
        const uint32_t a = stacks.acquire(2, 0, pool);
        CHECK(a == 0x2000000u - 0x10u);
        CHECK(stacks.acquire(2, 0, pool) == a);
        const uint32_t b = stacks.acquire(2, 1, pool);
        const uint32_t c = stacks.acquire(3, 0, pool);
        CHECK(b != a && c != a && c != b);
        CHECK(pool.reservations == 3);
    }

    // Round-robin thread ids 2..255, each thread created, used at depths 0 and 1, then deleted: far more than 64
    // distinct ids must never exhaust the pool, and the slots of a deleted thread are reused.
    {
        EeInvocationStacks stacks;
        BumpPool pool;
        int id = 2;
        for (int i = 0; i < 5000; ++i) {
            CHECK(stacks.acquire(id, 0, pool) != 0u);
            CHECK(stacks.acquire(id, 1, pool) != 0u);
            stacks.releaseThread(id);
            id = id == 255 ? 2 : id + 1;
        }
        CHECK(pool.reservations == 2);
        CHECK(stacks.slotsInUse() == 0u && stacks.slotsFree() == 2u);
    }

    // Many threads alive at once, deleted in a scattered order: a slot is never shared by two live keys, and a
    // released slot only goes to a new key.
    {
        EeInvocationStacks stacks;
        BumpPool pool;
        std::map<std::pair<int, int>, uint32_t> live;
        uint32_t state = 12345u;
        int nextId = 2;
        for (int step = 0; step < 20000; ++step) {
            state = state * 1664525u + 1013904223u;
            const bool create = live.size() < 40u && ((state >> 16) & 1u) != 0u;
            if (create || live.empty()) {
                const int id = nextId;
                nextId = nextId == 255 ? 2 : nextId + 1;
                bool idLive = false;
                for (const auto& [key, top] : live)
                    idLive = idLive || key.first == id;
                if (idLive)
                    continue;
                for (int depth = 0; depth < 1 + static_cast<int>((state >> 20) % 3u); ++depth) {
                    const uint32_t top = stacks.acquire(id, static_cast<size_t>(depth), pool);
                    CHECK(top != 0u);
                    live[{id, depth}] = top;
                }
            } else {
                auto victim = live.begin();
                std::advance(victim, static_cast<long>((state >> 8) % live.size()));
                const int id = victim->first.first;
                stacks.releaseThread(id);
                for (auto it = live.begin(); it != live.end();)
                    it = it->first.first == id ? live.erase(it) : std::next(it);
            }
            std::set<uint32_t> tops;
            for (const auto& [key, top] : live) {
                CHECK(tops.insert(top).second); // no slot shared by two live keys
                CHECK(stacks.acquire(key.first, static_cast<size_t>(key.second), pool) == top); // stable while alive
            }
        }
        CHECK(pool.reservations <= 64);
    }

    // The pool still runs dry when more than 64 keys are live at once, and acquire reports it with 0.
    {
        EeInvocationStacks stacks;
        BumpPool pool;
        for (int id = 2; id < 66; ++id)
            CHECK(stacks.acquire(id, 0, pool) != 0u);
        CHECK(stacks.acquire(66, 0, pool) == 0u);
        stacks.releaseThread(10);
        CHECK(stacks.acquire(66, 0, pool) != 0u);
    }

    // clear() forgets every slot and never hands an old one out again.
    {
        EeInvocationStacks stacks;
        BumpPool pool;
        const uint32_t before = stacks.acquire(2, 0, pool);
        stacks.releaseThread(2);
        stacks.clear();
        CHECK(stacks.acquire(2, 0, pool) != before);
    }

    std::puts("ee_invocation_stacks: ok");
    return 0;
}
