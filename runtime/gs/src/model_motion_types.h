// Vertex and texture records of the model-motion pass (backend neutral: produced by model_motion.h, consumed by a
// backend's guide pass).
#pragma once
#include <cstdint>
#include <span>

namespace gsr::nr::guides {
struct ModelMotionVertex {
    float current[3]{}, previous[3]{}, uv[2]{};
    uint32_t alphaTexture = ~0u;
};
struct ModelMotionTexture {
    uint64_t identity = 0;
    std::span<const uint32_t> pixels;
};
} // namespace gsr::nr::guides
