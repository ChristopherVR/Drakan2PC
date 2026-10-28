// Drakan: FPS counter model, see drakan_fpsmeter.h.
#include "drakan_fpsmeter.h"
#include "drakan_config.h"
#include "ps2_perf.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace drakan_fpsmeter {
namespace {
std::atomic<int> g_mode{0};
std::atomic<uint64_t> g_worlds{0};
std::atomic<int> g_corner{TopLeft};
std::atomic<void*> g_ee{nullptr}, g_gs{nullptr};

double qpcFreq() {
    static double f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return (double)x.QuadPart;
    }();
    return f;
}
int64_t qpcNow() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

constexpr int kRing = 512;
struct State {
    std::mutex m;
    float ring[kRing] = {}; // present intervals in ms
    int ringPos = 0, ringCount = 0;
    int64_t lastPresent = 0;
    int64_t winStart = 0;
    uint64_t winPresents = 0;
    uint64_t flips0 = 0, worlds0 = 0;
    double ee0 = 0, gs0 = 0, proc0 = 0;
    double presentedFps = 0, gameFps = 0, worldFps = 0, avgMs = 0, worstMs = 0, low1Ms = 0, cpuProc = 0, cpuEe = -1,
           cpuGs = -1;
    std::string gpu;
    unsigned bbW = 0, bbH = 0;
    std::vector<std::string> out;
    bool valid = false;
};
State g;

double ftSeconds(const FILETIME& a, const FILETIME& b) {
    ULARGE_INTEGER x, y;
    x.LowPart = a.dwLowDateTime;
    x.HighPart = a.dwHighDateTime;
    y.LowPart = b.dwLowDateTime;
    y.HighPart = b.dwHighDateTime;
    return (double)(x.QuadPart + y.QuadPart) * 1e-7;
}
double threadCpu(void* h) {
    if (!h)
        return -1;
    FILETIME c, e, k, u;
    if (!GetThreadTimes((HANDLE)h, &c, &e, &k, &u))
        return -1;
    return ftSeconds(k, u);
}
double procCpu() {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
        return 0;
    return ftSeconds(k, u);
}
int ncpu() {
    static int n = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return (int)std::max<DWORD>(1, si.dwNumberOfProcessors);
    }();
    return n;
}

void rebuild(int mode) {
    g.out.clear();
    if (mode <= 0)
        return;
    char b[160];
    std::snprintf(b, sizeof b, "World %5.1f fps", g.worldFps);
    g.out.emplace_back(b);
    std::snprintf(b, sizeof b, "Present %5.1f fps  %5.2f ms", g.presentedFps, g.avgMs);
    g.out.emplace_back(b);
    if (mode >= 2) {
        std::snprintf(b, sizeof b, "Flip events %5.1f /s", g.gameFps);
        g.out.emplace_back(b);
        std::snprintf(b, sizeof b, "Worst %5.2f ms  1%% low %5.1f fps", g.worstMs,
                      g.low1Ms > 0 ? 1000.0 / g.low1Ms : 0.0);
        g.out.emplace_back(b);
        std::snprintf(b, sizeof b, "CPU %3.0f%%", g.cpuProc);
        std::string s = b;
        if (g.cpuEe >= 0) {
            std::snprintf(b, sizeof b, "  EE %3.0f%%", g.cpuEe);
            s += b;
        }
        if (g.cpuGs >= 0) {
            std::snprintf(b, sizeof b, "  GS %3.0f%%", g.cpuGs);
            s += b;
        }
        g.out.push_back(s);
        if (!g.gpu.empty()) {
            std::snprintf(b, sizeof b, "%s  %ux%u", g.gpu.c_str(), g.bbW, g.bbH);
            g.out.emplace_back(b);
        }
    }
}
} // namespace

void configure() {
    const int m = drakan_cfg::getInt("video", "show_fps", "DRAKAN_SHOW_FPS", 0);
    g_mode.store(std::clamp(m, 0, 2));
    setCornerText(drakan_cfg::getString("video", "fps_position", "DRAKAN_FPS_POSITION", "top-left").c_str());
}

void setMode(int m) {
    g_mode.store(std::clamp(m, 0, 2));
}
int mode() {
    return g_mode.load();
}
int cycleMode() {
    const int n = (g_mode.load() + 1) % 3;
    g_mode.store(n);
    return n;
}
void setCorner(int c) {
    g_corner.store(std::clamp(c, 0, 3));
}
int corner() {
    return g_corner.load();
}
void setCornerText(const char* t) {
    std::string s = t ? t : "";
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    const bool top = s.find("bottom") == std::string::npos;
    const bool right = s.find("right") != std::string::npos;
    setCorner(top ? (right ? TopRight : TopLeft) : (right ? BottomRight : BottomLeft));
}
void setThreads(void* ee, void* gs) {
    if (ee)
        g_ee.store(ee);
    if (gs)
        g_gs.store(gs);
}
void setInfo(const std::string& gpu, unsigned w, unsigned h) {
    std::lock_guard<std::mutex> l(g.m);
    g.gpu = gpu;
    g.bbW = w;
    g.bbH = h;
}

void setWorldFrames(uint64_t completed) {
    g_worlds.store(completed, std::memory_order_release);
}
uint64_t worldFrames() {
    return g_worlds.load(std::memory_order_acquire);
}

void onPresent() {
    if (g_mode.load(std::memory_order_relaxed) <= 0) {
        g.valid = false; // the statistics window restarts when the counter is switched on
        return;
    }
    const int64_t now = qpcNow();
    std::lock_guard<std::mutex> l(g.m);
    const double f = qpcFreq();
    if (!g.valid) {
        g.valid = true;
        g.lastPresent = g.winStart = now;
        g.winPresents = 0;
        g.ringCount = g.ringPos = 0;
        g.flips0 = ps2perf::g_flips.load(std::memory_order_relaxed);
        g.worlds0 = worldFrames();
        g.ee0 = threadCpu(g_ee.load());
        g.gs0 = threadCpu(g_gs.load());
        g.proc0 = procCpu();
        return;
    }
    const float ms = (float)((double)(now - g.lastPresent) * 1000.0 / f);
    g.lastPresent = now;
    g.ring[g.ringPos] = ms;
    g.ringPos = (g.ringPos + 1) % kRing;
    g.ringCount = std::min(g.ringCount + 1, kRing);
    ++g.winPresents;
    const double el = (double)(now - g.winStart) / f;
    if (el < 0.5)
        return;
    g.presentedFps = (double)g.winPresents / el;
    const uint64_t fl = ps2perf::g_flips.load(std::memory_order_relaxed);
    g.gameFps = (double)(fl - g.flips0) / el;
    g.flips0 = fl;
    const auto worlds = worldFrames();
    g.worldFps = worlds >= g.worlds0 ? (worlds - g.worlds0) / el : 0;
    g.worlds0 = worlds;
    const int n = (int)std::min<uint64_t>(g.winPresents, (uint64_t)g.ringCount);
    float tmp[kRing];
    double sum = 0;
    float worst = 0;
    for (int i = 0; i < n; ++i) {
        const float v = g.ring[(g.ringPos - 1 - i + kRing * 2) % kRing];
        tmp[i] = v;
        sum += v;
        worst = std::max(worst, v);
    }
    g.avgMs = n ? sum / n : 0;
    g.worstMs = worst;
    if (g_mode.load() >= 2 && n > 0) {
        std::sort(tmp, tmp + n);
        const int k = std::max(0, (int)std::ceil(n * 0.99) - 1);
        g.low1Ms = tmp[std::min(k, n - 1)];
        const double ee = threadCpu(g_ee.load()), gs = threadCpu(g_gs.load()), pr = procCpu();
        g.cpuProc = std::max(0.0, (pr - g.proc0) / el * 100.0 / ncpu());
        g.cpuEe = ee >= 0 && g.ee0 >= 0 ? std::max(0.0, (ee - g.ee0) / el * 100.0) : -1;
        g.cpuGs = gs >= 0 && g.gs0 >= 0 ? std::max(0.0, (gs - g.gs0) / el * 100.0) : -1;
        g.ee0 = ee;
        g.gs0 = gs;
        g.proc0 = pr;
    }
    g.winStart = now;
    g.winPresents = 0;
    rebuild(g_mode.load());
}

std::vector<std::string> lines() {
    if (g_mode.load(std::memory_order_relaxed) <= 0)
        return {};
    std::lock_guard<std::mutex> l(g.m);
    if (g.out.empty())
        return {"..."};
    return g.out;
}
} // namespace drakan_fpsmeter
