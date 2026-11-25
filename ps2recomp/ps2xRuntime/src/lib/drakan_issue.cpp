#include "drakan_issue.h"

#include <string>

#if defined(_WIN32)
#include "drakan_issue_url.h"
#include "drakan_game.h"
#include "drakan_lifecycle.h"
#include "drakan_log.h"
#include "drakan_region.h"
#include "drakan_save_previews.h"
#include "ps2_runtime.h"

#include <windows.h>
#include <dxgi.h>
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
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace drakan_issue {
namespace {
namespace fs = std::filesystem;
constexpr char kHelperArgument[] = "--drakan-crash-reporter";
constexpr LONGLONG kLogLimit = 4 * 1024 * 1024;
using Handle = std::unique_ptr<void, decltype([](HANDLE h) { CloseHandle(h); })>;
HANDLE g_crashEvent = nullptr;
std::atomic_flag g_reporting;
std::mutex g_levelMutex;
std::string g_level;

std::string utf8(const wchar_t* value) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out.data(), size, nullptr, nullptr);
    out.pop_back();
    return out;
}

SystemDetails systemDetails() {
    SystemDetails system;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion =
        reinterpret_cast<RtlGetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (getVersion && getVersion(&version) == 0) {
        DWORD revision = 0, bytes = sizeof(revision);
        const auto build = version.dwBuildNumber;
        system.os = (build >= 22000 ? "Windows 11 (" : "Windows (") + std::to_string(version.dwMajorVersion) + "." +
                    std::to_string(version.dwMinorVersion) + "." + std::to_string(build);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                         RRF_RT_REG_DWORD, nullptr, &revision, &bytes) == ERROR_SUCCESS)
            system.os += "." + std::to_string(revision);
        system.os += ")";
    }

    wchar_t processor[128]{};
    DWORD bytes = sizeof(processor);
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                     RRF_RT_REG_SZ, nullptr, processor, &bytes) == ERROR_SUCCESS) {
        system.cpu = utf8(processor);
        const size_t trim = system.cpu.find_first_not_of(' ');
        if (trim != std::string::npos)
            system.cpu.erase(0, trim);
    }
    if (!system.cpu.empty())
        system.cpu += ", ";
    system.cpu += std::to_string(info.dwNumberOfProcessors) + " logical processors";

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory))
        system.ram = std::to_string(memory.ullTotalPhys / (1024 * 1024)) + " MiB";

    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT index = 0; index < 16; ++index) {
            IDXGIAdapter1* adapter = nullptr;
            if (FAILED(factory->EnumAdapters1(index, &adapter)))
                break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                if (!system.gpu.empty())
                    system.gpu += "; ";
                system.gpu += utf8(desc.Description);
                if (desc.DedicatedVideoMemory)
                    system.gpu += " (" + std::to_string(desc.DedicatedVideoMemory / (1024 * 1024)) + " MiB VRAM)";
            }
            adapter->Release();
        }
        factory->Release();
    }
    return system;
}

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

fs::path captureLog(HANDLE log, bool crashed, DWORD pid, DWORD status, const SystemDetails& system) {
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
    out << "OS: " << system.os << "\nCPU: " << system.cpu << "\nRAM: " << system.ram << "\nGPU: " << system.gpu << '\n';
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

bool regularLocalFile(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}

size_t copySaves(const fs::path& card, const fs::path& dest) {
    if (card.empty())
        return 0;
    size_t copied = 0;
    const std::wstring prefix = fs::path(drakan_region::build().saveId).wstring() + L"Slot";
    std::error_code ec;
    for (fs::directory_iterator slot(card, ec), end; !ec && slot != end; slot.increment(ec)) {
        const fs::path folder = slot->path();
        const auto name = folder.filename().wstring();
        const DWORD attrs = GetFileAttributesW(folder.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_DIRECTORY) ||
            (attrs & FILE_ATTRIBUTE_REPARSE_POINT) || !name.starts_with(prefix) || name.size() == prefix.size() ||
            name.find_first_not_of(L"0123456789", prefix.size()) != std::wstring::npos)
            continue;
        std::error_code fileError;
        for (fs::directory_iterator file(folder, fileError); !fileError && file != end; file.increment(fileError)) {
            const fs::path source = file->path();
            const auto ext = source.extension();
            const auto filename = source.filename().wstring();
            if ((ext != L".rsg" && ext != L".rlt" && !filename.ends_with(L".rsg.mods") &&
                 !filename.ends_with(L".rlt.mods")) ||
                !regularLocalFile(source))
                continue;
            std::error_code copyError;
            const auto target = dest / name;
            if (fs::file_size(source, copyError) > 32u * 1024u * 1024u || copyError)
                continue;
            fs::create_directories(target, copyError);
            if (!copyError && fs::copy_file(source, target / source.filename(), copyError))
                ++copied;
        }
    }
    return copied;
}

bool createZip(const fs::path& dir, const fs::path& zip) {
    wchar_t system[MAX_PATH]{};
    const UINT len = GetSystemDirectoryW(system, MAX_PATH);
    if (!len || len >= MAX_PATH)
        return false;
    const fs::path tar = fs::path(system) / L"tar.exe";
    std::wstring command =
        L"\"" + tar.wstring() + L"\" -a -cf \"" + zip.wstring() + L"\" -C \"" + dir.wstring() + L"\" .";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process))
        return false;
    Handle child(process.hProcess), thread(process.hThread);
    DWORD status = 1;
    if (WaitForSingleObject(child.get(), 30'000) != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), INFINITE);
        return false;
    }
    return GetExitCodeProcess(child.get(), &status) && status == 0 && regularLocalFile(zip);
}

fs::path bundleReport(const fs::path& log, const fs::path& card, bool includeSaves, bool includeScreenshot) {
    if (log.empty())
        return {};
    const fs::path staging = log.parent_path() / log.stem();
    std::error_code ec;
    if (!fs::create_directory(staging, ec))
        return {};
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{staging};
    if (!fs::copy_file(log, staging / L"session.log", ec))
        return {};
    const bool savesMissing = includeSaves && copySaves(card, staging / L"saves") == 0;
    bool screenshotMissing = false;
    if (includeScreenshot) {
        const auto screenshot = staging / L"gameplay.png";
        screenshotMissing = !drakan_save_previews::writeGameplayScreenshot(screenshot);
        if (screenshotMissing)
            fs::remove(screenshot, ec);
    }
    if (savesMissing || screenshotMissing) {
        std::ofstream note(staging / L"attachments.txt");
        if (savesMissing)
            note << "No save slot files could be copied from the active campaign.\n";
        if (screenshotMissing)
            note << "No gameplay screenshot was available.\n";
    }
    fs::path zip = log;
    zip.replace_extension(L".zip");
    fs::path pending = zip;
    pending.replace_extension(L".pending.zip");
    if (createZip(staging, pending) && MoveFileExW(pending.c_str(), zip.c_str(), MOVEFILE_WRITE_THROUGH))
        return zip;
    fs::remove(pending, ec);
    return {};
}

void offerReport(bool crashed, const fs::path& log, const SystemDetails& system, const fs::path& card = {},
                 std::string_view screen = {}, std::string_view campaign = {}, std::string_view level = {}) try {
    constexpr UINT kNotice = MB_SETFOREGROUND | MB_TOPMOST;
    const wchar_t* title = crashed ? L"Drakan crashed" : L"Report an Issue";
    const auto address = formUrl(crashed, "Windows", screen, campaign, level, system);
    const std::wstring url(address.begin(), address.end());
    std::wstring text = crashed ? L"Drakan closed unexpectedly. Open the crash report form on GitHub?"
                                : L"Open the bug report form on GitHub?";
    if (log.empty())
        text += L"\n\nThe game could not save a copy of the session log.";
    else
        text += L"\n\nReview the files in the report folder before attaching them to GitHub.";
    if (MessageBoxW(nullptr, text.c_str(), title, kNotice | MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
        return;

    if (!crashed && !log.empty()) {
        const auto ask = [&](const wchar_t* question) {
            return MessageBoxW(nullptr, question, title, kNotice | MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES;
        };
        const bool saves = ask(L"Include the current campaign's save slots in the report ZIP?");
        const bool screenshot = screen != "Title menu" && ask(L"Include a screenshot of the last gameplay frame?");
        const fs::path zip = bundleReport(log, card, saves, screenshot);
        if (zip.empty())
            MessageBoxW(nullptr, L"The ZIP could not be created. The session log is available in the report folder.",
                        title, kNotice | MB_OK | MB_ICONWARNING);
    }

    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!log.empty())
        ShellExecuteW(nullptr, L"open", log.parent_path().c_str(), nullptr, nullptr, SW_SHOWNOACTIVATE);
    if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <=
        32) {
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
    SystemDetails system;
    try {
        system = systemDetails();
        captured = captureLog(log, true, GetProcessId(parent), status, system);
    } catch (...) {
        // The prompt can still open the crash form when the log cannot be copied.
    }
    const wchar_t* prompt = _wgetenv(L"DRAKAN_CRASH_PROMPT");
    if (!prompt || std::wcscmp(prompt, L"0") != 0)
        offerReport(true, captured, system);
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
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::BeforeLevelLoad, [](const auto&) {
            std::lock_guard lock(g_levelMutex);
            g_level.clear();
        });
        drakan_lifecycle::subscribe(drakan_lifecycle::Kind::AfterLevelLoad, [](const auto& event) {
            if (event.ok) {
                std::lock_guard lock(g_levelMutex);
                g_level = event.level;
            }
        });
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

void report(std::string_view screen, std::string_view campaign, std::string_view level) {
    if (g_reporting.test_and_set())
        return;
    try {
        const auto& io = PS2Runtime::getIoPaths();
        const fs::path card = io.mcRoot.empty() ? io.elfDirectory / "mc0" : io.mcRoot;
        std::string location(level);
        if (location.empty() && screen != "Title menu") {
            std::lock_guard lock(g_levelMutex);
            location = g_level;
        }
        std::thread([card, screen = std::string(screen),
                     campaign = campaign.empty() ? std::string(drakan_game::currentName()) : std::string(campaign),
                     level = std::move(location)] {
            fs::path captured;
            SystemDetails system;
            try {
                system = systemDetails();
                drakan_log::flush();
                const Handle log = openLog();
                captured = captureLog(log.get(), false, GetCurrentProcessId(), 0, system);
            } catch (...) {
            }
            offerReport(false, captured, system, card, screen, campaign, level);
            g_reporting.clear();
        }).detach();
    } catch (...) {
        g_reporting.clear();
        offerReport(false, {}, {}, {}, screen, campaign, level);
    }
}
} // namespace drakan_issue
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>

namespace drakan_issue {
bool runHelper(int, char*[]) {
    return false;
}
void watchForCrashes() {}
void markCrash() {
    MAIN_THREAD_ASYNC_EM_ASM({ Module['onGameCrash'] && Module['onGameCrash'](); });
}
bool available() {
    return true;
}
void report(std::string_view screen, std::string_view campaign, std::string_view level) {
    // Copy the strings on the browser thread before the worker releases them.
    const std::string screenText(screen), campaignText(campaign), levelText(level);
    MAIN_THREAD_EM_ASM(
        {
            const show = Module["onIssueReport"];
            if (show) {
                const screen = UTF8ToString($0);
                const campaign = UTF8ToString($1);
                const level = UTF8ToString($2);
                setTimeout(function() { show(screen, campaign, level); }, 0);
            }
        },
        screenText.c_str(), campaignText.c_str(), levelText.c_str());
}
} // namespace drakan_issue
#elif !defined(__linux__) || defined(__ANDROID__)
namespace drakan_issue {
bool runHelper(int, char*[]) {
    return false;
}
void watchForCrashes() {}
void markCrash() {}
bool available() {
    return false;
}
void report(std::string_view, std::string_view, std::string_view) {}
} // namespace drakan_issue
#endif
