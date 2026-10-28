#pragma once
#include "runtime/object_motion_tracker.h"
#include <cstring>

namespace drakan_object_motion {
inline bool ramSpan(uint32_t a, uint32_t n, uint32_t size) {
    return a >= 0x100000u && n <= size && a <= size - n;
}
inline const uint8_t* resolveState(const uint8_t* ram, uint32_t ramSize, const uint8_t* scratch, uint32_t scratchSize,
                                   uint32_t guest, uint32_t bytes) {
    if ((guest & 0xffffc000u) == 0x70000000u) {
        const uint32_t offset = guest & 0x3fffu;
        return scratch && bytes <= scratchSize && offset <= scratchSize - bytes ? scratch + offset : nullptr;
    }
    const uint32_t address = guest & 0x1fffffffu;
    return ram && ramSpan(address, bytes, ramSize) ? ram + address : nullptr;
}
inline uint64_t captureHash(uint64_t h, const void* ptr, size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(ptr);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}
struct PoseSource {
    uint32_t address = 0, count = 0, controller = 0;
};
inline PoseSource provenPoseSource(const uint8_t* ram, uint32_t size, uint32_t object) {
    auto valid = [&](uint32_t p, uint32_t n) { return ramSpan(p, n, size); };
    auto rd = [&](uint32_t p) {
        uint32_t v;
        std::memcpy(&v, ram + p, 4);
        return v;
    };
    if (!valid(object, 0x120))
        return {};
    const uint32_t controller = rd(object + 0x50);
    if (!valid(controller, 0x12c) || rd(controller + 0xd4) != object)
        return {};
    const uint32_t vt = rd(controller), header = rd(controller + 0x120);
    // This exact vtable slot executes 106aa8 -> 1165b0's bone-matrix path.
    if (!valid(vt, 0x148) || rd(vt + 0x140) != 0 || rd(vt + 0x144) != 0x106aa8 || !valid(header, 12))
        return {};
    const uint32_t bones = rd(controller + 0x114), matrices = rd(controller + 0x10c);
    if (!(rd(controller + 8) & 0x100000u) || bones < 2 || bones > 4096 || !valid(matrices, bones * 0x6c))
        return {};
    const uint32_t count = rd(header + 8), source = rd(header);
    if (!count || count > 1048576 || count != rd(controller + 0x118) || !valid(source, count * 12))
        return {};
    // 106aa8/1165b0 use this owned pose buffer for the active LOD slot.
    // Its vertex count is checked against that slot by the geometry consumer.
    if (rd(controller + 0x10) >= 64)
        return {};
    return {source, count, controller};
}
struct InputTopology {
    uint64_t hash = 0;
    uint32_t vertices = 0, indices = 0, maxPositionIndex = 0;
    bool verified = false;
};
inline InputTopology inspectInputTopology(const uint8_t* ram, uint32_t size, uint32_t mesh, uint32_t part,
                                          bool indexed) {
    InputTopology out;
    auto valid = [&](uint32_t p, uint32_t n) { return ramSpan(p, n, size); };
    auto rd = [&](uint32_t p) {
        uint32_t v;
        std::memcpy(&v, ram + p, 4);
        return v;
    };
    if (!valid(part, 0x30))
        return out;
    out.hash = captureHash(14695981039346656037ull, ram + part, 0x30);
    if (indexed) {
        if (!valid(mesh, 0xc))
            return out;
        const uint32_t groups = rd(part + 8), selectors = rd(part + 0xc), table = rd(mesh + 8);
        if (!groups || groups > 65536 || !valid(selectors, groups * 2))
            return out;
        out.hash = captureHash(out.hash, ram + selectors, groups * 2);
        for (uint32_t i = 0; i < groups; ++i) {
            const uint32_t entry = table + ram[selectors + i * 2] * 4;
            if (entry < table || !valid(entry, 4))
                return out;
            uint16_t start = 0, count = 0;
            std::memcpy(&start, ram + entry, 2);
            std::memcpy(&count, ram + entry + 2, 2);
            if (!count || count > 256)
                return out;
            out.hash = captureHash(out.hash, ram + entry, 4);
            out.vertices += count;
            out.maxPositionIndex = std::max(out.maxPositionIndex, uint32_t(start) + count);
        }
        out.indices = groups * 2;
        out.verified = true;
    } else {
        out.vertices = rd(part + 0x10);
        // 1ecbac..1ecbbc uploads 64 packed input bytes per batch of 64,
        // including the last padded block. These encode output strip ordering.
        const uint32_t source = rd(part + 0x14), bytes = (out.vertices + 63u) & ~63u;
        if (!out.vertices || out.vertices > 1048576 || !valid(source, bytes))
            return out;
        out.hash = captureHash(out.hash, ram + source, bytes);
        out.verified = true;
    }
    return out;
}

// Exact 1ed8f0 mesh/part/group layout. Positions are deliberately excluded from
// the input-topology certificate; original indices and ordered UVs are hashed.
template <class Emit>
void captureInlineGroups(const uint8_t* ram, uint32_t size, Tracker& tracker, const Scope& owner, uint32_t mesh,
                         bool positionEvidence, Emit emit) {
    tracker.enableReferences(owner.sceneSerial);
    auto valid = [&](uint32_t p, uint32_t n) { return ramSpan(p, n, size); };
    auto rd = [&](uint32_t p) {
        uint32_t v;
        std::memcpy(&v, ram + p, 4);
        return v;
    };
    auto phys = [](uint32_t p) { return p & 0x1fffffffu; };
    if (mesh != owner.meshAddress || !valid(mesh, 0x34))
        return;
    const uint32_t count = rd(mesh + 0xc), parts = phys(rd(mesh + 0x10));
    if (count > 4096 || !valid(parts, count * 0x30))
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t part = parts + i * 0x30;
        const uint32_t groups = rd(part + 0x28), groupBase = phys(rd(part + 0x20));
        const uint32_t indexCount = rd(part + 0x10), indexBase = phys(rd(part + 0x2c));
        if (!groups || groups > 65536 || !valid(groupBase, groups * 16))
            continue;
        const bool indicesValid = indexCount > 0 && indexCount <= 1048576 && valid(indexBase, indexCount * 2);
        uint64_t parent = captureHash(14695981039346656037ull, ram + part, 0x30);
        if (indicesValid)
            parent = captureHash(parent, ram + indexBase, indexCount * 2);
        for (uint32_t g = 0; g < groups; ++g) {
            const uint32_t group = groupBase + g * 16;
            const uint32_t n = rd(group), positions = phys(rd(group + 8)), uv = phys(rd(group + 4));
            if (!n || n > 256 || !valid(positions, n * 12) || !valid(uv, n * 8))
                continue;
            uint64_t hash = captureHash(parent, ram + group, 16);
            hash = captureHash(hash, ram + uv, n * 8);
            Scope p = tracker.part(owner, group, 0x1ed8f0, hash, n, indexCount, indicesValid);
            p.sourcePc = 0x1ed8f0;
            tracker.reference(positions, p);
            emit(p, positions, positionEvidence ? captureHash(14695981039346656037ull, ram + positions, n * 12) : 0);
        }
    }
}
} // namespace drakan_object_motion
