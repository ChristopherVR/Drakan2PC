#include "drakan_update_linux.h"

#include "drakan_config.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace drakan_update_linux {
namespace {
namespace fs = std::filesystem;
using namespace std::chrono_literals;

constexpr std::string_view kReleaseUrl = "https://github.com/ChristopherVR/Drakan2PC/releases/tag/";
constexpr size_t kMaxOutput = 64;

struct Release {
    std::string tag;
    std::array<uint32_t, 3> version{};
};

std::optional<Release> readTag(std::string_view tag) {
    if (tag.ends_with('\n')) {
        tag.remove_suffix(1);
        if (tag.ends_with('\r'))
            tag.remove_suffix(1);
    }
    if (!tag.starts_with('v'))
        return {};
    Release release{std::string(tag)};
    tag.remove_prefix(1);
    for (size_t i = 0; i < release.version.size(); ++i) {
        const size_t dot = tag.find('.');
        if ((dot == std::string_view::npos) != (i + 1 == release.version.size()))
            return {};
        const auto number = tag.substr(0, dot);
        if (number.empty() || (number.size() > 1 && number.front() == '0'))
            return {};
        const auto end = number.data() + number.size();
        const auto parsed = std::from_chars(number.data(), end, release.version[i]);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
            return {};
        tag.remove_prefix(dot == std::string_view::npos ? number.size() : dot + 1);
    }
    return release;
}

std::string readSmallFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::array<char, kMaxOutput> bytes{};
    file.read(bytes.data(), bytes.size());
    return file.eof() && !file.bad() ? std::string(bytes.data(), static_cast<size_t>(file.gcount())) : std::string();
}

void writeSmallFile(const fs::path& path, const std::string& content) {
    std::string temp = path.string() + ".XXXXXX";
    const int fd = mkostemp(temp.data(), O_CLOEXEC);
    if (fd < 0)
        return;
    const ssize_t written = write(fd, content.data(), content.size());
    if (close(fd) == 0 && written == static_cast<ssize_t>(content.size()) && rename(temp.c_str(), path.c_str()) == 0)
        return;
    unlink(temp.c_str());
}

enum class Result { Running, Succeeded, Failed };

Result reap(pid_t& pid) {
    if (pid == 0)
        return Result::Failed;
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, WNOHANG);
    } while (waited == -1 && errno == EINTR);
    if (waited == 0)
        return Result::Running;
    pid = 0;
    return waited > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 ? Result::Succeeded : Result::Failed;
}

struct Child {
    pid_t pid = 0;
    int output = -1;
    std::string text;

    ~Child() { closeOutput(); }

    void closeOutput() {
        if (output >= 0) {
            close(output);
            output = -1;
        }
    }

    bool start(char* const args[]) {
        int pipeFds[2];
        if (pipe2(pipeFds, O_CLOEXEC) != 0)
            return false;
        const int reader = pipeFds[0], writer = pipeFds[1];
        posix_spawn_file_actions_t actions;
        const bool actionsReady = posix_spawn_file_actions_init(&actions) == 0;
        const bool spawned = actionsReady && fcntl(reader, F_SETFL, O_NONBLOCK) == 0 &&
                             posix_spawn_file_actions_addclose(&actions, reader) == 0 &&
                             posix_spawn_file_actions_adddup2(&actions, writer, STDOUT_FILENO) == 0 &&
                             (writer == STDOUT_FILENO || posix_spawn_file_actions_addclose(&actions, writer) == 0) &&
                             posix_spawnp(&pid, args[0], &actions, nullptr, args, environ) == 0;
        if (actionsReady)
            posix_spawn_file_actions_destroy(&actions);
        close(writer);
        if (!spawned) {
            close(reader);
            pid = 0;
            return false;
        }
        output = reader;
        text.clear();
        return true;
    }

    void readOutput() {
        std::array<char, kMaxOutput> buffer;
        while (output >= 0) {
            const ssize_t count = read(output, buffer.data(), buffer.size());
            if (count > 0 && text.size() + static_cast<size_t>(count) < kMaxOutput) {
                text.append(buffer.data(), static_cast<size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            if (count != 0)
                text.clear();
            closeOutput();
        }
    }

    Result poll() {
        const Result result = reap(pid);
        readOutput();
        if (result != Result::Running)
            closeOutput();
        return result;
    }
};

struct Updater {
    fs::path checkCache, dismissedCache;
    std::string helper, dismissed;
    std::optional<Release> installed, latest;
    std::chrono::sys_seconds lastAttempt{};
    Child check, update;
    pid_t browser = 0;
    Install status = Install::Unavailable;

    Updater() {
        if (!drakan_cfg::getBool("general", "check_for_updates", "DRAKAN_CHECK_FOR_UPDATES", false))
            return;
        const fs::path play(drakan_cfg::exeDir());
        auto local = readTag(readSmallFile(play / "DrakanPC-release.txt"));
        std::error_code error;
        const fs::path script = play / "drakan-update.py";
        if (!local || !fs::is_regular_file(script, error) || !fs::is_regular_file(play / "drakan-update.json", error))
            return;
        helper = script.string();
        installed = std::move(local);
        status = Install::Available;
        checkCache = play / "drakan_update_check.ini";
        dismissedCache = play / "drakan_update_dismissed.ini";
        if (auto ignored = readTag(readSmallFile(dismissedCache)))
            dismissed = std::move(ignored->tag);
        const std::string cache = readSmallFile(checkCache);
        const auto separator = cache.find('\n');
        if (separator != std::string::npos) {
            int64_t age = 0;
            const auto end = cache.data() + separator;
            const auto parsed = std::from_chars(cache.data(), end, age);
            if (parsed.ec == std::errc{} && parsed.ptr == end && age >= 0) {
                lastAttempt = std::chrono::sys_seconds(std::chrono::seconds(age));
                latest = readTag(std::string_view(cache).substr(separator + 1));
            }
        }
    }

    std::string_view available() const {
        if (!installed || !latest || latest->version <= installed->version || latest->tag == dismissed)
            return {};
        return latest->tag;
    }

    void saveCheck() const {
        writeSmallFile(checkCache,
                       std::to_string(lastAttempt.time_since_epoch().count()) + '\n' + (latest ? latest->tag : ""));
    }

    void pollCheck() {
        if (check.pid) {
            const auto result = check.poll();
            if (result == Result::Running)
                return;
            if (result == Result::Succeeded)
                if (auto found = readTag(check.text))
                    latest = std::move(found);
            saveCheck();
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        if (lastAttempt <= now && now - lastAttempt < 24h)
            return;
        lastAttempt = now;
        char python[] = "python3", command[] = "check";
        char* const args[]{python, helper.data(), command, nullptr};
        if (!check.start(args))
            saveCheck();
    }
};

Updater& updater() {
    static Updater state;
    return state;
}
} // namespace

Notice poll() {
    auto& state = updater();
    if (!state.installed)
        return {};
    reap(state.browser);
    state.pollCheck();
    if (state.status == Install::Downloading) {
        if (state.update.poll() != Result::Running) {
            state.status = Install::Failed;
        } else if (state.update.text == "READY\n") {
            state.update.closeOutput();
            state.status = Install::Ready;
        }
    }
    return {state.available(), state.status};
}

void start() {
    auto& state = updater();
    std::string tag(state.available());
    if (tag.empty() || state.status == Install::Downloading || state.status == Install::Ready)
        return;
    std::string pid = std::to_string(getpid());
    char python[] = "python3";
    char* const args[]{python, state.helper.data(), tag.data(), pid.data(), nullptr};
    state.status = state.update.start(args) ? Install::Downloading : Install::Failed;
}

void dismiss() {
    auto& state = updater();
    if (const std::string_view tag = state.available(); !tag.empty()) {
        state.dismissed = tag;
        writeSmallFile(state.dismissedCache, state.dismissed + '\n');
    }
}

void viewChanges() {
    auto& state = updater();
    if (reap(state.browser) == Result::Running)
        return;
    const auto tag = state.available();
    if (tag.empty())
        return;
    std::string url = std::string(kReleaseUrl) + std::string(tag);
    char opener[] = "xdg-open";
    char* const args[]{opener, url.data(), nullptr};
    if (posix_spawnp(&state.browser, opener, nullptr, nullptr, args, environ) == 0)
        return;
    char gio[] = "gio", open[] = "open";
    char* const fallback[]{gio, open, url.data(), nullptr};
    if (posix_spawnp(&state.browser, gio, nullptr, nullptr, fallback, environ) != 0)
        state.browser = 0;
}
} // namespace drakan_update_linux
