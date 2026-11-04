// A per-open, terrain/environment-only view of a campaign LVL. Removes placed gameplay actors before their init
// events can run; normal campaign / co-op reads keep their original bytes. No game content is stored in this code.
#pragma once
#include "hle/IsoFs.h"
#include <string>

namespace hle {
bool filterArenaLevel(const IsoFs& fs, IsoFs::Node& level, std::string& error);
bool arenaEnvironmentClass(uint32_t runtimeClass);
} // namespace hle
