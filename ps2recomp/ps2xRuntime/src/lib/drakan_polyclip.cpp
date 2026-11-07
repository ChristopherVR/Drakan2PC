#include "drakan_polyclip.h"

#include "drakan_config.h"
#include "drakan_region.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "world/PolyClip.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace drakan_polyclip {
namespace {

constexpr uint32_t kFanDraw = 0x146880u; // (a0 unused, a1 = vertex array (32 bytes each), a2 = count) -> GIF emitter
constexpr uint32_t kRamMask = 0x1fffffffu;
constexpr uint32_t kRamSize = 32u * 1024u * 1024u;
constexpr int kMaxFan = 8;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_enabled{true};
PS2Runtime::RecompiledFunction g_orig = nullptr;
world::polyclip::Params g_params;
bool g_log = false;
std::atomic<uint64_t> g_calls{0}, g_clipped{0}, g_dropped{0};
// PS2X_PERF accounting: nanoseconds spent in the hook itself (everything except the original fan draw) and fans seen.
bool g_perf = false;
uint64_t g_nowNs = 0; // measured cost of one steady_clock::now(), subtracted once per timed segment
std::atomic<uint64_t> g_hookNs{0}, g_hookFans{0};
using Clock = std::chrono::steady_clock;
inline uint64_t nsBetween(Clock::time_point a, Clock::time_point b) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

void hookFanDraw(uint8_t* ram, R5900Context* c, PS2Runtime* rt) {
    if (!g_enabled.load(std::memory_order_relaxed)) {
        g_orig(ram, c, rt);
        return;
    }
    using world::polyclip::Vertex;
    // Hook-only time (PS2X_PERF): everything except the original fan draw. `mark` is the start of the current hook
    // segment; the original call closes one segment and opens the next.
    Clock::time_point mark{};
    uint64_t own = 0;
    int segments = 0;
    if (g_perf)
        mark = Clock::now();
    auto callOrig = [&] {
        if (g_perf) {
            const auto t = Clock::now();
            own += nsBetween(mark, t);
            ++segments;
        }
        g_orig(ram, c, rt);
        if (g_perf)
            mark = Clock::now();
    };
    auto finish = [&] {
        if (g_perf) {
            own += nsBetween(mark, Clock::now());
            ++segments;
            const uint64_t timer = g_nowNs * static_cast<uint64_t>(segments);
            own = own > timer ? own - timer : 0;
            g_hookNs.fetch_add(own, std::memory_order_relaxed);
            g_hookFans.fetch_add(1, std::memory_order_relaxed);
        }
    };
    const uint32_t src = GPR_U32(c, 5) & kRamMask;
    const uint32_t n = GPR_U32(c, 6);
    Vertex v[kMaxFan];
    if (n < 3 || n > kMaxFan || src < 0x100000u || (src & 15u) || src + n * sizeof(Vertex) > kRamSize) {
        callOrig();
        finish();
        return;
    }
    std::memcpy(v, ram + src, n * sizeof(Vertex));
    if (g_log) {
        g_calls.fetch_add(1, std::memory_order_relaxed);
        bool out = false;
        for (uint32_t i = 0; i < n; ++i)
            if (std::fabs(v[i].x - 2048.0f) > 2040.0f || std::fabs(v[i].y - 2048.0f) > 2040.0f)
                out = true;
        static std::atomic<uint32_t> nOut{0};
        if (out && nOut.fetch_add(1) < 12) {
            std::fprintf(stderr, "[polyclip] outside n=%u:", n);
            for (uint32_t i = 0; i < n; ++i)
                std::fprintf(stderr, " [%g %g %g q=%g st=%g,%g]", v[i].x, v[i].y, v[i].z, v[i].q, v[i].s, v[i].t);
            std::fputc(10, stderr);
        }
    }
    if (!world::polyclip::needsClip(v, static_cast<int>(n), g_params)) {
        callOrig();
        finish();
        return;
    }
    // Clip every triangle of the fan and draw each remainder as its own fan. The scratch vertices live in the unused
    // stack below $sp (the original needs a few hundred bytes there, the scratch starts 4 KB down).
    const uint32_t sp = GPR_U32(c, 29) & kRamMask;
    const uint32_t scratch = (sp - 4096u - 16u * world::polyclip::kMaxClipped * 2u) & ~15u;
    if (scratch < 0x100000u) {
        callOrig();
        finish();
        return;
    }
    const uint64_t a1 = GPR_U64(c, 5), a2 = GPR_U64(c, 6);
    const uint32_t pc = c->pc;
    int drawn = 0;
    for (uint32_t i = 1; i + 1 < n; ++i) {
        const Vertex tri[3] = {v[0], v[i], v[i + 1]};
        Vertex out[world::polyclip::kMaxClipped];
        const int m = world::polyclip::clipPolygon(tri, 3, g_params, out);
        if (m < 3)
            continue;
        std::memcpy(ram + scratch, out, static_cast<size_t>(m) * sizeof(Vertex));
        SET_GPR_U64(c, 5, scratch);
        SET_GPR_U64(c, 6, static_cast<uint64_t>(m));
        c->pc = kFanDraw;
        callOrig();
        ++drawn;
    }
    SET_GPR_U64(c, 5, a1);
    SET_GPR_U64(c, 6, a2);
    c->pc = pc;
    finish();
    if (g_log) {
        g_clipped.fetch_add(1, std::memory_order_relaxed);
        if (!drawn)
            g_dropped.fetch_add(1, std::memory_order_relaxed);
        static std::atomic<uint32_t> tick{0};
        if ((tick.fetch_add(1) & 15u) == 0)
            std::fprintf(stderr, "[polyclip] fans=%llu clipped=%llu dropped=%llu\n",
                         static_cast<unsigned long long>(g_calls.load()),
                         static_cast<unsigned long long>(g_clipped.load()),
                         static_cast<unsigned long long>(g_dropped.load()));
    }
}

} // namespace

bool available() {
    return g_installed.load();
}
void configure(bool on) {
    g_enabled.store(on);
}
void takeHookStats(uint64_t& ns, uint64_t& fans) {
    ns = g_hookNs.exchange(0, std::memory_order_relaxed);
    fans = g_hookFans.exchange(0, std::memory_order_relaxed);
}
bool enabled() {
    return g_enabled.load();
}

void install(PS2Runtime& rt) {
    if (g_installed.load())
        return;
    if (!drakan_region::build().identityAddresses || !rt.hasFunction(kFanDraw))
        return;
    configure(drakan_cfg::getBool("video", "poly_near_clip", "DRAKAN_POLY_NEAR_CLIP", true));
    if (const char* e = std::getenv("DRAKAN_POLY_NEAR_W"))
        g_params.nearW = static_cast<float>(std::atof(e));
    g_log = std::getenv("DRAKAN_POLY_NEAR_CLIP_LOG") != nullptr;
    g_perf = std::getenv("PS2X_PERF") != nullptr;
    if (g_perf) {
        const auto a = Clock::now();
        for (int i = 0; i < 20000; ++i)
            (void)Clock::now();
        g_nowNs = nsBetween(a, Clock::now()) / 20000;
    }
    g_orig = rt.lookupFunction(kFanDraw);
    rt.replaceFunction(kFanDraw, &hookFanDraw);
    g_installed = true;
    std::fprintf(stderr, "[polyclip] installed (%s, near depth %.1f)\n", g_enabled ? "on" : "off", g_params.nearW);
}

} // namespace drakan_polyclip
