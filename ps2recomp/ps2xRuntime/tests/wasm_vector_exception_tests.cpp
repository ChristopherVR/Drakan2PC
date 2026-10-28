#include <smmintrin.h>
#include <cstdio>
#include <stdexcept>

// The memory bus returns/accepts vector registers across potentially throwing
// calls. JS exception trampolines cannot transport a Wasm v128 parameter/result.
// Native Wasm exceptions must support both the normal and exceptional paths.
volatile bool failRead = false;
volatile int observed = 0;
__attribute__((noinline)) __m128i readVector(int low, int high) {
    if (failRead)
        throw std::runtime_error("vector read failed");
    return _mm_set_epi32(high, low, high, low);
}
__attribute__((noinline)) void writeVector(int unused, __m128i value) {
    if (unused)
        throw std::runtime_error("vector write failed");
    observed = _mm_extract_epi32(value, 1);
}
int main() {
    unsigned caught = 0;
    try {
        const auto value = readVector(17, 29);
        writeVector(0, value);
        if (observed != 29)
            return 1;
        failRead = true;
        readVector(0, 0);
        return 2;
    } catch (const std::runtime_error&) {
        ++caught;
    }
    try {
        writeVector(1, _mm_setzero_si128());
        return 3;
    } catch (const std::runtime_error&) {
        ++caught;
    }
    std::puts("Wasm vector arguments/results and native exceptions passed");
    return caught == 2 ? 0 : 4;
}
