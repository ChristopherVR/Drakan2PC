// Batches of the vertex-program draws as a file, for the bounds tests and timings (diagnosis; private data, never
// committed). GSR_VP_BOUNDS_DUMP=<file> makes the Direct3D 11 backend append the first batches it draws (at most
// GSR_VP_BOUNDS_DUMP_COUNT, default 2,000, after GSR_VP_BOUNDS_DUMP_SKIP batches; both kinds, strip batches and
// batch-path batches, in the order drawn); tests/test_vertex_program_history.cpp reads the file given in
// GSR_VP_BOUNDS_BATCHES and checks program_draw::stripBounds and batchBounds against every vertex evaluated, and times
// them.
//
// Format: per batch a magic word, XYOFFSET x and y (12.4 fixed point), four counts, then the arrays as the batch holds
// them. Strip batch (kMagic): counts of constants, vertices, indices and records; constants, vertices, indices,
// records. Batch-path batch (kBatchMagic): counts of passes, vertices, indices and records; passes, vp::BatchVertex
// vertices, indices, records.
#pragma once
#include "gs_internal.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace gsr::program_dump {

inline constexpr uint32_t kMagic = 0x44425056u;      // "VPBD"
inline constexpr uint32_t kBatchMagic = 0x42425056u; // "VPBB"

inline void write(std::FILE* f, const VertexProgramBatch& b, int32_t offsetX, int32_t offsetY) {
    if (!f)
        return;
    if (b.batchPath) {
        const uint32_t head[7] = {kBatchMagic,   (uint32_t)offsetX, (uint32_t)offsetY, b.passCount,
                                  b.vertexCount, b.indexCount,      b.recordCount};
        std::fwrite(head, sizeof(head), 1, f);
        std::fwrite(b.passes, sizeof(vp::Pass), b.passCount, f);
        std::fwrite(b.batchVertices, sizeof(vp::BatchVertex), b.vertexCount, f);
        std::fwrite(b.indices, sizeof(uint32_t), b.indexCount, f);
        std::fwrite(b.records, sizeof(VertexProgramRecord), b.recordCount, f);
        return;
    }
    const uint32_t head[7] = {kMagic,        (uint32_t)offsetX, (uint32_t)offsetY, b.constantCount,
                              b.vertexCount, b.indexCount,      b.recordCount};
    std::fwrite(head, sizeof(head), 1, f);
    std::fwrite(b.constants, sizeof(vp::Constants), b.constantCount, f);
    std::fwrite(b.vertices, sizeof(VertexProgramVertex), b.vertexCount, f);
    std::fwrite(b.indices, sizeof(uint32_t), b.indexCount, f);
    std::fwrite(b.records, sizeof(VertexProgramRecord), b.recordCount, f);
}

// One batch read back, owning its arrays.
struct Batch {
    bool batchPath = false;
    int32_t offsetX = 0, offsetY = 0;
    std::vector<vp::Constants> constants;
    std::vector<VertexProgramVertex> vertices;
    std::vector<vp::Pass> passes;
    std::vector<vp::BatchVertex> batchVertices;
    std::vector<uint32_t> indices;
    std::vector<VertexProgramRecord> records;
    VertexProgramBatch view() const {
        VertexProgramBatch b;
        b.batchPath = batchPath;
        b.constants = constants.data();
        b.constantCount = (uint32_t)constants.size();
        b.vertices = vertices.data();
        b.vertexCount = batchPath ? (uint32_t)batchVertices.size() : (uint32_t)vertices.size();
        b.passes = passes.data();
        b.passCount = (uint32_t)passes.size();
        b.batchVertices = batchVertices.data();
        b.indices = indices.data();
        b.indexCount = (uint32_t)indices.size();
        b.records = records.data();
        b.recordCount = (uint32_t)records.size();
        return b;
    }
};

inline bool read(std::FILE* f, Batch& out) {
    uint32_t head[7];
    if (!f || std::fread(head, sizeof(head), 1, f) != 1 || (head[0] != kMagic && head[0] != kBatchMagic))
        return false;
    out.batchPath = head[0] == kBatchMagic;
    out.offsetX = (int32_t)head[1];
    out.offsetY = (int32_t)head[2];
    if (head[3] > (1u << 20) || head[4] > (1u << 24) || head[5] > (1u << 26) || head[6] > (1u << 20))
        return false;
    out.indices.resize(head[5]);
    out.records.resize(head[6]);
    bool ok;
    if (out.batchPath) {
        out.constants.clear();
        out.vertices.clear();
        out.passes.resize(head[3]);
        out.batchVertices.resize(head[4]);
        ok = std::fread(out.passes.data(), sizeof(vp::Pass), head[3], f) == head[3] &&
             std::fread(out.batchVertices.data(), sizeof(vp::BatchVertex), head[4], f) == head[4];
    } else {
        out.passes.clear();
        out.batchVertices.clear();
        out.constants.resize(head[3]);
        out.vertices.resize(head[4]);
        ok = std::fread(out.constants.data(), sizeof(vp::Constants), head[3], f) == head[3] &&
             std::fread(out.vertices.data(), sizeof(VertexProgramVertex), head[4], f) == head[4];
    }
    return ok && std::fread(out.indices.data(), sizeof(uint32_t), head[5], f) == head[5] &&
           std::fread(out.records.data(), sizeof(VertexProgramRecord), head[6], f) == head[6];
}

} // namespace gsr::program_dump
