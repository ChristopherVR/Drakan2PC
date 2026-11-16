#include "motion_history.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>

namespace gsr::motion {
bool Key::operator<(const Key& b) const {
    return std::tie(object, generation, mesh, part, pass, batch) <
           std::tie(b.object, b.generation, b.mesh, b.part, b.pass, b.batch);
}
History::~History() {
    delete shadow_;
}
void History::setVerify(bool on) {
    if (on == (shadow_ != nullptr))
        return;
    delete shadow_;
    shadow_ = nullptr;
    if (!on)
        return;
    // From here on both see the same calls; the shadow starts from this history's state as of the next reset.
    shadow_ = new History(budget_, manual_);
    shadow_->fast_ = false;
    reset();
}
void History::recycle(Entries& entries) {
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
    if (shadow_)
        shadow_->reset();
}
void History::commitFrame() {
    recycle(previous_);
    previous_ = std::move(current_);
    current_.clear();
    referenceEpoch_ = epoch_;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
    if (shadow_)
        shadow_->commitFrame();
}
void History::beginFrame(uint64_t epoch, bool cut) {
    if (shadow_)
        shadow_->beginFrame(epoch, cut);
    if (cut || !epoch || (epoch_ && (epoch < epoch_ || (!manual_ && epoch != epoch_ && epoch != epoch_ + 1)))) {
        History* const shadow = std::exchange(shadow_, nullptr); // already done above
        reset();
        shadow_ = shadow;
    }
    if (epoch == epoch_)
        return;
    if (!manual_) {
        History* const shadow = std::exchange(shadow_, nullptr);
        commitFrame();
        shadow_ = shadow;
    }
    recycle(current_);
    ambiguous_.clear();
    epoch_ = epoch;
    bytes_ = 0;
    for (const auto& item : previous_)
        bytes_ += item.second.vertices.size() * sizeof(Vertex);
}
Result History::track(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                      bool known, std::vector<Previous>& output) {
    if (!fast_)
        return trackReference(key, topology, vertices, count, sx, sy, known, output);
    output.clear();
    if (count > 1000000)
        return {};
    output.resize(count);
    return track(key, topology, vertices, count, sx, sy, known, output.data());
}
Result History::track(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                      bool known, Previous* output) {
    Result result;
    if (!fast_) {
        result = trackReference(key, topology, vertices, count, sx, sy, known, shadowOutput_);
        if (shadowOutput_.size() == count)
            std::copy(shadowOutput_.begin(), shadowOutput_.end(), output);
        else
            std::fill(output, output + count, Previous{});
    } else
        result = trackFast(key, topology, vertices, count, sx, sy, known, output);
    if (shadow_)
        compare(result, output, key, topology, vertices, count, sx, sy, known);
    return result;
}
void History::compare(const Result& result, const Previous* output, const Key& key, uint64_t topology,
                      const Vertex* vertices, uint32_t count, float sx, float sy, bool known) {
    std::vector<Previous>& reference = shadowOutput_;
    const Result expected = shadow_->trackReference(key, topology, vertices, count, sx, sy, known, reference);
    bool same = expected.matched == result.matched && expected.vertices == result.vertices && shadow_->bytes_ == bytes_;
    if (same && reference.size() == count)
        same = std::memcmp(reference.data(), output, count * sizeof(Previous)) == 0;
    else if (same) // the first implementation leaves no entries for an oversized batch; this one writes defaults
        same = count > 1000000;
    ++verifyChecked_;
    verifyDiffer_ += !same;
}
// The first implementation: one vertex at a time, leaving each loop at the first vertex that decides.
Result History::trackReference(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx,
                               float sy, bool known, std::vector<Previous>& output) {
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
    store(key, topology, vertices, count);
    return {match, match ? count : 0};
}
void History::store(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count) {
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
}
namespace {
// Exponent bits not all set: neither infinite nor NaN (std::isfinite, as integer operations the compiler can
// vectorise).
inline uint32_t finiteBit(float f) {
    uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return (u & 0x7f800000u) != 0x7f800000u;
}
} // namespace
// The same decisions and entries as trackReference, with every vertex loop free of early exits: a batch is decided
// after the whole loop (the first implementation stopped at the first vertex that decided it; the remaining vertices
// could not change the outcome) and the entries are written straight into the caller's buffer.
Result History::trackFast(const Key& key, uint64_t topology, const Vertex* vertices, uint32_t count, float sx, float sy,
                          bool known, Previous* output) {
    const auto none = [&]() {
        std::fill(output, output + count, Previous{});
        return Result{};
    };
    if (count > 1000000 || !known || !key.object || !key.mesh || !epoch_ || !vertices || !count || !std::isfinite(sx) ||
        !std::isfinite(sy) || sx <= 0 || sy <= 0)
        return none();
    if (ambiguous_.count(key))
        return none();
    const auto duplicate = current_.find(key);
    if (duplicate != current_.end()) {
        bytes_ -= duplicate->second.vertices.size() * sizeof(Vertex);
        if (pool_.size() < 4096)
            pool_.push_back(std::move(duplicate->second.vertices));
        current_.erase(duplicate);
        ambiguous_.insert(key);
        return none();
    }
    uint32_t finite = 1;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = vertices[i];
        finite &=
            finiteBit(v.x) & finiteBit(v.y) & finiteBit(v.z) & uint32_t(v.z > 0) & finiteBit(v.u) & finiteBit(v.v);
    }
    if (!finite)
        return none();
    const auto found = previous_.find(key);
    bool match = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                 found->second.topology == topology && found->second.vertices.size() == count;
    if (match) {
        const Vertex* previous = found->second.vertices.data();
        uint32_t bad = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& old = previous[i];
            const auto& now = vertices[i];
            const float ratio = now.z / old.z;
            bad |= uint32_t(std::abs(old.u - now.u) > 1e-4f) | uint32_t(std::abs(old.v - now.v) > 1e-4f) |
                   (finiteBit(ratio) ^ 1u) | uint32_t(ratio < 0.01f) | uint32_t(ratio > 100) |
                   uint32_t(std::abs(old.x - now.x) > 4096) | uint32_t(std::abs(old.y - now.y) > 4096);
            output[i] = {old.x * sx * ratio, old.y * sy * ratio, ratio, 1};
        }
        match = !bad;
    }
    if (!match)
        std::fill(output, output + count, Previous{});
    store(key, topology, vertices, count);
    return {match, match ? count : 0};
}
} // namespace gsr::motion
