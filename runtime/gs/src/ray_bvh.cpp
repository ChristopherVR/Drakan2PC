// Binned SAH BVH builder, see ray_bvh.h.
#include "ray_bvh.h"

#include <algorithm>
#include <cmath>

namespace gsr::ray {
namespace {

constexpr int kBins = 12;

float area(const Box& b) {
    if (b.empty())
        return 0;
    const float x = b.max[0] - b.min[0], y = b.max[1] - b.min[1], z = b.max[2] - b.min[2];
    return x * y + y * z + z * x;
}

struct Task {
    uint32_t node, first, count;
};

} // namespace

Box Bvh::bounds() const {
    Box b;
    if (!nodes.empty()) {
        b.grow(nodes[0].min);
        b.grow(nodes[0].max);
    }
    return b;
}

Bvh build(std::span<const Box> boxes, uint32_t leafSize) {
    Bvh out;
    const auto n = static_cast<uint32_t>(boxes.size());
    if (!n)
        return out;
    leafSize = std::max(1u, leafSize);
    std::vector<float> centre(size_t(n) * 3);
    for (uint32_t i = 0; i < n; ++i)
        for (int a = 0; a < 3; ++a)
            centre[size_t(i) * 3 + a] = 0.5f * (boxes[i].min[a] + boxes[i].max[a]);
    out.order.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        out.order[i] = i;
    out.nodes.reserve(size_t(n) * 2 / leafSize + 2);
    out.nodes.emplace_back();
    std::vector<Task> stack{{0, 0, n}};
    while (!stack.empty()) {
        const Task t = stack.back();
        stack.pop_back();
        Box bounds, centres;
        for (uint32_t i = t.first; i < t.first + t.count; ++i) {
            bounds.grow(boxes[out.order[i]]);
            centres.grow(&centre[size_t(out.order[i]) * 3]);
        }
        auto leaf = [&] {
            Node& node = out.nodes[t.node];
            std::copy(bounds.min, bounds.min + 3, node.min);
            std::copy(bounds.max, bounds.max + 3, node.max);
            node.first = t.first;
            node.count = t.count;
        };
        if (t.count <= leafSize) {
            leaf();
            continue;
        }
        // Binned SAH over the widest centroid axes; cost relative to the leaf (intersection 1, traversal 1).
        int bestAxis = -1, bestSplit = 0;
        float bestCost = area(bounds) * float(t.count);
        // One pass bins the items on all three centroid axes.
        Box bins[3][kBins];
        uint32_t binCounts[3][kBins]{};
        float lo[3], scale[3];
        bool usable[3];
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = centres.min[axis];
            const float extent = centres.max[axis] - lo[axis];
            usable[axis] = extent > 1e-12f;
            scale[axis] = usable[axis] ? kBins / extent : 0.0f;
        }
        for (uint32_t i = t.first; i < t.first + t.count; ++i) {
            const uint32_t item = out.order[i];
            const float* c = &centre[size_t(item) * 3];
            for (int axis = 0; axis < 3; ++axis) {
                const int b = std::min(kBins - 1, int((c[axis] - lo[axis]) * scale[axis]));
                bins[axis][b].grow(boxes[item]);
                ++binCounts[axis][b];
            }
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!usable[axis])
                continue;
            const Box* binBox = bins[axis];
            const uint32_t* binCount = binCounts[axis];
            float rightArea[kBins]{};
            uint32_t rightCount[kBins]{};
            Box acc;
            uint32_t count = 0;
            for (int b = kBins - 1; b > 0; --b) {
                acc.grow(binBox[b]);
                count += binCount[b];
                rightArea[b] = area(acc);
                rightCount[b] = count;
            }
            acc = Box{};
            count = 0;
            for (int b = 0; b < kBins - 1; ++b) {
                acc.grow(binBox[b]);
                count += binCount[b];
                if (!count || !rightCount[b + 1])
                    continue;
                const float cost =
                    1.0f * area(bounds) + area(acc) * float(count) + rightArea[b + 1] * float(rightCount[b + 1]);
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = axis;
                    bestSplit = b;
                }
            }
        }
        uint32_t mid;
        if (bestAxis < 0) {
            // No useful split (coincident centroids): split the range in half to keep leaves small.
            if (t.count <= leafSize * 4u) {
                leaf();
                continue;
            }
            mid = t.first + t.count / 2;
        } else {
            const float lo = centres.min[bestAxis], scale = kBins / (centres.max[bestAxis] - lo);
            auto* begin = out.order.data() + t.first;
            auto* split = std::partition(begin, begin + t.count, [&](uint32_t item) {
                return std::min(kBins - 1, int((centre[size_t(item) * 3 + bestAxis] - lo) * scale)) <= bestSplit;
            });
            mid = t.first + uint32_t(split - begin);
            if (mid == t.first || mid == t.first + t.count)
                mid = t.first + t.count / 2;
        }
        const auto left = static_cast<uint32_t>(out.nodes.size());
        out.nodes.emplace_back();
        out.nodes.emplace_back();
        Node& node = out.nodes[t.node];
        std::copy(bounds.min, bounds.min + 3, node.min);
        std::copy(bounds.max, bounds.max + 3, node.max);
        node.first = left;
        node.count = 0;
        stack.push_back({left, t.first, mid - t.first});
        stack.push_back({left + 1, mid, t.first + t.count - mid});
    }
    return out;
}

Bvh buildTriangles(std::span<const surface::Point> vertices, uint32_t leafSize) {
    std::vector<Box> boxes(vertices.size() / 3);
    for (size_t i = 0; i < boxes.size(); ++i)
        for (int j = 0; j < 3; ++j) {
            const auto& p = vertices[i * 3 + j];
            const float v[3] = {p.x, p.y, p.z};
            boxes[i].grow(v);
        }
    return build(boxes, leafSize);
}

} // namespace gsr::ray
