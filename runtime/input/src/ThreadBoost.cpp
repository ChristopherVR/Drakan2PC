#include "input/ThreadBoost.h"

#include <cstdlib>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace drakan_input {

#if defined(_WIN32)
namespace {
bool enabled() {
    static const bool on = [] {
        const char* e = std::getenv("DRAKAN_THREAD_BOOST");
        return !(e && e[0] == '0');
    }();
    return on;
}

void boostProcess() {
    // PROCESS_POWER_THROTTLING_STATE through SetProcessInformation (declared locally so an older SDK still builds).
    // ControlMask bits set + StateMask bits clear = never throttle: EXECUTION_SPEED (EcoQoS) and
    // IGNORE_TIMER_RESOLUTION (keep timeBeginPeriod's 1 ms while the window is covered).
    struct PowerThrottling {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    } s = {1, 0x1u | 0x4u, 0};
    using Fn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    if (HMODULE k = GetModuleHandleW(L"kernel32.dll"))
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k, "SetProcessInformation"))))
            fn(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &s, sizeof s);
}
} // namespace

void boostThread(ThreadRole role) {
    if (!enabled())
        return;
    static const bool process = (boostProcess(), true);
    (void)process;
    if (role == ThreadRole::Input) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        return;
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    // MMCSS class "Games" through avrt.dll, loaded on demand (no import library); joined for the thread's life.
    using Fn = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
    static const Fn join = [] {
        HMODULE m = LoadLibraryW(L"avrt.dll");
        return m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "AvSetMmThreadCharacteristicsW")))
                 : nullptr;
    }();
    if (join) {
        DWORD task = 0;
        join(L"Games", &task);
    }
}
#else
void boostThread(ThreadRole) {}
#endif

} // namespace drakan_input
