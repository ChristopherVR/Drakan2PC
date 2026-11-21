#include "drakan_update.h"
#include "drakan_config.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace drakan_update {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr wchar_t kHost[] = L"github.com";
constexpr wchar_t kLatestPath[] = L"/ChristopherVR/Drakan2PC/releases/latest";
constexpr std::wstring_view kReleaseUrl = L"https://github.com/ChristopherVR/Drakan2PC/releases/tag/";
constexpr wchar_t kCacheSection[] = L"update";
constexpr auto kCheckInterval = 24h;
using HttpHandle = std::unique_ptr<void, decltype([](HINTERNET handle) { WinHttpCloseHandle(handle); })>;

struct Release {
    std::array<uint32_t, 3> number{};
    std::string tag;
};

std::optional<Release> readTag(std::string_view tag) {
    if (!tag.starts_with('v'))
        return {};
    const std::string_view name = tag;
    Release release;
    tag.remove_prefix(1);
    for (size_t i = 0; i < release.number.size(); ++i) {
        const size_t end = tag.find('.');
        if ((end == std::string_view::npos) != (i + 1 == release.number.size()))
            return {};
        const std::string_view component = tag.substr(0, end);
        if (component.empty() || (component.size() > 1 && component.front() == '0'))
            return {};
        const auto result = std::from_chars(component.data(), component.data() + component.size(), release.number[i]);
        if (result.ec != std::errc{} || result.ptr != component.data() + component.size())
            return {};
        tag.remove_prefix(end == std::string_view::npos ? component.size() : end + 1);
    }
    release.tag = name;
    return release;
}

std::string narrowAscii(std::wstring_view text) {
    std::string narrow;
    for (wchar_t c : text) {
        if (c > 127)
            return {};
        narrow.push_back(static_cast<char>(c));
    }
    return narrow;
}

std::optional<Release> readLocation(std::wstring_view location) {
    if (!location.starts_with(kReleaseUrl))
        return {};
    return readTag(narrowAscii(location.substr(kReleaseUrl.size())));
}

std::optional<Release> fetchLatest() {
    const HttpHandle session(WinHttpOpen(L"DrakanPC", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session || !WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 3000))
        return {};
    const HttpHandle connection(WinHttpConnect(session.get(), kHost, INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection)
        return {};
    const HttpHandle request(WinHttpOpenRequest(connection.get(), L"HEAD", kLatestPath, nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request)
        return {};
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof disabled) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr))
        return {};
    DWORD status = 0;
    DWORD size = sizeof status;
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status != HTTP_STATUS_REDIRECT)
        return {};
    std::array<wchar_t, 256> location{};
    size = sizeof location;
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, location.data(),
                             &size, WINHTTP_NO_HEADER_INDEX))
        return {};
    return readLocation({location.data(), size / sizeof(wchar_t)});
}

fs::path playFolder() {
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!size || size == path.size())
        return {};
    path.resize(size);
    return fs::path(path).parent_path();
}

std::optional<Release> installedRelease(const fs::path& folder) {
    std::ifstream marker(folder / L"DrakanPC-release.txt", std::ios::binary);
    std::array<char, 64> buffer{};
    marker.read(buffer.data(), buffer.size());
    if (!marker.eof() || marker.bad())
        return {};
    std::string_view tag(buffer.data(), static_cast<size_t>(marker.gcount()));
    if (tag.ends_with('\n')) {
        tag.remove_suffix(1);
        if (tag.ends_with('\r'))
            tag.remove_suffix(1);
    }
    return readTag(tag);
}

std::string readCache(const fs::path& path, const wchar_t* key) {
    std::array<wchar_t, 64> value{};
    const DWORD size =
        GetPrivateProfileStringW(kCacheSection, key, L"", value.data(), static_cast<DWORD>(value.size()), path.c_str());
    return size < value.size() - 1 ? narrowAscii({value.data(), size}) : std::string();
}

void writeCache(const fs::path& path, const wchar_t* key, const std::wstring& value) {
    WritePrivateProfileStringW(kCacheSection, key, value.c_str(), path.c_str());
}

std::wstring wide(std::string_view text) {
    return {text.begin(), text.end()};
}

using CheckTime = std::chrono::sys_seconds;

struct UpdateState {
    fs::path cache;
    std::optional<Release> installed;
    std::optional<Release> latest;
    std::string dismissed;
    CheckTime lastAttempt{};
    std::future<std::optional<Release>> pending;

    UpdateState() {
        const fs::path folder = playFolder();
        if (folder.empty() || !drakan_cfg::getBool("general", "check_for_updates", "DRAKAN_CHECK_FOR_UPDATES", false))
            return;
        installed = installedRelease(folder);
        if (!installed)
            return;
        cache = folder / L"drakan_updates.ini";
        latest = readTag(readCache(cache, L"latest"));
        dismissed = readCache(cache, L"dismissed");
        const std::string seconds = readCache(cache, L"last_attempt");
        int64_t value = 0;
        const auto parsed = std::from_chars(seconds.data(), seconds.data() + seconds.size(), value);
        if (parsed.ec == std::errc{} && parsed.ptr == seconds.data() + seconds.size() && value >= 0)
            lastAttempt = CheckTime(std::chrono::seconds(value));
    }

    void poll() {
        if (!installed)
            return;
        if (pending.valid() && pending.wait_for(0s) == std::future_status::ready) {
            auto found = pending.get();
            if (found) {
                latest = std::move(found);
                writeCache(cache, L"latest", wide(latest->tag));
            }
            writeCache(cache, L"last_attempt", std::to_wstring(lastAttempt.time_since_epoch().count()));
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        if (pending.valid() || (lastAttempt <= now && now - lastAttempt < kCheckInterval))
            return;
        lastAttempt = now;
        try {
            // The worker owns its request only. This future never waits for it during game shutdown.
            std::packaged_task<std::optional<Release>()> check([] {
                try {
                    return fetchLatest();
                } catch (const std::exception&) {
                    return std::optional<Release>{};
                }
            });
            auto result = check.get_future();
            std::thread(std::move(check)).detach();
            pending = std::move(result);
        } catch (const std::exception&) {
            // Failing to start a check must not affect the game.
        }
    }

    std::string_view available() const {
        if (!installed || !latest || latest->number <= installed->number || latest->tag == dismissed)
            return {};
        return latest->tag;
    }
};

UpdateState& state() {
    static UpdateState update;
    return update;
}
} // namespace

std::string_view availableVersion() {
    auto& update = state();
    update.poll();
    return update.available();
}

void dismiss() {
    auto& update = state();
    if (!update.available().empty()) {
        update.dismissed = update.latest->tag;
        writeCache(update.cache, L"dismissed", wide(update.dismissed));
    }
}

void viewChanges() {
    if (const auto tag = state().available(); !tag.empty()) {
        std::wstring url(kReleaseUrl);
        url += wide(tag);
        ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}
} // namespace drakan_update
