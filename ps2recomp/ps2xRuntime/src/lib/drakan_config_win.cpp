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
const std::filesystem::path& exeDirPath() {
    static const std::filesystem::path dir = [] {
        std::wstring exe(32768, L'\0'); // the longest path Windows returns
        const DWORD n = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (n == 0 || n >= exe.size())
            return std::filesystem::path();
        exe.resize(n);
        return std::filesystem::path(exe).parent_path();
    }();
    return dir;
}

const std::string& exeDir() {
    static const std::string dir = [] {
        const std::wstring& wide = exeDirPath().native();
        if (wide.empty())
            return std::string();
        // The ANSI code page: what the narrow CRT and std::filesystem::path(std::string) callers of exeDir() expect.
        const int wideLen = static_cast<int>(wide.size());
        const int len = WideCharToMultiByte(CP_ACP, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(len > 0 ? len : 0), '\0');
        if (len > 0)
            WideCharToMultiByte(CP_ACP, 0, wide.data(), wideLen, out.data(), len, nullptr, nullptr);
        if (!out.empty() && out.back() != '\\' && out.back() != '/')
            out += '\\';
        return out;
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
