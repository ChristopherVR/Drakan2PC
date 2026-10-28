#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace gsr {
class Renderer;
}
namespace drakan_save_previews {
void attach(gsr::Renderer* renderer);
uint64_t generation();
void registerSlot(int slot, const std::vector<uint32_t>& nativePixels);
// The HD companion of the save in slot `slot` (<save>.rsg.<fnv64>.preview.png beside it on the card), empty when the
// slot has no save or the save has no companion (an old save). Hashes the save file: call when the slot changes.
std::string previewPath(int slot);
} // namespace drakan_save_previews
