#include "ps2_perf.h"

#include "drakan_polyclip.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace ps2perf {
std::atomic<uint64_t> g_flips{0}, g_flips2{0};
std::atomic<uint64_t> g_gifPackets{0}, g_gifBytes{0};
std::atomic<uint64_t> g_vu1Cycles{0}, g_vu1Runs{0}, g_vu1Pairs{0};

#if defined(_WIN32)
namespace {
std::atomic<bool> g_stop{false};
std::thread g_reporter;
std::thread g_sampler;
std::thread g_eeSampler;

double threadCpuSeconds(HANDLE h) {
    FILETIME c, e, k, u;
    if (!GetThreadTimes(h, &c, &e, &k, &u))
        return 0.0;
    ULARGE_INTEGER ku, uu;
    ku.LowPart = k.dwLowDateTime;
    ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime;
    uu.HighPart = u.dwHighDateTime;
    return static_cast<double>(ku.QuadPart + uu.QuadPart) * 1e-7;
}

std::vector<DWORD> listThreads() {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    const DWORD pid = GetCurrentProcessId();
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid)
                out.push_back(te.th32ThreadID);
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return out;
}

// Stall forensics (PS2X_PERF): the call chain of a busy or blocked host thread as module+offset frames, taken with
// Suspend/GetThreadContext/RtlVirtualUnwind. Main-module frames print as RVAs (resolve with the linker .map).
std::string sampleThreadChain(DWORD tid) {
    HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!h)
        return "(open failed)";
    std::string out;
    if (SuspendThread(h) != static_cast<DWORD>(-1)) {
        CONTEXT ctx{};
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(h, &ctx)) {
            CONTEXT c = ctx;
            const uint64_t mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            for (int depth = 0; depth < 28 && c.Rip != 0; ++depth) {
                HMODULE m = nullptr;
                char name[MAX_PATH] = "?";
                uint64_t base = 0;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(c.Rip), &m) &&
                    m) {
                    GetModuleFileNameA(m, name, MAX_PATH);
                    base = reinterpret_cast<uintptr_t>(m);
                    const char* slash = std::strrchr(name, '\\');
                    if (slash)
                        std::memmove(name, slash + 1, std::strlen(slash));
                }
                char b[160];
                std::snprintf(b, sizeof(b), "%s%s+0x%llx", depth ? " < " : "", base == mainBase ? "exe" : name,
                              static_cast<unsigned long long>(c.Rip - base));
                out += b;
                DWORD64 imageBase = 0;
                PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
                if (!fe) {
                    c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                    c.Rsp += 8;
                } else {
                    PVOID handlerData = nullptr;
                    DWORD64 establisher = 0;
                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fe, &c, &handlerData, &establisher, nullptr);
                }
            }
        }
        ResumeThread(h);
    }
    CloseHandle(h);
    return out;
}
} // namespace

void start(void* gameThreadNativeHandle, uint64_t (*vsyncTick)(void*), void* vsyncUser) {
    const char* perf = std::getenv("PS2X_PERF");
    if (perf && perf[0] && perf[0] != '0') {
        HANDLE gameH = reinterpret_cast<HANDLE>(gameThreadNativeHandle);
        const DWORD gameTid = GetThreadId(gameH);
        g_reporter = std::thread([=]() {
            std::map<DWORD, double> prev;
            uint64_t prevFlips2 = 0, prevFlips = 0, prevVs = 0, prevGp = 0, prevGb = 0, prevVc = 0, prevVr = 0,
                     prevVp = 0;
            auto prevT = std::chrono::steady_clock::now();
            int line = 0;
            int stalledSeconds = 0;
            while (!g_stop.load()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double>(now - prevT).count();
                prevT = now;
                const uint64_t flips = g_flips.load();
                const uint64_t vs = vsyncTick ? vsyncTick(vsyncUser) : 0;
                double gameCpu = 0.0, otherCpu = 0.0;
                std::string others;
                std::vector<DWORD> busy, all;
                for (DWORD tid : listThreads()) {
                    HANDLE h = OpenThread(THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
                    if (!h)
                        continue;
                    const double cpu = threadCpuSeconds(h);
                    CloseHandle(h);
                    const double d = cpu - prev[tid];
                    prev[tid] = cpu;
                    all.push_back(tid);
                    if (tid == gameTid)
                        gameCpu = d;
                    else if (d > 0.02 * dt) {
                        otherCpu += d;
                        if (d > 0.5 * dt)
                            busy.push_back(tid);
                        char b[48];
                        std::snprintf(b, sizeof(b), " t%lu:%.0f%%", static_cast<unsigned long>(tid), 100.0 * d / dt);
                        others += b;
                    }
                }
                const uint64_t gp = g_gifPackets.load(), gb = g_gifBytes.load();
                const double gifPk = (gp - prevGp) / dt, gifMb = (gb - prevGb) / dt / 1048576.0;
                prevGp = gp;
                prevGb = gb;
                const uint64_t vc = g_vu1Cycles.load(), vr = g_vu1Runs.load();
                const double vuMc = (vc - prevVc) / dt / 1e6, vuRuns = (vr - prevVr) / dt;
                const uint64_t vp = g_vu1Pairs.load();
                const double vuMp = (vp - prevVp) / dt / 1e6;
                prevVc = vc;
                prevVr = vr;
                prevVp = vp;
                const uint64_t df2 = g_flips2.load() - prevFlips2;
                prevFlips2 += df2;
                const uint64_t df = flips - prevFlips;
                prevFlips = flips;
                const uint64_t dv = vs - prevVs;
                prevVs = vs;
                uint64_t clipNs = 0, clipFans = 0;
                drakan_polyclip::takeHookStats(clipNs, clipFans);
                std::fprintf(
                    stderr,
                    "[perf %d] flips/s=%.1f flips2/s=%.1f vblank/s=%.1f gifpk/s=%.0f gifMB/s=%.1f vu1Mcyc/s=%.1f "
                    "vu1runs/s=%.0f vu1Mpairs/s=%.1f ee_thread_cpu=%.0f%% ee_ms/flip=%.2f other_cpu=%.0f%% "
                    "clip_fans/s=%.0f clip_us/s=%.1f clip_ee_pct=%.3f%s\n",
                    line++, df / dt, df2 / dt, dv / dt, gifPk, gifMb, vuMc, vuRuns, vuMp, 100.0 * gameCpu / dt,
                    df ? 1000.0 * gameCpu / df : 0.0, 100.0 * otherCpu / dt, clipFans / dt, clipNs / dt / 1e3,
                    gameCpu > 0 ? 100.0 * (clipNs / 1e9) / gameCpu : 0.0, others.c_str());
                // No flips for three seconds: print where the EE thread and every busy thread are, once per second.
                stalledSeconds = df == 0 ? stalledSeconds + 1 : 0;
                if (stalledSeconds >= 3 && stalledSeconds <= 8) {
                    std::fprintf(stderr, "[perf-stall] ee thread %lu: %s\n", static_cast<unsigned long>(gameTid),
                                 sampleThreadChain(gameTid).c_str());
                    if (stalledSeconds == 3)
                        for (DWORD tid : all)
                            if (tid != gameTid && tid != GetCurrentThreadId())
                                std::fprintf(stderr, "[perf-stall] thread %lu: %s\n", static_cast<unsigned long>(tid),
                                             sampleThreadChain(tid).c_str());
                    for (DWORD tid : busy)
                        std::fprintf(stderr, "[perf-stall] busy thread %lu: %s\n", static_cast<unsigned long>(tid),
                                     sampleThreadChain(tid).c_str());
                }
            }
        });
    }

    if (const char* fa = std::getenv("DRAKAN_FRAME_AUDIT"); fa && std::atoi(fa) >= 3 && gameThreadNativeHandle) {
        // Diagnostic: timestamped RIP samples of the EE thread, "[ft] X <us> <rva> <caller rva>".
        HANDLE gameH = reinterpret_cast<HANDLE>(gameThreadNativeHandle);
        HANDLE dup = nullptr;
        DuplicateHandle(GetCurrentProcess(), gameH, GetCurrentProcess(), &dup,
                        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0);
        g_eeSampler = std::thread([dup]() {
            const uint64_t mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mainBase);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(mainBase + dos->e_lfanew);
            const uint64_t mainSize = nt->OptionalHeader.SizeOfImage;
            while (!g_stop.load() && dup) {
                if (SuspendThread(dup) != static_cast<DWORD>(-1)) {
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    uint64_t rip = 0, caller = 0;
                    if (GetThreadContext(dup, &ctx)) {
                        rip = ctx.Rip;
                        CONTEXT c = ctx;
                        for (int depth = 0; depth < 14 && c.Rip != 0; ++depth) {
                            if (c.Rip - mainBase < mainSize) {
                                caller = c.Rip - mainBase;
                                break;
                            }
                            DWORD64 imageBase = 0;
                            PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
                            if (!fe) {
                                c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                                c.Rsp += 8;
                            } else {
                                PVOID handlerData = nullptr;
                                DWORD64 establisher = 0;
                                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fe, &c, &handlerData,
                                                 &establisher, nullptr);
                            }
                        }
                    }
                    ResumeThread(dup);
                    std::fprintf(stderr, "[ft] X %lld %llx %llx %llx\n",
                                 (long long)std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count(),
                                 (unsigned long long)(rip - mainBase), (unsigned long long)caller,
                                 (unsigned long long)mainSize);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
        });
    }

    const char* hp = std::getenv("PS2X_HOSTPROF");
    if (hp && std::atof(hp) > 0.0) {
        const double seconds = std::atof(hp);
        const char* dl = std::getenv("PS2X_HOSTPROF_DELAY");
        const double delay = dl ? std::atof(dl) : 0.0;
        g_sampler = std::thread([=]() {
            const DWORD self = GetCurrentThreadId();
            std::this_thread::sleep_for(std::chrono::duration<double>(delay));
            struct Sample {
                DWORD tid;
                uint64_t rip;
                uint64_t caller; // first return address inside the main module (for samples in system DLLs), as RVA
            };
            const uint64_t mainBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            uint64_t mainSize = 0;
            {
                const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mainBase);
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(mainBase + dos->e_lfanew);
                mainSize = nt->OptionalHeader.SizeOfImage;
            }
            std::vector<Sample> samples;
            samples.reserve(2000000);
            std::map<DWORD, HANDLE> handles;
            const auto t0 = std::chrono::steady_clock::now();
            auto lastList = t0 - std::chrono::seconds(10);
            while (!g_stop.load() &&
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastList > std::chrono::seconds(1)) {
                    lastList = now;
                    for (DWORD tid : listThreads())
                        if (tid != self && !handles.count(tid)) {
                            HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                                  FALSE, tid);
                            if (h)
                                handles[tid] = h;
                        }
                }
                for (auto& [tid, h] : handles) {
                    if (SuspendThread(h) == static_cast<DWORD>(-1))
                        continue;
                    CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                    if (GetThreadContext(h, &ctx) && samples.size() < samples.capacity()) {
                        uint64_t caller = 0;
                        if (ctx.Rip - mainBase >= mainSize) {
                            CONTEXT c = ctx;
                            for (int depth = 0; depth < 14 && c.Rip != 0; ++depth) {
                                DWORD64 imageBase = 0;
                                PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
                                if (!fe) {
                                    c.Rip = *reinterpret_cast<const DWORD64*>(c.Rsp);
                                    c.Rsp += 8;
                                } else {
                                    PVOID handlerData = nullptr;
                                    DWORD64 establisher = 0;
                                    RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fe, &c, &handlerData,
                                                     &establisher, nullptr);
                                }
                                if (c.Rip - mainBase < mainSize) {
                                    caller = c.Rip - mainBase;
                                    break;
                                }
                            }
                        }
                        samples.push_back({tid, ctx.Rip, caller});
                    }
                    ResumeThread(h);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(700));
            }
            // resolve the module of each distinct page, write csv: tid,module,rva,count
            std::map<std::tuple<DWORD, std::string, uint64_t, uint64_t>, uint64_t> hist;
            std::map<uint64_t, std::pair<std::string, uint64_t>> modCache;
            for (const Sample& s : samples) {
                const uint64_t page = s.rip >> 12;
                auto it = modCache.find(page);
                if (it == modCache.end()) {
                    HMODULE m = nullptr;
                    std::string name = "?";
                    uint64_t base = 0;
                    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCWSTR>(s.rip), &m) &&
                        m) {
                        char buf[MAX_PATH];
                        GetModuleFileNameA(m, buf, MAX_PATH);
                        name = buf;
                        const size_t p = name.find_last_of("\\/");
                        if (p != std::string::npos)
                            name = name.substr(p + 1);
                        base = reinterpret_cast<uintptr_t>(m);
                    }
                    it = modCache.emplace(page, std::make_pair(name, base)).first;
                }
                ++hist[{s.tid, it->second.first, s.rip - it->second.second, s.caller}];
            }
            FILE* f = std::fopen("hostprof.csv", "w");
            if (f) {
                std::fprintf(f, "tid,module,rva,count,caller\n");
                for (auto& [k, c] : hist)
                    std::fprintf(f, "%lu,%s,%llx,%llu,%llx\n", static_cast<unsigned long>(std::get<0>(k)),
                                 std::get<1>(k).c_str(), static_cast<unsigned long long>(std::get<2>(k)),
                                 static_cast<unsigned long long>(c), static_cast<unsigned long long>(std::get<3>(k)));
                std::fclose(f);
            }
            std::fprintf(stderr, "[hostprof] %zu samples written to hostprof.csv\n", samples.size());
            for (auto& [tid, h] : handles)
                CloseHandle(h);
        });
    }
}

void stop() {
    g_stop.store(true);
    if (g_reporter.joinable())
        g_reporter.join();
    if (g_sampler.joinable())
        g_sampler.join();
    if (g_eeSampler.joinable())
        g_eeSampler.join();
}
#else
void start(void*, uint64_t (*)(void*), void*) {}
void stop() {}
#endif
} // namespace ps2perf
