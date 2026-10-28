#pragma once
#include <cstdint>
#include <vector>

namespace gsr {
// Immutable, owned snapshot of native resident geometry BEFORE visibility culling.
// No guest pointers survive the EE-to-renderer queue handoff.
struct SceneTriangle {
    float position[3][3]{};
    float color[3][3]{}; // GS vertex lighting, normalized to 0..1 (128 = unity)
    uint32_t layer = 0, water = 0;
};
struct SceneGeometry {
    uint64_t identity = 0;
    uint32_t layers = 0, waterLayers = 0;
    std::vector<SceneTriangle> triangles;
};
} // namespace gsr
