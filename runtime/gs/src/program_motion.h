// Previous-frame inputs of vertex-program draws, for motion vectors made by the vertex shader (backend neutral, unit
// tested; gsr/vertex_program.h, docs/journal/2026-11-12/gpu-vertex-implementation.md).
//
// On the CPU path the temporal upscaler's motion vectors come from motion::History: per object-motion scope it keeps
// the screen positions of the vertices a draw had in the previous world and hands them to the velocity pass. A
// vertex-program draw has no screen positions on the CPU. What it has instead is cheaper and says the same: the
// record's constants (the previous matrix among them) and its raw object-space positions. ProgramHistory keeps those
// per scope; the velocity shader evaluates the position part of the stage a second time with the previous constants
// and the previous raw position and gets, bit for bit, the screen position the CPU path remembered.
//
// The rules are those of motion::History, so both paths agree on which draws have a history:
//   * an entry is one record under one scope key (object, generation, mesh, part, pass, ordinal of the record under
//     that key in this world); a key met twice in one world is ambiguous and never matched;
//   * the previous entry must come from the reference world (the one before, or the last one the temporal consumer
//     accepted in manual mode), with the same topology value and vertex count;
//   * vertex correspondence: History compares the texture coordinates of the processed vertices within 1e-4; here the
//     raw texture coordinate rows must be equal (they are inputs, and an object's do not change between worlds).
// What History decides from processed values (a depth that is not positive, a depth ratio outside 0.01 .. 100, a
// screen distance above 4096) cannot be decided here without evaluating the vertices; the velocity shader applies the
// same limits per vertex instead of per draw.
#pragma once
#include "motion_history.h"

#include <gsr/vertex_program.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gsr::motion {

// Per vertex of a vertex-program batch: what the velocity shader needs of the previous world.
struct ProgramPrevious {
    uint32_t pos[4] = {0, 0, 0, 0}; // previous raw position (float bits)
    uint32_t constants = 0;         // index of the previous constants in the batch's previous-constants array
    uint32_t valid = 0;             // 1 when the two fields above are a real previous state
    uint32_t pad[2] = {0, 0};
};
static_assert(sizeof(ProgramPrevious) == 32, "two rows");

class ProgramHistory {
public:
    explicit ProgramHistory(uint64_t budgetBytes = 64ull * 1024 * 1024, bool manualReference = false)
        : budget_(budgetBytes), manual_(manualReference) {}

    // One epoch per complete engine world; the same calls, at the same places, as motion::History.
    void beginFrame(uint64_t epoch, bool cut = false) {
        if (cut || !epoch || (epoch_ && (epoch < epoch_ || (!manual_ && epoch != epoch_ && epoch != epoch_ + 1))))
            reset();
        if (epoch == epoch_)
            return;
        if (!manual_)
            commitFrame();
        current_.clear();
        ambiguous_.clear();
        epoch_ = epoch;
        bytes_ = bytesOf(previous_);
    }
    void commitFrame() {
        previous_ = std::move(current_);
        current_.clear();
        referenceEpoch_ = epoch_;
        bytes_ = bytesOf(previous_);
    }
    void reset() {
        previous_.clear();
        current_.clear();
        ambiguous_.clear();
        epoch_ = referenceEpoch_ = bytes_ = 0;
    }
    uint64_t bytes() const { return bytes_; }

    // A value for `topology` from what must not change between two worlds of one record: the program, the strip
    // lengths and the raw texture coordinate rows.
    static uint64_t shapeOf(const vp::Header& header, const uint32_t* strips, const vp::Vertex* vertices) {
        uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](uint64_t value) { h = (h ^ value) * 1099511628211ull; };
        mix(header.program);
        mix(header.stripCount);
        mix(header.vertexCount);
        for (uint32_t i = 0; i < header.stripCount; ++i)
            mix(strips[i]);
        for (uint32_t i = 0; i < header.vertexCount; ++i) {
            uint64_t st[2];
            std::memcpy(st, vertices[i].st, 16);
            mix(st[0]);
            mix(st[1]);
        }
        return h;
    }

    // Tracks one record. Writes `count` entries to `out`: the previous raw positions with `previousConstants` as
    // their constants index when the record has a history (the caller then appends *previous to its array at that
    // index), zeroed entries otherwise. `known`: the scope certifies the object (Valid, TopologyStable, none of the
    // rejecting flags), as for History::track.
    bool track(const Key& key, uint64_t topology, const vp::Constants& constants, const vp::Vertex* vertices,
               uint32_t count, bool known, uint32_t previousConstants, ProgramPrevious* out,
               const vp::Constants** previous) {
        for (uint32_t i = 0; i < count; ++i)
            out[i] = ProgramPrevious{};
        *previous = nullptr;
        if (!known || !key.object || !key.mesh || !epoch_ || !vertices || !count || count > vp::kMaxVertices)
            return false;
        // Repeated scope and ordinal in one world: ambiguous, never a second object.
        if (ambiguous_.count(key))
            return false;
        const auto duplicate = current_.find(key);
        if (duplicate != current_.end()) {
            bytes_ -= sizeOf(duplicate->second);
            current_.erase(duplicate);
            ambiguous_.insert(key);
            return false;
        }
        const auto found = previous_.find(key);
        const bool match = found != previous_.end() && found->second.epoch == referenceEpoch_ &&
                           found->second.topology == topology && found->second.positions.size() == size_t(count) * 4u;
        if (match) {
            const uint32_t* old = found->second.positions.data();
            for (uint32_t i = 0; i < count; ++i) {
                std::memcpy(out[i].pos, old + size_t(i) * 4u, 16);
                out[i].constants = previousConstants;
                out[i].valid = 1;
            }
            *previous = &found->second.constants;
        }
        const uint64_t size = sizeof(vp::Constants) + uint64_t(count) * 16u;
        if (size <= budget_ && bytes_ <= budget_ - size) {
            Entry e;
            e.epoch = epoch_;
            e.topology = topology;
            e.constants = constants;
            e.positions.resize(size_t(count) * 4u);
            for (uint32_t i = 0; i < count; ++i)
                std::memcpy(e.positions.data() + size_t(i) * 4u, vertices[i].pos, 16);
            current_.emplace(key, std::move(e));
            bytes_ += size;
        }
        return match;
    }

private:
    struct Entry {
        uint64_t epoch = 0, topology = 0;
        vp::Constants constants{};
        std::vector<uint32_t> positions; // four words per vertex
    };
    using Entries = std::unordered_map<Key, Entry, KeyHash>;
    static uint64_t sizeOf(const Entry& e) { return sizeof(vp::Constants) + e.positions.size() * 4u; }
    static uint64_t bytesOf(const Entries& entries) {
        uint64_t total = 0;
        for (const auto& item : entries)
            total += sizeOf(item.second);
        return total;
    }
    Entries previous_, current_;
    std::unordered_set<Key, KeyHash> ambiguous_;
    uint64_t epoch_ = 0, referenceEpoch_ = 0, bytes_ = 0, budget_;
    bool manual_ = false;
};

} // namespace gsr::motion
