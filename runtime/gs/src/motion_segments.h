// Object-motion scopes inside one draw batch (backend neutral, unit tested).
#pragma once
#include <cstdint>
#include <vector>

#include <gsr/object_motion_scope.h>

namespace gsr::motion {

// The runtime brackets every object's draws with an ObjectMotionScope (Begin ... End) so the temporal upscaler can
// match this world's vertices with the previous world's, object by object. A scope note used to end the current draw
// batch, which cost a draw call per object even where neighbouring objects share every GS register (terrain chunks,
// repeated models): about a quarter more draw calls than the same stream without an upscaler.
//
// A backend that accepts deferred notes (BackendApi::deferObjectMotion) instead gets, with the batch, the notes that
// arrived while the batch was open, each with the number of batch vertices queued before it. The vertices between
// two notes are one segment and are tracked exactly as the separate draw was: same scope, same order, same vertices.
struct Note {
    uint32_t vertex = 0; // batch vertices queued before this note
    ObjectMotionScope scope;
};

// Walk a batch of `count` vertices: `segment(first, n)` for every non-empty run of vertices under one scope, in
// order, and `apply(scope)` for every note, in order, between the segments it separates. Notes whose position lies
// past the batch (it cannot happen with the core's bookkeeping) are applied after the last segment.
template <class Apply, class Segment>
void forEachSegment(const Note* notes, uint32_t noteCount, uint32_t count, Apply&& apply, Segment&& segment) {
    uint32_t first = 0;
    for (uint32_t i = 0; i < noteCount; ++i) {
        const uint32_t end = notes[i].vertex < count ? notes[i].vertex : count;
        if (end > first) {
            segment(first, end - first);
            first = end;
        }
        apply(notes[i].scope);
    }
    if (count > first)
        segment(first, count - first);
}

// Core side: the notes of the open batch.
class Deferred {
public:
    void add(uint32_t batchVertices, const ObjectMotionScope& scope) { notes_.push_back({batchVertices, scope}); }
    bool empty() const { return notes_.empty(); }
    const Note* data() const { return notes_.data(); }
    uint32_t size() const { return static_cast<uint32_t>(notes_.size()); }
    void clear() { notes_.clear(); }

private:
    std::vector<Note> notes_;
};

} // namespace gsr::motion
