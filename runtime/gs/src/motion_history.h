#pragma once
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gsr::motion {
struct Key {
    uint64_t object = 0, generation = 0, mesh = 0, part = 0;
    uint32_t pass = 0, batch = 0;
    bool operator<(const Key& b) const;
    bool operator==(const Key& b) const {
        return object == b.object && generation == b.generation && mesh == b.mesh && part == b.part && pass == b.pass &&
               batch == b.batch;
    }
};
// The history is looked up several times for every world draw: hashed, not ordered (nothing depends on the order).
struct KeyHash {
    size_t operator()(const Key& k) const {
        uint64_t h = 1469598103934665603ull;
        for (uint64_t v : {k.object, k.generation, k.mesh, k.part, (uint64_t(k.pass) << 32) | k.batch})
            h = (h ^ v) * 1099511628211ull;
        return static_cast<size_t>(h ^ (h >> 29));
    }
};
struct Vertex {
    float x, y, z, u, v;
};
// Native previous position times currentZ/previousZ, then raster scale.
// Interpolate numerator/denominator separately before projecting previous XY.
struct Previous {
    float x = 0, y = 0, ratio = 1, valid = 0;
};
struct Result {
    bool matched = false;
    uint32_t vertices = 0;
};
class History {
public:
    explicit History(uint64_t budgetBytes = 64ull * 1024 * 1024, bool manualReference = false)
        : budget_(budgetBytes), manual_(manualReference) {}
    // One epoch per complete engine world, independently of host presents/vsync.
    void beginFrame(uint64_t epoch, bool reset = false);
    Result track(const Key&, uint64_t topology, const Vertex*, uint32_t count, float rasterX, float rasterY,
                 bool correspondenceKnown, std::vector<Previous>& output);
    void reset();
    void commitFrame(); // manual mode: advance only after the temporal consumer accepted this world
    uint64_t bytes() const { return bytes_; }

private:
    struct Entry {
        uint64_t epoch = 0, topology = 0;
        std::vector<Vertex> vertices;
    };
    using Entries = std::unordered_map<Key, Entry, KeyHash>;
    Entries previous_, current_;
    std::unordered_set<Key, KeyHash> ambiguous_;
    // Vertex vectors of retired entries, reused by later track() calls instead of a fresh allocation per draw.
    std::vector<std::vector<Vertex>> pool_;
    void recycle(Entries& entries);
    uint64_t epoch_ = 0, referenceEpoch_ = 0, bytes_ = 0, budget_;
    bool manual_ = false;
};
} // namespace gsr::motion
