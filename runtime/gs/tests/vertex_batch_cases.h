// The inputs every test of the batch stage shares (the CPU evaluation of the description, Direct3D 11 stream output,
// WebGPU compute), so that each path is compared on the same vertices, and the references they are compared with:
// vp::applyPass / vp::finishRow / vp::evaluateVertex of gsr/vertex_batch.h, the code proven against the microprogram.
//   * one pass alone, per kind: rows and constants of the game's magnitudes, and every other record with operands
//     chosen to hit the rules (denormals, the largest finite values, infinities, NaN, signed zeros, exact results,
//     cancellation), NaN and infinities in the registers MAX and MINI read raw, a zero length for the point light,
//     every squaring count;
//   * the chains the microprogram's dispatcher can build from a flag word (grid or flat grid, point light, either
//     texture generator, light, transform or project), followed by texture matrix runs and a fade, up to
//     kBatchMaxPasses passes;
//   * random chains: any kinds in any order, 1 to kBatchMaxPasses passes, any subset of them per vertex;
//   * with GSR_VB_RECORDS=<file>[;<file>...] the records dumped from VU1 recordings (DRAKAN_VU_BENCH_BATCH_DUMP;
//     private data, everything passes without).
// Every vertex also has "previous frame" inputs (a position row, grid values, a pass mask and a pass table of
// another record) for the position-only twin of the chain.
// GSR_VB_CASES=<n> multiplies the generated counts (default 1: 30,000 + 30,240 + 60,000 vertices).
// Standard C++ only.
#pragma once
#include "gsr/vertex_batch.h"
#include "gsr/vertex_batch_stage.h"
#include "vertex_program_cases.h"

#include <string>

namespace gsr::vbcases {

// A vertex as the shader's input: a vp::BatchVertex with `slot` replaced by the row of its record's first pass in
// the constants table, then the previous frame's position row and sel.
struct GpuVertex {
    vp::Row row;
    uint32_t mask, gridX, gridZ, base;
    uint32_t previousPos[4];
    uint32_t previousMask, previousGridX, previousGridZ, previousBase;
};
static_assert(sizeof(GpuVertex) == 96, "six rows");

struct Record {
    uint32_t firstPass, passCount;
};
struct Cases {
    std::vector<vp::Pass> passes; // the constants table: thirteen rows per pass
    std::vector<Record> records;
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t> recordOf, previousRecordOf; // per vertex
    size_t single = 0, flagChains = 0, randomChains = 0, recorded = 0, recordedRecords = 0;
};
constexpr uint32_t kResultWords = 28; // per vertex: the packet (12), the previous place (4), the row (12)

using vpcases::bitsOf;
using vpcases::edgeBits;

inline vp::BatchVertex batchVertexOf(const GpuVertex& v) {
    return {v.row, v.mask, v.gridX, v.gridZ, 0u};
}
inline vp::BatchVertex previousVertexOf(const GpuVertex& v) {
    vp::BatchVertex p{};
    std::memcpy(p.row.pos, v.previousPos, 16);
    p.passMask = v.previousMask;
    p.gridX = v.previousGridX;
    p.gridZ = v.previousGridZ;
    return p;
}

// ---- generated passes and rows ----
template <class Rng>
float unitOf(Rng& rng) {
    return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng);
}
// A pass with constants of the game's magnitudes.
template <class Rng>
vp::Pass plausiblePass(uint32_t kind, Rng& rng) {
    vp::Pass p{};
    p.kind = kind;
    const auto f = [&](uint32_t row, uint32_t lane, float value) { p.k[row][lane] = bitsOf(value); };
    switch (kind) {
        case vp::PassGrid:
        case vp::PassGridFlat:
            f(0, 0, 4000.0f * unitOf(rng)), f(0, 1, 65536.0f + 2000.0f * unitOf(rng));
            f(0, 2, 4000.0f * unitOf(rng)), f(0, 3, 1.0f);
            f(1, 0, 128.0f), f(1, 2, 128.0f);
            p.param = 2 + rng() % 14;
            break;
        case vp::PassPointLight:
            for (uint32_t l = 0; l < 3; ++l) {
                f(0, l, 900.0f * unitOf(rng));
                f(1, l, unitOf(rng));
                p.k[2][l] = rng() % 256;
            }
            f(3, 1, -200.0f * (1.0f + unitOf(rng))), f(3, 2, 300.0f * (1.5f + unitOf(rng)));
            f(3, 3, 40.0f * unitOf(rng)), f(4, 3, 0.2f * unitOf(rng));
            p.param = rng() % 4 == 0 ? rng() % (vp::kBatchMaxExponent + 1) : 4 + rng() % 2;
            break;
        case vp::PassTexGenInt:
        case vp::PassTexGenPos:
            for (uint32_t r = 0; r < 4; ++r)
                for (uint32_t l = 0; l < 2; ++l)
                    f(r, l, unitOf(rng) * (kind == vp::PassTexGenInt ? 0.05f : 0.01f));
            break;
        case vp::PassLight:
            for (uint32_t l = 0; l < 4; ++l) {
                p.k[0][l] = rng() % 160;
                for (uint32_t r = 0; r < 4; ++r) {
                    f(1 + r, l, unitOf(rng) / 127.0f);
                    p.k[5 + r][l] = rng() % 200;
                }
            }
            break;
        case vp::PassTransform:
        case vp::PassProject: {
            for (uint32_t r = 0; r < 4; ++r)
                for (uint32_t l = 0; l < 4; ++l)
                    f(r, l, unitOf(rng) * (r == 3 ? 3000.0f : 1.6f));
            const float fog[4] = {0.0f, 100.0f + 50.0f * unitOf(rng), 60000.0f + 1000.0f * unitOf(rng),
                                  0.004f * (1.0f + unitOf(rng))};
            const float offset[4] = {2048.0f, 2048.0f, 8388608.0f, 255.0f + unitOf(rng)};
            const float scale[4] = {320.0f + unitOf(rng), -240.0f + unitOf(rng),
                                    -8388608.0f * (1.0f + 0.1f * unitOf(rng)), 0.0f};
            for (uint32_t l = 0; l < 4; ++l)
                f(4, l, fog[l]), f(5, l, offset[l]), f(6, l, scale[l]);
            break;
        }
        case vp::PassFade:
            f(0, 3, (1.0f + unitOf(rng)) / 4000.0f);
            break;
        case vp::PassTexMatrix:
            f(0, 0, 1.0f + 0.5f * unitOf(rng)), f(0, 1, 0.5f * unitOf(rng));
            f(1, 0, 0.5f * unitOf(rng)), f(1, 1, 1.0f + 0.5f * unitOf(rng));
            f(2, 0, unitOf(rng)), f(2, 1, unitOf(rng));
            break;
        default:
            break;
    }
    return p;
}
// A pass with arbitrary operands in every word it can read.
template <class Rng>
vp::Pass wildPass(uint32_t kind, Rng& rng) {
    vp::Pass p{};
    p.kind = kind;
    const uint32_t how = static_cast<uint32_t>(rng());
    p.param = how % 3 == 0 ? rng() % 13 : how % 3 == 1 ? rng() % (vp::kBatchMaxExponent + 1) : rng();
    for (uint32_t r = 0; r < 12; ++r)
        for (uint32_t l = 0; l < 4; ++l) {
            const uint32_t pick = static_cast<uint32_t>(rng());
            p.k[r][l] = pick % 5 == 0 ? rng() % 256 : pick % 5 == 1 ? static_cast<uint32_t>(rng()) : edgeBits(rng);
        }
    return p;
}
// The registers MAX and MINI read raw (height limits of the point light, depth clamp of the transform), set to the
// operands where the engine and an operand-normalising MAX differ or might: NaN of either sign, infinities, the
// largest finite values, denormals, signed zeros.
template <class Rng>
void spoilRawRegisters(vp::Pass& p, Rng& rng) {
    static const uint32_t kOdd[] = {0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0xFFFFFFFFu, 0x7F800000u, 0xFF800000u,
                                    0x7F7FFFFFu, 0xFF7FFFFFu, 0x00000001u, 0x807FFFFFu, 0x00000000u, 0x80000000u};
    const uint32_t row = p.kind == vp::PassPointLight ? 3u : 4u;
    const uint32_t how = static_cast<uint32_t>(rng());
    if (how & 1u)
        p.k[row][1] = kOdd[(how >> 2) % 12u];
    if (how & 2u)
        p.k[row][2] = kOdd[(how >> 6) % 12u];
}
template <class Rng>
vp::Row plausibleRow(uint32_t firstKind, Rng& rng) {
    vp::Row r{};
    const bool grid = firstKind == vp::PassGrid || firstKind == vp::PassGridFlat;
    const bool normals = firstKind == vp::PassLight || firstKind == vp::PassTexGenInt;
    for (uint32_t l = 0; l < 4; ++l) {
        r.pos[l] = bitsOf(l == 3 ? 1.0f : 900.0f * unitOf(rng));
        r.col[l] = normals ? static_cast<uint32_t>(static_cast<int32_t>(rng() % 255) - 127) : rng() % 256;
        r.st[l] = bitsOf(l == 2 ? 1.0f / (300.0f + 200.0f * unitOf(rng)) : l == 3 ? 0.0f : 8.0f * unitOf(rng));
    }
    if (grid)
        r.pos[1] = rng() % 4096;
    if (firstKind == vp::PassProject)
        r.pos[3] = bitsOf(400.0f + 300.0f * unitOf(rng));
    return r;
}
template <class Rng>
vp::Row wildRow(Rng& rng) {
    vp::Row r{};
    uint32_t* words = reinterpret_cast<uint32_t*>(&r);
    for (uint32_t w = 0; w < 12; ++w) {
        const uint32_t pick = static_cast<uint32_t>(rng());
        words[w] = pick % 4 == 0   ? static_cast<uint32_t>(static_cast<int32_t>(rng() % 513) - 256)
                   : pick % 4 == 1 ? static_cast<uint32_t>(rng())
                                   : edgeBits(rng);
    }
    return r;
}

// Appends a record of `passes` and `count` vertices. `subset`: each vertex takes a random subset of the passes
// (else all of them). The previous-frame inputs are filled in by finishPrevious().
template <class Rng>
void addRecord(Cases& c, const std::vector<vp::Pass>& passes, uint32_t count, bool wild, bool subset, Rng& rng) {
    const uint32_t first = static_cast<uint32_t>(c.passes.size());
    c.passes.insert(c.passes.end(), passes.begin(), passes.end());
    c.records.push_back({first, static_cast<uint32_t>(passes.size())});
    const uint32_t all = passes.size() >= 32 ? ~0u : (1u << passes.size()) - 1u;
    for (uint32_t i = 0; i < count; ++i) {
        GpuVertex v{};
        v.row = wild ? wildRow(rng) : plausibleRow(passes.empty() ? 0u : passes[0].kind, rng);
        v.mask = subset && (rng() % 4 != 0) ? static_cast<uint32_t>(rng()) & all : all;
        v.gridX = wild ? edgeBits(rng) : bitsOf(4000.0f * unitOf(rng));
        v.gridZ = wild ? edgeBits(rng) : bitsOf(4000.0f * unitOf(rng));
        v.base = first * vp::ir::kPassRows;
        c.vertices.push_back(v);
        c.recordOf.push_back(static_cast<uint32_t>(c.records.size()) - 1u);
    }
}

// One pass alone, per kind.
inline void addSingle(Cases& c, uint32_t recordsPerKind, uint32_t seed) {
    std::mt19937 rng(seed);
    const size_t before = c.vertices.size();
    for (const uint32_t kind : vp::ir::kBatchKinds)
        for (uint32_t r = 0; r < recordsPerKind; ++r) {
            const bool wild = (r & 1u) != 0u;
            vp::Pass p = wild ? wildPass(kind, rng) : plausiblePass(kind, rng);
            if (r % 4u >= 2u)
                spoilRawRegisters(p, rng);
            if (kind == vp::PassPointLight && !wild)
                p.param = r / 4u % (vp::kBatchMaxExponent + 2u); // every count, and one above the bound
            addRecord(c, {p}, 6, wild, false, rng);
            if (kind == vp::PassPointLight) {
                // a zero length: the vertex in the light (also as denormals and zeros of either sign), and nearly
                GpuVertex &in = c.vertices[c.vertices.size() - 1], &near = c.vertices[c.vertices.size() - 2];
                for (uint32_t l = 0; l < 3; ++l) {
                    in.row.pos[l] = (r & 2u) && l == 1 ? (rng() & 0x80000000u) | (rng() % 3u) : p.k[0][l];
                    near.row.pos[l] = p.k[0][l] + (rng() % 3u) - 1u;
                }
                if (r & 2u)
                    c.passes.back().k[0][1] = (rng() & 0x80000000u) | (rng() & 0x007FFFFFu);
            }
            if (kind == vp::PassFade || kind == vp::PassProject || kind == vp::PassTransform) {
                // 1 / w and w themselves at the edges: zero of either sign, the largest value, a denormal
                static const uint32_t kOdd[] = {0u, 0x80000000u, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x00000001u, 0x7F800000u};
                GpuVertex& v = c.vertices[c.vertices.size() - 1];
                (kind == vp::PassFade ? v.row.st[2] : v.row.pos[3]) = kOdd[r % 6u];
            }
        }
    c.single += c.vertices.size() - before;
}

// The chains a flag word of the batch path selects (journal, gpu-vertex-batch-path.md 2.2), every combination, with
// 0 to n texture matrix runs and a fade behind them.
inline void addFlagChains(Cases& c, uint32_t rounds, uint32_t seed) {
    using namespace vp;
    std::mt19937 rng(seed);
    const size_t before = c.vertices.size();
    for (uint32_t round = 0; round < rounds; ++round)
        for (uint32_t shape = 0; shape < 96; ++shape) {
            const bool wild = (round % 4u) == 3u;
            const uint32_t gridKind = shape % 3u;         // none, grid, flat grid
            const bool point = ((shape / 3u) & 1u) != 0u; // only behind a grid
            const uint32_t texGen = (shape / 6u) % 3u;    // none, from integers, from positions
            const bool lit = ((shape / 18u) & 1u) != 0u;
            const bool project = shape >= 72u && gridKind == 0u && texGen == 0u; // clip-space positions
            std::vector<uint32_t> kinds;
            if (gridKind)
                kinds.push_back(gridKind == 1u ? PassGrid : PassGridFlat);
            if (gridKind && point)
                kinds.push_back(PassPointLight);
            if (texGen == 1u && (!gridKind || point))
                kinds.push_back(PassTexGenInt);
            else if (texGen)
                kinds.push_back(PassTexGenPos);
            if (lit)
                kinds.push_back(PassLight);
            kinds.push_back(project ? PassProject : PassTransform);
            // layers: the texture matrix accumulates; the longest chain fills the record
            const uint32_t room = kBatchMaxPasses - static_cast<uint32_t>(kinds.size());
            const uint32_t layers = shape % 8u == 7u ? room - 1u : rng() % 10u;
            for (uint32_t l = 0; l < layers && kinds.size() + 1u < kBatchMaxPasses; ++l)
                kinds.push_back(PassTexMatrix);
            if (rng() % 3u != 0u)
                kinds.push_back(PassFade);
            std::vector<Pass> passes;
            for (const uint32_t kind : kinds) {
                passes.push_back(wild && rng() % 2u ? wildPass(kind, rng) : plausiblePass(kind, rng));
                if (wild && rng() % 4u == 0u)
                    spoilRawRegisters(passes.back(), rng);
            }
            addRecord(c, passes, 5, wild && rng() % 2u, false, rng);
        }
    c.flagChains += c.vertices.size() - before;
}

// Any kinds in any order, any subset per vertex; now and then a value that is no kind (such a pass changes nothing).
inline void addRandomChains(Cases& c, uint32_t records, uint32_t seed) {
    std::mt19937 rng(seed);
    const size_t before = c.vertices.size();
    for (uint32_t r = 0; r < records; ++r) {
        const uint32_t length = r % 8u == 0u ? vp::kBatchMaxPasses : 1u + rng() % vp::kBatchMaxPasses;
        const bool wild = (r % 3u) == 2u;
        std::vector<vp::Pass> passes;
        for (uint32_t i = 0; i < length; ++i) {
            const uint32_t pick = static_cast<uint32_t>(rng());
            const uint32_t kind = pick % 64u == 0u ? ((pick >> 8) & 1u ? 0u : 11u + (pick >> 9) % 5u)
                                                   : vp::ir::kBatchKinds[(pick >> 8) % 10u];
            passes.push_back(wild && (pick >> 20) % 2u ? wildPass(kind, rng) : plausiblePass(kind, rng));
            if ((pick >> 24) % 8u == 0u)
                spoilRawRegisters(passes.back(), rng);
        }
        addRecord(c, passes, 6, wild && rng() % 2u, r % 8u != 0u, rng);
    }
    c.randomChains += c.vertices.size() - before;
}

// Calls `use` for every record of a dump (u32 size, then the record) that parses.
template <class Use>
uint32_t forEachDumped(const char* path, Use&& use) {
    FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::printf("GSR_VB_RECORDS: cannot open %s\n", path);
        return 0;
    }
    std::vector<uint8_t> buf;
    uint32_t size = 0, records = 0;
    while (std::fread(&size, 4, 1, f) == 1 && size < (1u << 20)) {
        buf.resize(size);
        if (std::fread(buf.data(), 1, size, f) != size)
            break;
        vp::BatchView rec;
        if (!vp::parseBatch(buf.data(), size, rec))
            continue;
        use(rec);
        ++records;
    }
    std::fclose(f);
    return records;
}
// The files of GSR_VB_RECORDS.
inline std::vector<std::string> dumpFiles() {
    std::vector<std::string> files;
    const char* list = std::getenv("GSR_VB_RECORDS");
    if (!list)
        return files;
    std::string name;
    for (const char* at = list;; ++at) {
        if (*at == ';' || *at == 0) {
            if (!name.empty())
                files.push_back(name);
            name.clear();
            if (*at == 0)
                break;
        } else {
            name += *at;
        }
    }
    return files;
}
inline void addDumps(Cases& c) {
    for (const std::string& file : dumpFiles()) {
        const size_t before = c.vertices.size();
        const uint32_t records = forEachDumped(file.c_str(), [&](const vp::BatchView& rec) {
            const uint32_t first = static_cast<uint32_t>(c.passes.size());
            c.passes.insert(c.passes.end(), rec.passes, rec.passes + rec.header.passCount);
            c.records.push_back({first, rec.header.passCount});
            for (uint32_t i = 0; i < rec.header.vertexCount; ++i) {
                const vp::BatchVertex& b = rec.vertices[i];
                GpuVertex v{};
                v.row = b.row;
                v.mask = b.passMask;
                v.gridX = b.gridX;
                v.gridZ = b.gridZ;
                v.base = first * vp::ir::kPassRows;
                c.vertices.push_back(v);
                c.recordOf.push_back(static_cast<uint32_t>(c.records.size()) - 1u);
            }
        });
        c.recorded += c.vertices.size() - before;
        c.recordedRecords += records;
        std::printf("%u recorded batch records (%zu vertices) added from %s\n", records, c.vertices.size() - before,
                    file.c_str());
    }
}

// The previous frame of a vertex: the pass table of an earlier record (three records back, the last ones wrap), a
// pass mask valid for it, and the position row and grid values of another vertex.
inline void finishPrevious(Cases& c) {
    const uint32_t records = static_cast<uint32_t>(c.records.size());
    const size_t count = c.vertices.size();
    c.previousRecordOf.resize(count);
    for (size_t i = 0; i < count; ++i) {
        GpuVertex& v = c.vertices[i];
        const uint32_t record = (c.recordOf[i] + records - 3u) % records;
        const Record& r = c.records[record];
        const GpuVertex& other = c.vertices[(i + count - 7u) % count];
        const uint32_t all = r.passCount >= 32 ? ~0u : (1u << r.passCount) - 1u;
        std::memcpy(v.previousPos, i % 3 == 0 ? v.row.pos : other.row.pos, 16);
        v.previousMask = i % 5 == 0 ? other.mask & all : all;
        v.previousGridX = other.gridX;
        v.previousGridZ = other.gridZ;
        v.previousBase = r.firstPass * vp::ir::kPassRows;
        c.previousRecordOf[i] = record;
    }
}

inline Cases standardCases() {
    uint32_t scale = 1;
    if (const char* text = std::getenv("GSR_VB_CASES"))
        scale = static_cast<uint32_t>(std::max(1, std::atoi(text)));
    Cases c;
    addSingle(c, 500 * scale, 20261114u);
    addFlagChains(c, 63 * scale, 20261115u);
    addRandomChains(c, 10000 * scale, 20261116u);
    addDumps(c);
    finishPrevious(c);
    return c;
}
inline void describe(const Cases& c) {
    std::printf("%zu vertices in %zu records with %zu passes: %zu of one pass alone, %zu of flag-word chains, %zu of "
                "random chains, %zu recorded (%zu records)\n",
                c.vertices.size(), c.records.size(), c.passes.size(), c.single, c.flagChains, c.randomChains,
                c.recorded, c.recordedRecords);
}

// ---- the references, from gsr/vertex_batch.h alone ----
// The row after the passes of the vertex: the loop of vp::evaluateVertex.
inline vp::Row oracleRow(const vp::Pass* passes, uint32_t passCount, const vp::BatchVertex& v) {
    vp::Row r = v.row;
    for (uint32_t i = 0; i < passCount; ++i)
        if ((v.passMask >> i) & 1u)
            vp::applyPass(passes[i], r, v.gridX, v.gridZ);
    return r;
}
// The packet as the shaders return it: vp::finishRow for s, t, q, the row's own words for the rest (finishRow's
// fields are the low bits of those).
inline vp::ir::Raw oracleRaw(const vp::Row& r) {
    const vp::Output o = vp::finishRow(r);
    return {{r.pos[0] & 0xFFFFu, r.pos[1] & 0xFFFFu, r.pos[2], r.pos[3]},
            {r.col[0] & 0xFFu, r.col[1] & 0xFFu, r.col[2] & 0xFFu, r.col[3] & 0xFFu},
            {o.s, o.t, o.q, 0u}};
}
struct Reference {
    std::vector<vp::Row> rows;
    std::vector<vp::ir::Raw> raws;
    std::vector<vpcases::PreviousPlace> previous;
};
inline Reference reference(const Cases& c) {
    Reference ref;
    const size_t count = c.vertices.size();
    ref.rows.resize(count);
    ref.raws.resize(count);
    ref.previous.resize(count);
    for (size_t i = 0; i < count; ++i) {
        const GpuVertex& v = c.vertices[i];
        const Record& r = c.records[c.recordOf[i]];
        ref.rows[i] = oracleRow(c.passes.data() + r.firstPass, r.passCount, batchVertexOf(v));
        ref.raws[i] = oracleRaw(ref.rows[i]);
        const Record& p = c.records[c.previousRecordOf[i]];
        const vp::Output o = vp::evaluateVertex(c.passes.data() + p.firstPass, p.passCount, previousVertexOf(v));
        ref.previous[i] = {{o.x, o.y, o.z, 0u}};
    }
    return ref;
}

// Compares what a shader returned (kResultWords per vertex) with the references. Lane w of the texture row is not
// compared in the row: nothing reads it (the packet has no such field), so the shaders do not carry it.
inline uint64_t compareShader(const char* path, const Cases& c, const Reference& ref, const uint32_t* got) {
    uint64_t bad = 0;
    for (size_t i = 0; i < c.vertices.size(); ++i) {
        const uint32_t* g = got + i * kResultWords;
        const bool packet = std::memcmp(&ref.raws[i], g, 48) == 0;
        const bool before = std::memcmp(&ref.previous[i], g + 12, 16) == 0;
        const bool row = std::memcmp(&ref.rows[i], g + 16, 44) == 0;
        if (packet && before && row)
            continue;
        if (++bad <= 6) {
            const Record& r = c.records[c.recordOf[i]];
            std::printf("  [FAIL] %s, vertex %zu (%s%s%s; mask %08x, kinds", path, i, packet ? "" : "packet ",
                        before ? "" : "previous place ", row ? "" : "row", c.vertices[i].mask);
            for (uint32_t p = 0; p < r.passCount; ++p)
                std::printf(" %u", c.passes[r.firstPass + p].kind);
            std::printf("):");
            uint32_t w[kResultWords];
            std::memcpy(w, &ref.raws[i], 48);
            std::memcpy(w + 12, &ref.previous[i], 16);
            std::memcpy(w + 16, &ref.rows[i], 48);
            for (uint32_t k = 0; k < kResultWords; ++k)
                if (k != 27 && g[k] != w[k])
                    std::printf(" [%u] %08x!%08x", k, g[k], w[k]);
            std::printf("\n");
        }
    }
    return bad;
}

// The chains of the cost measurements: what a record of 1, 3, 7 and 13 passes typically is.
inline std::vector<uint32_t> benchKinds(uint32_t passes) {
    using namespace vp;
    std::vector<uint32_t> kinds;
    if (passes >= 3)
        kinds.push_back(PassGrid);
    if (passes >= 13)
        kinds.push_back(PassPointLight);
    if (passes >= 3)
        kinds.push_back(PassTexGenPos);
    kinds.push_back(PassTransform);
    while (kinds.size() + (passes >= 7 ? 1u : 0u) < passes)
        kinds.push_back(PassTexMatrix);
    if (passes >= 7)
        kinds.push_back(PassFade);
    return kinds;
}
inline Cases benchCases(uint32_t passes, uint32_t vertices) {
    std::mt19937 rng(20261117u + passes);
    Cases c;
    const std::vector<uint32_t> kinds = benchKinds(passes);
    for (uint32_t done = 0; done < vertices; done += 32) {
        std::vector<vp::Pass> list;
        for (const uint32_t kind : kinds)
            list.push_back(plausiblePass(kind, rng));
        addRecord(c, list, 32, false, false, rng);
    }
    c.vertices.resize(vertices);
    c.recordOf.resize(vertices);
    finishPrevious(c);
    return c;
}

} // namespace gsr::vbcases
