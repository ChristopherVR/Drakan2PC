// Bounding volume hierarchies for the WebGPU ray pass (wgpu_ray.cpp), which traces in WGSL compute instead of DXR:
// a binned-SAH BVH over each triangle set (the BLAS) and one over the frame's instances (the TLAS), built on the CPU
// in the flat layout the shader walks.
//
// Node (two float4): min.xyz + first, max.xyz + count (bit-cast u32). count = 0: an interior node whose children are
// nodes `first` and `first + 1`; count > 0: a leaf over items [first, first + count) (triangles in a BLAS, instances in
// the TLAS). Indices are relative to the hierarchy; the pass rebases them when it packs several into one buffer.
#pragma once
#include "surface_geometry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gsr::ray {

struct Node {
    float min[3]{};
    uint32_t first = 0;
    float max[3]{};
    uint32_t count = 0;
};
static_assert(sizeof(Node) == 32);

struct Box {
    float min[3]{1e30f, 1e30f, 1e30f}, max[3]{-1e30f, -1e30f, -1e30f};
    void grow(const float* p) {
        for (int i = 0; i < 3; ++i) {
            min[i] = p[i] < min[i] ? p[i] : min[i];
            max[i] = p[i] > max[i] ? p[i] : max[i];
        }
    }
    void grow(const Box& b) {
        grow(b.min);
        grow(b.max);
    }
    bool empty() const { return !(min[0] <= max[0]); }
};

// A BVH over boxes; `order` lists the input indices in leaf order (a leaf's items are order[first .. first+count)).
struct Bvh {
    std::vector<Node> nodes;
    std::vector<uint32_t> order;
    Box bounds() const;
};

// Builds over `boxes` (at most `leafSize` items per leaf). Empty input gives no nodes.
Bvh build(std::span<const Box> boxes, uint32_t leafSize = 4);
// A BVH over triangles given as three points each.
Bvh buildTriangles(std::span<const surface::Point> vertices, uint32_t leafSize = 4);

} // namespace gsr::ray
