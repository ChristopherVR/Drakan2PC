#pragma once

#include <string_view>

namespace drakan_update_installer {
enum class State { Idle, Downloading, Failed, Launched };

// Title thread only. Start is called after the player approves the update.
bool eligible();
void start(std::string_view canonicalTag);
State poll();
} // namespace drakan_update_installer
