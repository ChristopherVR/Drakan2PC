// Logical CPUs this process may actually run on: the process affinity mask where the platform has one (the `cores:N`
// setting of runtime/input narrows it), else std::thread::hardware_concurrency(). Worker pools size from this so they
// do not over-subscribe a restricted mask.
#pragma once

#include <algorithm>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <sched.h>
#endif

namespace gsr {

inline unsigned usableCpus() {
    unsigned n = std::thread::hardware_concurrency();
#if defined(_WIN32)
    DWORD_PTR proc = 0, sys = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc) {
        unsigned bits = 0;
        for (DWORD_PTR m = proc; m; m &= m - 1)
            ++bits;
        n = n ? std::min(n, bits) : bits;
    }
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        const unsigned bits = static_cast<unsigned>(CPU_COUNT(&set));
        if (bits)
            n = n ? std::min(n, bits) : bits;
    }
#endif
    return std::max(n, 1u);
}

} // namespace gsr
