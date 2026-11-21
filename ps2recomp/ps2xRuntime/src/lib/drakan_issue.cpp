#include "drakan_issue.h"

#if defined(_WIN32)
#include "drakan_log.h"
#include "drakan_region.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace drakan_issue {
namespace {
namespace fs = std::filesystem;
constexpr char kHelperArgument[] = "--drakan-crash-reporter";
constexpr wchar_t kBugUrl[] = L"https://github.com/ChristopherVR/Drakan2PC/issues/new?template=bug-report.yml";
constexpr wchar_t kCrashUrl[] = L"https://github.com/ChristopherVR/Drakan2PC/issues/new?template=crash-report.yml";
constexpr LONGLONG kLogLimit = 4 * 1024 * 1024;
using Handle = std::unique_ptr<void, decltype([](HANDLE h) { CloseHandle(h); })>;
HANDLE g_crashEvent = nullptr;
std::atomic_flag g_reporting;

Handle openLog(bool inherit = false) {
    if (drakan_log::path().empty())
        return {};
    SECURITY_ATTRIBUTES security{sizeof security, nullptr, inherit};
    const HANDLE h =
        CreateFileW(drakan_log::path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    return Handle(h == INVALID_HANDLE_VALUE ? nullptr : h);
}

bool copyLog(std::ostream& out, HANDLE source, LONGLONG offset, LONGLONG length) {
    LARGE_INTEGER start{};
    start.QuadPart = offset;
    if (!SetFilePointerEx(source, start, nullptr, FILE_BEGIN))
        return false;
    std::array<char, 64 * 1024> buffer;
    while (length > 0) {
        DWORD count = 0;
        const auto requested = static_cast<DWORD>(std::min<LONGLONG>(length, buffer.size()));
        if (!ReadFile(source, buffer.data(), requested, &count, nullptr) || count == 0)
            return false;
        if (!out.write(buffer.data(), count))
            return false;
        length -= count;
    }
    return true;
}

fs::path reportDirectory() {
    if (const wchar_t* logs = _wgetenv(L"DRAKAN_LOG_DIR"); logs && *logs)
        return fs::path(logs) / L"reports";
    if (const wchar_t* appData = _wgetenv(L"LOCALAPPDATA"); appData && *appData)
        return fs::path(appData) / L"DrakanPC" / L"reports";
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    return ec ? fs::path() : temp / L"DrakanPC" / L"reports";
}

fs::path captureLog(HANDLE log, bool crashed, DWORD pid, DWORD status) {
    const fs::path base = reportDirectory();
    if (base.empty())
        return {};
    std::error_code ec;
    const fs::path dir = fs::absolute(base, ec);
    if (ec)
        return {};
    fs::create_directories(dir, ec);
    if (ec)
        return {};

    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    char stamp[48];
    std::snprintf(stamp, sizeof stamp, "%04u%02u%02u-%02u%02u%02u-%03uZ", utc.wYear, utc.wMonth, utc.wDay, utc.wHour,
                  utc.wMinute, utc.wSecond, utc.wMilliseconds);
    const fs::path path =
        dir / (std::string(crashed ? "drakan-crash-" : "drakan-issue-") + stamp + "-" + std::to_string(pid) + ".log");
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return {};
    out << "Drakan PC " << (crashed ? "crash" : "issue") << " report\n"
        << "Captured (UTC): " << stamp << "\nPlatform: Windows\nDisc build: " << drakan_region::build().serial << '\n';
    if (crashed)
        out << "Process exit status: 0x" << std::hex << status << std::dec << '\n';
    out << "\n--- Session log ---\n";
    LARGE_INTEGER size{};
    if (!log || !GetFileSizeEx(log, &size) || size.QuadPart == 0) {
        out << "No session log was available.\n";
    } else if (size.QuadPart <= kLogLimit) {
        if (!copyLog(out, log, 0, size.QuadPart))
            out << "\nThe session log could only be copied in part.\n";
    } else {
        constexpr LONGLONG kLogHead = 64 * 1024;
        constexpr LONGLONG kLogTail = kLogLimit - kLogHead;
        const bool head = copyLog(out, log, 0, kLogHead);
        out << "\n--- Middle of session log omitted (4 MiB capture limit) ---\n";
        if (!copyLog(out, log, size.QuadPart - kLogTail, kLogTail) || !head)
            out << "\nThe session log could only be copied in part.\n";
    }
    out.close();
    if (!out) {
        fs::remove(path, ec);
        return {};
    }
    return path;
}

void offerReport(bool crashed, const fs::path& log) try {
    constexpr UINT kNotice = MB_SETFOREGROUND | MB_TOPMOST;
    const wchar_t* title = crashed ? L"Drakan crashed" : L"Report an Issue";
    const wchar_t* url = crashed ? kCrashUrl : kBugUrl;
    std::wstring text = crashed ? L"Drakan closed unexpectedly. Open the crash report form on GitHub?"
                                : L"Open the bug report form on GitHub?";
    if (log.empty())
        text += L"\n\nThe game could not save a copy of the session log.";
    else
        text += L"\n\nThe report folder will open with a copy of the session log. Review it for personal information "
                L"before attaching it to your report.\n\n" +
                log.wstring();
    if (MessageBoxW(nullptr, text.c_str(), title, kNotice | MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
        return;

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!log.empty())
        ShellExecuteW(nullptr, L"open", log.parent_path().c_str(), nullptr, nullptr, SW_SHOWNOACTIVATE);
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        const std::wstring error = std::wstring(L"Drakan could not open your browser. Open this address to report the "
                                                L"issue.\n\n") +
                                   url;
        MessageBoxW(nullptr, error.c_str(), title, kNotice | MB_OK | MB_ICONWARNING);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
} catch (...) {
    MessageBoxW(nullptr, L"Open github.com/ChristopherVR/Drakan2PC/issues to report the issue.", L"Drakan PC",
                MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
}

Handle inheritedHandle(const char* value) {
    uintptr_t handle = 0;
    const char* end = value + std::strlen(value);
    const auto parsed = std::from_chars(value, end, handle);
    return parsed.ec == std::errc{} && parsed.ptr == end ? Handle(reinterpret_cast<HANDLE>(handle)) : Handle{};
}

void watchParent(HANDLE parent, HANDLE event, HANDLE log) {
    DWORD status = 0;
    if (WaitForSingleObject(parent, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(parent, &status))
        return;
    // Explicit fatal paths signal the event. Windows can terminate the process with an exception status before any
    // handler in the game executes (for example, during a fail-fast).
    constexpr DWORD kControlCExit = 0xc000013au;
    constexpr DWORD kFatalAppExit = 0x40000015u;
    const bool exceptionExit = (status >= 0x80000000u && status != kControlCExit) || status == kFatalAppExit;
    if (WaitForSingleObject(event, 0) != WAIT_OBJECT_0 && !exceptionExit)
        return;

    fs::path captured;
    try {
        captured = captureLog(log, true, GetProcessId(parent), status);
    } catch (...) {
        // The prompt can still open the crash form when the log cannot be copied.
    }
    const wchar_t* prompt = _wgetenv(L"DRAKAN_CRASH_PROMPT");
    if (!prompt || std::wcscmp(prompt, L"0") != 0)
        offerReport(true, captured);
}
} // namespace

bool runHelper(int argc, char* argv[]) {
    if (argc < 2 || std::strcmp(argv[1], kHelperArgument) != 0)
        return false;
    if (argc == 5) {
        const Handle parent = inheritedHandle(argv[2]);
        const Handle event = inheritedHandle(argv[3]);
        const Handle log = inheritedHandle(argv[4]);
        if (parent && event)
            watchParent(parent.get(), event.get(), log.get());
    }
    return true;
}

void watchForCrashes() {
    try {
        std::wstring exe(32768, L'\0');
        const DWORD size = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (size == 0 || size == exe.size())
            return;
        exe.resize(size);

        Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId()));
        SECURITY_ATTRIBUTES security{sizeof security, nullptr, TRUE};
        Handle event(CreateEventW(&security, TRUE, FALSE, nullptr));
        Handle log = openLog(true);
        if (!parent || !event)
            return;
        HANDLE handles[] = {parent.get(), event.get(), log.get()};
        const size_t count = log ? 3 : 2;
        std::wstring command = L"\"" + exe + L"\" --drakan-crash-reporter";
        for (HANDLE h : handles)
            command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(h));

        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &bytes))
            return;
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof startup;
        startup.lpAttributeList = attrs;
        PROCESS_INFORMATION process{};
        const BOOL started = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                                       count * sizeof(HANDLE), nullptr, nullptr) &&
                             CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                                            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                                            &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(attrs);
        if (started) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            SetHandleInformation(event.get(), HANDLE_FLAG_INHERIT, 0);
            g_crashEvent = event.release(); // needed until the game process exits
        }
    } catch (...) {
        // Reporting is optional during gameplay, even if the helper could not start.
    }
}

void markCrash() {
    if (g_crashEvent)
        SetEvent(g_crashEvent);
}

bool available() {
    return true;
}

void report() {
    if (g_reporting.test_and_set())
        return;
    try {
        std::thread([] {
            fs::path captured;
            try {
                drakan_log::flush();
                const Handle log = openLog();
                captured = captureLog(log.get(), false, GetCurrentProcessId(), 0);
            } catch (...) {
            }
            offerReport(false, captured);
            g_reporting.clear();
        }).detach();
    } catch (...) {
        g_reporting.clear();
        offerReport(false, {});
    }
}
} // namespace drakan_issue
#else
namespace drakan_issue {
bool runHelper(int, char*[]) {
    return false;
}
void watchForCrashes() {}
void markCrash() {}
bool available() {
    return false;
}
void report() {}
} // namespace drakan_issue
#endif
