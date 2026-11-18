#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace drakan_save_backup {

// Publish a verified backup beside the card. An identical backup is reused.
bool archiveOriginal(const std::filesystem::path& cardRoot, const std::filesystem::path& original,
                     std::span<const uint8_t> expected = {});
bool archiveSlot(const std::filesystem::path& cardRoot, const std::filesystem::path& primary,
                 std::span<const uint8_t> expected = {});

} // namespace drakan_save_backup
