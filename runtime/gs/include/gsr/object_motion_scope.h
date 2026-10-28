#pragma once
#include <cstdint>
#include <type_traits>

namespace gsr {
// Copy by value with the VIF job, then replay through the SAME GS/GIF queue.
// IDs are guest object/resource/part addresses, never draw-order ordinals.
// Valid identifies an engine object; it does NOT certify vertex correspondence.
struct ObjectMotionScope {
    enum : uint32_t {
        Begin = 1,
        End = 2,
        Valid = 4,
        Animated = 8,
        TopologyStable = 16,
        Reset = 32,
        Clipped = 64,
        Ambiguous = 128,
        HasTransform = 256,
        Rejected = 512
    };
    uint64_t objectId = 0, generation = 0;
    uint64_t meshId = 0, partId = 0;
    uint64_t frameEpoch = 0, sceneSerial = 0, submissionSerial = 0, topologyHash = 0;
    uint32_t flags = 0, sourcePc = 0;
    uint32_t objectAddress = 0, meshAddress = 0, partAddress = 0, passKey = 0;
    uint32_t vertexCount = 0, indexCount = 0;
    float objectToWorld[16]{}; // row-vector/row-major only when HasTransform
};
static_assert(std::is_trivially_copyable_v<ObjectMotionScope>);
static_assert(sizeof(ObjectMotionScope) == 160);
} // namespace gsr
