#pragma once
#include <gsr/scene_models.h>
#include <map>
#include <utility>

namespace drakan_shadow {
// EE-thread ownership for one world submission. Native projection remains available even if the later modern
// pass fails. Only direct ray casting is suppressed; receivers, reflections and contact AO keep the same models.
class Frame {
    std::shared_ptr<const gsr::SceneModels> scene;
    std::map<uint32_t, uint64_t> projected;

public:
    void begin(std::shared_ptr<const gsr::SceneModels> models) {
        scene = std::move(models);
        projected.clear();
    }
    void record(uint32_t object, uint64_t generation) {
        if (scene && object && generation)
            projected[object] = generation;
    }
    std::shared_ptr<const gsr::SceneModels> finish() {
        auto source = std::move(scene);
        if (!source || projected.empty())
            return source;
        auto result = std::make_shared<gsr::SceneModels>(*source);
        for (auto& instance : result->instances) {
            const auto found = projected.find(instance.object);
            instance.nativeShadow = found != projected.end() && found->second == instance.generation;
        }
        projected.clear();
        return result;
    }
};
} // namespace drakan_shadow
