// Vertex-program draws: everything about them that is the same for every backend (unit tested, no platform headers;
// gsr/vertex_program.h, gsr/vertex_path.h, docs/journal/2026-11-12/gpu-vertex-implementation.md).
//
// A batch of records goes through the shared Impl::draw (gs_draw.inl) like any other draw, so the target, texture,
// blend, depth, alpha test, scissor, passes and what the target remembers are one code. Three things differ and are
// decided here:
//   * which records may take the path at all (stateTakes, backendTakes), each decline with its counted reason;
//   * what Impl::draw reads of a draw's vertices: three stand-in vertices over the exact bounds answer it (standIns,
//     programBounds);
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
// The bounds of a batch are exact (programBounds: the vertices' places estimated with an error bound, the possible
// extremes evaluated exactly), so a draw that covers no pixel of its target is skipped and only the rows its vertices
// span count as drawn, as on the CPU path. That holds for the small scratch targets the game reads back every frame
// (shadow and portrait targets, 128 wide) as well; before the bounds were exact, records into a target less than
// kWideTarget wide kept the CPU evaluation, and GSR_VERTEX_PROGRAM_DEBUG bit 8 restores that rule for diagnosis.
#pragma once
#include "gs_internal.h"
#include "program_motion.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gsr::program_draw {

inline constexpr uint32_t kWideTarget = 512; // the old lower limit (GSR_VERTEX_PROGRAM_DEBUG bit 8)
inline constexpr uint32_t kAnyTarget = 64;   // one page: every target the game draws into

// The GS state of a record (decided per record, before it joins a batch). Only triangle strips. Flat shading takes the
// colour of a triangle's last vertex and FST reads UV registers a record does not have. `minWidth`: the narrowest
// target taken (kAnyTarget; kWideTarget for the diagnosis rule).
inline bool stateTakes(uint32_t drawFlags, uint64_t frameRegister, uint32_t program, uint32_t prim,
                       uint32_t minWidth = kAnyTarget) {
    const bool lit = program != vp::StripsFlat;
    if ((prim & 7u) != 4u) // the batch's index list is built for triangle strips
        return false;
    if ((drawFlags & DF_FST) || (lit && !(drawFlags & DF_IIP)))
        return false;
    const uint32_t fbw = static_cast<uint32_t>((frameRegister >> 16) & 0x3Fu);
    return fbw * 64u >= minWidth;
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

// What a batch-path record (gsr/vertex_batch.h) needs beyond stateTakes. Per vertex, the last of its passes that
// writes the position or Q (grid, texture generation, transform, project) must be a Transform or Project pass whose
// constants can never set the skip bit (vp::passNeverSkips): then every vertex is drawn, as the shader assumes, and
// is world geometry with a perspective Q, which is what the draw's stand-ins tell Impl::draw. A record whose vertex
// keeps its raw position (no transform, the "processed rows" form) or whose Q a later pass replaces is evaluated on
// the CPU (DrawState); a possible skip bit counts as SkipBit. `vertexBytes`: the record's vp::BatchVertex array,
// read without assuming alignment.
inline bool batchTakes(const vp::Pass* passes, uint32_t passCount, const uint8_t* vertexBytes, uint32_t vertexCount,
                       vpath::Fallback* why) {
    uint32_t writers = 0, safe = 0, skipping = 0;
    for (uint32_t p = 0; p < passCount && p < 32u; ++p) {
        const uint32_t kind = passes[p].kind;
        if (kind == vp::PassTransform || kind == vp::PassProject) {
            writers |= 1u << p;
            (vp::passNeverSkips(passes[p]) ? safe : skipping) |= 1u << p;
        } else if (kind == vp::PassGrid || kind == vp::PassGridFlat || kind == vp::PassTexGenInt ||
                   kind == vp::PassTexGenPos) {
            writers |= 1u << p;
        }
    }
    for (uint32_t i = 0; i < vertexCount; ++i) {
        uint32_t mask;
        std::memcpy(&mask, vertexBytes + size_t(i) * sizeof(vp::BatchVertex) + offsetof(vp::BatchVertex, passMask), 4);
        uint32_t last = 0; // the highest set bit of the vertex's writers
        for (uint32_t bits = mask & writers; bits; bits &= bits - 1u)
            last = bits & (0u - bits);
        if (!(last & safe)) {
            if (why)
                *why = (last & skipping) ? vpath::Fallback::SkipBit : vpath::Fallback::DrawState;
            return false;
        }
    }
    return true;
}

// The shape of a batch-path record for its topology value: strip lengths, indices, pass kinds and pass masks.
inline uint64_t batchShapeOf(const vp::BatchHeader& h, const uint32_t* strips, const uint16_t* indices,
                             const vp::Pass* passes, const uint8_t* vertexBytes) {
    uint64_t v = 1469598103934665603ull ^ vp::Batch;
    const auto mix = [&v](uint64_t value) { v = (v ^ value) * 1099511628211ull; };
    for (uint64_t value :
         {uint64_t(h.stripCount), uint64_t(h.indexCount), uint64_t(h.vertexCount), uint64_t(h.passCount)})
        mix(value);
    for (uint32_t i = 0; i < h.stripCount; ++i)
        mix(strips[i]);
    for (uint32_t i = 0; i < h.indexCount; ++i)
        mix(indices[i]);
    for (uint32_t i = 0; i < h.passCount; ++i)
        mix(passes[i].kind);
    for (uint32_t i = 0; i < h.vertexCount; ++i) {
        uint32_t mask;
        std::memcpy(&mask, vertexBytes + size_t(i) * sizeof(vp::BatchVertex) + offsetof(vp::BatchVertex, passMask), 4);
        mix(mask);
    }
    return v;
}

// The exact bounds of a batch of batch-path records: the smallest and largest x and y (native pixels, XYOFFSET
// subtracted) of the vertices its triangles use, as the CPU path's Impl::draw computes them from the processed
// vertices. They decide whether a draw is skipped (outside the scissor), the rows of the target it marks as written
// and the destination copy, and a target smaller than the scissor (a 512 wide one, in flight over Ravenshold) shows
// the difference. Evaluating every vertex exactly would cost what the shader saves, so each vertex's place is first
// estimated in doubles with a bound on the estimate's error (the VU's arithmetic truncates where IEEE rounds; a
// relative 2^-20 per operation chain covers both), and only the vertices that can be an extreme are evaluated
// exactly, with their position passes alone (the place depends on nothing else). A vertex the estimate cannot bound
// (a register that is not an ordinary float, a value near the largest float, a fixed-point place that saturates or
// wraps, a second grid pass or a position pass after the transform) is always evaluated exactly. Everything that
// depends on a pass alone is prepared once per record.
struct Bounds {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
    uint32_t exact = 0; // vertices evaluated exactly (statistics)
};
namespace batchdetail {
inline bool ordinary(uint32_t bits) {
    const uint32_t e = (bits >> 23) & 0xFFu;
    return (bits & 0x7FFFFFFFu) == 0u || (e != 0u && e != 0xFFu);
}
inline double asDouble(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
inline uint32_t lowestBit(uint32_t bits) {
    uint32_t n = 0;
    while (!((bits >> n) & 1u))
        ++n;
    return n;
}
// A position pass of a record, prepared for the estimate.
struct PreparedPass {
    uint32_t kind = 0;
    bool usable = false; // every constant the estimate reads is an ordinary float
    double m[4][4] = {}; // Transform: m[c][l] = k[c][l]
    double offset[2] = {}, scale[2] = {};
    double gridY = 0, gridW = 0;
};
inline void prepare(const vp::Pass& pass, PreparedPass& out) {
    out.kind = pass.kind;
    out.usable = true;
    if (pass.kind == vp::PassGrid || pass.kind == vp::PassGridFlat) {
        out.usable = ordinary(pass.k[0][1]) && ordinary(pass.k[0][3]);
        out.gridY = asDouble(pass.k[0][1]);
        out.gridW = asDouble(pass.k[0][3]);
        return;
    }
    for (int r = 0; r < 7; ++r)
        for (int l = 0; l < 4; ++l)
            out.usable = out.usable && ordinary(pass.k[r][l]);
    for (int c = 0; c < 4; ++c)
        for (int l = 0; l < 4; ++l)
            out.m[c][l] = asDouble(pass.k[c][l]);
    for (int l = 0; l < 2; ++l) {
        out.offset[l] = asDouble(pass.k[5][l]);
        out.scale[l] = asDouble(pass.k[6][l]);
    }
}
constexpr double kRel = 1.0 / (1u << 20);
constexpr double kLarge = 1e36; // well below the largest float: no product or sum on the way saturates
// One Transform or Project pass on an estimated position (`pos`, with its error bound `posErr`): the fixed-point x
// and y (before the truncation to an integer and the 16-bit wrap) replace lanes 0 and 1. Lane 2 is not estimated (no
// bound depends on it, and no position pass may follow). False when the result cannot be bounded.
inline bool projectEstimate(const PreparedPass& p, double pos[4], double posErr[4]) {
    double clip[4] = {0, 0, 0, 0}, clipErr[4] = {0, 0, 0, 0};
    for (int l = 0; l < 4; ++l) {
        if (l == 2)
            continue;
        if (p.kind == vp::PassTransform) {
            double sum = 0, magnitude = 0, carried = 0;
            for (int c = 0; c < 4; ++c) {
                const double term = p.m[c][l] * pos[c];
                sum += term;
                magnitude += std::fabs(term);
                carried += std::fabs(p.m[c][l]) * posErr[c];
            }
            if (!(magnitude < kLarge))
                return false; // the VU saturates at the largest float where a double does not
            clip[l] = sum;
            clipErr[l] = kRel * magnitude + carried;
        } else {
            clip[l] = pos[l];
            clipErr[l] = posErr[l];
        }
    }
    const double w = clip[3];
    if (!(std::fabs(w) > 4.0 * clipErr[3] + 1e-30) || !(std::fabs(1.0 / w) < kLarge))
        return false; // 1 / w not bounded
    const double q = 1.0 / w, qErr = std::fabs(q) * (clipErr[3] / (std::fabs(w) - clipErr[3]) + kRel);
    for (int l = 0; l < 2; ++l) {
        const double scaled = clip[l] * p.scale[l];
        if (!(std::fabs(scaled) < kLarge && std::fabs(scaled * q) < kLarge))
            return false;
        const double value = p.offset[l] + scaled * q;
        const double valueErr = std::fabs(p.scale[l]) * (clipErr[l] * std::fabs(q) + std::fabs(clip[l]) * qErr) +
                                kRel * (std::fabs(p.offset[l]) + 2.0 * std::fabs(scaled * q));
        pos[l] = value * 16.0;
        posErr[l] = valueErr * 16.0 + kRel * std::fabs(pos[l]) + 1.0; // the truncation to an integer
    }
    return true;
}
// The bound of a projected place: false when it saturates or wraps.
inline bool placeOf(const double pos[4], const double posErr[4], double place[2], double& error) {
    error = std::max(posErr[0], posErr[1]);
    for (int l = 0; l < 2; ++l) {
        place[l] = pos[l];
        if (!(pos[l] - error >= 0.0 && pos[l] + error <= 65535.0)) // saturation or the 16-bit wrap
            return false;
    }
    return true;
}
// Estimates the fixed-point x and y (before the 16-bit wrap) of a vertex from its position passes (`mask` over the
// record's passes, `prepared` indexed by pass): false when it cannot be bounded.
inline bool estimatePlace(const PreparedPass* prepared, uint32_t mask, const vp::BatchVertex& v, double place[2],
                          double& error) {
    double pos[4] = {0, 0, 0, 0}, posErr[4] = {0, 0, 0, 0};
    bool raw = true; // the position lanes are still the raw row (a grid pass replaces all four)
    bool projected = false;
    for (uint32_t bits = mask; bits; bits &= bits - 1u) {
        const PreparedPass& p = prepared[lowestBit(bits)];
        if (!p.usable || projected)
            return false; // a constant that is not an ordinary float, or a position pass after the transform
        if (p.kind == vp::PassGrid || p.kind == vp::PassGridFlat) {
            if (!ordinary(v.gridX) || !ordinary(v.gridZ) || (p.kind == vp::PassGrid && !raw))
                return false; // a second grid pass reads its height from the first one's float
            const double height = p.kind == vp::PassGridFlat ? 0.0 : double(int32_t(v.row.pos[1]));
            pos[0] = asDouble(v.gridX);
            pos[1] = p.gridY + height * 2.0 - 65536.0;
            pos[2] = asDouble(v.gridZ);
            pos[3] = p.gridW;
            posErr[0] = posErr[2] = posErr[3] = 0;
            posErr[1] = kRel * (std::fabs(p.gridY) + std::fabs(height * 2.0) + 65536.0);
            raw = false;
            continue;
        }
        if (raw) {
            for (int l = 0; l < 4; ++l) {
                if (!ordinary(v.row.pos[l]))
                    return false;
                pos[l] = asDouble(v.row.pos[l]);
            }
            raw = false;
        }
        if (!projectEstimate(p, pos, posErr))
            return false;
        projected = true;
    }
    return projected && placeOf(pos, posErr, place, error);
}
// The common vertex, prepared once per pass: a raw position through one Transform pass (every strip vertex, and a
// batch vertex whose only position pass is a Transform). The same operations in the same order as projectEstimate
// with no error carried in, so the same bound (|m * p| is |m| * |p| in doubles, and the carried error is zero); the
// lane loop is unrolled to lanes 0, 1 and 3 and the magnitudes of the matrix are taken once.
struct FastTransform {
    bool usable = false;
    double m[4][3] = {}, am[4][3] = {}; // [c][lanes 0, 1, 3]
    double offset[2] = {}, scale[2] = {};
};
inline void prepareFast(const PreparedPass& p, FastTransform& f) {
    static constexpr int kLane[3] = {0, 1, 3};
    f.usable = p.usable && p.kind == vp::PassTransform;
    for (int c = 0; c < 4; ++c)
        for (int k = 0; k < 3; ++k) {
            f.m[c][k] = p.m[c][kLane[k]];
            f.am[c][k] = std::fabs(f.m[c][k]);
        }
    for (int l = 0; l < 2; ++l) {
        f.offset[l] = p.offset[l];
        f.scale[l] = p.scale[l];
    }
}
// The transform of a position whose lane 1 carries the error `error1` (a grid height; zero for a raw position):
// projectEstimate's carried error is |m[1][l]| * error1 then.
inline bool estimateFastAt(const FastTransform& f, const double pos[4], double error1, double place[2], double& error) {
    const double apos[4] = {std::fabs(pos[0]), std::fabs(pos[1]), std::fabs(pos[2]), std::fabs(pos[3])};
    double clip[3], clipErr[3];
    for (int k = 0; k < 3; ++k) {
        double sum = 0, magnitude = 0;
        for (int c = 0; c < 4; ++c) {
            sum += f.m[c][k] * pos[c];
            magnitude += f.am[c][k] * apos[c];
        }
        if (!(magnitude < kLarge))
            return false; // the VU saturates at the largest float where a double does not
        clip[k] = sum;
        clipErr[k] = kRel * magnitude + f.am[1][k] * error1;
    }
    const double w = clip[2];
    if (!(std::fabs(w) > 4.0 * clipErr[2] + 1e-30) || !(std::fabs(1.0 / w) < kLarge))
        return false; // 1 / w not bounded
    const double q = 1.0 / w, qErr = std::fabs(q) * (clipErr[2] / (std::fabs(w) - clipErr[2]) + kRel);
    double out[2], outErr[2];
    for (int l = 0; l < 2; ++l) {
        const double scaled = clip[l] * f.scale[l];
        if (!(std::fabs(scaled) < kLarge && std::fabs(scaled * q) < kLarge))
            return false;
        const double value = f.offset[l] + scaled * q;
        const double valueErr = std::fabs(f.scale[l]) * (clipErr[l] * std::fabs(q) + std::fabs(clip[l]) * qErr) +
                                kRel * (std::fabs(f.offset[l]) + 2.0 * std::fabs(scaled * q));
        out[l] = value * 16.0;
        outErr[l] = valueErr * 16.0 + kRel * std::fabs(out[l]) + 1.0; // the truncation to an integer
    }
    error = std::max(outErr[0], outErr[1]);
    for (int l = 0; l < 2; ++l) {
        place[l] = out[l];
        if (!(out[l] - error >= 0.0 && out[l] + error <= 65535.0)) // saturation or the 16-bit wrap
            return false;
    }
    return true;
}
inline bool estimateFast(const FastTransform& f, const uint32_t raw[4], double place[2], double& error) {
    if (!f.usable || !ordinary(raw[0]) || !ordinary(raw[1]) || !ordinary(raw[2]) || !ordinary(raw[3]))
        return false;
    const double pos[4] = {asDouble(raw[0]), asDouble(raw[1]), asDouble(raw[2]), asDouble(raw[3])};
    return estimateFastAt(f, pos, 0.0, place, error);
}
// The terrain vertex: a grid pass (heights from the raw row, or flat) and then one Transform, as estimatePlace does it.
inline bool estimateGridFast(const PreparedPass& grid, const FastTransform& f, const vp::BatchVertex& v,
                             double place[2], double& error) {
    if (!grid.usable || !f.usable || !ordinary(v.gridX) || !ordinary(v.gridZ))
        return false;
    const double height = grid.kind == vp::PassGridFlat ? 0.0 : double(int32_t(v.row.pos[1]));
    const double pos[4] = {asDouble(v.gridX), grid.gridY + height * 2.0 - 65536.0, asDouble(v.gridZ), grid.gridW};
    return estimateFastAt(f, pos, kRel * (std::fabs(grid.gridY) + std::fabs(height * 2.0) + 65536.0), place, error);
}
// A strip record's constants as the Transform pass with the same x and y: the strip stage computes them with the
// operations of passTransform (whose offset is first multiplied by one, which changes no bit).
inline void prepareStrip(const vp::Constants& k, PreparedPass& out) {
    out.kind = vp::PassTransform;
    out.usable = true;
    for (int c = 0; c < 4; ++c)
        for (int l = 0; l < 4; ++l) {
            out.usable = out.usable && ordinary(k.mvp[c][l]);
            out.m[c][l] = asDouble(k.mvp[c][l]);
        }
    for (int l = 0; l < 2; ++l) {
        out.usable = out.usable && ordinary(k.offset[l]) && ordinary(k.scale[l]);
        out.offset[l] = asDouble(k.offset[l]);
        out.scale[l] = asDouble(k.scale[l]);
    }
}
// The exact x and y of a raw position through one transform with the VU's arithmetic: the place lanes of
// vp::evaluate (strips) and of passTransform (batch path), nothing else. `m` holds the matrix rows by column.
inline void exactTransformPlace(const uint32_t (*m)[4], const uint32_t offset[4], const uint32_t scale[4],
                                const uint32_t pos[4], int32_t& x, int32_t& y) {
    using namespace vp::vu;
    uint32_t p[4], clip[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        p[i] = operand(pos[i]);
    for (int l = 0; l < 4; ++l) {
        if (l == 2)
            continue;
        uint32_t acc = mul(operand(m[0][l]), p[0]);
        acc = add(acc, mul(operand(m[1][l]), p[1]));
        acc = add(acc, mul(operand(m[2][l]), p[2]));
        clip[l] = add(acc, mul(operand(m[3][l]), p[3]));
    }
    const uint32_t q = reciprocal(clip[3]);
    int32_t fixed[2];
    for (int l = 0; l < 2; ++l)
        fixed[l] = toFixed(add(operand(offset[l]), mul(mul(clip[l], operand(scale[l])), q)), 4);
    x = static_cast<uint16_t>(fixed[0]);
    y = static_cast<uint16_t>(fixed[1]);
}
inline void exactStripPlace(const vp::Constants& k, const vp::Vertex& v, int32_t& x, int32_t& y) {
    exactTransformPlace(k.mvp, k.offset, k.scale, v.pos, x, y);
}
// The exact x and y (16-bit fixed point) of a vertex: its position passes with the VU's arithmetic.
inline void exactPlace(const vp::Pass* passes, uint32_t mask, const vp::BatchVertex& v, int32_t& x, int32_t& y) {
    if (mask && !(mask & (mask - 1u)) && passes[lowestBit(mask)].kind == vp::PassTransform) {
        const vp::Pass& p = passes[lowestBit(mask)];
        exactTransformPlace(p.k, p.k[5], p.k[6], v.row.pos, x, y);
        return;
    }
    vp::Row r = v.row;
    for (uint32_t bits = mask; bits; bits &= bits - 1u)
        vp::applyPass(passes[lowestBit(bits)], r, v.gridX, v.gridZ);
    x = static_cast<uint16_t>(r.pos[0]);
    y = static_cast<uint16_t>(r.pos[1]);
}

// The two passes of every bounds computation. Each vertex's estimate (place and error bound) narrows the limits the
// extremes are known to reach (the smallest x is at most place + error of every vertex, and so on); only the
// vertices whose interval reaches past a limit can be an extreme, and only those are evaluated exactly. The limits
// only tighten as vertices come, so a vertex that cannot reach a limit when it is estimated never can: it is not
// kept.
struct Extremes {
    double lowX = 1e300, lowY = 1e300, highX = -1e300, highY = -1e300; // the extremes' guaranteed limits
    int32_t exact[4] = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};   // min x, min y, max x, max y (fixed)
    uint32_t evaluated = 0;
    void add(int32_t x, int32_t y) {
        exact[0] = std::min(exact[0], x);
        exact[1] = std::min(exact[1], y);
        exact[2] = std::max(exact[2], x);
        exact[3] = std::max(exact[3], y);
        ++evaluated;
    }
    // True when the vertex can be an extreme now (and is to be kept).
    bool estimate(double x, double y, double error) {
        lowX = std::min(lowX, x + error);
        lowY = std::min(lowY, y + error);
        highX = std::max(highX, x - error);
        highY = std::max(highY, y - error);
        return reaches(x, y, error);
    }
    bool reaches(double x, double y, double error) const {
        return x - error <= lowX || y - error <= lowY || x + error >= highX || y + error >= highY;
    }
    // True when the vertex can still change an exact extreme found so far (its exact place lies in its interval).
    bool improves(double x, double y, double error) const {
        return x - error < exact[0] || y - error < exact[1] || x + error > exact[2] || y + error > exact[3];
    }
};
// The exact evaluation of the kept vertices: the best estimate of each extreme first, so that its exact value rules
// out the vertices that cannot beat it; then every other kept vertex that can still change an extreme.
template <class Estimate, class Evaluate>
void finishExtremes(Extremes& e, const std::vector<Estimate>& kept, Evaluate&& evaluate) {
    size_t best[4] = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
    for (size_t i = 0; i < kept.size(); ++i) {
        const Estimate& k = kept[i];
        if (!e.reaches(k.x, k.y, k.error))
            continue;
        if (best[0] == SIZE_MAX || k.x < kept[best[0]].x)
            best[0] = i;
        if (best[1] == SIZE_MAX || k.y < kept[best[1]].y)
            best[1] = i;
        if (best[2] == SIZE_MAX || k.x > kept[best[2]].x)
            best[2] = i;
        if (best[3] == SIZE_MAX || k.y > kept[best[3]].y)
            best[3] = i;
    }
    for (int d = 0; d < 4; ++d) {
        bool done = best[d] == SIZE_MAX;
        for (int earlier = 0; earlier < d && !done; ++earlier)
            done = best[earlier] == best[d];
        if (!done)
            evaluate(kept[best[d]]);
    }
    for (size_t i = 0; i < kept.size(); ++i) {
        const Estimate& k = kept[i];
        if (i == best[0] || i == best[1] || i == best[2] || i == best[3] || !e.reaches(k.x, k.y, k.error) ||
            !e.improves(k.x, k.y, k.error))
            continue;
        evaluate(k);
    }
}
inline Bounds boundsOf(const Extremes& e, int32_t offsetX, int32_t offsetY) {
    Bounds out;
    out.exact = e.evaluated;
    if (e.exact[0] == INT32_MAX)
        return out;
    out.any = true;
    out.x0 = (e.exact[0] - offsetX) * (1.0f / 16.0f);
    out.y0 = (e.exact[1] - offsetY) * (1.0f / 16.0f);
    out.x1 = (e.exact[2] - offsetX) * (1.0f / 16.0f);
    out.y1 = (e.exact[3] - offsetY) * (1.0f / 16.0f);
    return out;
}
} // namespace batchdetail

// Working storage of batchBounds (kept by the caller across draws: no allocation per draw).
struct BoundsScratch {
    std::vector<uint8_t> used;
    struct Estimate {
        uint32_t vertex, positionMask, record;
        double x, y, error;
    };
    std::vector<Estimate> estimates;
};

inline Bounds batchBounds(const VertexProgramBatch& b, int32_t offsetX, int32_t offsetY, BoundsScratch& scratch) {
    if (!b.batchPath || !b.indexCount)
        return {};
    // Vertices the triangles use (a strip of one or two indices has none).
    scratch.used.assign(b.vertexCount, 0);
    for (uint32_t i = 0; i < b.indexCount; ++i)
        if (b.indices[i] < b.vertexCount)
            scratch.used[b.indices[i]] = 1;
    scratch.estimates.clear();
    batchdetail::Extremes e;
    batchdetail::PreparedPass prepared[vp::kBatchMaxPasses];
    batchdetail::FastTransform fast[vp::kBatchMaxPasses];
    for (uint32_t r = 0; r < b.recordCount; ++r) {
        const VertexProgramRecord& record = b.records[r];
        if (record.constants + record.passCount > b.passCount || record.passCount > vp::kBatchMaxPasses ||
            record.firstVertex + record.vertexCount > b.vertexCount)
            continue;
        const vp::Pass* passes = b.passes + record.constants;
        uint32_t positionPasses = 0, transforms = 0, grids = 0;
        for (uint32_t p = 0; p < record.passCount; ++p)
            if (motion::positionPass(passes[p].kind)) {
                positionPasses |= 1u << p;
                batchdetail::prepare(passes[p], prepared[p]);
                if (passes[p].kind == vp::PassTransform) {
                    transforms |= 1u << p;
                    batchdetail::prepareFast(prepared[p], fast[p]);
                } else if (passes[p].kind == vp::PassGrid || passes[p].kind == vp::PassGridFlat) {
                    grids |= 1u << p;
                }
            }
        for (uint32_t i = record.firstVertex; i < record.firstVertex + record.vertexCount; ++i) {
            if (!scratch.used[i])
                continue;
            const vp::BatchVertex& v = b.batchVertices[i];
            const uint32_t mask = v.passMask & positionPasses;
            double place[2], error = 0;
            // the common chains: one transform from the raw position, or a grid pass and then a transform
            const uint32_t first = mask & (0u - mask), rest = mask & ~first;
            bool known;
            if (first && !rest && (first & transforms))
                known = batchdetail::estimateFast(fast[batchdetail::lowestBit(first)], v.row.pos, place, error);
            else if ((first & grids) && rest && !(rest & (rest - 1u)) && (rest & transforms))
                known = batchdetail::estimateGridFast(prepared[batchdetail::lowestBit(first)],
                                                      fast[batchdetail::lowestBit(rest)], v, place, error);
            else
                known = batchdetail::estimatePlace(prepared, mask, v, place, error);
            if (!known) {
                int32_t x, y;
                batchdetail::exactPlace(passes, mask, v, x, y);
                e.add(x, y);
                continue;
            }
            if (e.estimate(place[0], place[1], error))
                scratch.estimates.push_back({i, mask, r, place[0], place[1], error});
        }
    }
    // A vertex can be an extreme only when its interval reaches past the final limit; those are evaluated exactly.
    batchdetail::finishExtremes(e, scratch.estimates, [&](const BoundsScratch::Estimate& k) {
        int32_t x, y;
        batchdetail::exactPlace(b.passes + b.records[k.record].constants, k.positionMask, b.batchVertices[k.vertex], x,
                                y);
        e.add(x, y);
    });
    return batchdetail::boundsOf(e, offsetX, offsetY);
}

// The exact bounds of a batch of strip records, by the same method as batchBounds: per vertex the estimate of the
// strip stage's place from the record's constants (one prepared transform per record), then the vertices that can
// be an extreme evaluated exactly (the place lanes of vp::evaluate alone).
inline Bounds stripBounds(const VertexProgramBatch& b, int32_t offsetX, int32_t offsetY, BoundsScratch& scratch) {
    if (b.batchPath || !b.indexCount)
        return {};
    scratch.used.assign(b.vertexCount, 0);
    for (uint32_t i = 0; i < b.indexCount; ++i)
        if (b.indices[i] < b.vertexCount)
            scratch.used[b.indices[i]] = 1;
    scratch.estimates.clear();
    batchdetail::Extremes e;
    batchdetail::PreparedPass prepared;
    batchdetail::FastTransform fast;
    const auto evaluate = [&](uint32_t i) {
        int32_t x, y;
        batchdetail::exactStripPlace(b.constants[b.vertices[i].constants], b.vertices[i].in, x, y);
        e.add(x, y);
    };
    for (uint32_t r = 0; r < b.recordCount; ++r) {
        const VertexProgramRecord& record = b.records[r];
        if (record.constants >= b.constantCount || record.firstVertex + record.vertexCount > b.vertexCount)
            continue;
        batchdetail::prepareStrip(b.constants[record.constants], prepared);
        batchdetail::prepareFast(prepared, fast);
        for (uint32_t i = record.firstVertex; i < record.firstVertex + record.vertexCount; ++i) {
            if (!scratch.used[i])
                continue;
            double place[2], error = 0;
            if (b.vertices[i].constants != record.constants ||
                !batchdetail::estimateFast(fast, b.vertices[i].in.pos, place, error)) {
                evaluate(i);
                continue;
            }
            if (e.estimate(place[0], place[1], error))
                scratch.estimates.push_back({i, 0, r, place[0], place[1], error});
        }
    }
    batchdetail::finishExtremes(e, scratch.estimates, [&](const BoundsScratch::Estimate& k) { evaluate(k.vertex); });
    return batchdetail::boundsOf(e, offsetX, offsetY);
}

// The exact bounds of either kind of batch.
inline Bounds programBounds(const VertexProgramBatch& b, int32_t offsetX, int32_t offsetY, BoundsScratch& scratch) {
    return b.batchPath ? batchBounds(b, offsetX, offsetY, scratch) : stripBounds(b, offsetX, offsetY, scratch);
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

// The stand-ins over exact bounds (batchBounds): the same classification, with the corners of the bounds.
inline void standInsOver(const Bounds& b, DrawVertex out[3]) {
    out[0].x = b.x0;
    out[0].y = b.y0;
    out[1].x = b.x1;
    out[1].y = b.y0;
    out[2].x = b.x0;
    out[2].y = b.y1;
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

// The topology value of a batch-path record. Its vertices correspond between two worlds when everything their texture
// coordinates come from is unchanged (the CPU path matches processed texture coordinates): the raw texture rows, the
// constants of the passes that generate or transform them, and the raw position of a vertex whose coordinates are
// generated from it.
inline uint64_t batchTopologyOf(const ObjectMotionScope& scope, const VertexProgramRecord& record,
                                const vp::BatchVertex* vertices, const vp::Pass* passes, const GsDrawState& s) {
    uint64_t h = scope.topologyHash;
    const auto mix = [&h](uint64_t value) { h = (h ^ value) * 1099511628211ull; };
    for (uint64_t value : {uint64_t(record.vertexCount), s.tex0, s.alpha, uint64_t(s.flags), s.test, record.shape})
        mix(value);
    uint32_t fromPosition = 0;
    for (uint32_t p = 0; p < record.passCount; ++p) {
        const vp::Pass& pass = passes[record.constants + p];
        if (pass.kind == vp::PassTexGenInt || pass.kind == vp::PassTexGenPos || pass.kind == vp::PassTexMatrix) {
            uint64_t k[24];
            std::memcpy(k, pass.k, sizeof(k));
            for (uint64_t value : k)
                mix(value);
        }
        if (pass.kind == vp::PassTexGenPos)
            fromPosition |= 1u << p;
    }
    for (uint32_t i = 0; i < record.vertexCount; ++i) {
        const vp::BatchVertex& v = vertices[record.firstVertex + i];
        uint64_t row[2];
        std::memcpy(row, v.row.st, 16);
        mix(row[0]);
        mix(row[1]);
        if (v.passMask & fromPosition) {
            std::memcpy(row, v.row.pos, 16);
            mix(row[0]);
            mix(row[1]);
        }
    }
    return h;
}

// trackBatch for a batch of batch-path records: the same walk, key rule and scope notes. Output: `previous` has one
// entry per batch vertex; `previousPasses` are the passes those entries name, the first of them at table row
// `firstPreviousRow` (a backend appends them after the batch's own passes).
template <class Apply, class Current>
MotionCounts trackBatchPath(motion::PairedHistory& history, const VertexProgramBatch& batch, const GsDrawState& s,
                            uint32_t firstPreviousRow, std::vector<motion::BatchPrevious>& previous,
                            std::vector<vp::Pass>& previousPasses, Apply&& apply, Current&& current) {
    constexpr uint32_t kRows = sizeof(vp::Pass) / 16u;
    MotionCounts counts;
    previous.assign(batch.vertexCount, motion::BatchPrevious{});
    previousPasses.clear();
    uint32_t note = 0;
    for (uint32_t r = 0; r < batch.recordCount; ++r) {
        while (note < batch.noteCount && batch.notes[note].vertex <= r)
            apply(batch.notes[note++].scope);
        const VertexProgramRecord& record = batch.records[r];
        if (!record.vertexCount || record.firstVertex + record.vertexCount > batch.vertexCount ||
            record.constants + record.passCount > batch.passCount)
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
            continue; // ProgramHistory::trackBatch refuses these before it looks at a vertex
        const vp::Pass* old = nullptr;
        uint32_t oldCount = 0;
        const uint32_t row = firstPreviousRow + kRows * static_cast<uint32_t>(previousPasses.size());
        if (history.program.trackBatch(key, batchTopologyOf(scope, record, batch.batchVertices, batch.passes, s),
                                       batch.passes + record.constants, record.passCount,
                                       batch.batchVertices + record.firstVertex, record.vertexCount, known, row,
                                       previous.data() + record.firstVertex, &old, &oldCount)) {
            previousPasses.insert(previousPasses.end(), old, old + oldCount);
            ++counts.matched;
            counts.matchedVertices += record.vertexCount;
        }
    }
    while (note < batch.noteCount)
        apply(batch.notes[note++].scope);
    return counts;
}

} // namespace gsr::program_draw
