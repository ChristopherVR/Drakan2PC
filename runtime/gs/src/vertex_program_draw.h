// Vertex-program draws: everything about them that is the same for every backend (unit tested, no platform headers;
// gsr/vertex_program.h, gsr/vertex_path.h, docs/journal/2026-11-12/gpu-vertex-implementation.md).
//
// A batch of records goes through the shared Impl::draw (gs_draw.inl) like any other draw, so the target, texture,
// blend, depth, alpha test, scissor, passes and what the target remembers are one code. Three things differ and are
// decided here:
//   * which records may take the path at all (stateTakes, backendTakes), each decline with its counted reason;
//   * what Impl::draw reads of a draw's vertices: three stand-in vertices answer it (standIns);
//   * the motion vectors: the previous constants and raw positions of every record (trackBatch), which a velocity
//     shader turns into the value the CPU path computes in motion::History::track (historyOf is that computation,
//     written once for the shader text, the tests and the documentation).
//
// What a backend adds (members of its Impl, used by gs_draw.inl under kCaps.vertexPrograms):
//   programBatch             the batch being drawn, null for an ordinary draw
//   prepareProgramMotion     the batch's previous stream ready for the velocity pass; false: no velocity pass
//   bindProgramDraw          after bindDraw: the batch's buffers and vertex program replace the stand-ins'
//   drawProgramVertices      one indexed pass over the batch
//   drawProgramVelocity      the velocity pass of the colour pass just drawn
// The bounds of a batch are its scissor (the vertices are not known on the CPU). That is exact for everything a
// frame-sized target remembers per draw, and too coarse for the small scratch targets the game reads back (shadow
// and portrait targets, 128 wide): records into a target less than kMinTargetWidth wide keep the CPU evaluation.
#pragma once
#include "gs_internal.h"
#include "program_motion.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace gsr::program_draw {

inline constexpr uint32_t kMinTargetWidth = 512;

// The GS state of a record (decided per record, before it joins a batch). Only triangle strips. Flat shading takes the
// colour of a triangle's last vertex and FST reads UV registers a record does not have; a small target needs exact
// bounds.
inline bool stateTakes(uint32_t drawFlags, uint64_t frameRegister, uint32_t program, uint32_t prim) {
    const bool lit = program != vp::StripsFlat;
    if ((prim & 7u) != 4u) // the batch's index list is built for triangle strips
        return false;
    if ((drawFlags & DF_FST) || (lit && !(drawFlags & DF_IIP)))
        return false;
    const uint32_t fbw = static_cast<uint32_t>((frameRegister >> 16) & 0x3Fu);
    return fbw * 64u >= kMinTargetWidth;
}

// What a backend knows when it is asked for a record.
struct BackendState {
    bool shaderFailed = false;    // a compile, a buffer or the device failed: off for the session
    bool arithmetic = true;       // the adapter has the arithmetic the exact shader needs
    bool shaderReady = false;     // the background compile has finished
    bool worldOpen = false;       // the engine's world phase
    bool hudMapped = false;       // outside the world, a HUD or menu map may move the vertices of a draw
    bool rayEffects = false;      // the surface reconstruction un-projects the triangles of a draw
    bool terrainDetail = false;   // terrain classification reads the vertices
    bool lodStatistic = false;    // GSR_LODSTAT
    bool vertexDiagnosis = false; // a debug switch that prints or inspects vertices
};
inline bool backendTakes(const BackendState& b, vpath::Fallback* why) {
    vpath::Fallback reason = vpath::Fallback::BackendNoPath;
    bool takes = false;
    if (b.shaderFailed)
        reason = vpath::Fallback::Internal;
    else if (!b.arithmetic)
        reason = vpath::Fallback::AdapterNoSupport;
    else if (!b.shaderReady)
        reason = vpath::Fallback::NotReady;
    else if (b.rayEffects || b.terrainDetail || b.lodStatistic || b.vertexDiagnosis)
        reason = vpath::Fallback::Feature;
    else if (!b.worldOpen && b.hudMapped)
        reason = vpath::Fallback::DrawState;
    else
        takes = true;
    if (!takes && why)
        *why = reason;
    return takes;
}

// Three vertices over the scissor: perspective (q is not a UI value), fog 0, so Impl::draw classifies the draw as
// the world geometry a record is, and takes the scissor as its bounds.
inline void standIns(const GsDrawState& st, DrawVertex out[3]) {
    const float x0 = static_cast<float>(st.scissor & 0x7FFu), x1 = static_cast<float>((st.scissor >> 16) & 0x7FFu);
    const float y0 = static_cast<float>((st.scissor >> 32) & 0x7FFu),
                y1 = static_cast<float>((st.scissor >> 48) & 0x7FFu);
    for (int i = 0; i < 3; ++i) {
        out[i] = DrawVertex{};
        out[i].x = x0;
        out[i].y = y0;
        out[i].r = out[i].g = out[i].b = out[i].a = 128.0f;
        out[i].q = 0.5f;
    }
    out[1].x = x1;
    out[2].y = y1;
}

// The `history` value of a vertex, as motion::History::track computes it from the processed vertex of this world
// (x, y in native pixels, z the GS depth as a float) and of the reference world, with the limits it applies. The
// velocity shader of a vertex-program draw repeats exactly these operations in this order (sx, sy: render pixels per
// native pixel). History::track rejects a whole draw when one vertex fails a limit; a shader can only reject the
// vertex, and the pixel shader then rejects every triangle that has such a vertex.
struct HistoryValue {
    float x = 0, y = 0, ratio = 1, valid = 0;
};
inline HistoryValue historyOf(float nowX, float nowY, float nowZ, float oldX, float oldY, float oldZ, float sx,
                              float sy, bool previousKnown) {
    if (!previousKnown || !(nowZ > 0) || !(oldZ > 0))
        return {};
    const float ratio = nowZ / oldZ;
    if (!std::isfinite(ratio) || ratio < 0.01f || ratio > 100 || std::abs(oldX - nowX) > 4096 ||
        std::abs(oldY - nowY) > 4096)
        return {};
    return {oldX * sx * ratio, oldY * sy * ratio, ratio, 1};
}

// What does not change between two worlds of one record, as a topology value for ProgramHistory: the scope's own
// hash, the record's shape (program, strip lengths), its raw texture coordinates and the GS state History mixes in.
inline uint64_t topologyOf(const ObjectMotionScope& scope, const VertexProgramRecord& record,
                           const VertexProgramVertex* vertices, const GsDrawState& s) {
    uint64_t h = scope.topologyHash;
    const auto mix = [&h](uint64_t value) { h = (h ^ value) * 1099511628211ull; };
    for (uint64_t value : {uint64_t(record.vertexCount), s.tex0, s.alpha, uint64_t(s.flags), s.test, record.shape})
        mix(value);
    for (uint32_t i = 0; i < record.vertexCount; ++i) {
        uint64_t st[2];
        std::memcpy(st, vertices[record.firstVertex + i].in.st, 16);
        mix(st[0]);
        mix(st[1]);
    }
    return h;
}

struct MotionCounts {
    uint32_t records = 0, matched = 0, matchedVertices = 0, scoped = 0;
};

// Applies the batch's scope notes in order without tracking anything (no temporal consumer, or a draw that was
// skipped): the scope still advances exactly as it does for an ordinary batch.
template <class Apply>
void applyNotes(const VertexProgramBatch& batch, Apply&& apply) {
    for (uint32_t i = 0; i < batch.noteCount; ++i)
        apply(batch.notes[i].scope);
}

// Walks the records of a batch in stream order, applying every scope note at its place (`apply(scope)`; `current()`
// returns the scope in force), and asks the history for each record's previous state under the key rule of the CPU
// path: motion::Key from the scope, plus the ordinal of the record under that key in this world.
// Output: `previous` has one entry per batch vertex; `previousConstants` are the constants those entries index,
// starting at `firstPreviousConstants` (a backend appends them after the batch's own constants, so the shader loads
// both from one buffer).
template <class Apply, class Current>
MotionCounts trackBatch(motion::PairedHistory& history, const VertexProgramBatch& batch, const GsDrawState& s,
                        uint32_t firstPreviousConstants, std::vector<motion::ProgramPrevious>& previous,
                        std::vector<vp::Constants>& previousConstants, Apply&& apply, Current&& current) {
    MotionCounts counts;
    previous.assign(batch.vertexCount, motion::ProgramPrevious{});
    previousConstants.clear();
    uint32_t note = 0;
    for (uint32_t r = 0; r < batch.recordCount; ++r) {
        while (note < batch.noteCount && batch.notes[note].vertex <= r)
            apply(batch.notes[note++].scope);
        const VertexProgramRecord& record = batch.records[r];
        if (!record.vertexCount || record.firstVertex + record.vertexCount > batch.vertexCount ||
            record.constants >= batch.constantCount)
            continue;
        const ObjectMotionScope& scope = current();
        motion::Key key{scope.objectId, scope.generation, scope.meshId, scope.partId, scope.passKey, 0};
        key.batch = history.programOrdinals[key]++;
        const uint32_t required = ObjectMotionScope::Valid | ObjectMotionScope::TopologyStable;
        const uint32_t rejected = ObjectMotionScope::Reset | ObjectMotionScope::Clipped | ObjectMotionScope::Ambiguous |
                                  ObjectMotionScope::Rejected;
        const bool known = (scope.flags & required) == required && !(scope.flags & rejected);
        ++counts.records;
        counts.scoped += known;
        if (!known || !key.object || !key.mesh)
            continue; // ProgramHistory::track refuses these before it looks at a vertex
        const vp::Constants* old = nullptr;
        const uint32_t index = firstPreviousConstants + static_cast<uint32_t>(previousConstants.size());
        if (history.program.track(key, topologyOf(scope, record, batch.vertices, s), batch.constants[record.constants],
                                  &batch.vertices[record.firstVertex].in, record.vertexCount, known, index,
                                  previous.data() + record.firstVertex, &old, sizeof(VertexProgramVertex))) {
            previousConstants.push_back(*old);
            ++counts.matched;
            counts.matchedVertices += record.vertexCount;
        }
    }
    while (note < batch.noteCount)
        apply(batch.notes[note++].scope);
    return counts;
}

} // namespace gsr::program_draw
