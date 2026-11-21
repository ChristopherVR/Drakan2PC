#pragma once

#include <string_view>

namespace drakan_update_linux {
enum class Install { Unavailable, Available, Downloading, Failed, Ready };

struct Notice {
    std::string_view version;
    Install install = Install::Unavailable;
};

// Call on the UI thread while the main title menu is in the foreground.
Notice poll();
void start();
void dismiss();
void viewChanges();
} // namespace drakan_update_linux
