// Guest RAM reservation (runtime/ps2_guest_ram.h): reserve a large range, commit the retail part, grow and shrink the
// committed prefix, zero after decommit, stray accesses beyond the committed part, release. No GPU, no game data.
#include "runtime/ps2_guest_ram.h"

#include <cstdio>
#include <cstring>

namespace {
int g_failed = 0;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                          \
            ++g_failed;                                                                                                \
        }                                                                                                              \
    } while (0)

constexpr size_t kMiB = 1024u * 1024u;
} // namespace

int main() {
    {
        GuestRamReservation ram;
        CHECK(ram.reserve(256u * kMiB, 32u * kMiB));
        CHECK(ram.base() != nullptr && ram.size() == 256u * kMiB && ram.committedEnd() == 32u * kMiB);
        // committed retail part: zeroed, writable at both ends
        CHECK(ram.base()[0] == 0 && ram.base()[32u * kMiB - 1] == 0);
        ram.base()[0] = 1;
        ram.base()[32u * kMiB - 1] = 2;

        // growth: rounded to the granularity, monotonic, writable
        CHECK(ram.commitTo(32u * kMiB + 100u));
        CHECK(ram.committedEnd() == 32u * kMiB + GuestRamReservation::kGranularity);
        CHECK(ram.commitTo(40u * kMiB) && ram.committedEnd() == 40u * kMiB);
        CHECK(ram.commitTo(36u * kMiB) && ram.committedEnd() == 40u * kMiB); // never shrinks by commit
        std::memset(ram.base() + 32u * kMiB, 0xab, 8u * kMiB);
        CHECK(ram.base()[40u * kMiB - 1] == 0xab);
        CHECK(ram.commitTo(1024u * kMiB) && ram.committedEnd() == 256u * kMiB); // clamped to the reservation
        ram.decommitFrom(40u * kMiB);

        // give back: committed prefix shrinks, the pages read 0 when used again
        ram.decommitFrom(34u * kMiB);
        CHECK(ram.committedEnd() == 34u * kMiB);
        CHECK(ram.commitTo(40u * kMiB));
        CHECK(ram.base()[34u * kMiB] == 0 && ram.base()[40u * kMiB - 1] == 0);
        CHECK(ram.base()[33u * kMiB] == 0xab); // kept below the give-back point
        CHECK(ram.base()[0] == 1 && ram.base()[32u * kMiB - 1] == 2);

        // a stray access beyond the committed prefix works (Windows: committed by the fault handler)
        const uint64_t before = GuestRamReservation::strayCommits();
        volatile uint8_t* stray = ram.base() + 200u * kMiB + 12345u;
        CHECK(*stray == 0);
        *stray = 7;
        CHECK(*stray == 7);
        if (ram.explicitCommit())
            CHECK(GuestRamReservation::strayCommits() == before + 1);
        ram.release();
        CHECK(ram.base() == nullptr && ram.committedEnd() == 0);
    }
    {
        // reserve again after release; several reservations at once
        GuestRamReservation a, b;
        CHECK(a.reserve(64u * kMiB, 0) && b.reserve(64u * kMiB, 1));
        CHECK(a.committedEnd() == 0 && b.committedEnd() == GuestRamReservation::kGranularity);
        CHECK(a.commitTo(GuestRamReservation::kGranularity));
        a.base()[0] = 3;
        b.base()[0] = 4;
        CHECK(a.base()[0] == 3 && b.base()[0] == 4);
    }

    if (g_failed)
        std::fprintf(stderr, "%d check(s) failed\n", g_failed);
    else
        std::printf("guest_ram_reservation_tests: all checks passed\n");
    return g_failed ? 1 : 0;
}
