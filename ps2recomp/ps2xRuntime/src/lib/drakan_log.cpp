#include "drakan_log.h"

#if defined(_WIN32)
#include "drakan_config.h"
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace drakan_log {
namespace {
constexpr const char* kMarker = "DRKFLUSH-7f3a";
constexpr size_t kQueueCap = 8u << 20; // bytes kept in memory between the reader and the writer thread

struct State {
    HANDLE pipeRead = nullptr;
    HANDLE sinkFile = INVALID_HANDLE_VALUE;
    HANDLE sinkOrig = INVALID_HANDLE_VALUE;    // original stderr (file / pipe given by a test script)
    HANDLE sinkConsole = INVALID_HANDLE_VALUE; // CONOUT$ when console = 1
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::string> q;
    size_t qBytes = 0;
    size_t dropped = 0;
    bool writing = false; // the writer thread holds a chunk it has not finished writing
    std::atomic<bool> markerSeen{false};
};
State* g = nullptr;

void writeAll(HANDLE h, const char* p, size_t n) {
    while (n) {
        DWORD w = 0;
        if (!WriteFile(h, p, static_cast<DWORD>(n > (1u << 20) ? (1u << 20) : n), &w, nullptr) || w == 0)
            return;
        p += w;
        n -= w;
    }
}

void readerMain() {
    std::vector<char> buf(64 * 1024);
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(g->pipeRead, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) || n == 0)
            return;
        std::lock_guard<std::mutex> lk(g->m);
        while (g->qBytes + n > kQueueCap && !g->q.empty()) {
            g->qBytes -= g->q.front().size();
            g->dropped += g->q.front().size();
            g->q.pop_front();
        }
        g->q.emplace_back(buf.data(), n);
        g->qBytes += n;
        g->cv.notify_one();
    }
}

void writerMain() {
    for (;;) {
        std::string chunk;
        {
            std::unique_lock<std::mutex> lk(g->m);
            g->cv.wait(lk, [] { return !g->q.empty(); });
            chunk = std::move(g->q.front());
            g->qBytes -= chunk.size();
            g->q.pop_front();
            g->writing = true;
        }
        // Destinations may block (console selection, full pipe, slow disk); only this thread waits for them.
        if (g->sinkFile != INVALID_HANDLE_VALUE)
            writeAll(g->sinkFile, chunk.data(), chunk.size());
        if (g->sinkOrig != INVALID_HANDLE_VALUE)
            writeAll(g->sinkOrig, chunk.data(), chunk.size());
        if (g->sinkConsole != INVALID_HANDLE_VALUE)
            writeAll(g->sinkConsole, chunk.data(), chunk.size());
        if (chunk.find(kMarker) != std::string::npos)
            g->markerSeen.store(true);
        {
            std::lock_guard<std::mutex> lk(g->m);
            g->writing = false;
        }
    }
}
} // namespace

void flush(int timeoutMs) {
    if (!g)
        return;
    // PeekNamedPipe would block behind the reader thread's pending ReadFile: write a marker through the pipe instead
    // and wait until the writer thread has written it (everything before it is then in the sinks).
    g->markerSeen.store(false);
    fflush(stdout);
    std::fprintf(stderr, "[log] flush marker %s\n", kMarker);
    fflush(stderr);
    const auto t0 = std::chrono::steady_clock::now();
    while (!g->markerSeen.load() && std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(timeoutMs))
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (g->sinkFile != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g->sinkFile);
}

void init() {
    if (g)
        return;
    g = new State();
    const bool wantConsole = drakan_cfg::getBool("general", "console", "DRAKAN_CONSOLE", false);

    // original stderr: keep it as a sink when a script redirected it to a file / pipe (not when it is a console or
    // invalid)
    HANDLE oe = GetStdHandle(STD_ERROR_HANDLE);
    if (oe && oe != INVALID_HANDLE_VALUE) {
        const DWORD t = GetFileType(oe);
        if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) {
            HANDLE dup = nullptr;
            if (DuplicateHandle(GetCurrentProcess(), oe, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS))
                g->sinkOrig = dup;
        }
    }

    if (wantConsole) {
        if (!GetConsoleWindow())
            AllocConsole();
        HANDLE in = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, 0, nullptr);
        if (in != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(in, &mode))
                SetConsoleMode(in,
                               (mode & ~(DWORD)(ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE)) | ENABLE_EXTENDED_FLAGS);
            CloseHandle(in);
        }
        g->sinkConsole = CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
    }

    // drakan.log (+ the previous run as drakan.1.log); DRAKAN_LOG_DIR=<dir> moves both (test runs keep the player's
    // logs next to the exe untouched)
    {
        const char* logDir = std::getenv("DRAKAN_LOG_DIR");
        const std::string base = logDir && *logDir ? std::string(logDir) + "/" : drakan_cfg::exeDir();
        const std::string cur = base + "drakan.log", prev = base + "drakan.1.log";
        MoveFileExA(cur.c_str(), prev.c_str(),
                    MOVEFILE_REPLACE_EXISTING); // fails quietly when another instance still has it open
        g->sinkFile = CreateFileA(cur.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    HANDLE r = nullptr, w = nullptr;
    if (!CreatePipe(&r, &w, nullptr, 1u << 20))
        return;
    g->pipeRead = r;
    // route the CRT's stdout / stderr (fd 1, 2) and the Win32 std handles into the pipe
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(w), _O_BINARY);
    if (fd < 0)
        return;
    // A GUI-subsystem process started without redirection has invalid CRT stdio (fileno -2): give both streams a real
    // descriptor first.
    FILE* dummy = nullptr;
    if (_fileno(stdout) < 0)
        freopen_s(&dummy, "NUL", "wb", stdout);
    if (_fileno(stderr) < 0)
        freopen_s(&dummy, "NUL", "wb", stderr);
    fflush(stdout);
    fflush(stderr);
    _dup2(fd, _fileno(stdout));
    _dup2(fd, _fileno(stderr));
    SetStdHandle(STD_OUTPUT_HANDLE, w);
    SetStdHandle(STD_ERROR_HANDLE, w);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    std::thread(readerMain).detach();
    std::thread(writerMain).detach();
}
} // namespace drakan_log
#else
namespace drakan_log {
void init() {}
void flush(int) {}
} // namespace drakan_log
#endif
