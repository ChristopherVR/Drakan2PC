// Vertex-program draws: a VU1 run handed to the renderer as its inputs instead of its output (prototype, off by
// default; docs/journal/2026-11-12/gpu-vertex-feasibility.md).
//
// The game's one VU1 microprogram has a stage that draws triangle strips of an object that needs no clipping: one
// matrix, a viewport scale and offset, fog limits, a colour or four directional lights, and per vertex a position, a
// texture coordinate and (lit) a normal. This header describes such a run in a backend-neutral record and evaluates
// it on the CPU with the VU's arithmetic (every operation rounds toward zero, operands and results have no
// denormals, infinities or NaNs). `evaluate()` is both the fallback of a renderer without a vertex-program path and
// the reference the shader of a backend is checked against; it uses double arithmetic only where a float result is
// rounded, exactly as the shader does, so it needs no rounding-mode switch.
//
// A record travels in the GIF stream as one tag with FLG = 3 (a GS ignores its data) whose upper half is kTagMagic,
// so the stream order with every other packet is kept and a GS capture carries the record unchanged.
#pragma once
#include <cstdint>
#include <cstring>

namespace gsr::vp {

constexpr uint64_t kTagMagic = 0x3130474F52505556ull; // upper 64 bits of the wrapping GIF tag
constexpr uint32_t kMaxVertices = 238;                // slots of the program's vertex window

enum Program : uint32_t {
    StripsFlat = 1, // constant colour
    StripsLit = 2,  // ambient plus four directional terms from the normal, clamped at 200
};

// Per-run constants, as 16 rows of four 32-bit words (the layout a backend uploads as is).
struct Constants {
    uint32_t mvp[4][4];        // rows 0..3: the object-to-clip matrix columns the set-up stage left in VF01..VF04
    uint32_t scale[4];         // row 4: viewport scale
    uint32_t offset[4];        // row 5: viewport offset
    uint32_t fog[4];           // row 6: y, z = depth clamp of the fog term, w = fog slope
    int32_t colour[4];         // row 7: colour / ambient as integers, w = alpha
    uint32_t lightDir[4][4];   // rows 8..11: light direction matrix (lit program)
    int32_t lightColour[4][4]; // rows 12..15: light colour matrix as integers (lit program)
};
static_assert(sizeof(Constants) == 256, "sixteen rows");

struct Vertex {
    uint32_t pos[4];   // object-space position (float bits), w as stored
    uint32_t st[4];    // texture coordinate (float bits); z is the third component the program scales by 1 / w
    int32_t normal[4]; // integer normal (lit program)
};
static_assert(sizeof(Vertex) == 48, "three rows");

struct Header {
    uint32_t program;     // Program
    uint32_t prim;        // GS PRIM value of the strip tag (11 bits)
    uint32_t stripCount;  // strip lengths follow the constants, padded to a multiple of four words
    uint32_t vertexCount; // sum of the strip lengths
};

// What the program would have put into the GS packet for one vertex.
struct Output {
    uint32_t s, t, q; // float bits
    uint8_t r, g, b, a;
    uint16_t x, y; // 12.4 fixed point, as the GS takes them (low 16 bits)
    uint32_t z;
    uint8_t fog;
    bool adc; // the vertex does not complete a triangle
};

namespace vu {
inline float asFloat(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
inline uint32_t asBits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
// Operand rule: a denormal is a signed zero, an infinity or NaN the largest finite value of its sign.
inline uint32_t operand(uint32_t bits) {
    const uint32_t sign = bits & 0x80000000u, mag = bits & 0x7FFFFFFFu;
    if (mag <= 0x007FFFFFu)
        return sign;
    return sign | (mag < 0x7F7FFFFFu ? mag : 0x7F7FFFFFu);
}
// Rounds an exact (or correctly rounded, with the sign of what was lost in `residual`) double toward zero.
inline uint32_t roundZero(double value, double residual = 0.0) {
    const float nearest = static_cast<float>(value);
    uint32_t bits = asBits(nearest);
    if (value == 0.0)
        return bits & 0x80000000u;
    const double back = static_cast<double>(nearest);
    if ((value > 0.0 && (back > value || (back == value && residual < 0.0))) ||
        (value < 0.0 && (back < value || (back == value && residual > 0.0))))
        --bits;
    if ((bits & 0x7F800000u) == 0u)
        bits &= 0x80000000u;
    return bits;
}
inline uint32_t add(uint32_t a, uint32_t b) {
    const double x = asFloat(a), y = asFloat(b), sum = x + y;
    const double back = sum - x;
    const double residual = (x - (sum - back)) + (y - back);
    return roundZero(sum, residual);
}
inline uint32_t mul(uint32_t a, uint32_t b) {
    return roundZero(static_cast<double>(asFloat(a)) * static_cast<double>(asFloat(b)));
}
inline uint32_t neg(uint32_t a) {
    return a ^ 0x80000000u;
}
// 1 / w, w already an operand: the largest magnitude whose product with |w| does not exceed one.
inline uint32_t reciprocal(uint32_t w) {
    const uint32_t sign = w & 0x80000000u, mag = w & 0x7FFFFFFFu;
    if (mag == 0u)
        return sign | 0x7F7FFFFFu;
    const double aw = asFloat(mag);
    uint32_t q = asBits(1.0f / asFloat(mag));
    for (int i = 0; i < 2; ++i)
        if (static_cast<double>(asFloat(q)) * aw > 1.0)
            --q;
    for (int i = 0; i < 2; ++i)
        if (static_cast<double>(asFloat(q + 1u)) * aw <= 1.0)
            ++q;
    if ((q & 0x7F800000u) == 0u)
        q = 0u;
    return sign | q;
}
inline uint32_t fromInt(int32_t v) {
    // exact in double for every 32-bit integer
    return roundZero(static_cast<double>(v));
}
// Float to fixed point with `shift` fraction bits: truncation, saturating like the VU.
inline int32_t toFixed(uint32_t v, uint32_t shift) {
    const double scaled = static_cast<double>(asFloat(roundZero(static_cast<double>(asFloat(v)) * (1u << shift))));
    if (scaled >= 2147483648.0)
        return 0x7FFFFFFF;
    if (scaled <= -2147483648.0)
        return static_cast<int32_t>(0x80000000u);
    return static_cast<int32_t>(scaled);
}
inline bool less(uint32_t a, uint32_t b) {
    return asFloat(a) < asFloat(b);
}
} // namespace vu

// The hand-written twin of the stage evaluator of gsr/vertex_stage.h, which is the documented oracle (the shaders are
// generated from the same description); tests/test_vertex_stage.cpp pins the two to each other.
inline Output evaluate(uint32_t program, const Constants& k, const Vertex& v) {
    using namespace vu;
    uint32_t p[4], clip[4];
    for (int i = 0; i < 4; ++i)
        p[i] = operand(v.pos[i]);
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = mul(operand(k.mvp[0][l]), p[0]);
        acc = add(acc, mul(operand(k.mvp[1][l]), p[1]));
        acc = add(acc, mul(operand(k.mvp[2][l]), p[2]));
        clip[l] = add(acc, mul(operand(k.mvp[3][l]), p[3]));
    }
    const uint32_t q = reciprocal(clip[3]);
    uint32_t depth = less(clip[3], operand(k.fog[2])) ? clip[3] : operand(k.fog[2]);
    depth = less(operand(k.fog[1]), depth) ? depth : operand(k.fog[1]);
    int32_t fixed[4];
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = operand(k.offset[l]);
        if (l == 3)
            acc = add(acc, neg(mul(operand(k.fog[3]), depth)));
        const uint32_t scaled = mul(clip[l], operand(k.scale[l]));
        fixed[l] = toFixed(add(acc, mul(scaled, q)), 4);
    }
    Output o{};
    o.s = mul(operand(v.st[0]), q);
    o.t = mul(operand(v.st[1]), q);
    o.q = mul(operand(v.st[2]), q);
    o.x = static_cast<uint16_t>(fixed[0]);
    o.y = static_cast<uint16_t>(fixed[1]);
    o.z = static_cast<uint32_t>(fixed[2]);
    o.fog = static_cast<uint8_t>(static_cast<uint32_t>(fixed[3]) >> 4);
    o.adc = ((static_cast<uint32_t>(fixed[3]) >> 15) & 1u) != 0u;
    o.a = static_cast<uint8_t>(k.colour[3]);
    if (program == StripsFlat) {
        o.r = static_cast<uint8_t>(k.colour[0]);
        o.g = static_cast<uint8_t>(k.colour[1]);
        o.b = static_cast<uint8_t>(k.colour[2]);
        return o;
    }
    uint32_t n[4], term[4];
    for (int i = 0; i < 4; ++i)
        n[i] = fromInt(v.normal[i]);
    for (int l = 0; l < 4; ++l) {
        uint32_t acc = mul(operand(k.lightDir[0][l]), n[0]);
        acc = add(acc, mul(operand(k.lightDir[1][l]), n[1]));
        acc = add(acc, mul(operand(k.lightDir[2][l]), n[2]));
        acc = add(acc, mul(operand(k.lightDir[3][l]), n[3]));
        term[l] = less(0u, acc) ? acc : 0u;
    }
    uint8_t rgb[3];
    for (int l = 0; l < 3; ++l) {
        uint32_t acc = mul(fromInt(k.lightColour[0][l]), term[0]);
        acc = add(acc, mul(fromInt(k.lightColour[1][l]), term[1]));
        acc = add(acc, mul(fromInt(k.lightColour[2][l]), term[2]));
        acc = add(acc, mul(fromInt(k.lightColour[3][l]), term[3]));
        acc = add(acc, fromInt(k.colour[l]));
        const uint32_t limit = asBits(200.0f);
        acc = less(acc, limit) ? acc : limit;
        rgb[l] = static_cast<uint8_t>(toFixed(acc, 0));
    }
    o.r = rgb[0];
    o.g = rgb[1];
    o.b = rgb[2];
    return o;
}

// True when no vertex of the run can carry the "do not draw" bit, decided from the constants alone: the bit is bit 15
// of the fixed-point fog lane, which without a scale on that lane is offset.w - slope * clamp(w) and therefore lies
// between its values at the two clamp ends.
inline bool neverSkipsVertices(const Constants& k) {
    using namespace vu;
    if ((operand(k.scale[3]) & 0x7FFFFFFFu) != 0u)
        return false;
    for (int end = 1; end <= 2; ++end) {
        const uint32_t lane = add(operand(k.offset[3]), neg(mul(operand(k.fog[3]), operand(k.fog[end]))));
        const int32_t fixed = toFixed(lane, 4);
        if (fixed < 0 || fixed > 0x7FFF)
            return false;
    }
    return !less(operand(k.fog[2]), operand(k.fog[1]));
}

// Record layout helpers (all sizes in bytes, multiples of 16).
inline uint32_t stripTableBytes(uint32_t stripCount) {
    return ((stripCount + 3u) / 4u) * 16u;
}
inline uint32_t recordBytes(uint32_t stripCount, uint32_t vertexCount) {
    return 16u + static_cast<uint32_t>(sizeof(Constants)) + stripTableBytes(stripCount) +
           vertexCount * static_cast<uint32_t>(sizeof(Vertex));
}

// A parsed record (pointers into the caller's buffer).
struct View {
    Header header{};
    const Constants* constants = nullptr;
    const uint32_t* strips = nullptr;
    const Vertex* vertices = nullptr;
};
inline bool parse(const uint8_t* data, uint32_t size, View& out) {
    if (size < 16u + sizeof(Constants))
        return false;
    std::memcpy(&out.header, data, 16);
    const Header& h = out.header;
    if ((h.program != StripsFlat && h.program != StripsLit) || h.vertexCount > kMaxVertices ||
        h.stripCount > kMaxVertices || recordBytes(h.stripCount, h.vertexCount) != size)
        return false;
    out.constants = reinterpret_cast<const Constants*>(data + 16);
    out.strips = reinterpret_cast<const uint32_t*>(data + 16 + sizeof(Constants));
    out.vertices = reinterpret_cast<const Vertex*>(data + 16 + sizeof(Constants) + stripTableBytes(h.stripCount));
    uint32_t sum = 0;
    for (uint32_t i = 0; i < h.stripCount; ++i)
        sum += out.strips[i];
    return sum == h.vertexCount;
}

} // namespace gsr::vp
