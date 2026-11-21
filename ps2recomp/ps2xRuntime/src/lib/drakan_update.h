#pragma once

#include <string_view>

namespace drakan_update {
// Called on the title thread. Polls the background check and returns the newer release if it has not been dismissed.
std::string_view availableVersion();
void dismiss();
void viewChanges();
} // namespace drakan_update
