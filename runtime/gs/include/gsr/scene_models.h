#pragma once
#include <cmath>
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
    bool hasContribution() const {
        if (!lights.empty())
            return true;
        for (float channel : ambient)
            if (std::isfinite(channel) && channel > 0)
                return true;
        return false;
    }
};
struct SceneModelTexture {
    uint64_t identity = 0;
    uint32_t nativeId = 0, flags = 0;
    // Original GS alpha domain (128 is unity), like the renderer's texel cache.
    std::vector<uint32_t> pixels;               // owned 128x128 RGBA8, no guest pointers
    float roughness = .75f, reflectance = .04f; // conservative dielectric fallback; optional material overrides
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
    bool nativeShadow = false; // this world submission's native character projector owns direct shadow casting
    // Motion-only snapshots (SceneModels::motionOnly): the object was left out of the previous snapshot because it
    // stood still, with this transform and opacity and the same mesh. Stands in for its entry in that snapshot.
    bool hasPreviousTransform = false;
    float previousTransform[12]{};
    float previousOpacity = 1;
};
struct SceneModels {
    uint64_t level = 0;
    std::vector<std::shared_ptr<const SceneModelMesh>> meshes;
    std::vector<std::shared_ptr<const SceneModelTexture>> textures;
    std::vector<SceneModelInstance> instances;
    uint32_t rejectedInstances = 0, untexturedTriangles = 0;
    SceneLighting lighting;
    // The snapshot serves object motion only (sceneModelsMotionOnly below): instances that do not deform and have
    // the transform they had in the previous capture are left out (unchangedInstances counts them). An instance that
    // starts to move after being left out carries its previous transform itself (hasPreviousTransform).
    bool motionOnly = false;
    uint32_t unchangedInstances = 0;
};

// Who reads a model snapshot decides what it must hold. The ray effects (water, reflections, shadows, global
// illumination) need every resident instance. The temporal consumers (super resolution, neural rendering) only use
// instances that moved or deformed since the previous world: a static model's motion follows from depth and the
// camera. With only temporal consumers the capture, which runs on the game's main thread every world, may therefore
// skip everything that stands still.
constexpr bool sceneModelsMotionOnly(bool rayEffects, bool temporalConsumers) {
    return temporalConsumers && !rayEffects;
}
} // namespace gsr
