#include "drakan_crash.h"

#if defined(_WIN32)
#include "drakan_config.h"
#include "drakan_log.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <windows.h>
#include <crtdbg.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>

void ps2TraceRingFlush(const char* why);
std::string ps2FormatDispatchHistoryForCrash();

namespace drakan_crash {
namespace {
PS2Runtime* g_rt = nullptr;
std::atomic<int> g_inCrash{0};

typedef BOOL(WINAPI* MiniDumpWriteDumpFn)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);

void writeDump(EXCEPTION_POINTERS* ep) {
    const char* off = std::getenv("DRAKAN_CRASH_DUMP");
    if (off && off[0] == '0')
        return;
    HMODULE dh = LoadLibraryA("dbghelp.dll");
    if (!dh)
        return;
    auto fn = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dh, "MiniDumpWriteDump"));
    if (!fn) {
        FreeLibrary(dh);
        return;
    }
    const std::string path = drakan_cfg::exeDir() + "crash_" + std::to_string(GetCurrentProcessId()) + ".dmp";
    HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        FreeLibrary(dh);
        return;
    }
    struct MEI {
        DWORD ThreadId;
        EXCEPTION_POINTERS* ExceptionPointers;
        BOOL ClientPointers;
    } mei{GetCurrentThreadId(), ep, FALSE};
    // MiniDumpNormal (0): stacks + module list; the 32 MB guest RAM is deliberately not included.
    const BOOL written =
        fn(GetCurrentProcess(), GetCurrentProcessId(), f, 0, ep ? static_cast<void*>(&mei) : nullptr, nullptr, nullptr);
    const DWORD error = written ? ERROR_SUCCESS : GetLastError();
    CloseHandle(f);
    FreeLibrary(dh);
    if (written)
        std::fprintf(stderr, "[crash] minidump written: %s\n", path.c_str());
    else
        std::fprintf(stderr, "[crash] minidump failed: error=%lu path=%s\n", (unsigned long)error, path.c_str());
}

void dumpGuestState() {
    if (!g_rt)
        return;
    uint32_t pc = 0, ra = 0, sp = 0;
    int tid = 0;
    const bool ok = g_rt->eeScheduler().sampleLive(pc, ra, sp, tid);
    std::fprintf(stderr, "[crash] guest live: ok=%d tid=%d pc=0x%x ra=0x%x sp=0x%x\n", ok, tid, pc, ra, sp);
    if (const R5900Context* c = g_rt->eeScheduler().liveContextForCrash()) {
        static const char* names[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2",
                                        "t3",   "t4", "t5", "t6", "t7", "s0", "s1", "s2", "s3", "s4", "s5",
                                        "s6",   "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};
        for (int i = 0; i < 32; i += 4) {
            uint64_t lo[4];
            for (int k = 0; k < 4; ++k)
                lo[k] = static_cast<uint64_t>(_mm_cvtsi128_si64(c->r[i + k]));
            std::fprintf(stderr, "[crash] %s=%016llx %s=%016llx %s=%016llx %s=%016llx\n", names[i],
                         (unsigned long long)lo[0], names[i + 1], (unsigned long long)lo[1], names[i + 2],
                         (unsigned long long)lo[2], names[i + 3], (unsigned long long)lo[3]);
        }
    }
    std::fprintf(stderr, "[crash] dispatch history (this thread): %s\n", ps2FormatDispatchHistoryForCrash().c_str());
    ps2TraceRingFlush("crash");
}

LONG WINAPI filter(EXCEPTION_POINTERS* ep) {
    if (g_inCrash.exchange(1))
        return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void* addr = ep->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCSTR>(addr), &mod);
    char modName[MAX_PATH] = "?";
    if (mod)
        GetModuleFileNameA(mod, modName, MAX_PATH);
    std::fprintf(stderr, "[crash] unhandled exception 0x%08lx at %p (module %s + 0x%llx) thread=%lu\n",
                 (unsigned long)code, addr, modName,
                 (unsigned long long)(reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod)),
                 (unsigned long)GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
        std::fprintf(stderr, "[crash] access violation: %s address %p\n",
                     ep->ExceptionRecord->ExceptionInformation[0] == 0   ? "read"
                     : ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write"
                                                                         : "execute",
                     (void*)ep->ExceptionRecord->ExceptionInformation[1]);
    std::fprintf(stderr, "[crash] host rip=%p rsp=%p rbx=%p rsi=%p rdi=%p\n", (void*)ep->ContextRecord->Rip,
                 (void*)ep->ContextRecord->Rsp, (void*)ep->ContextRecord->Rbx, (void*)ep->ContextRecord->Rsi,
                 (void*)ep->ContextRecord->Rdi);
    writeDump(ep);
    dumpGuestState();
    drakan_log::flush(1500);
    return EXCEPTION_CONTINUE_SEARCH; // let WER / the debugger terminate the process
}
} // namespace

void install(PS2Runtime* rt) {
    g_rt = rt;
    SetUnhandledExceptionFilter(filter);
    // abort() / std::terminate must not open the CRT "abnormal termination" dialog on an unattended run
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // DRAKAN_CRASH_TEST=<seconds>[:abort|:throw]: self test of the crash report (null write on a helper thread /
    // abort() / uncaught exception)
    if (const char* t = std::getenv("DRAKAN_CRASH_TEST")) {
        const double secs = std::atof(t);
        const char* mode = std::strchr(t, ':');
        const int kind = !mode ? 0 : (mode[1] == 'a' ? 1 : 2);
        std::thread([secs, kind]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(secs * 1000.0)));
            if (kind == 0)
                *static_cast<volatile int*>(nullptr) = 1;
            else if (kind == 1)
                std::abort();
            else {
                // MSVC worker threads can start with the CRT's default terminate handler.
                // Install this explicitly so the self-test exercises our fatal reporting path.
                std::set_terminate([]() { fatal("std::terminate (crash self-test thread)"); });
                throw std::runtime_error("DRAKAN_CRASH_TEST");
            }
        }).detach();
    }
}

void fatal(const char* why) {
    if (g_inCrash.exchange(1))
        return;
    std::fprintf(stderr, "[crash] fatal: %s\n", why ? why : "?");
    writeDump(nullptr);
    dumpGuestState();
    drakan_log::flush(1500);
    TerminateProcess(GetCurrentProcess(), 3);
}
} // namespace drakan_crash
#else
namespace drakan_crash {
void install(PS2Runtime*) {}
void fatal(const char*) {}
} // namespace drakan_crash
#endif
