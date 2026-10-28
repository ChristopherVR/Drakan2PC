// Drakan test aid (headless UI navigation): DRAKAN_PADSEQ="<startMs>:<BUTTON>[:<durMs>];..." presses DualShock buttons
// at the given times (ms since the first query, default duration 150 ms). BUTTON = SELECT L3 R3 START UP RIGHT DOWN
// LEFT L2 R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE. Returns the mask of pressed buttons (bit layout of scePad: 1<<14 =
// Cross ...).
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace drakan_ui_aid {
inline uint16_t padSeqMask() {
    struct Ev {
        long t, d;
        uint16_t m;
    };
    static const std::vector<Ev> evs = []() {
        std::vector<Ev> v;
        const char* e = std::getenv("DRAKAN_PADSEQ");
        if (!e)
            return v;
        static const char* names[16] = {"SELECT", "L3", "R3", "START", "UP",       "RIGHT",  "DOWN",  "LEFT",
                                        "L2",     "R2", "L1", "R1",    "TRIANGLE", "CIRCLE", "CROSS", "SQUARE"};
        std::string s(e);
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(';', p);
            if (q == std::string::npos)
                q = s.size();
            std::string item = s.substr(p, q - p);
            p = q + 1;
            size_t c1 = item.find(':');
            if (c1 == std::string::npos)
                continue;
            size_t c2 = item.find(':', c1 + 1);
            Ev ev{std::atol(item.c_str()), 150, 0};
            std::string b = item.substr(c1 + 1, c2 == std::string::npos ? std::string::npos : c2 - c1 - 1);
            if (c2 != std::string::npos)
                ev.d = std::atol(item.c_str() + c2 + 1);
            for (int i = 0; i < 16; ++i)
                if (b == names[i])
                    ev.m = static_cast<uint16_t>(1u << i);
            if (ev.m)
                v.push_back(ev);
        }
        return v;
    }();
    if (evs.empty())
        return 0;
    static const auto t0 = std::chrono::steady_clock::now();
    const long t = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    uint16_t m = 0;
    for (const Ev& e : evs)
        if (t >= e.t && t < e.t + e.d)
            m |= e.m;
    return m;
}
} // namespace drakan_ui_aid
