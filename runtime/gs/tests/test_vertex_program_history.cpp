// The motion-vector `history` value of a vertex-program draw, shader against CPU, bit for bit, by stream output.
//
// On the CPU path motion::History::track computes, per vertex, {old.x * sx * ratio, old.y * sy * ratio, ratio, 1}
// with ratio = now.z / old.z from the processed vertices of two worlds, or {0, 0, 1, 0} when a limit rejects it.
// The velocity shader of a vertex-program draw (vertex_program_hlsl.h, vpHistory) evaluates the stage for the
// current vertex and its position part for the previous raw position with the previous constants and repeats those
// operations. Direct3D specifies a float division to one unit in the last place only, so that the two agree is a
// property of the adapter and is measured here: every strip vertex of the shared cases (and of GSR_VP_RECORDS) is
// paired with a previous state three times (the previous constants a few last bits away from the current ones, as
// between two frames of a moving camera; unrelated ones; none) and all four words are compared.
//
// GSR_VP_WARP=1: the software adapter; GSR_VP_ADAPTER=<n>; GSR_VP_ARITHMETIC=doubles|limbs.
#include "program_motion.h"
#include "vertex_program_draw.h"
#include "vertex_program_hlsl.h"
#include "vertex_batch_cases.h"
#include "vertex_program_cases.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace gsr;
using vpcases::GpuVertex;

namespace {
const D3D11_INPUT_ELEMENT_DESC kLayout[] = {
    {"VPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPNRM", 0, DXGI_FORMAT_R32G32B32A32_SINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPPREVPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VPPREVSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 1, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

bool makeBuffer(ID3D11Device* dev, UINT bytes, UINT bind, const void* data, ComPtr<ID3D11Buffer>& out,
                D3D11_USAGE usage = D3D11_USAGE_DEFAULT) {
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = bytes;
    bd.BindFlags = bind;
    bd.Usage = usage;
    bd.CPUAccessFlags = usage == D3D11_USAGE_STAGING ? D3D11_CPU_ACCESS_READ : 0;
    D3D11_SUBRESOURCE_DATA init{data, 0, 0};
    return SUCCEEDED(dev->CreateBuffer(&bd, data ? &init : nullptr, &out));
}

struct Pass {
    int32_t offset[2];
    float raster[2];
    uint32_t seed;
};

struct Totals {
    uint64_t pairs = 0, differ = 0, valid = 0, ratioNotOne = 0;
};

// One pass over the strip vertices with one arithmetic library. Returns false on a set-up failure.
bool run(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11VertexShader* vs, ID3D11GeometryShader* so,
         ID3D11InputLayout* layout, const std::vector<GpuVertex>& vertices, const std::vector<vp::Constants>& base,
         const Pass& pass, const char* library, Totals& totals) {
    // Constants: the current ones, then one "previous frame" copy of each with the low bits of its matrix moved.
    std::mt19937 rng(pass.seed);
    std::vector<vp::Constants> constants = base;
    constants.reserve(base.size() * 2);
    for (size_t k = 0; k < base.size(); ++k) {
        vp::Constants c = base[k];
        for (auto& row : c.mvp)
            for (uint32_t& word : row)
                if ((word & 0x7F800000u) != 0x7F800000u && (word & 0x7FFFFFFFu) != 0u)
                    word ^= rng() & 0x3FFFu; // up to 2^-10 of the value
        constants.push_back(c);
    }
    std::vector<motion::ProgramPrevious> previous(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        const GpuVertex& v = vertices[i];
        motion::ProgramPrevious& p = previous[i];
        std::memcpy(p.pos, v.in.pos, 16);
        const uint32_t kind = rng() & 15u;
        p.valid = kind != 0; // one in sixteen has no previous state
        p.constants = kind < 3 ? v.previous : (uint32_t)base.size() + v.constants; // unrelated or the moved copy
        if (kind & 4u)
            for (int l = 0; l < 3; ++l)
                if ((p.pos[l] & 0x7F800000u) != 0x7F800000u && (p.pos[l] & 0x7FFFFFFFu) != 0u)
                    p.pos[l] ^= rng() & 0xFFFu; // an animated vertex
    }
    const UINT count = (UINT)vertices.size();
    ComPtr<ID3D11Buffer> vb, pb, kb, cb, cbMotion, ob, staging;
    ComPtr<ID3D11ShaderResourceView> ksrv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)constants.size() * 16;
    const int32_t words[4] = {pass.offset[0], pass.offset[1], 0, 0};
    const float raster[4] = {pass.raster[0], pass.raster[1], 0, 0};
    if (!makeBuffer(dev, count * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, vertices.data(), vb) ||
        !makeBuffer(dev, count * sizeof(motion::ProgramPrevious), D3D11_BIND_VERTEX_BUFFER, previous.data(), pb) ||
        !makeBuffer(dev, (UINT)(constants.size() * sizeof(vp::Constants)), D3D11_BIND_SHADER_RESOURCE, constants.data(),
                    kb) ||
        FAILED(dev->CreateShaderResourceView(kb.Get(), &sd, &ksrv)) ||
        !makeBuffer(dev, 16, D3D11_BIND_CONSTANT_BUFFER, words, cb) ||
        !makeBuffer(dev, 16, D3D11_BIND_CONSTANT_BUFFER, raster, cbMotion) ||
        !makeBuffer(dev, count * 16, D3D11_BIND_STREAM_OUTPUT, nullptr, ob) ||
        !makeBuffer(dev, count * 16, 0, nullptr, staging, D3D11_USAGE_STAGING)) {
        std::printf("[FAIL] %s: buffers\n", library);
        return false;
    }
    ID3D11Buffer* streams[2] = {vb.Get(), pb.Get()};
    const UINT strides[2] = {sizeof(GpuVertex), sizeof(motion::ProgramPrevious)}, offsets[2] = {0, 0}, zero = 0;
    ID3D11Buffer* cbs[2] = {cb.Get(), cbMotion.Get()};
    ID3D11Buffer* none = nullptr;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 2, streams, strides, offsets);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->VSSetConstantBuffers(5, 2, cbs);
    ctx->VSSetShaderResources(8, 1, ksrv.GetAddressOf());
    ctx->GSSetShader(so, nullptr, 0);
    ctx->SOSetTargets(1, ob.GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    ctx->SOSetTargets(1, &none, &zero);
    ctx->CopyResource(staging.Get(), ob.Get());
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        std::printf("[FAIL] %s: read back\n", library);
        return false;
    }
    const uint32_t* got = static_cast<const uint32_t*>(ms.pData);
    uint32_t shown = 0;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const GpuVertex& v = vertices[i];
        const motion::ProgramPrevious& p = previous[i];
        const vp::Output now = vp::evaluate(v.program, constants[v.constants], v.in);
        vp::Vertex before = v.in;
        std::memcpy(before.pos, p.pos, 16);
        const vp::Output old = vp::evaluate(v.program, constants[p.constants], before);
        // the processed vertex as gs_core's kick makes it from the packet
        const auto native = [&](uint16_t fixed, int32_t offset) { return ((int)fixed - offset) * (1.0f / 16.0f); };
        const program_draw::HistoryValue want = program_draw::historyOf(
            native(now.x, pass.offset[0]), native(now.y, pass.offset[1]), (float)now.z, native(old.x, pass.offset[0]),
            native(old.y, pass.offset[1]), (float)old.z, pass.raster[0], pass.raster[1], p.valid != 0);
        ++totals.pairs;
        totals.valid += want.valid != 0;
        totals.ratioNotOne += want.valid != 0 && want.ratio != 1.0f;
        if (std::memcmp(&want, got + i * 4, 16) != 0) {
            ++totals.differ;
            if (shown++ < 4) {
                uint32_t w[4];
                std::memcpy(w, &want, 16);
                std::printf("  [FAIL] %s, vertex %zu: shader %08x %08x %08x %08x, CPU %08x %08x %08x %08x (depth now "
                            "%u, before %u)\n",
                            library, i, got[i * 4], got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], w[0], w[1], w[2],
                            w[3], now.z, old.z);
            }
        }
    }
    ctx->Unmap(staging.Get(), 0);
    return true;
}
// ---- the batch path (vbHistory, VSBatchHistoryRaw) ----
const D3D11_INPUT_ELEMENT_DESC kBatchLayout[] = {
    {"VBPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBCOL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

// One pass over the batch vertices with one library. Every vertex gets a previous state of one of three kinds: its
// own record's passes with the low bits of every matrix and origin moved (a moving camera; the raw row also moved for
// one in eight, an animated vertex), the unrelated record vbcases::finishPrevious chose, or none (bit 31 clear).
bool runBatch(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11VertexShader* vs, ID3D11GeometryShader* so,
              ID3D11InputLayout* layout, const vbcases::Cases& cases, const Pass& pass, const char* library,
              Totals& totals) {
    constexpr uint32_t kRows = vp::ir::kPassRows;
    std::mt19937 rng(pass.seed);
    std::vector<vp::Pass> table = cases.passes;
    const uint32_t moved = (uint32_t)table.size();
    for (size_t k = 0; k < cases.passes.size(); ++k) {
        vp::Pass p = cases.passes[k];
        if (p.kind == vp::PassTransform || p.kind == vp::PassGrid || p.kind == vp::PassGridFlat)
            for (int r = 0; r < 4; ++r)
                for (uint32_t& word : p.k[r])
                    if ((word & 0x7F800000u) != 0x7F800000u && (word & 0x7FFFFFFFu) != 0u)
                        word ^= rng() & 0x3FFFu;
        table.push_back(p);
    }
    std::vector<vbcases::GpuVertex> vertices = cases.vertices;
    for (size_t i = 0; i < vertices.size(); ++i) {
        vbcases::GpuVertex& v = vertices[i];
        const uint32_t kind = rng() & 15u;
        if (kind >= 3) {
            const vbcases::Record& r = cases.records[cases.recordOf[i]];
            v.previousBase = (moved + r.firstPass) * kRows;
            v.previousMask = v.mask;
            v.previousGridX = v.gridX;
            v.previousGridZ = v.gridZ;
            std::memcpy(v.previousPos, v.row.pos, 16);
            if (kind & 8u && (kind & 4u))
                for (int l = 0; l < 3; ++l)
                    if ((v.previousPos[l] & 0x7F800000u) != 0x7F800000u && (v.previousPos[l] & 0x7FFFFFFFu) != 0u)
                        v.previousPos[l] ^= rng() & 0xFFFu;
        }
        if (kind != 0)
            v.previousMask |= motion::kBatchPreviousKnown;
    }
    const UINT count = (UINT)vertices.size();
    ComPtr<ID3D11Buffer> vb, kb, cb, cbMotion, ob, staging;
    ComPtr<ID3D11ShaderResourceView> ksrv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)table.size() * kRows;
    const int32_t words[4] = {pass.offset[0], pass.offset[1], 0, 0};
    const float raster[4] = {pass.raster[0], pass.raster[1], 0, 0};
    if (!makeBuffer(dev, count * sizeof(vbcases::GpuVertex), D3D11_BIND_VERTEX_BUFFER, vertices.data(), vb) ||
        !makeBuffer(dev, (UINT)(table.size() * sizeof(vp::Pass)), D3D11_BIND_SHADER_RESOURCE, table.data(), kb) ||
        FAILED(dev->CreateShaderResourceView(kb.Get(), &sd, &ksrv)) ||
        !makeBuffer(dev, 16, D3D11_BIND_CONSTANT_BUFFER, words, cb) ||
        !makeBuffer(dev, 16, D3D11_BIND_CONSTANT_BUFFER, raster, cbMotion) ||
        !makeBuffer(dev, count * 16, D3D11_BIND_STREAM_OUTPUT, nullptr, ob) ||
        !makeBuffer(dev, count * 16, 0, nullptr, staging, D3D11_USAGE_STAGING)) {
        std::printf("[FAIL] %s: batch buffers\n", library);
        return false;
    }
    const UINT stride = sizeof(vbcases::GpuVertex), offset = 0, zero = 0;
    ID3D11Buffer* cbs[2] = {cb.Get(), cbMotion.Get()};
    ID3D11Buffer* none = nullptr;
    ctx->IASetInputLayout(layout);
    ctx->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(vs, nullptr, 0);
    ctx->VSSetConstantBuffers(5, 2, cbs);
    ctx->VSSetShaderResources(8, 1, ksrv.GetAddressOf());
    ctx->GSSetShader(so, nullptr, 0);
    ctx->SOSetTargets(1, ob.GetAddressOf(), &zero);
    ctx->Draw(count, 0);
    ctx->SOSetTargets(1, &none, &zero);
    ctx->CopyResource(staging.Get(), ob.Get());
    D3D11_MAPPED_SUBRESOURCE ms{};
    if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        std::printf("[FAIL] %s: batch read back\n", library);
        return false;
    }
    const uint32_t* got = static_cast<const uint32_t*>(ms.pData);
    uint32_t shown = 0;
    const auto native = [&](uint16_t fixed, int32_t off) { return ((int)fixed - off) * (1.0f / 16.0f); };
    for (size_t i = 0; i < vertices.size(); ++i) {
        const vbcases::GpuVertex& v = vertices[i];
        const vbcases::Record& r = cases.records[cases.recordOf[i]];
        const vp::Output now = vp::evaluateVertex(table.data() + r.firstPass, r.passCount, vbcases::batchVertexOf(v));
        vp::BatchVertex before = vbcases::previousVertexOf(v);
        before.passMask &= ~motion::kBatchPreviousKnown;
        const vp::Output old = vp::evaluateVertex(table.data() + v.previousBase / kRows, vp::kBatchMaxPasses, before);
        const program_draw::HistoryValue want = program_draw::historyOf(
            native(now.x, pass.offset[0]), native(now.y, pass.offset[1]), (float)now.z, native(old.x, pass.offset[0]),
            native(old.y, pass.offset[1]), (float)old.z, pass.raster[0], pass.raster[1],
            (v.previousMask & motion::kBatchPreviousKnown) != 0);
        ++totals.pairs;
        totals.valid += want.valid != 0;
        totals.ratioNotOne += want.valid != 0 && want.ratio != 1.0f;
        if (std::memcmp(&want, got + i * 4, 16) != 0) {
            ++totals.differ;
            if (shown++ < 4) {
                uint32_t w[4];
                std::memcpy(w, &want, 16);
                std::printf("  [FAIL] %s, batch vertex %zu: shader %08x %08x %08x %08x, CPU %08x %08x %08x %08x\n",
                            library, i, got[i * 4], got[i * 4 + 1], got[i * 4 + 2], got[i * 4 + 3], w[0], w[1], w[2],
                            w[3]);
            }
        }
    }
    ctx->Unmap(staging.Get(), 0);
    return true;
}
// GSR_VP_BOUNDS_BATCH=<n>: the largest batch of the bounds checks (default: the given one), to time them at the batch
// sizes of a game.
uint32_t boundsBatch(uint32_t fallback) {
    const char* e = std::getenv("GSR_VP_BOUNDS_BATCH");
    const uint32_t n = e ? (uint32_t)std::strtoul(e, nullptr, 10) : 0u;
    return n ? n : fallback;
}
} // namespace

int main() {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    const bool warp = std::getenv("GSR_VP_WARP") != nullptr;
    ComPtr<IDXGIAdapter> chosen;
    if (const char* index = std::getenv("GSR_VP_ADAPTER")) {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapters((UINT)std::atoi(index), &chosen))) {
            std::printf("no adapter %s: skipped\n", index);
            return 0;
        }
    }
    const D3D_DRIVER_TYPE type = chosen ? D3D_DRIVER_TYPE_UNKNOWN
                                 : warp ? D3D_DRIVER_TYPE_WARP
                                        : D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(D3D11CreateDevice(chosen.Get(), type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
        std::printf("no Direct3D 11 device: skipped\n");
        return 0;
    }
    char adapterName[160] = "?";
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(dev.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetDesc(&desc)))
            std::snprintf(adapterName, sizeof(adapterName), "%ls", desc.Description);
    }
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    const bool doubleSupport = SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
                               doubles.DoublePrecisionFloatShaderOps;
    std::printf("Direct3D 11 on %s\n", adapterName);

    const vpcases::StandardCases standard = vpcases::standardCases();
    std::vector<GpuVertex> strips;
    for (const GpuVertex& v : standard.cases.vertices)
        if (v.program == vp::StripsFlat || v.program == vp::StripsLit)
            strips.push_back(v);
    std::printf("%zu strip vertices (%zu of them recorded)\n", strips.size(), standard.recorded);

    // XYOFFSET of the game's frame (1728, 1824 in pixels) and of none; the raster scales of the internal scales 3
    // and 8 with a reduced render rectangle, and 1.
    const Pass passes[3] = {{{1728 * 16, 1824 * 16}, {5.3333335f, 5.3333335f}, 11u},
                            {{1728 * 16, 1824 * 16}, {2.0f, 2.0010417f}, 12u},
                            {{0, 0}, {1.0f, 1.0f}, 13u}};
    const vp::ArithmeticRequest only = vp::parseArithmeticRequest(std::getenv("GSR_VP_ARITHMETIC"));
    uint64_t bad = 0;
    for (const vp::ArithmeticRequest request : {vp::ArithmeticRequest::Limbs, vp::ArithmeticRequest::Doubles}) {
        if (only != vp::ArithmeticRequest::Auto && only != request)
            continue;
        const vp::Arithmetic arithmetic = vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport, request);
        if (arithmetic == vp::Arithmetic::None)
            continue;
        const char* library = vp::arithmeticName(arithmetic);
        const std::string text = vertexProgramHlsl(arithmetic, vp::gameStages(), true);
        // The renderer's flags (d3d11_vertex_program.cpp): no strictness flag, `precise` in the text carries it.
        const D3D_SHADER_MACRO macros[] = {{"VP_RAW_ONLY", "1"}, {"VP_MOTION", "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3DCompile(text.c_str(), text.size(), "vertex_program_history", macros, nullptr,
                              "VSProgramHistoryRaw", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
            std::printf("[FAIL] %s: compile: %s\n", library, err ? (const char*)err->GetBufferPointer() : "?");
            return 1;
        }
        static const D3D11_SO_DECLARATION_ENTRY kOut[] = {{0, "RAWHISTORY", 0, 0, 4, 0}};
        const UINT outStride = 16;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11GeometryShader> so;
        ComPtr<ID3D11InputLayout> layout;
        if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) ||
            FAILED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), kOut, 1,
                                                             &outStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                             &so)) ||
            FAILED(dev->CreateInputLayout(kLayout, 6, blob->GetBufferPointer(), blob->GetBufferSize(), &layout))) {
            std::printf("[FAIL] %s: the device refused the shader\n", library);
            return 1;
        }
        Totals totals;
        for (const Pass& pass : passes)
            if (!run(dev.Get(), ctx.Get(), vs.Get(), so.Get(), layout.Get(), strips, standard.cases.constants, pass,
                     library, totals))
                return 1;
        std::printf("%s: history value, shader against CPU: %llu vertex pairs (%llu with a valid history, %llu of "
                    "those with a depth ratio other than 1), %llu differ\n",
                    library, (unsigned long long)totals.pairs, (unsigned long long)totals.valid,
                    (unsigned long long)totals.ratioNotOne, (unsigned long long)totals.differ);
        bad += totals.differ;
        if (totals.ratioNotOne < totals.pairs / 20) {
            std::printf("[FAIL] %s: the cases do not exercise the division\n", library);
            ++bad;
        }
    }
    {
        // program_draw::stripBounds (estimate, then the possible extremes exactly) against vp::evaluate of every
        // vertex, over batches of 1 to 200 strip vertices with the triangles (i, i + 1, i + 2); one record per run of
        // vertices with the same constants. Also its cost per bounded vertex, against evaluating every vertex.
        std::vector<VertexProgramVertex> vertices(strips.size());
        const size_t recordedFrom = strips.size() - standard.recorded;
        for (size_t i = 0; i < strips.size(); ++i)
            vertices[i] = VertexProgramVertex{strips[i].in, strips[i].constants, strips[i].program, {0, 0}};
        std::mt19937 rng(78u);
        program_draw::BoundsScratch scratch;
        uint64_t batches = 0, differ = 0, bounded = 0, exactly = 0;
        double boundsSeconds = 0, everySeconds = 0;
        // the same for the batches of recorded game vertices alone (the cases put them last)
        double gameSeconds = 0;
        uint64_t gameBounded = 0, gameExactly = 0;
        for (size_t first = 0; first < vertices.size();) {
            const uint32_t n = std::min<uint32_t>(1u + rng() % boundsBatch(200u), (uint32_t)(vertices.size() - first));
            std::vector<uint32_t> indices;
            for (uint32_t i = 0; i + 2 < n; ++i)
                for (uint32_t c = 0; c < 3; ++c)
                    indices.push_back(i + c);
            std::vector<VertexProgramRecord> records;
            for (uint32_t i = 0; i < n; ++i) {
                if (records.empty() || records.back().constants != vertices[first + i].constants) {
                    VertexProgramRecord r;
                    r.firstVertex = i;
                    r.constants = vertices[first + i].constants;
                    r.program = vertices[first + i].program;
                    records.push_back(r);
                }
                ++records.back().vertexCount;
            }
            VertexProgramBatch b;
            b.records = records.data();
            b.recordCount = (uint32_t)records.size();
            b.vertices = vertices.data() + first;
            b.vertexCount = n;
            b.constants = standard.cases.constants.data();
            b.constantCount = (uint32_t)standard.cases.constants.size();
            b.indices = indices.data();
            b.indexCount = (uint32_t)indices.size();
            const int32_t ox = (int32_t)(rng() % 65536u), oy = (int32_t)(rng() % 65536u);
            // the shortest of five calls: the machine is shared, the shortest is the one not interrupted
            program_draw::Bounds got;
            double best = 1e30;
            for (int rep = 0; rep < 5; ++rep) {
                const auto r0 = std::chrono::steady_clock::now();
                got = program_draw::stripBounds(b, ox, oy, scratch);
                best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - r0).count());
            }
            const auto t1 = std::chrono::steady_clock::now();
            int32_t want[4] = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
            for (uint32_t i = 0; i < (n >= 3 ? n : 0); ++i) {
                const VertexProgramVertex& v = b.vertices[i];
                const vp::Output o = vp::evaluate(v.program, b.constants[v.constants], v.in);
                want[0] = std::min(want[0], (int32_t)o.x);
                want[1] = std::min(want[1], (int32_t)o.y);
                want[2] = std::max(want[2], (int32_t)o.x);
                want[3] = std::max(want[3], (int32_t)o.y);
            }
            const auto t2 = std::chrono::steady_clock::now();
            boundsSeconds += best;
            everySeconds += std::chrono::duration<double>(t2 - t1).count();
            const bool any = want[0] != INT32_MAX;
            const bool same =
                got.any == any &&
                (!any || (got.x0 == (want[0] - ox) * (1.0f / 16.0f) && got.y0 == (want[1] - oy) * (1.0f / 16.0f) &&
                          got.x1 == (want[2] - ox) * (1.0f / 16.0f) && got.y1 == (want[3] - oy) * (1.0f / 16.0f)));
            if (!same && differ < 4)
                std::printf("  [FAIL] strip bounds at vertex %zu: got %g %g %g %g, want %g %g %g %g\n", first, got.x0,
                            got.y0, got.x1, got.y1, (want[0] - ox) / 16.0, (want[1] - oy) / 16.0, (want[2] - ox) / 16.0,
                            (want[3] - oy) / 16.0);
            differ += !same;
            ++batches;
            bounded += n >= 3 ? n : 0;
            exactly += got.exact;
            if (first >= recordedFrom) {
                gameSeconds += best;
                gameBounded += n >= 3 ? n : 0;
                gameExactly += got.exact;
            }
            first += n;
        }
        std::printf("strip bounds: %llu batches (%llu vertices, %llu of them evaluated exactly), %llu differ; %.1f ns "
                    "per vertex (every vertex evaluated: %.1f ns)\n",
                    (unsigned long long)batches, (unsigned long long)bounded, (unsigned long long)exactly,
                    (unsigned long long)differ, bounded ? boundsSeconds * 1e9 / (double)bounded : 0.0,
                    bounded ? everySeconds * 1e9 / (double)bounded : 0.0);
        std::printf("%s bounds of the recorded game vertices alone: %llu vertices, %llu evaluated exactly, %.1f ns per "
                    "vertex\n",
                    "strip", (unsigned long long)gameBounded, (unsigned long long)gameExactly,
                    gameBounded ? gameSeconds * 1e9 / (double)gameBounded : 0.0);
        bad += differ;
    }
    // The batch path: every vertex of the batch cases (and of GSR_VB_RECORDS).
    const vbcases::Cases batchCases = vbcases::standardCases();
    std::printf("batch path: ");
    vbcases::describe(batchCases);
    {
        // program_draw::batchBounds (estimate, then the possible extremes exactly) against the exact evaluation of
        // every vertex, over batches of 1 to 64 vertices of the cases with the triangles (i, i + 1, i + 2).
        std::vector<vp::BatchVertex> vertices(batchCases.vertices.size());
        const size_t recordedFrom = vertices.size() - batchCases.recorded;
        for (size_t i = 0; i < vertices.size(); ++i) {
            vertices[i] = vbcases::batchVertexOf(batchCases.vertices[i]);
            vertices[i].slot = batchCases.vertices[i].base;
        }
        std::mt19937 rng(77u);
        program_draw::BoundsScratch scratch;
        uint64_t batches = 0, differ = 0, bounded = 0, exactly = 0;
        double boundsSeconds = 0, everySeconds = 0;
        // the same for the batches of recorded game vertices alone (the cases put them last)
        double gameSeconds = 0;
        uint64_t gameBounded = 0, gameExactly = 0;
        for (size_t first = 0; first < vertices.size();) {
            const uint32_t n = std::min<uint32_t>(1u + rng() % boundsBatch(64u), (uint32_t)(vertices.size() - first));
            std::vector<uint32_t> indices;
            for (uint32_t i = 0; i + 2 < n; ++i)
                for (uint32_t c = 0; c < 3; ++c)
                    indices.push_back(i + c);
            // one record per run of vertices of one case record (a record's vertices are contiguous)
            std::vector<VertexProgramRecord> records;
            for (uint32_t i = 0; i < n; ++i) {
                const vbcases::Record& cr = batchCases.records[batchCases.recordOf[first + i]];
                if (records.empty() || records.back().constants != cr.firstPass) {
                    VertexProgramRecord r;
                    r.firstVertex = i;
                    r.constants = cr.firstPass;
                    r.passCount = cr.passCount;
                    r.program = vp::Batch;
                    records.push_back(r);
                }
                ++records.back().vertexCount;
            }
            VertexProgramBatch b;
            b.batchPath = true;
            b.records = records.data();
            b.recordCount = (uint32_t)records.size();
            b.batchVertices = vertices.data() + first;
            b.vertexCount = n;
            b.passes = batchCases.passes.data();
            b.passCount = (uint32_t)batchCases.passes.size();
            b.indices = indices.data();
            b.indexCount = (uint32_t)indices.size();
            const int32_t ox = (int32_t)(rng() % 65536u), oy = (int32_t)(rng() % 65536u);
            // the shortest of five calls: the machine is shared, the shortest is the one not interrupted
            program_draw::Bounds got;
            double best = 1e30;
            for (int rep = 0; rep < 5; ++rep) {
                const auto r0 = std::chrono::steady_clock::now();
                got = program_draw::batchBounds(b, ox, oy, scratch);
                best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - r0).count());
            }
            const auto t1 = std::chrono::steady_clock::now();
            int32_t want[4] = {INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN};
            for (uint32_t i = 0; i < (n >= 3 ? n : 0); ++i) {
                const vp::BatchVertex& v = b.batchVertices[i];
                const vp::Output o = vp::evaluateVertex(b.passes + v.slot / vp::ir::kPassRows, vp::kBatchMaxPasses, v);
                want[0] = std::min(want[0], (int32_t)o.x);
                want[1] = std::min(want[1], (int32_t)o.y);
                want[2] = std::max(want[2], (int32_t)o.x);
                want[3] = std::max(want[3], (int32_t)o.y);
            }
            const auto t2 = std::chrono::steady_clock::now();
            boundsSeconds += best;
            everySeconds += std::chrono::duration<double>(t2 - t1).count();
            const bool any = want[0] != INT32_MAX;
            const bool same =
                got.any == any &&
                (!any || (got.x0 == (want[0] - ox) * (1.0f / 16.0f) && got.y0 == (want[1] - oy) * (1.0f / 16.0f) &&
                          got.x1 == (want[2] - ox) * (1.0f / 16.0f) && got.y1 == (want[3] - oy) * (1.0f / 16.0f)));
            if (!same && differ < 4)
                std::printf("  [FAIL] batch bounds at vertex %zu: got %g %g %g %g, want %g %g %g %g\n", first, got.x0,
                            got.y0, got.x1, got.y1, (want[0] - ox) / 16.0, (want[1] - oy) / 16.0, (want[2] - ox) / 16.0,
                            (want[3] - oy) / 16.0);
            differ += !same;
            ++batches;
            bounded += n >= 3 ? n : 0;
            exactly += got.exact;
            if (first >= recordedFrom) {
                gameSeconds += best;
                gameBounded += n >= 3 ? n : 0;
                gameExactly += got.exact;
            }
            first += n;
        }
        std::printf("batch bounds: %llu batches (%llu vertices, %llu of them evaluated exactly), %llu differ; %.1f ns "
                    "per vertex (every vertex evaluated: %.1f ns)\n",
                    (unsigned long long)batches, (unsigned long long)bounded, (unsigned long long)exactly,
                    (unsigned long long)differ, bounded ? boundsSeconds * 1e9 / (double)bounded : 0.0,
                    bounded ? everySeconds * 1e9 / (double)bounded : 0.0);
        std::printf("%s bounds of the recorded game vertices alone: %llu vertices, %llu evaluated exactly, %.1f ns per "
                    "vertex\n",
                    "batch", (unsigned long long)gameBounded, (unsigned long long)gameExactly,
                    gameBounded ? gameSeconds * 1e9 / (double)gameBounded : 0.0);
        bad += differ;
    }
    for (const vp::ArithmeticRequest request : {vp::ArithmeticRequest::Limbs, vp::ArithmeticRequest::Doubles}) {
        if (only != vp::ArithmeticRequest::Auto && only != request)
            continue;
        const vp::Arithmetic arithmetic = vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport, request);
        if (arithmetic == vp::Arithmetic::None)
            continue;
        const char* library = vp::arithmeticName(arithmetic);
        const std::string text = vertexBatchHlsl(arithmetic, true);
        const D3D_SHADER_MACRO macros[] = {
            {"VP_RAW_ONLY", "1"}, {"VP_MOTION", "1"}, {"VP_PREVIOUS", "1"}, {nullptr, nullptr}};
        ComPtr<ID3DBlob> blob, err;
        if (FAILED(D3DCompile(text.c_str(), text.size(), "vertex_batch_history", macros, nullptr, "VSBatchHistoryRaw",
                              "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &err))) {
            std::printf("[FAIL] %s: batch compile: %s\n", library, err ? (const char*)err->GetBufferPointer() : "?");
            return 1;
        }
        static const D3D11_SO_DECLARATION_ENTRY kOut[] = {{0, "RAWHISTORY", 0, 0, 4, 0}};
        const UINT outStride = 16;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11GeometryShader> so;
        ComPtr<ID3D11InputLayout> layout;
        if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) ||
            FAILED(dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(), kOut, 1,
                                                             &outStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                             &so)) ||
            FAILED(dev->CreateInputLayout(kBatchLayout, 6, blob->GetBufferPointer(), blob->GetBufferSize(), &layout))) {
            std::printf("[FAIL] %s: the device refused the batch shader\n", library);
            return 1;
        }
        Totals totals;
        for (const Pass& pass : passes)
            if (!runBatch(dev.Get(), ctx.Get(), vs.Get(), so.Get(), layout.Get(), batchCases, pass, library, totals))
                return 1;
        std::printf("%s: batch path history value, shader against CPU: %llu vertex pairs (%llu with a valid history, "
                    "%llu of those with a depth ratio other than 1), %llu differ\n",
                    library, (unsigned long long)totals.pairs, (unsigned long long)totals.valid,
                    (unsigned long long)totals.ratioNotOne, (unsigned long long)totals.differ);
        bad += totals.differ;
        if (totals.ratioNotOne < totals.pairs / 20) {
            std::printf("[FAIL] %s: the batch cases do not exercise the division\n", library);
            ++bad;
        }
    }
    std::printf("history values: %llu differ in total\n", (unsigned long long)bad);
    return bad ? 1 : 0;
}
