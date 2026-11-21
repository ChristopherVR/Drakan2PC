#include "drakan_update_installer_win.h"
#include "drakan_window.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <winver.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

extern "C" void* GetWindowHandle(void);

namespace drakan_update_installer {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
using Sha256 = std::array<BYTE, 32>;
using HttpHandle = std::unique_ptr<void, decltype([](HINTERNET handle) { WinHttpCloseHandle(handle); })>;
using FileHandle = std::unique_ptr<void, decltype([](HANDLE handle) { CloseHandle(handle); })>;

constexpr wchar_t kSetupKey[] = L"Software\\DrakanPC\\Setup";
constexpr wchar_t kUpdateFolder[] = L"DrakanPC-Updates";
constexpr std::wstring_view kFilePrefix = L"DrakanPC-Setup-";
constexpr DWORD kMaxDownloadSize = 1024 * 1024 * 1024;
constexpr DWORD kMaxChecksumSize = 64 * 1024;

struct ReleaseVersion {
    std::wstring tag;
    std::array<unsigned, 3> numbers{};
};

std::optional<ReleaseVersion> parseVersion(std::string_view remaining) {
    if (!remaining.starts_with('v'))
        return {};
    ReleaseVersion version{std::wstring(remaining.begin(), remaining.end())};
    remaining.remove_prefix(1);
    for (size_t i = 0; i < version.numbers.size(); ++i) {
        const size_t dot = remaining.find('.');
        if ((dot == std::string_view::npos) != (i + 1 == version.numbers.size()))
            return {};
        const auto value = remaining.substr(0, dot);
        if (value.empty() || (value.size() > 1 && value.front() == '0'))
            return {};
        const auto end = value.data() + value.size();
        const auto parsed = std::from_chars(value.data(), end, version.numbers[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != end || version.numbers[i] > UINT16_MAX)
            return {};
        remaining.remove_prefix(dot == std::string_view::npos ? value.size() : dot + 1);
    }
    return version;
}

std::wstring registryString(const wchar_t* name) {
    std::wstring value(32768, L'\0');
    DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, kSetupKey, name, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value.data(),
                     &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t) || size % sizeof(wchar_t) != 0)
        return {};
    value.resize(size / sizeof(wchar_t) - 1);
    return value.find(L'\0') == std::wstring::npos ? value : std::wstring{};
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

std::optional<Sha256> parseDigest(std::string_view value) {
    Sha256 digest{};
    if (value.size() != 2 * digest.size())
        return {};
    for (size_t i = 0; i < digest.size(); ++i) {
        const int high = hexDigit(value[2 * i]);
        const int low = hexDigit(value[2 * i + 1]);
        if (high < 0 || low < 0)
            return {};
        digest[i] = static_cast<BYTE>((high << 4) | low);
    }
    return digest;
}

std::optional<Sha256> installerChecksum(std::string_view contents) {
    constexpr std::string_view name = "DrakanPC-Setup.exe";
    std::optional<Sha256> digest;
    while (!contents.empty()) {
        const size_t newline = contents.find('\n');
        std::string_view line = contents.substr(0, newline);
        contents.remove_prefix(newline == std::string_view::npos ? contents.size() : newline + 1);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        const size_t separator = line.find("  ");
        if (separator == std::string_view::npos || line.substr(separator + 2) != name)
            continue;
        if (digest)
            return {};
        digest = parseDigest(line.substr(0, separator));
        if (!digest)
            return {};
    }
    return digest;
}

struct SetupInstallation {
    fs::path builder;
    fs::path play;

    bool operator==(const SetupInstallation&) const = default;
};

std::optional<SetupInstallation> setupInstallation() {
    const fs::path builder(registryString(L"BuilderDir"));
    const fs::path play(registryString(L"PlayDir"));
    if (!builder.is_absolute() || !play.is_absolute() || builder == builder.root_path() || play == play.root_path())
        return {};
    std::wstring exe(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    if (!size || size == exe.size())
        return {};
    exe.resize(size);
    std::error_code error;
    if (!fs::equivalent(fs::path(exe).parent_path(), play, error) ||
        !fs::is_regular_file(builder / L"Build-Drakan.cmd", error))
        return {};
    return SetupInstallation{builder, play};
}

std::optional<fs::path> downloadFolder() {
    PWSTR folder = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder) != S_OK)
        return {};
    const std::unique_ptr<wchar_t, decltype([](wchar_t* path) { CoTaskMemFree(path); })> local(folder);
    return fs::path(local.get()) / kUpdateFolder;
}

bool plainDirectory(const fs::path& directory) {
    const DWORD attributes = GetFileAttributesW(directory.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool isUpdateFile(std::wstring_view name) {
    if (!name.starts_with(kFilePrefix) || !name.ends_with(L".exe"))
        return false;
    GUID id{};
    const std::wstring guid(name.substr(kFilePrefix.size(), name.size() - kFilePrefix.size() - 4));
    return guid.size() == 38 && guid.front() == L'{' && guid.back() == L'}' &&
           CLSIDFromString(guid.c_str(), &id) == S_OK;
}

FileHandle lockDownloads(const fs::path& directory) {
    const HANDLE lock = CreateFileW((directory / L"download.lock").c_str(), GENERIC_READ, 0, nullptr, OPEN_ALWAYS,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    return FileHandle(lock == INVALID_HANDLE_VALUE ? nullptr : lock);
}

bool reapDownloads(const fs::path& directory) {
    std::error_code error;
    for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            !isUpdateFile(path.filename().native()))
            continue;
        if (!DeleteFileW(path.c_str()))
            return false; // Another updater's installer still owns the pending payload.
    }
    return !error;
}

struct RequestTarget {
    std::wstring host;
    std::wstring path;
};

std::optional<RequestTarget> assetRedirect(std::wstring_view location) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(location.data(), static_cast<DWORD>(location.size()), ICU_REJECT_USERPWD, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
        !parts.dwHostNameLength || !parts.dwUrlPathLength)
        return {};
    const std::wstring_view host(parts.lpszHostName, parts.dwHostNameLength);
    if (host != L"release-assets.githubusercontent.com" && host != L"objects.githubusercontent.com" &&
        host != L"github-releases.githubusercontent.com")
        return {};
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    return RequestTarget{std::wstring(host), std::move(path)};
}

template <class Receiver>
bool readResponse(HINTERNET request, DWORD maxSize, Clock::time_point deadline, Receiver receive) {
    DWORD length = 0;
    DWORD size = sizeof(length);
    const bool hasLength = WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                               WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX);
    if (hasLength ? length == 0 || length > maxSize : GetLastError() != ERROR_WINHTTP_HEADER_NOT_FOUND)
        return false;
    std::array<BYTE, 65536> buffer;
    DWORD total = 0;
    while (Clock::now() < deadline) {
        DWORD count = 0;
        if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &count))
            return false;
        if (!count)
            return total > 0 && (!hasLength || total == length);
        if (count > maxSize - total || !receive(buffer.data(), count))
            return false;
        total += count;
    }
    return false;
}

template <class Receiver>
bool download(const ReleaseVersion& version, const wchar_t* asset, DWORD maxSize, Receiver receive) {
    const HttpHandle session(WinHttpOpen(L"DrakanPC", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 5000, 15000, 15000, 30000))
        return false;
    RequestTarget target{L"github.com", L"/ChristopherVR/Drakan2PC/releases/download/" + version.tag + L"/" + asset};
    const auto deadline = Clock::now() + 30min;
    for (int redirects = 0; redirects < 4; ++redirects) {
        const HttpHandle connection(WinHttpConnect(session.get(), target.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection)
            return false;
        const HttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", target.path.c_str(), nullptr,
                                                    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                    WINHTTP_FLAG_SECURE | WINHTTP_FLAG_ESCAPE_DISABLE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
        if (!request || !WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) ||
            !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr))
            return false;
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
            return false;
        if (status == HTTP_STATUS_OK)
            return readResponse(request.get(), maxSize, deadline, receive);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308)
            return false;
        std::array<wchar_t, 8192> location{};
        size = sizeof(location);
        if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(),
                                 &size, WINHTTP_NO_HEADER_INDEX))
            return false;
        auto redirected = assetRedirect({location.data(), size / sizeof(wchar_t)});
        if (!redirected)
            return false;
        target = std::move(*redirected);
    }
    return false;
}

bool checksumMatches(HANDLE file, const Sha256& expected) {
    BCRYPT_ALG_HANDLE provider = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return false;
    const std::unique_ptr<void, decltype([](void* handle) { BCryptCloseAlgorithmProvider(handle, 0); })> algorithm(
        provider);
    BCRYPT_HASH_HANDLE context = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm.get(), &context, nullptr, 0, nullptr, 0, 0)))
        return false;
    const std::unique_ptr<void, decltype([](void* handle) { BCryptDestroyHash(handle); })> hash(context);
    std::array<BYTE, 65536> buffer;
    DWORD count = 0;
    do {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) ||
            (count && !BCRYPT_SUCCESS(BCryptHashData(hash.get(), buffer.data(), count, 0))))
            return false;
    } while (count);
    Sha256 actual{};
    return BCRYPT_SUCCESS(BCryptFinishHash(hash.get(), actual.data(), static_cast<ULONG>(actual.size()), 0)) &&
           actual == expected;
}

bool markInternetDownload(const fs::path& path) {
    const auto stream = path.native() + L":Zone.Identifier";
    const HANDLE output =
        CreateFileW(stream.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        return false;
    const FileHandle marker(output);
    constexpr char contents[] = "[ZoneTransfer]\r\nZoneId=3\r\n";
    constexpr DWORD size = static_cast<DWORD>(sizeof(contents) - 1);
    DWORD written = 0;
    return WriteFile(marker.get(), contents, size, &written, nullptr) && written == size;
}

bool resourceBelongsToFile(const void* resource, HANDLE file) {
    std::wstring mapName(32768, L'\0'), fileName(32768, L'\0');
    const DWORD mapLength = K32GetMappedFileNameW(GetCurrentProcess(), const_cast<void*>(resource), mapName.data(),
                                                  static_cast<DWORD>(mapName.size()));
    const DWORD fileLength =
        GetFinalPathNameByHandleW(file, fileName.data(), static_cast<DWORD>(fileName.size()), VOLUME_NAME_NT);
    return mapLength != 0 && mapLength < mapName.size() && fileLength == mapLength &&
           CompareStringOrdinal(mapName.data(), static_cast<int>(mapLength), fileName.data(),
                                static_cast<int>(fileLength), TRUE) == CSTR_EQUAL;
}

bool versionMatches(const fs::path& path, HANDLE file, const ReleaseVersion& expected) {
    const auto unload = [](HMODULE module) { FreeLibrary(module); };
    const std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(unload)> image(
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (!image)
        return false;
    const auto version = FindResourceW(image.get(), MAKEINTRESOURCEW(VS_VERSION_INFO), MAKEINTRESOURCEW(16));
    const auto data = version ? LoadResource(image.get(), version) : nullptr;
    const auto info = data ? LockResource(data) : nullptr;
    // FindResource can consult a localized MUI sidecar. Check the version of the verified executable itself.
    if (!info || !resourceBelongsToFile(info, file))
        return false;
    void* block = nullptr;
    UINT length = 0;
    if (!VerQueryValueW(info, L"\\", &block, &length) || length < sizeof(VS_FIXEDFILEINFO))
        return false;
    const auto& fixed = *static_cast<const VS_FIXEDFILEINFO*>(block);
    if (fixed.dwSignature != 0xfeef04bd || HIWORD(fixed.dwProductVersionMS) != expected.numbers[0] ||
        LOWORD(fixed.dwProductVersionMS) != expected.numbers[1] ||
        HIWORD(fixed.dwProductVersionLS) != expected.numbers[2] || LOWORD(fixed.dwProductVersionLS) != 0 ||
        !VerQueryValueW(info, L"\\VarFileInfo\\Translation", &block, &length))
        return false;
    const auto translation = static_cast<const WORD*>(block);
    for (size_t i = 0; i + 1 < length / sizeof(WORD); i += 2) {
        wchar_t productKey[44];
        swprintf_s(productKey, L"\\StringFileInfo\\%04x%04x\\ProductName", static_cast<unsigned>(translation[i]),
                   static_cast<unsigned>(translation[i + 1]));
        void* name = nullptr;
        UINT characters = 0;
        if (!VerQueryValueW(info, productKey, &name, &characters) || !characters)
            continue;
        std::wstring_view productName(static_cast<const wchar_t*>(name), characters - 1);
        if (const auto last = productName.find_last_not_of(L' '); last != std::wstring_view::npos)
            productName.remove_suffix(productName.size() - last - 1);
        if (productName == L"Drakan PC Builder")
            return true;
    }
    return false;
}

struct DownloadedSetup {
    SetupInstallation installation;
    fs::path path;
    FileHandle lock;
    FileHandle directoryLock;
    bool handedOff = false;

    ~DownloadedSetup() {
        lock.reset();
        if (!handedOff && !path.empty())
            DeleteFileW(path.c_str());
    }
};

std::unique_ptr<DownloadedSetup> downloadVerified(SetupInstallation installation, const ReleaseVersion& version) {
    std::string checksums;
    if (!download(version, L"SHA256SUMS.txt", kMaxChecksumSize, [&checksums](const BYTE* bytes, DWORD count) {
            checksums.append(reinterpret_cast<const char*>(bytes), count);
            return true;
        }))
        return {};
    const auto checksum = installerChecksum(checksums);
    if (!checksum)
        return {};

    const auto directory = downloadFolder();
    if (!directory || (!CreateDirectoryW(directory->c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) ||
        !plainDirectory(*directory))
        return {};
    auto setup = std::make_unique<DownloadedSetup>();
    setup->directoryLock = lockDownloads(*directory);
    if (!setup->directoryLock || !reapDownloads(*directory))
        return {};
    GUID id{};
    std::array<wchar_t, 39> guid{};
    if (CoCreateGuid(&id) != S_OK || !StringFromGUID2(id, guid.data(), static_cast<int>(guid.size())))
        return {};
    {
        const fs::path path = *directory / (std::wstring(kFilePrefix) + guid.data() + L".exe");
        const HANDLE output =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE)
            return {};
        const FileHandle writer(output);
        setup->path = path;
        if (!download(version, L"DrakanPC-Setup.exe", kMaxDownloadSize,
                      [&writer](const BYTE* bytes, DWORD count) {
                          DWORD written = 0;
                          return WriteFile(writer.get(), bytes, count, &written, nullptr) && written == count;
                      }) ||
            !FlushFileBuffers(writer.get()))
            return {};
    }
    if (!markInternetDownload(setup->path))
        return {};
    // The write handle closes before a read-only lock is acquired. Verification uses the locked file and the lock
    // stays open through the shell handoff, so an unverified replacement cannot run.
    setup->installation = std::move(installation);
    const HANDLE lock = CreateFileW(setup->path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
        return {};
    setup->lock.reset(lock);
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(lock, FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        !checksumMatches(lock, *checksum) || !versionMatches(setup->path, lock, version))
        return {};
    return setup;
}

std::wstring quote(std::wstring_view argument) {
    std::wstring result(L"\"");
    size_t slashes = 0;
    for (wchar_t c : argument) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
        result.push_back(c);
        slashes = 0;
    }
    result.append(2 * slashes, L'\\');
    result.push_back(L'"');
    return result;
}

bool launch(DownloadedSetup& setup, HWND owner) {
    if (setupInstallation() != setup.installation)
        return false;
    const std::wstring arguments = L"/DRAKANUPDATE=1 /DRAKANWAITPID=" + std::to_wstring(GetCurrentProcessId()) + L" " +
                                   quote(L"/DIR=" + setup.installation.builder.native()) + L" " +
                                   quote(L"/PLAYDIR=" + setup.installation.play.native()) +
                                   L" /SILENT /NORESTART /NOCLOSEAPPLICATIONS";
    const fs::path directory = setup.path.parent_path();
    SHELLEXECUTEINFOW execution{};
    execution.cbSize = sizeof(execution);
    execution.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_LOG_USAGE;
    execution.hwnd = owner;
    execution.lpVerb = L"open";
    execution.lpFile = setup.path.c_str();
    execution.lpParameters = arguments.c_str();
    execution.lpDirectory = directory.c_str();
    execution.nShow = SW_SHOWNORMAL;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return false;
    const bool opened = ShellExecuteExW(&execution);
    if (SUCCEEDED(initialized))
        CoUninitialize();
    if (!opened)
        return false;
    setup.handedOff = true;
    return true;
}

struct UpdateJob {
    State state = State::Idle;
    std::future<bool> pending;
};

UpdateJob& job() {
    static UpdateJob update;
    return update;
}
} // namespace

bool eligible() {
    static const bool supported = setupInstallation().has_value();
    return supported;
}

void start(std::string_view canonicalTag) {
    auto& update = job();
    if (update.state == State::Downloading || update.state == State::Launched)
        return;
    try {
        auto installation = setupInstallation();
        auto version = parseVersion(canonicalTag);
        if (installation && version) {
            const auto window = drakan_win::handle();
            const auto owner = static_cast<HWND>(window ? window : GetWindowHandle());
            std::packaged_task<bool()> work(
                [installation = std::move(*installation), version = std::move(*version), owner]() mutable {
                    const auto setup = downloadVerified(std::move(installation), version);
                    return setup && launch(*setup, owner);
                });
            auto pending = work.get_future();
            std::thread(std::move(work)).detach();
            update.pending = std::move(pending);
            update.state = State::Downloading;
            return;
        }
    } catch (const std::exception&) {
        std::fputs("[update] could not start the installer download\n", stderr);
    }
    update.state = State::Failed;
}

State poll() {
    static const bool cleaned = [] {
        if (const auto directory = downloadFolder(); directory && plainDirectory(*directory))
            if (const auto lock = lockDownloads(*directory))
                reapDownloads(*directory);
        return true;
    }();
    (void)cleaned;
    auto& update = job();
    if (update.pending.valid() && update.pending.wait_for(0s) == std::future_status::ready) {
        update.state = State::Failed;
        try {
            if (update.pending.get())
                update.state = State::Launched;
        } catch (const std::exception&) {
            std::fputs("[update] could not prepare the installer\n", stderr);
        }
    }
    return update.state;
}
} // namespace drakan_update_installer
