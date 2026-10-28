#include "motion_history.h"
#include <algorithm>
#include <cmath>
#include <tuple>

namespace gsr::motion {
bool Key::operator<(const Key& b) const {
    return std::tie(object, generation, mesh, part, pass, batch) <
           std::tie(b.object, b.generation, b.mesh, b.part, b.pass, b.batch);
}
void History::recycle(std::map<Key, Entry>& entries) {
    for (auto& item : entries)
        if (pool_.size() < 4096 && item.second.vertices.capacity())
            pool_.push_back(std::move(item.second.vertices));
    entries.clear();
}
void History::reset() {
    recycle(previous_);
    recycle(current_);
    ambiguous_.clear();
    epoch_ = referenceEpoch_ = bytes_ = 0;
}
void History::commitFrame() {
    recycle(previous_);
    previous_ = std::move(current_);
    current_.clear();
    referenceEpoch_ = epoch_;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
}
void History::beginFrame(uint64_t epoch, bool cut) {
    if (cut || !epoch || (epoch_ && (epoch < epoch_ || (!manual_ && epoch != epoch_ && epoch != epoch_ + 1))))
        reset();
    if (epoch == epoch_)
        return;
    if (!manual_)
        commitFrame();
    recycle(current_);
    ambiguous_.clear();
    epoch_ = epoch;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
}
Result History::track(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                      bool known, std::vector<Previous>& output) {
    output.clear();
    if (count > 1000000)
        return {};
    output.assign(count, {});
    if (!known || !key.object || !key.mesh || !epoch_ || !vertices || !count || count > 1000000 || !std::isfinite(sx) ||
        !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return {};
    // Repeated scope/batch IDs in one world are ambiguous, never a second object.
    if (ambiguous_.count(key))
        return {};
    const auto duplicate = current_.find(key);
    if (duplicate != current_.end()) {
        bytes_ -= duplicate->second.vertices.size() * sizeof(Vertex);
        if (pool_.size() < 4096)
            pool_.push_back(std::move(duplicate->second.vertices));
        current_.erase(duplicate);
        ambiguous_.insert(key);
        return {};
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || v.z <= 0 || !std::isfinite(v.u) ||
            !std::isfinite(v.v))
            return {};
    }
    const auto found = previous_.find(key);
    bool match = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                 found->second.topology == topology && found->second.vertices.size() == count;
    if (match) {
        const Vertex* previous = found->second.vertices.data();
        for (uint32_t i = 0; i < count; ++i) {
            const auto& old = previous[i];
            const auto& now = vertices[i];
            // Texture coordinates identify vertex correspondence, and reject newly
            // generated clipping intersections/reordering instead of inventing flow.
            const float ratio = now.z / old.z;
            if (std::abs(old.u - now.u) > 1e-4f || std::abs(old.v - now.v) > 1e-4f || !std::isfinite(ratio) ||
                ratio < 0.01f || ratio > 100 || std::abs(old.x - now.x) > 4096 || std::abs(old.y - now.y) > 4096) {
                match = false;
                break;
            }
            output[i] = {old.x * sx * ratio, old.y * sy * ratio, ratio, 1};
        }
        if (!match)
            std::fill(output.begin(), output.end(), Previous{});
    }
    const uint64_t size = uint64_t(count) * sizeof(Vertex);
    if (size <= budget_ && bytes_ <= budget_ - size) {
        Entry e;
        e.epoch = epoch_;
        e.topology = topology;
        if (!pool_.empty()) {
            e.vertices = std::move(pool_.back());
            pool_.pop_back();
        }
        e.vertices.assign(vertices, vertices + count);
        current_.emplace(key, std::move(e));
        bytes_ += size;
    }
    return {match, match ? count : 0};
}
} // namespace gsr::motion
