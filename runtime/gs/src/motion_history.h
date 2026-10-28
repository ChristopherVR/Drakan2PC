#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace gsr::motion {
struct Key {
    uint64_t object = 0, generation = 0, mesh = 0, part = 0;
    uint32_t pass = 0, batch = 0;
    bool operator<(const Key& b) const;
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
    std::map<Key, Entry> previous_, current_;
    std::set<Key> ambiguous_;
    // Vertex vectors of retired entries, reused by later track() calls instead of a fresh allocation per draw.
    std::vector<std::vector<Vertex>> pool_;
    void recycle(std::map<Key, Entry>& entries);
    uint64_t epoch_ = 0, referenceEpoch_ = 0, bytes_ = 0, budget_;
    bool manual_ = false;
};
} // namespace gsr::motion
