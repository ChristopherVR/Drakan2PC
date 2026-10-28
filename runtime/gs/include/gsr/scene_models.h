#pragma once
#include <cstdint>
#include <memory>
#include <vector>
namespace gsr {
struct SceneLight {
    enum : uint32_t { Directional = 0, Point = 1 };
    uint32_t type = Directional;
    float vector[3]{}, color[3]{}, radiusSquared = 0;
};
struct SceneLighting {
    float ambient[3]{};
    std::vector<SceneLight> lights;
};
struct SceneModelTexture {
    uint64_t identity = 0;
    uint32_t nativeId = 0, flags = 0;
    // Original GS alpha domain (128 is unity), like the renderer's texel cache.
    std::vector<uint32_t> pixels; // owned 128x128 RGBA8, no guest pointers
};
struct SceneModelTriangle {
    float position[3][3]{}, uv[3][2]{};
    uint32_t material = ~0u;
};
struct SceneModelMesh {
    uint64_t identity = 0;
    uint64_t topology = 0; // ordered native indices/materials, independent of pose
    uint32_t nativeId = 0;
    std::vector<SceneModelTriangle> triangles;
};
struct SceneModelInstance {
    uint32_t mesh = 0, object = 0;
    uint64_t generation = 0; // object lifetime, never inferred from its address alone
    // Row-major 3x4 object-to-world, suitable for a DXR instance transform.
    float transform[12]{};
    float opacity = 1;
};
struct SceneModels {
    uint64_t level = 0;
    std::vector<std::shared_ptr<const SceneModelMesh>> meshes;
    std::vector<std::shared_ptr<const SceneModelTexture>> textures;
    std::vector<SceneModelInstance> instances;
    uint32_t rejectedInstances = 0, untexturedTriangles = 0;
    SceneLighting lighting;
};
} // namespace gsr
