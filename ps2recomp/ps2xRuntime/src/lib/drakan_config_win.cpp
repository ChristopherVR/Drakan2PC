// Win32 part of the settings module (platform service: exe location, DPI awareness). The ini logic is in
// drakan_config.cpp.
#include "drakan_config.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdlib>

namespace drakan_cfg {
const std::string& exeDir() {
    static std::string dir = [] {
        char buf[MAX_PATH * 2];
        const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
        std::string p(buf, n);
        const size_t s = p.find_last_of("\\/");
        return s == std::string::npos ? std::string() : p.substr(0, s + 1);
    }();
    return dir;
}

void enableDpiAwareness() {
    // Restart from the settings menu: the old instance (DRAKAN_WAIT_PID) is closing, wait until it has released the
    // window, audio and saves.
    if (const char* wp = std::getenv("DRAKAN_WAIT_PID")) {
        if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)std::strtoul(wp, nullptr, 10))) {
            WaitForSingleObject(h, 15000);
            CloseHandle(h);
        }
        SetEnvironmentVariableA("DRAKAN_WAIT_PID", nullptr);
    }
    using Fn = BOOL(WINAPI*)(HANDLE);
    if (HMODULE u = GetModuleHandleA("user32.dll"))
        if (auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(u, "SetProcessDpiAwarenessContext"))))
            fn(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4))); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
}

} // namespace drakan_cfg
