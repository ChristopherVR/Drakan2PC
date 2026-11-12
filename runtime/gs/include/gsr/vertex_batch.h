// Vertex-program draws of the batch path: one index-list run of the game's VU1 microprogram handed to the renderer as
// its inputs (docs/journal/2026-11-12/gpu-vertex-feasibility.md and the batch path notes that follow it).
//
// The batch path keeps three arrays of up to 225 vertices in VU memory (position, colour or normal, texture
// coordinate) and works on them in place, one stage per microprogram run: a grid generator, a point light, two
// texture-coordinate generators, directional lighting, the transform, a depth fade of alpha and a texture matrix. A
// later run walks index lists and copies the processed vertices into strip packets. Every stage reads and writes only
// the three quadwords of the vertex it is at, so the stages a vertex went through compose into one function of that
// vertex's raw row. A record carries the raw rows, the constants of each stage (a "pass") and, per vertex, the set of
// passes that apply to it; `evaluateVertex()` runs them with the VU's arithmetic (gsr/vertex_program.h) and is both
// the fallback of a renderer without a vertex-program path and the reference a shader is checked against.
// `applyPass()` is also what brings VU memory up to date when a later run has to be executed by the microprogram
// itself.
//
// Each pass body is straight-line code over the row and the pass constants: `vu::*` calls, selects and loops with a
// fixed count, so it converts mechanically into a shader body.
#pragma once
#include "gsr/vertex_program.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace gsr::vp {

constexpr uint32_t kBatchSlots = 225;     // vertices of one batch window (15 x 15)
constexpr uint32_t kBatchMaxPasses = 24;  // passes of one record (bits of Vertex::passMask in use)
constexpr uint32_t kBatchMaxIndices = 64; // more do not fit the microprogram's packet buffer
constexpr uint32_t kBatchMaxExponent = 8; // squarings of the point light's cosine term

enum PassKind : uint32_t {
    PassGrid = 1,       // position from a grid origin, a step per column and row, and an integer height
    PassPointLight = 2, // colour plus a light term from the direction to a point, alpha from the height difference
    PassTexGenInt = 3,  // texture coordinate from the integer vector in the colour row
    PassTexGenPos = 4,  // texture coordinate from the position
    PassLight = 5,      // integer normal to colour: four directional terms and ambient, clamped at 200
    PassTransform = 6,  // position to fixed point window coordinates and fog, 1 / w into the texture row
    PassFade = 7,       // alpha scaled by a ramp of the depth
    PassTexMatrix = 8,  // 2 x 3 matrix on the texture coordinate
    PassProject = 9,    // the transform's second half for a position that already is in clip space
    PassGridFlat = 10,  // the grid generator with height zero for every vertex
};

// One vertex of the batch window as VU memory holds it: three quadwords.
struct Row {
    uint32_t pos[4];
    uint32_t col[4];
    uint32_t st[4];
};
static_assert(sizeof(Row) == 48, "three rows");

// The constants of one stage. Rows by kind:
//   Grid*       k[0] origin (y and w are used), k[1] step; param = columns. The x of a column and the z of a row
//               are sums the generator accumulates along the grid; they come with the vertex (gridX, gridZ).
//   PointLight  k[0] light position, k[1] axis, k[2] light colour (integers), k[3] height limits, k[4] alpha slope;
//               param = number of squarings of the cosine term (0 = no light term)
//   TexGen*     k[0..3] the four matrix rows
//   Light       k[0] ambient and alpha (integers), k[1..4] light directions, k[5..8] light colours (integers)
//   Transform   k[0..3] matrix, k[4] fog (y, z = depth clamp, w = slope), k[5] offset, k[6] scale
//   Project     k[4], k[5], k[6] as Transform
//   Fade        k[0].w slope
//   TexMatrix   k[0], k[1] matrix rows, k[2] translation
struct Pass {
    uint32_t kind;
    uint32_t param;
    uint32_t first; // first slot and number of slots the stage ran over (information; Vertex::passMask decides)
    uint32_t count;
    uint32_t k[12][4];
};
static_assert(sizeof(Pass) == 208, "thirteen rows");

struct BatchVertex {
    Row row;           // raw row; lanes a pass of this vertex writes before reading need not be meaningful
    uint32_t passMask; // bit i: pass i of the record applies to this vertex
    uint32_t gridX;    // x of the vertex's grid column and z of its grid row (float bits, gridAxis), for PassGrid
    uint32_t gridZ;
    uint32_t slot; // slot the position came from (information)
};
static_assert(sizeof(BatchVertex) == 64, "four rows");

struct BatchHeader {
    uint32_t program;     // Batch
    uint32_t prim;        // GS PRIM value of the strip tag (11 bits)
    uint32_t stripCount;  // strip lengths (in indices) follow the passes
    uint32_t indexCount;  // 16-bit indices into the vertices follow the strip lengths
    uint32_t vertexCount; // BatchVertex records follow the indices
    uint32_t passCount;
    uint32_t flags; // kBatchEmptyTag
    uint32_t reserved;
};
static_assert(sizeof(BatchHeader) == 32, "two rows");
constexpr uint32_t kBatchEmptyTag = 1u; // the run ends with a tag of zero vertices (the clipper variant's flush)

namespace vu {
// acc + x * y, the product rounded before the sum like the VU does.
inline uint32_t madd(uint32_t acc, uint32_t x, uint32_t y) {
    return add(acc, mul(x, y));
}
constexpr uint32_t kOne = 0x3F800000u;
} // namespace vu

// The x of the first `count` columns (or the z of the first rows) of the grid generator: the accumulator starts at
// the origin and takes one step per column, each sum rounded; a vertex gets accumulator minus zero. The sums depend
// on the column alone, so whoever builds a record computes them once per batch and gives each vertex its two values.
inline void gridAxis(uint32_t origin, uint32_t step, uint32_t count, uint32_t* out) {
    using namespace vu;
    uint32_t acc = mul(operand(origin), kOne);
    const uint32_t inc = mul(operand(step), kOne);
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = sub(acc, 0u);
        acc = add(acc, inc);
    }
}

inline void passGrid(const Pass& p, Row& r, uint32_t gridX, uint32_t gridZ, bool flat) {
    using namespace vu;
    const uint32_t height = flat ? 0u : fromInt(static_cast<int32_t>(r.pos[1]));
    const uint32_t y = add(mul(operand(p.k[0][1]), kOne), mul(height, asBits(2.0f)));
    r.pos[0] = gridX;
    r.pos[1] = sub(y, asBits(65536.0f));
    r.pos[2] = gridZ;
    r.pos[3] = sub(mul(operand(p.k[0][3]), kOne), 0u);
}

inline void passPointLight(const Pass& p, Row& r) {
    using namespace vu;
    uint32_t d[3], n[3], along[3];
    for (int l = 0; l < 3; ++l)
        d[l] = sub(operand(p.k[0][l]), operand(r.pos[l]));
    const uint32_t rlen = reciprocalLength(d[0], d[1], d[2]);
    n[0] = mul(d[0], asBits(-1.0f));
    n[1] = d[1];
    n[2] = mul(d[2], asBits(-1.0f));
    uint32_t height = maxRaw(d[1], p.k[3][1]);
    height = minRaw(height, p.k[3][2]);
    for (int l = 0; l < 3; ++l)
        along[l] = mul(n[l], operand(p.k[1][l]));
    uint32_t cosine = add(add(along[0], along[1]), along[2]);
    uint32_t alpha = sub(asBits(128.0f), mul(operand(p.k[4][3]), operand(p.k[3][1])));
    alpha = madd(alpha, operand(p.k[4][3]), operand(height));
    cosine = maxRaw(cosine, 0u);
    const uint32_t steep = mul(absRaw(n[1]), operand(rlen));
    cosine = mul(operand(cosine), operand(rlen));
    alpha = sub(alpha, mul(operand(p.k[3][3]), steep));
    for (uint32_t i = 0; i < kBatchMaxExponent; ++i)
        cosine = i < p.param ? mul(cosine, cosine) : cosine;
    cosine = p.param == 0u ? 0u : cosine;
    alpha = minRaw(alpha, asBits(128.0f));
    for (int l = 0; l < 3; ++l) {
        const uint32_t light = mul(fromInt(static_cast<int32_t>(p.k[2][l])), asBits(2.0f));
        uint32_t c = madd(mul(fromInt(static_cast<int32_t>(r.col[l])), kOne), light, cosine);
        c = minRaw(c, asBits(255.0f));
        r.col[l] = static_cast<uint32_t>(toFixed(c, 0));
    }
    r.col[3] = static_cast<uint32_t>(toFixed(alpha, 0));
}

inline void passTexGen(const Pass& p, Row& r, bool fromIntegers) {
    using namespace vu;
    uint32_t v[4];
    for (int l = 0; l < 4; ++l)
        v[l] = fromIntegers ? fromInt(static_cast<int32_t>(r.col[l])) : operand(r.pos[l]);
    for (int l = 0; l < 2; ++l) {
        uint32_t acc = mul(operand(p.k[0][l]), v[0]);
        acc = madd(acc, operand(p.k[1][l]), v[1]);
        acc = madd(acc, operand(p.k[2][l]), v[2]);
        r.st[l] = madd(acc, operand(p.k[3][l]), v[3]);
    }
    r.st[2] = kOne;
    r.st[3] = 0u;
}

inline void passLight(const Pass& p, Row& r) {
    using namespace vu;
    uint32_t n[4], term[4];
    for (int l = 0; l < 4; ++l)
        n[l] = fromInt(static_cast<int32_t>(r.col[l]));
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = mul(operand(p.k[1][l]), n[0]);
        acc = madd(acc, operand(p.k[2][l]), n[1]);
        acc = madd(acc, operand(p.k[3][l]), n[2]);
        acc = madd(acc, operand(p.k[4][l]), n[3]);
        term[l] = maxRaw(acc, 0u);
    }
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = 0u;
        if (l < 3) {
            acc = mul(fromInt(static_cast<int32_t>(p.k[5][l])), term[0]);
            acc = madd(acc, fromInt(static_cast<int32_t>(p.k[6][l])), term[1]);
            acc = madd(acc, fromInt(static_cast<int32_t>(p.k[7][l])), term[2]);
            acc = madd(acc, fromInt(static_cast<int32_t>(p.k[8][l])), term[3]);
        }
        acc = madd(acc, fromInt(static_cast<int32_t>(p.k[0][l])), kOne);
        acc = minRaw(acc, asBits(200.0f));
        r.col[l] = static_cast<uint32_t>(toFixed(acc, 0));
    }
}

// Clip coordinates to the fixed point position and 1 / w; shared by Transform and Project.
inline void projectRow(const Pass& p, const uint32_t clip[4], Row& r) {
    using namespace vu;
    const uint32_t q = reciprocal(clip[3]);
    uint32_t depth = minRaw(clip[3], p.k[4][2]);
    depth = maxRaw(depth, p.k[4][1]);
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = mul(operand(p.k[5][l]), kOne);
        if (l == 3)
            acc = sub(acc, mul(operand(p.k[4][3]), operand(depth)));
        const uint32_t scaled = mul(clip[l], operand(p.k[6][l]));
        r.pos[l] = static_cast<uint32_t>(toFixed(madd(acc, scaled, q), 4));
    }
    r.st[2] = add(0u, q);
}

inline void passTransform(const Pass& p, Row& r) {
    using namespace vu;
    uint32_t v[4], clip[4];
    for (int l = 0; l < 4; ++l)
        v[l] = operand(r.pos[l]);
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = mul(operand(p.k[0][l]), v[0]);
        acc = madd(acc, operand(p.k[1][l]), v[1]);
        acc = madd(acc, operand(p.k[2][l]), v[2]);
        clip[l] = madd(acc, operand(p.k[3][l]), v[3]);
    }
    projectRow(p, clip, r);
}

inline void passProject(const Pass& p, Row& r) {
    uint32_t clip[4];
    for (int l = 0; l < 4; ++l)
        clip[l] = vu::operand(r.pos[l]);
    projectRow(p, clip, r);
}

inline void passFade(const Pass& p, Row& r) {
    using namespace vu;
    const uint32_t w = reciprocal(operand(r.st[2]));
    uint32_t f = sub(kOne, mul(operand(p.k[0][3]), w));
    f = maxRaw(f, 0u);
    f = mul(operand(f), fromInt(static_cast<int32_t>(r.col[3])));
    r.col[3] = static_cast<uint32_t>(toFixed(f, 0));
}

inline void passTexMatrix(const Pass& p, Row& r) {
    using namespace vu;
    const uint32_t s = operand(r.st[0]), t = operand(r.st[1]);
    for (int l = 0; l < 2; ++l) {
        uint32_t acc = mul(operand(p.k[0][l]), s);
        acc = madd(acc, operand(p.k[1][l]), t);
        r.st[l] = madd(acc, operand(p.k[2][l]), kOne);
    }
}

// One stage on one vertex row, in place, with every lane the microprogram stores.
inline void applyPass(const Pass& p, Row& r, uint32_t gridX, uint32_t gridZ) {
    switch (p.kind) {
        case PassGrid:
            passGrid(p, r, gridX, gridZ, false);
            break;
        case PassGridFlat:
            passGrid(p, r, gridX, gridZ, true);
            break;
        case PassPointLight:
            passPointLight(p, r);
            break;
        case PassTexGenInt:
            passTexGen(p, r, true);
            break;
        case PassTexGenPos:
            passTexGen(p, r, false);
            break;
        case PassLight:
            passLight(p, r);
            break;
        case PassTransform:
            passTransform(p, r);
            break;
        case PassFade:
            passFade(p, r);
            break;
        case PassTexMatrix:
            passTexMatrix(p, r);
            break;
        case PassProject:
            passProject(p, r);
            break;
        default:
            break;
    }
}

// What the index-list run copies into the packet from a processed row.
inline Output finishRow(const Row& r) {
    using namespace vu;
    Output o{};
    o.s = mul(operand(r.st[0]), operand(r.st[2]));
    o.t = mul(operand(r.st[1]), operand(r.st[2]));
    o.q = r.st[2];
    o.r = static_cast<uint8_t>(r.col[0]);
    o.g = static_cast<uint8_t>(r.col[1]);
    o.b = static_cast<uint8_t>(r.col[2]);
    o.a = static_cast<uint8_t>(r.col[3]);
    o.x = static_cast<uint16_t>(r.pos[0]);
    o.y = static_cast<uint16_t>(r.pos[1]);
    o.z = r.pos[2];
    o.fog = static_cast<uint8_t>(r.pos[3] >> 4);
    o.adc = ((r.pos[3] >> 15) & 1u) != 0u;
    return o;
}

inline Output evaluateVertex(const Pass* passes, uint32_t passCount, const BatchVertex& v) {
    Row r = v.row;
    for (uint32_t i = 0; i < passCount; ++i)
        if ((v.passMask >> i) & 1u)
            applyPass(passes[i], r, v.gridX, v.gridZ);
    return finishRow(r);
}

// Record layout (all sizes in bytes, multiples of 16): header, passes, strip lengths, indices, vertices.
inline uint32_t batchStripBytes(uint32_t stripCount) {
    return ((stripCount + 3u) / 4u) * 16u;
}
inline uint32_t batchIndexBytes(uint32_t indexCount) {
    return ((indexCount + 7u) / 8u) * 16u;
}
inline uint32_t batchRecordBytes(uint32_t passCount, uint32_t stripCount, uint32_t indexCount, uint32_t vertexCount) {
    return static_cast<uint32_t>(sizeof(BatchHeader)) + passCount * static_cast<uint32_t>(sizeof(Pass)) +
           batchStripBytes(stripCount) + batchIndexBytes(indexCount) +
           vertexCount * static_cast<uint32_t>(sizeof(BatchVertex));
}

struct BatchView {
    BatchHeader header{};
    const Pass* passes = nullptr;
    const uint32_t* strips = nullptr;
    const uint16_t* indices = nullptr;
    const BatchVertex* vertices = nullptr;
};

inline bool parseBatch(const uint8_t* data, uint32_t size, BatchView& out) {
    if (size < sizeof(BatchHeader))
        return false;
    std::memcpy(&out.header, data, sizeof(BatchHeader));
    const BatchHeader& h = out.header;
    if (h.program != Batch || h.passCount > kBatchMaxPasses || h.indexCount > kBatchMaxIndices ||
        h.stripCount > h.indexCount || h.vertexCount > h.indexCount ||
        batchRecordBytes(h.passCount, h.stripCount, h.indexCount, h.vertexCount) != size)
        return false;
    const uint8_t* at = data + sizeof(BatchHeader);
    out.passes = reinterpret_cast<const Pass*>(at);
    at += size_t(h.passCount) * sizeof(Pass);
    out.strips = reinterpret_cast<const uint32_t*>(at);
    at += batchStripBytes(h.stripCount);
    out.indices = reinterpret_cast<const uint16_t*>(at);
    at += batchIndexBytes(h.indexCount);
    out.vertices = reinterpret_cast<const BatchVertex*>(at);
    uint32_t sum = 0;
    for (uint32_t i = 0; i < h.stripCount; ++i) {
        if (out.strips[i] == 0u)
            return false;
        sum += out.strips[i];
    }
    if (sum != h.indexCount)
        return false;
    for (uint32_t i = 0; i < h.indexCount; ++i)
        if (out.indices[i] >= h.vertexCount)
            return false;
    for (uint32_t i = 0; i < h.passCount; ++i)
        if (out.passes[i].kind < PassGrid || out.passes[i].kind > PassGridFlat ||
            (out.passes[i].kind == PassPointLight && out.passes[i].param > kBatchMaxExponent))
            return false;
    for (uint32_t i = 0; i < h.vertexCount; ++i)
        if (h.passCount < 32u && (out.vertices[i].passMask >> h.passCount) != 0u)
            return false;
    return true;
}

// The GIF packets the microprogram sends for the record, with the lanes a GS does not read left zero: one packed
// tag per strip (four registers per vertex: ST, RGBAQ, FOG, XYZ2) and, with kBatchEmptyTag, a closing empty tag.
// A renderer without the vertex-program path feeds these to its ordinary GIF handling.
inline void appendGifTag(std::vector<uint8_t>& out, uint32_t prim, uint32_t vertexCount) {
    const uint64_t lo =
        uint64_t(vertexCount) | (1ull << 15) | (1ull << 46) | (uint64_t(prim & 0x7FFu) << 47) | (4ull << 60);
    const uint64_t hi = 0x5A12ull;
    const size_t at = out.size();
    out.resize(at + 16u);
    std::memcpy(out.data() + at, &lo, 8);
    std::memcpy(out.data() + at + 8, &hi, 8);
}
inline void batchToGif(const BatchView& rec, std::vector<std::vector<uint8_t>>& packets) {
    const BatchHeader& h = rec.header;
    const uint16_t* index = rec.indices;
    for (uint32_t s = 0; s < h.stripCount; ++s) {
        std::vector<uint8_t> pk;
        appendGifTag(pk, h.prim, rec.strips[s]);
        for (uint32_t i = 0; i < rec.strips[s]; ++i, ++index) {
            const Output o = evaluateVertex(rec.passes, h.passCount, rec.vertices[*index]);
            uint32_t w[16] = {};
            w[0] = o.s;
            w[1] = o.t;
            w[2] = o.q;
            w[4] = o.r;
            w[5] = o.g;
            w[6] = o.b;
            w[7] = o.a;
            w[11] = uint32_t(o.fog) << 4;
            w[12] = o.x;
            w[13] = o.y;
            w[14] = o.z;
            w[15] = o.adc ? 0x8000u : 0u;
            const size_t at = pk.size();
            pk.resize(at + 64u);
            std::memcpy(pk.data() + at, w, 64);
        }
        packets.push_back(std::move(pk));
    }
    if (h.flags & kBatchEmptyTag) {
        std::vector<uint8_t> pk;
        appendGifTag(pk, h.prim, 0);
        packets.push_back(std::move(pk));
    }
}

// True when no vertex a Transform or Project pass produces can carry the "do not draw" bit (see
// neverSkipsVertices): a backend that cannot express the bit per triangle draws the record only then.
inline bool passNeverSkips(const Pass& p) {
    Constants k{};
    std::memcpy(k.fog, p.k[4], 16);
    std::memcpy(k.offset, p.k[5], 16);
    std::memcpy(k.scale, p.k[6], 16);
    return neverSkipsVertices(k);
}

} // namespace gsr::vp
