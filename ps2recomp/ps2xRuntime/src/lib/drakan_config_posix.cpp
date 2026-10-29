// Native POSIX host services. The configuration parser itself remains platform-free.
#include "drakan_config.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <thread>

#if !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace drakan_cfg {
const std::string& exeDir() {
    static const std::string dir = [] {
        namespace fs = std::filesystem;
        std::error_code error;
#if defined(__linux__)
        const fs::path executable = fs::read_symlink("/proc/self/exe", error);
        if (!error && executable.is_absolute())
            return executable.parent_path().generic_string() + "/";
#endif
        // Platforms without /proc can use the launch directory until they provide
        // their own executable-location service (including a virtual browser FS).
        const fs::path launchDir = fs::current_path(error);
        if (error)
            throw std::runtime_error("Cannot determine the Drakan configuration directory: " + error.message());
        return launchDir.generic_string() + "/";
    }();
    return dir;
}

void enableDpiAwareness() {
    // DPI awareness is a Win32 process setting; native window backends own scaling.
#if !defined(__EMSCRIPTEN__)
    // Restart from the settings menu: the old instance (DRAKAN_WAIT_PID) is closing, wait (up to 15 s) until it has
    // released the window, audio and saves.
    if (const char* wp = std::getenv("DRAKAN_WAIT_PID")) {
        const pid_t pid = static_cast<pid_t>(std::strtol(wp, nullptr, 10));
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (pid > 0 && (kill(pid, 0) == 0 || errno == EPERM) && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        unsetenv("DRAKAN_WAIT_PID");
    }
#endif
}
} // namespace drakan_cfg
