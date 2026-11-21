#include "drakan_issue.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include "drakan_issue_url.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace drakan_issue {
namespace {
namespace fs = std::filesystem;
constexpr int kManual = -1;
constexpr int kRuntimeFailure = 0;
constexpr off_t kLogLimit = 4 * 1024 * 1024;
volatile sig_atomic_t g_notice = -1;
int g_reports = -1;
using ReportUrl = std::array<char, 2048>;

struct Pipe {
    int fds[2]{-1, -1};
    Pipe() = default;
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    ~Pipe() {
        if (fds[0] != -1)
            close(fds[0]);
        if (fds[1] != -1)
            close(fds[1]);
    }
    bool open(int flags = 0) { return pipe2(fds, O_CLOEXEC | flags) == 0; }
};

void writeAll(int fd, const char* bytes, size_t count) {
    while (count) {
        const ssize_t written = write(fd, bytes, count);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            break;
        bytes += written;
        count -= static_cast<size_t>(written);
    }
}

bool notify(int reason) {
    if (g_notice == -1)
        return false;
    return write(g_notice, &reason, sizeof reason) == sizeof reason;
}

void onSignal(int signal) {
    const int savedErrno = errno;
    notify(signal);
    errno = savedErrno;
    raise(signal); // SA_RESETHAND restores the normal termination and core-dump behavior.
}

fs::path reportFolder() {
    fs::path base;
    if (const char* logs = std::getenv("DRAKAN_LOG_DIR"); logs && *logs)
        base = fs::path(logs) / "reports";
    else if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state)
        base = fs::path(state) / "drakanpc" / "reports";
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".local/state/drakanpc/reports";
    else
        base = fs::temp_directory_path() / "drakanpc-reports";
    fs::create_directories(base);
    std::string folder = (fs::absolute(base) / "report-XXXXXX").string();
    return mkdtemp(folder.data()) ? fs::path(folder) : fs::path();
}

void copyLog(std::ostream& out, int log, off_t offset, off_t length) {
    std::array<char, 16 * 1024> block;
    while (length > 0) {
        const ssize_t count = pread(log, block.data(), std::min<off_t>(length, block.size()), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            out << "\nThe session log could only be copied in part.\n";
            return;
        }
        out.write(block.data(), count);
        offset += count;
        length -= count;
    }
}

fs::path captureReport(int log, int reason, std::string_view url) {
    const bool crash = reason != kManual;
    const fs::path dir = reportFolder();
    if (dir.empty())
        return {};
    const char* name = crash ? "drakan-crash.log" : "drakan-issue.log";
    std::ofstream out(dir / name, std::ios::binary);
    out << "Drakan PC " << (crash ? "crash" : "issue") << " report\nPlatform: Linux\n";
    if (reason > 0)
        out << "Process signal: " << reason << '\n';
    out << "\n--- Session log ---\n";
    const off_t size = lseek(log, 0, SEEK_END);
    if (size <= 0) {
        out << "No session log was available.\n";
    } else if (size <= kLogLimit) {
        copyLog(out, log, 0, size);
    } else {
        constexpr off_t kHead = 64 * 1024;
        copyLog(out, log, 0, kHead);
        out << "\n--- Middle of session log omitted (4 MiB capture limit) ---\n";
        copyLog(out, log, size - (kLogLimit - kHead), kLogLimit - kHead);
    }
    out.close();
    const bool captured = out.good();
    if (captured)
        std::fprintf(stderr, "[report] Session log saved to %s\n", (dir / name).c_str());

    const fs::path html = dir / "report.html";
    std::ofstream page(html);
    const char* title = crash ? "Drakan closed unexpectedly" : "Report an Issue";
    page << "<!doctype html><html lang=en><meta charset=utf-8><meta name=viewport "
            "content=\"width=device-width,initial-scale=1\"><title>"
         << title
         << "</title><style>body{color-scheme:dark;background:#161311;color:#f2e2c3;font:1.1rem/1.6 system-ui;"
            "max-width:40rem;margin:12vh auto;padding:0 1.5rem}a{color:#ffcf85}</style>"
         << "<main><h1>" << title << "</h1>";
    if (captured)
        page << "<p>Review the <a href=\"" << name
             << "\" download>session log</a> for personal information before "
                "attaching it to your report.</p>";
    else
        page << "<p>The game could not save a copy of the session log.</p>";
    page << "<p><a href=\"";
    for (const char c : url) {
        if (c == '&')
            page << "&amp;";
        else
            page << c;
    }
    page << "\">Open the GitHub " << (crash ? "crash" : "bug") << " report form</a></p></main></html>";
    page.close();
    return page.good() ? html : fs::path();
}

void openReport(const char* destination) {
    // Detach the desktop opener from the game and its log pipes, including when run under WSL.
    const pid_t child = fork();
    if (child != 0) {
        if (child > 0)
            while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {
            }
        return;
    }
    if (fork() != 0)
        _exit(0);
    const int sink = open("/dev/null", O_RDWR);
    if (sink >= 0) {
        for (const int fd : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO})
            dup2(sink, fd);
        if (sink > STDERR_FILENO)
            close(sink);
    }
    for (const char* program : {"xdg-open", "wslview", "gio"}) {
        char* args[] = {const_cast<char*>(program), const_cast<char*>(destination), nullptr, nullptr};
        if (std::string_view(program) == "gio") {
            args[1] = const_cast<char*>("open");
            args[2] = const_cast<char*>(destination);
        }
        pid_t opener;
        if (posix_spawnp(&opener, program, nullptr, nullptr, args, environ) == 0) {
            int status;
            pid_t waited;
            do {
                waited = waitpid(opener, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited == opener && WIFEXITED(status) && WEXITSTATUS(status) == 0)
                break;
        }
    }
    _exit(0);
}

void offerReport(int log, int reason, const char* reportedUrl = "") {
    const bool crash = reason != kManual;
    const std::string url = *reportedUrl ? reportedUrl : formUrl(crash, "Linux");
    fs::path page;
    try {
        page = captureReport(log, reason, url);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[report] Could not save a report: %s\n", error.what());
    }
    const char* prompt = std::getenv("DRAKAN_CRASH_PROMPT");
    if (crash && prompt && std::string_view(prompt) == "0")
        return;
    if (!page.empty())
        std::fprintf(stderr, "[report] Open %s to report the issue.\n", page.c_str());
    std::fprintf(stderr, "[report] GitHub form: %s\n", url.c_str());
    openReport(page.empty() ? url.c_str() : page.c_str());
}

void logChunk(pollfd& source, int destination, int log) {
    if (!source.revents)
        return;
    std::array<char, 16 * 1024> buffer;
    const ssize_t count = read(source.fd, buffer.data(), buffer.size());
    if (count > 0) {
        writeAll(log, buffer.data(), count);
        writeAll(destination, buffer.data(), count);
    } else if (count == 0 || errno != EINTR) {
        close(source.fd);
        source.fd = -1;
    }
}

void monitor(int out, int err, int notice, int reports, int log) {
    setsid(); // The monitor survives a signal delivered to the game's foreground process group.
    std::signal(SIGPIPE, SIG_IGN);
    pollfd inputs[]{{out, POLLIN, 0}, {err, POLLIN, 0}, {notice, POLLIN, 0}, {reports, POLLIN, 0}};
    auto drain = [&] {
        // A child can inherit stdout or stderr. Drain the bytes already queued without waiting on that child.
        for (int i = 0; i < 64 && poll(inputs, 2, 0) > 0; ++i) {
            logChunk(inputs[0], STDOUT_FILENO, log);
            logChunk(inputs[1], STDERR_FILENO, log);
        }
    };
    int crash = kManual;
    for (;;) {
        const int ready = poll(inputs, std::size(inputs), -1);
        if (ready < 0 && errno != EINTR)
            return;
        if (ready <= 0)
            continue;
        logChunk(inputs[0], STDOUT_FILENO, log);
        logChunk(inputs[1], STDERR_FILENO, log);
        if (inputs[3].revents) {
            ReportUrl url{};
            const ssize_t count = read(reports, url.data(), url.size());
            if (count == sizeof url) {
                drain();
                offerReport(log, kManual, url.data());
                continue; // Handle reports already queued before the game closed.
            }
            if (count == 0)
                inputs[3].fd = -1;
        }
        if (inputs[2].revents) {
            int reason;
            const ssize_t count = read(notice, &reason, sizeof reason);
            if (count == 0)
                break;
            if (count == sizeof reason)
                crash = reason;
        }
    }
    drain();
    if (crash != kManual)
        offerReport(log, crash);
}
} // namespace

bool runHelper(int, char*[]) {
    return false;
}

void watchForCrashes() {
    Pipe out, err, notice, reports;
    if (!out.open() || !err.open() || !notice.open(O_NONBLOCK) ||
        socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, reports.fds) != 0)
        return;
    FILE* log = std::tmpfile();
    if (!log)
        return;
    fcntl(fileno(log), F_SETFD, FD_CLOEXEC);
    const pid_t watcher = fork();
    if (watcher == 0) {
        close(out.fds[1]);
        close(err.fds[1]);
        close(notice.fds[1]);
        close(reports.fds[1]);
        monitor(out.fds[0], err.fds[0], notice.fds[0], reports.fds[0], fileno(log));
        _exit(0);
    }
    std::fclose(log);
    if (watcher < 0)
        return;
    std::fflush(stdout);
    std::fflush(stderr);
    dup2(out.fds[1], STDOUT_FILENO);
    dup2(err.fds[1], STDERR_FILENO);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    g_notice = notice.fds[1];
    notice.fds[1] = -1;
    g_reports = reports.fds[1];
    reports.fds[1] = -1;

    struct sigaction action{};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    for (const int signal : {SIGABRT, SIGBUS, SIGFPE, SIGILL, SIGSEGV, SIGSYS})
        sigaction(signal, &action, nullptr);
}

void markCrash() {
    notify(kRuntimeFailure);
}

bool available() {
    return true;
}

void report(std::string_view screen, std::string_view campaign, std::string_view level) {
    const auto url = formUrl(false, "Linux", screen, campaign, level);
    ReportUrl message{};
    if (g_reports != -1 && url.size() < message.size()) {
        url.copy(message.data(), url.size());
        if (send(g_reports, message.data(), message.size(), MSG_NOSIGNAL) == sizeof message)
            return;
    }
    std::fprintf(stderr, "[report] Open %s to report an issue.\n", url.c_str());
}
} // namespace drakan_issue
#endif
