// Native POSIX host services. The configuration parser itself remains platform-free.
#include "drakan_config.h"

#include <filesystem>
#include <stdexcept>

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
}
} // namespace drakan_cfg
