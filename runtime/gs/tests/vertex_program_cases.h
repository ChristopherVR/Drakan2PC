// The inputs every vertex-stage test shares (Direct3D 11 stream output, WebGPU compute, the CPU evaluations), so
// that each path is compared on the same vertices:
//   * generated strip vertices: matrices and viewport constants of the game's magnitudes with random vertices, and
//     every fourth case with operands chosen to hit the rounding rules (results that are exact or lie just beside a
//     float, sums of very unlike magnitudes, denormals, the largest finite values, infinities, NaN, zero and
//     negative w);
//   * probe vertices for three stages that exist only in the tests and use every operation of the description the
//     strip programs do not (division, square root, reciprocal square root, subtraction, the fixed-point formats,
//     comparisons, selects, integer operations);
//   * with GSR_VP_RECORDS=<file>, the records dumped from a VU1 recording (private data; everything passes without).
// Standard C++ only.
#pragma once
#include "gsr/vertex_program.h"
#include "gsr/vertex_stage.h"
#include "vertex_stage_emit.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <random>
#include <vector>

namespace gsr::vpcases {

struct GpuVertex {
    vp::Vertex in;
    uint32_t constants, program;
    uint32_t previous; // constants of "the previous frame": the stage is evaluated a second time with them
    uint32_t pad;
};
static_assert(sizeof(GpuVertex) == 64, "four rows");

struct Cases {
    std::vector<vp::Constants> constants;
    std::vector<GpuVertex> vertices;
};

inline uint32_t bitsOf(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

inline void addGenerated(Cases& c, uint32_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    const auto pick = [&](std::initializer_list<uint32_t> list) { return *(list.begin() + rng() % list.size()); };
    // operands that stress the rules, as raw bits
    const auto odd = [&]() -> uint32_t {
        switch (rng() % 12) {
            case 0:
                return pick({0u, 0x80000000u});
            case 1:
                return pick({0x00000001u, 0x807FFFFFu, 0x00800000u}); // denormals and the smallest normal
            case 2:
                return pick({0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFEu, 0x7F800000u, 0xFFC00000u}); // largest, inf, NaN
            case 3:
                return bitsOf(1.0f) + rng() % 3 - 1;
            case 4:
                return bitsOf(unit(rng) * 1e-12f);
            case 5:
                return bitsOf(unit(rng) * 1e12f);
            default:
                return bitsOf(unit(rng) * 4096.0f);
        }
    };
    for (uint32_t i = 0; i < count; ++i) {
        const bool wild = (i % 4) == 3; // every fourth case takes arbitrary operands everywhere
        if (i % 8 == 0) {
            vp::Constants k{};
            for (int r = 0; r < 4; ++r)
                for (int l = 0; l < 4; ++l)
                    k.mvp[r][l] = wild ? odd() : bitsOf(unit(rng) * (r == 3 ? 3000.0f : 1.6f));
            const float scale[4] = {320.0f + unit(rng), -240.0f + unit(rng), -8388608.0f * (1.0f + 0.1f * unit(rng)),
                                    0.0f};
            const float offset[4] = {2048.0f, 2048.0f, 8388608.0f, 255.0f + unit(rng)};
            const float fog[4] = {0.0f, 100.0f + 50.0f * unit(rng), 60000.0f + 1000.0f * unit(rng),
                                  0.004f * (1.0f + unit(rng))};
            for (int l = 0; l < 4; ++l) {
                k.scale[l] = wild && (rng() & 1) ? odd() : bitsOf(scale[l]);
                k.offset[l] = wild && (rng() & 1) ? odd() : bitsOf(offset[l]);
                k.fog[l] = wild && (rng() & 1) ? odd() : bitsOf(fog[l]);
                k.colour[l] = wild ? (int32_t)rng() : (int32_t)(rng() % 256);
            }
            for (int r = 0; r < 4; ++r)
                for (int l = 0; l < 4; ++l) {
                    k.lightDir[r][l] = wild ? odd() : bitsOf(unit(rng) / 127.0f);
                    k.lightColour[r][l] = wild && (rng() % 4 == 0) ? (int32_t)rng() : (int32_t)(rng() % 200);
                }
            c.constants.push_back(k);
        }
        GpuVertex v{};
        for (int l = 0; l < 4; ++l) {
            v.in.pos[l] = wild ? odd() : bitsOf(l == 3 ? 1.0f : unit(rng) * 900.0f);
            v.in.st[l] = wild ? odd() : bitsOf(l == 2 ? 1.0f : unit(rng) * 8.0f);
            v.in.normal[l] = wild && (rng() % 4 == 0) ? (int32_t)rng() : (int32_t)(rng() % 255) - 127;
        }
        v.constants = (uint32_t)c.constants.size() - 1;
        v.program = (i & 1) ? vp::StripsLit : vp::StripsFlat;
        c.vertices.push_back(v);
    }
}

// A float chosen to stress an arithmetic rule (raw bits; also used by the operand-pair tests of the library).
template <class Rng>
uint32_t edgeBits(Rng& rng) {
    static const uint32_t kFixed[] = {0u,          0x80000000u, 0x00000001u, 0x807FFFFFu, 0x007FFFFFu, 0x00800000u,
                                      0x80800000u, 0x00800001u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFEu, 0x7F800000u,
                                      0xFF800000u, 0x7FC00000u, 0xFFC00001u, 0x3F800000u, 0xBF800000u, 0x3F7FFFFFu,
                                      0x3F800001u, 0x40000000u, 0x3F000000u, 0x4B800000u, 0x4B7FFFFFu, 0x4F000000u,
                                      0xCF000000u, 0x4EFFFFFFu, 0x5F000000u, 0x00FFFFFFu, 0x01000000u, 0x7F000000u};
    const uint32_t r = static_cast<uint32_t>(rng());
    switch (r % 8u) {
        case 0:
            return kFixed[(r >> 8) % (sizeof(kFixed) / sizeof(kFixed[0]))];
        case 1: // any bits
            return static_cast<uint32_t>(rng());
        case 2: // exponent extremes with any significand
            return (static_cast<uint32_t>(rng()) & 0x807FFFFFu) |
                   (((r >> 8) & 1u ? 1u + ((r >> 9) % 4u) : 251u + ((r >> 9) % 4u)) << 23);
        case 3: // few significand bits (exact results, ties)
            return (static_cast<uint32_t>(rng()) & 0xFF800000u) | (((r >> 8) & 0xFu) << (19u + ((r >> 12) % 4u)));
        case 4: // all ones or a lone last bit in the significand
            return (static_cast<uint32_t>(rng()) & 0xFF800000u) | ((r >> 8) & 1u ? 0x007FFFFFu : (r >> 9) & 3u);
        default: // moderate exponents: sums that carry, cancel and lose bits
            return (static_cast<uint32_t>(rng()) & 0x807FFFFFu) | ((100u + (r >> 8) % 56u) << 23);
    }
}

// ---- the probe stages (program ids 100, 101, 102): every operation the strip programs do not use ----
inline constexpr uint32_t kProbeFirst = 100, kProbeCount = 3;
inline vp::ir::Stage buildProbe(uint32_t which) {
    using namespace vp::ir;
    Builder s(which == 0 ? "ProbeA" : which == 1 ? "ProbeB" : "ProbeC");
    const Value a = s.attributeFloat(0, 0), b = s.attributeFloat(0, 1), c = s.attributeFloat(0, 2);
    const Value d = s.attributeFloat(0, 3), e = s.attributeFloat(1, 0), f = s.attributeFloat(1, 1);
    const Value k = s.constantFloat(0, 0), m = s.constantFloat(1, 1);
    const Value n0 = s.attribute(2, 0), n1 = s.attribute(2, 1), n2 = s.attribute(2, 2), n3 = s.attribute(2, 3);
    if (which == 0) {
        s.output(S, s.div(a, b));
        s.output(T, s.sqrt(c));
        s.output(Q, s.rsqrt(d, a));
        s.output(Z, s.sub(a, c));
        s.output(FogLane, s.msub(k, b, d));
        s.output(X, s.ftoi(e, 4));
        s.output(Y, s.ftoi(f, 12));
        s.output(R, s.shr(s.div(k, m), 23));
        s.output(G, s.shr(s.div(e, f), 16));
        s.output(B, s.shr(s.sqrt(e), 8));
        s.output(A, s.rsqrt(f, m));
    } else if (which == 1) {
        s.output(S, s.itof(n0, 4));
        s.output(T, s.itof(n1, 12));
        s.output(Q, s.itof(n2, 15));
        s.output(Z, s.ftoi(c, 15));
        s.output(FogLane, s.ftoi(d, 12));
        s.output(X, s.ftoi(s.itof(n3, 0), 4));
        s.output(Y, s.shr(s.ftoi(a, 15), 16));
        Value flags = s.less(a, b);
        flags = s.bitOr(flags, s.shl(s.lessEqual(b, c), 1));
        flags = s.bitOr(flags, s.shl(s.equal(c, d), 2));
        flags = s.bitOr(flags, s.shl(s.intLess(n0, n1), 3));
        flags = s.bitOr(flags, s.shl(s.intEqual(n2, n3), 4));
        flags = s.bitOr(flags, s.shl(s.lessEqual(e, e), 5));
        flags = s.bitOr(flags, s.shl(s.equal(a, s.neg(a)), 6));
        s.output(R, flags);
        s.output(G, s.bitXor(s.bitAnd(n0, n1), s.shr(n2, 7)));
        s.output(B, s.shr(s.ftoi(b, 0), 24));
        s.output(A, s.ftoi(s.div(e, f), 4));
    } else {
        s.output(S, s.select(s.less(a, b), s.abs(c), s.neg(d)));
        s.output(T, s.max(a, b));
        s.output(Q, s.min(c, d));
        s.output(Z, s.intAdd(n0, n1));
        s.output(FogLane, s.bitXor(s.intSub(n2, s.shl(n3, 3)), s.shr(n0, 5)));
        s.output(X, s.ftoi(s.madd(a, b, c), 0));
        s.output(Y, s.ftoi(s.add(a, d), 4));
        s.output(R, s.shr(s.max(e, s.imm(0u)), 24));
        s.output(G, s.shr(s.min(f, k), 20));
        s.output(B, s.select(s.intLess(n0, n1), n2, n3));
        s.output(A, s.shr(s.sub(e, f), 23));
    }
    return s.finish();
}
inline const vp::ir::Stage& probe(uint32_t which) {
    static const vp::ir::Stage stages[kProbeCount] = {buildProbe(0), buildProbe(1), buildProbe(2)};
    return stages[which];
}
// The stages a test shader holds: the game's two and the probes.
inline std::vector<vp::StageEntry> testStages() {
    std::vector<vp::StageEntry> list = vp::gameStages();
    for (uint32_t i = 0; i < kProbeCount; ++i)
        list.push_back({kProbeFirst + i, &probe(i)});
    return list;
}
inline const vp::ir::Stage* testStageOf(uint32_t program) {
    if (program >= kProbeFirst && program < kProbeFirst + kProbeCount)
        return &probe(program - kProbeFirst);
    return vp::ir::stageOf(program);
}

inline void addProbes(Cases& c, uint32_t count, uint32_t seed) {
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < count; ++i) {
        if (i % 16 == 0) {
            vp::Constants k{};
            uint32_t* words = reinterpret_cast<uint32_t*>(&k);
            for (int w = 0; w < 64; ++w)
                words[w] = edgeBits(rng);
            c.constants.push_back(k);
        }
        GpuVertex v{};
        for (int l = 0; l < 4; ++l) {
            v.in.pos[l] = edgeBits(rng);
            v.in.st[l] = edgeBits(rng);
            const uint32_t r = rng();
            v.in.normal[l] = static_cast<int32_t>(r % 3 == 0   ? rng()
                                                  : r % 3 == 1 ? (rng() % 513) - 256
                                                               : 1u << (r >> 27));
        }
        if (i % 5 == 0) // near-equal operands: cancellation, quotients beside one, roots of squares
            v.in.pos[1] = v.in.pos[0] + (rng() % 5) - 2, v.in.pos[2] = v.in.pos[0] ^ (rng() % 4);
        v.constants = (uint32_t)c.constants.size() - 1;
        v.program = kProbeFirst + i % kProbeCount;
        c.vertices.push_back(v);
    }
}

// Records as the VU1 replay dumps them (DRAKAN_VU_BENCH_GPUPROG_DUMP): u32 size, then the record.
inline void addRecords(Cases& c, const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("GSR_VP_RECORDS: cannot open %s\n", path);
        return;
    }
    std::vector<uint8_t> buf;
    uint32_t size = 0, records = 0;
    while (std::fread(&size, 4, 1, f) == 1 && size < (1u << 20)) {
        buf.resize(size);
        if (std::fread(buf.data(), 1, size, f) != size)
            break;
        vp::View rec;
        if (!vp::parse(buf.data(), size, rec))
            continue;
        c.constants.push_back(*rec.constants);
        for (uint32_t i = 0; i < rec.header.vertexCount; ++i)
            c.vertices.push_back(
                GpuVertex{rec.vertices[i], (uint32_t)c.constants.size() - 1, rec.header.program, 0, 0});
        ++records;
    }
    std::fclose(f);
    std::printf("%u recorded runs added from %s\n", records, path);
}

// The standard set: 400,000 generated strip vertices, 200,000 probe vertices, then the records when given.
struct StandardCases {
    Cases cases;
    size_t generated = 0, probes = 0, recorded = 0;
};
// `reduced`: a tenth of the generated vertices and probes (the same seeds, so a prefix of the full set's kinds), for a
// software adapter where the full set does not finish in a test's time.
inline StandardCases standardCases(bool reduced = false) {
    StandardCases s;
    addGenerated(s.cases, reduced ? 40000u : 400000u, 20261112u);
    s.generated = s.cases.vertices.size();
    addProbes(s.cases, reduced ? 20000u : 200000u, 20261113u);
    s.probes = s.cases.vertices.size() - s.generated;
    if (const char* path = std::getenv("GSR_VP_RECORDS"))
        addRecords(s.cases, path);
    s.recorded = s.cases.vertices.size() - s.generated - s.probes;
    // the previous frame's constants of a vertex: another set of the same kind, a few sets earlier
    const uint32_t sets = static_cast<uint32_t>(s.cases.constants.size());
    for (GpuVertex& v : s.cases.vertices)
        v.previous = (v.constants + sets - 3u) % sets;
    return s;
}

// The reference: every vertex through the stage description on the CPU.
template <class Arith = vp::ir::DoubleArithmetic>
std::vector<vp::ir::Raw> reference(const Cases& c) {
    using Evaluator = vp::ir::Evaluator<Arith>;
    std::vector<std::unique_ptr<Evaluator>> evaluators(kProbeFirst + kProbeCount);
    std::vector<uint32_t> bound(evaluators.size(), ~0u);
    std::vector<vp::ir::Raw> out(c.vertices.size());
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const GpuVertex& v = c.vertices[i];
        if (!evaluators[v.program])
            evaluators[v.program] = std::make_unique<Evaluator>(*testStageOf(v.program));
        if (bound[v.program] != v.constants) {
            evaluators[v.program]->bind(c.constants[v.constants]);
            bound[v.program] = v.constants;
        }
        out[i] = evaluators[v.program]->vertexRaw(v.in);
    }
    return out;
}

// The motion-vector twin: x, y and z of every vertex evaluated with its "previous" constants (and a zero).
struct PreviousPlace {
    uint32_t place[4];
};
inline std::vector<PreviousPlace> referencePrevious(const Cases& c) {
    using Evaluator = vp::ir::Evaluator<>;
    std::vector<std::unique_ptr<Evaluator>> evaluators(kProbeFirst + kProbeCount);
    std::vector<PreviousPlace> out(c.vertices.size());
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const GpuVertex& v = c.vertices[i];
        if (!evaluators[v.program])
            evaluators[v.program] = std::make_unique<Evaluator>(*testStageOf(v.program));
        evaluators[v.program]->bind(c.constants[v.previous]);
        const vp::ir::Raw raw = evaluators[v.program]->vertexRaw(v.in);
        out[i] = {{raw.place[0], raw.place[1], raw.place[2], 0u}};
    }
    return out;
}
// Compares what a shader returned (16 words per vertex: the packet, then the previous place) with the references.
inline uint64_t compareShader(const char* path, const Cases& c, const std::vector<vp::ir::Raw>& want,
                              const std::vector<PreviousPlace>& previous, const uint32_t* got) {
    uint64_t bad = 0;
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const bool now = std::memcmp(&want[i], got + i * 16, 48) == 0;
        const bool before = std::memcmp(&previous[i], got + i * 16 + 12, 16) == 0;
        if (now && before)
            continue;
        if (++bad <= 6) {
            std::printf("  [FAIL] %s, vertex %zu (program %u)%s:", path, i, c.vertices[i].program,
                        now ? ", previous place" : "");
            uint32_t w[16];
            std::memcpy(w, &want[i], 48);
            std::memcpy(w + 12, &previous[i], 16);
            for (int k = 0; k < 16; ++k)
                std::printf(" %08x%s%08x", got[i * 16 + k], got[i * 16 + k] == w[k] ? "=" : "!", w[k]);
            std::printf("\n");
        }
    }
    return bad;
}

// Compares two evaluations (12 words per vertex); prints the first few differences.
inline uint64_t compareRaw(const char* path, const Cases& c, const std::vector<vp::ir::Raw>& want,
                           const uint32_t* got) {
    uint64_t bad = 0;
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        if (std::memcmp(&want[i], got + i * 12, sizeof(vp::ir::Raw)) == 0)
            continue;
        if (++bad <= 6) {
            std::printf("  [FAIL] %s, vertex %zu (program %u):", path, i, c.vertices[i].program);
            const uint32_t* w = reinterpret_cast<const uint32_t*>(&want[i]);
            for (int k = 0; k < 12; ++k)
                std::printf(" %08x%s%08x", got[i * 12 + k], got[i * 12 + k] == w[k] ? "=" : "!", w[k]);
            std::printf("\n");
        }
    }
    return bad;
}

} // namespace gsr::vpcases
