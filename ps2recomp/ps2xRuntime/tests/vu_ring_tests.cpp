#include "vu/ps2_vu1_ring.h"
#include <cstdint>
#include <cstdio>
#include <random>

int main() {
    std::mt19937 random(0x1a501);
    uint64_t checks = 0;
    const uint32_t sizes[] = {1,   2,    3,    7,    16,   255,   256,   257,        511,
                              512, 1023, 1024, 1025, 4096, 16384, 65536, 0x10000000u};
    for (const uint32_t size : sizes)
        for (uint32_t i = 0; i < 100000; ++i) {
            const uint32_t at = i < size ? i : random();
            const uint32_t start = random();
            const uint32_t expected = ((at % size) + size - (start % size)) % size;
            if (Vu1Ring::wrap(at, size) != at % size || Vu1Ring::distance(at, start, size) != expected)
                return 1;
            checks += 2;
        }
    std::printf("PASS: %llu exact ring calculations, including wraps and non-power-of-two fallback\n",
                static_cast<unsigned long long>(checks));
}
