// JSON dump of a Snapshot for offline diffing (stable key order, fixed float precision). std only.
#pragma once
#include <string>

#include "world/Snapshot.h"

namespace world {

// Pretty-printed, one entity per line. Floats are written with 3 decimals.
std::string toJson(const Snapshot& s);

} // namespace world
