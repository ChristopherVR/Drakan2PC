// The generated HLSL of the batch path (src/vertex_program_hlsl.h: vertexBatchHlsl; pass bodies, loop and library
// from the one description, gsr/vertex_batch_stage.h) against gsr/vertex_batch.h, the CPU evaluation proven against
// the microprogram, bit for bit. The shader runs over the shared test vertices (tests/vertex_batch_cases.h) with
// stream output and returns per vertex the packet (position, depth, fog lane, colour, the bits of s, t and q), the
// position of the previous frame's inputs (the motion-vector twin) and the row after the chain. Both arithmetic
// libraries are run: doubles (when the adapter has double support in shaders) and integer limbs (always).
// tests/test_vertex_batch_webgpu.cpp is the same suite for the generated WGSL.
//
// GSR_VB_RECORDS=<file>[;<file>...] adds records dumped from VU1 recordings (private data; passes without).
// GSR_VP_WARP=1 runs on the software adapter, GSR_VP_ADAPTER=<n> on the n-th adapter of the machine.
// GSR_VP_ARITHMETIC=doubles | limbs runs one library only. GSR_VB_STEP=<n> takes every n-th vertex (the software
// adapter is slow). GSR_VB_COST=1 also compiles what a renderer compiles (the raw entry point without the test's
// outputs, the draw's entry point) and prints FXC's time and the size. GSR_VB_BENCH=<repeats> times chains of 1, 3,
// 7 and 13 passes (shader invocations per millisecond, stream output). GSR_VP_DUMP=<directory> writes the generated
// HLSL there. Without a Direct3D 11 device the test is skipped (exit 77).
#include "gs_shaders.h"
#include "vertex_batch_cases.h"
#include "vertex_program_hlsl.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <string>

#pragma comment(lib, "dxgi")

using Microsoft::WRL::ComPtr;
using namespace gsr;
using vbcases::Cases;
using vbcases::GpuVertex;

namespace {
double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void dump(const std::string& name, const void* data, size_t size) {
    const char* dir = std::getenv("GSR_VP_DUMP");
    if (!dir)
        return;
    if (FILE* f = std::fopen((std::string(dir) + "/" + name).c_str(), "wb")) {
        std::fwrite(data, 1, size, f);
        std::fclose(f);
    }
}
const D3D11_INPUT_ELEMENT_DESC kBatchLayout[] = {
    {"VBPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBCOL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBST", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPPOS", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 64, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"VBPSEL", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 80, D3D11_INPUT_PER_VERTEX_DATA, 0},
};
constexpr UINT kChunk = 262144; // vertices per draw

struct Gpu {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    bool buffer(UINT bytes, UINT bind, const void* data, ComPtr<ID3D11Buffer>& out,
                D3D11_USAGE usage = D3D11_USAGE_DEFAULT) const {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = bytes;
        bd.BindFlags = bind;
        bd.Usage = usage;
        bd.CPUAccessFlags = usage == D3D11_USAGE_STAGING ? D3D11_CPU_ACCESS_READ : 0;
        D3D11_SUBRESOURCE_DATA init{data, 0, 0};
        return SUCCEEDED(dev->CreateBuffer(&bd, data ? &init : nullptr, &out));
    }
    // The constants table of the cases: every pass as it lies in a record, thirteen rows each.
    bool passTable(const Cases& cases, ComPtr<ID3D11Buffer>& table, ComPtr<ID3D11ShaderResourceView>& view) const {
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = DXGI_FORMAT_R32G32B32A32_UINT;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = (UINT)cases.passes.size() * vp::ir::kPassRows;
        return buffer((UINT)(cases.passes.size() * sizeof(vp::Pass)), D3D11_BIND_SHADER_RESOURCE, cases.passes.data(),
                      table) &&
               SUCCEEDED(dev->CreateShaderResourceView(table.Get(), &sd, &view));
    }
    // Blocks until everything submitted so far has run.
    void finish() const {
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> query;
        if (FAILED(dev->CreateQuery(&qd, &query)))
            return;
        ctx->End(query.Get());
        ctx->Flush();
        BOOL done = FALSE;
        while (ctx->GetData(query.Get(), &done, sizeof(done), 0) != S_OK || !done) {
        }
    }
};

ComPtr<ID3DBlob> compile(const char* library, const char* what, const std::string& text, const char* entry,
                         const D3D_SHADER_MACRO* macros, UINT flags) {
    ComPtr<ID3DBlob> blob, err;
    const double start = milliseconds();
    if (FAILED(D3DCompile(text.c_str(), text.size(), "vertex_batch", macros, nullptr, entry, "vs_5_0", flags, 0, &blob,
                          &err))) {
        std::printf("[FAIL] %s: compile of %s: %s\n", library, what, err ? (const char*)err->GetBufferPointer() : "?");
        return {};
    }
    std::printf("%s: %s: %zu characters of HLSL, compiled by FXC in %.0f ms to %zu bytes\n", library, what, text.size(),
                milliseconds() - start, (size_t)blob->GetBufferSize());
    return blob;
}

// A stream-output pipeline of the raw entry point: `outWords` integers per vertex.
struct Pipeline {
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11GeometryShader> so;
    ComPtr<ID3D11InputLayout> layout;
    UINT stride = 0;
    bool create(const Gpu& gpu, ID3DBlob* blob, bool previous, bool row) {
        std::vector<D3D11_SO_DECLARATION_ENTRY> out = {
            {0, "RAWPLACE", 0, 0, 4, 0}, {0, "RAWCOLOUR", 0, 0, 4, 0}, {0, "RAWSTQ", 0, 0, 4, 0}};
        if (previous)
            out.push_back({0, "RAWPREVIOUS", 0, 0, 4, 0});
        if (row)
            for (const char* name : {"RAWROWPOS", "RAWROWCOL", "RAWROWST"})
                out.push_back({0, name, 0, 0, 4, 0});
        stride = (UINT)out.size() * 16u;
        return SUCCEEDED(gpu.dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)) &&
               SUCCEEDED(gpu.dev->CreateGeometryShaderWithStreamOutput(blob->GetBufferPointer(), blob->GetBufferSize(),
                                                                       out.data(), (UINT)out.size(), &stride, 1,
                                                                       D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &so)) &&
               SUCCEEDED(gpu.dev->CreateInputLayout(kBatchLayout, previous ? 6 : 4, blob->GetBufferPointer(),
                                                    blob->GetBufferSize(), &layout));
    }
    void bind(const Gpu& gpu, ID3D11Buffer* vb, ID3D11ShaderResourceView* table) const {
        const UINT vertexStride = sizeof(GpuVertex), zero = 0;
        gpu.ctx->IASetInputLayout(layout.Get());
        gpu.ctx->IASetVertexBuffers(0, 1, &vb, &vertexStride, &zero);
        gpu.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        gpu.ctx->VSSetShader(vs.Get(), nullptr, 0);
        gpu.ctx->VSSetShaderResources(8, 1, &table);
        gpu.ctx->GSSetShader(so.Get(), nullptr, 0);
    }
};

// Chains of 1, 3, 7 and 13 passes, 262144 vertices per draw. Eight draws append to one stream-output buffer (no
// draw's output is overwritten before it is read), then the last vertex of the last draw is read back and checked
// against the CPU evaluation, so the time covers finished, correct work. A run is as many such rounds as take a tenth
// of a second or more.
void bench(const Gpu& gpu, const char* library, const Pipeline& pipeline, const char* what, int repeats) {
    constexpr UINT kRound = 8; // draws per stream-output buffer
    for (const uint32_t passes : {1u, 3u, 7u, 13u}) {
        const Cases cases = vbcases::benchCases(passes, kChunk);
        const vbcases::Record& record = cases.records[cases.recordOf[kChunk - 1]];
        const vp::ir::Raw want =
            vbcases::oracleRaw(vbcases::oracleRow(cases.passes.data() + record.firstPass, record.passCount,
                                                  vbcases::batchVertexOf(cases.vertices[kChunk - 1])));
        ComPtr<ID3D11Buffer> vb, out, table, staging;
        ComPtr<ID3D11ShaderResourceView> view;
        if (!gpu.buffer(kChunk * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, cases.vertices.data(), vb) ||
            !gpu.buffer(kRound * kChunk * pipeline.stride, D3D11_BIND_STREAM_OUTPUT, nullptr, out) ||
            !gpu.buffer(pipeline.stride, 0, nullptr, staging, D3D11_USAGE_STAGING) ||
            !gpu.passTable(cases, table, view))
            return;
        pipeline.bind(gpu, vb.Get(), view.Get());
        const UINT zero = 0;
        ID3D11Buffer* none = nullptr;
        bool wrong = false;
        const auto run = [&](UINT rounds) {
            const double start = milliseconds();
            for (UINT round = 0; round < rounds; ++round) {
                gpu.ctx->SOSetTargets(1, out.GetAddressOf(), &zero);
                for (UINT d = 0; d < kRound; ++d)
                    gpu.ctx->Draw(kChunk, 0);
                gpu.ctx->SOSetTargets(1, &none, &zero);
                const D3D11_BOX last{(kRound * kChunk - 1) * pipeline.stride, 0, 0,
                                     kRound * kChunk * pipeline.stride,       1, 1};
                gpu.ctx->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, out.Get(), 0, &last);
                D3D11_MAPPED_SUBRESOURCE ms{};
                if (SUCCEEDED(gpu.ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
                    wrong = wrong || std::memcmp(ms.pData, &want, 48) != 0;
                    gpu.ctx->Unmap(staging.Get(), 0);
                } else {
                    wrong = true;
                }
            }
            return milliseconds() - start;
        };
        UINT rounds = 1;
        while (run(rounds) < 100.0 && rounds < 1024)
            rounds *= 2;
        const UINT draws = rounds * kRound;
        std::vector<double> rates;
        for (int r = 0; r < repeats; ++r)
            rates.push_back(double(kChunk) * draws / run(rounds));
        std::sort(rates.begin(), rates.end());
        // the adapter's own count of vertex shader invocations over one more run
        uint64_t counted = 0;
        {
            D3D11_QUERY_DESC qd{D3D11_QUERY_PIPELINE_STATISTICS, 0};
            ComPtr<ID3D11Query> query;
            D3D11_QUERY_DATA_PIPELINE_STATISTICS statistics{};
            if (SUCCEEDED(gpu.dev->CreateQuery(&qd, &query))) {
                gpu.ctx->Begin(query.Get());
                run(rounds);
                gpu.ctx->End(query.Get());
                while (gpu.ctx->GetData(query.Get(), &statistics, sizeof(statistics), 0) != S_OK) {
                }
                counted = statistics.VSInvocations;
            }
        }
        std::printf("%s, %s: chain of %2u passes, %.0f invocations per ms (median of %d runs of %u x %u; lowest %.0f, "
                    "highest %.0f; the adapter counts %llu invocations in a run of %llu)%s\n",
                    library, what, passes, rates[rates.size() / 2], repeats, draws, kChunk, rates.front(), rates.back(),
                    (unsigned long long)counted, (unsigned long long)draws * kChunk,
                    wrong ? " [FAIL] the last vertex read back differs from the CPU evaluation" : "");
    }
}

// One arithmetic library: compile, run every vertex with stream output, compare. Returns the differing vertices
// (or 1 after a failure).
uint64_t runLibrary(const Gpu& gpu, vp::Arithmetic arithmetic, const Cases& cases, const vbcases::Reference& ref,
                    ID3D11ShaderResourceView* table) {
    ID3D11DeviceContext* ctx = gpu.ctx.Get();
    const char* library = vp::arithmeticName(arithmetic);
    const std::string text = vertexBatchHlsl(arithmetic, true);
    dump(std::string("vertex_batch_") + library + ".hlsl", text.data(), text.size());
    const D3D_SHADER_MACRO macros[] = {{"VP_RAW_ONLY", "1"}, {"VP_PREVIOUS", "1"}, {"VB_ROW", "1"}, {nullptr, nullptr}};
    const ComPtr<ID3DBlob> blob =
        compile(library, "the test's entry point (packet, previous position, row)", text, "VSBatchRaw", macros,
                D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_IEEE_STRICTNESS);
    if (!blob)
        return 1;
    Pipeline pipeline;
    const UINT count = (UINT)cases.vertices.size();
    ComPtr<ID3D11Buffer> vb, ob, staging;
    if (!pipeline.create(gpu, blob.Get(), true, true) || pipeline.stride != vbcases::kResultWords * 4u ||
        !gpu.buffer(kChunk * sizeof(GpuVertex), D3D11_BIND_VERTEX_BUFFER, nullptr, vb) ||
        !gpu.buffer(kChunk * pipeline.stride, D3D11_BIND_STREAM_OUTPUT, nullptr, ob) ||
        !gpu.buffer(kChunk * pipeline.stride, 0, nullptr, staging, D3D11_USAGE_STAGING)) {
        std::printf("[FAIL] %s: the device refused the shader or a buffer\n", library);
        return 1;
    }
    pipeline.bind(gpu, vb.Get(), table);
    const UINT zero = 0;
    ID3D11Buffer* none = nullptr;
    std::vector<uint32_t> got(size_t(count) * vbcases::kResultWords);
    std::vector<GpuVertex> chunk(kChunk);
    const double start = milliseconds();
    for (UINT first = 0; first < count; first += kChunk) {
        const UINT n = std::min(kChunk, count - first);
        std::memcpy(chunk.data(), cases.vertices.data() + first, size_t(n) * sizeof(GpuVertex));
        ctx->UpdateSubresource(vb.Get(), 0, nullptr, chunk.data(), 0, 0);
        ctx->SOSetTargets(1, ob.GetAddressOf(), &zero);
        ctx->Draw(n, 0);
        ctx->SOSetTargets(1, &none, &zero);
        ctx->CopyResource(staging.Get(), ob.Get());
        D3D11_MAPPED_SUBRESOURCE ms{};
        if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
            std::printf("[FAIL] %s: read back\n", library);
            return 1;
        }
        std::memcpy(got.data() + size_t(first) * vbcases::kResultWords, ms.pData, size_t(n) * pipeline.stride);
        ctx->Unmap(staging.Get(), 0);
    }
    const double took = milliseconds() - start;
    uint64_t bad = vbcases::compareShader(library, cases, ref, got.data());
    std::printf("%s: generated HLSL of the batch path against vp::evaluateVertex (the packet, the position of the "
                "previous frame's inputs, the row after the chain): %u vertices, %llu differ (%.0f ms)\n",
                library, count, (unsigned long long)bad, took);

    const bool cost = std::getenv("GSR_VB_COST") != nullptr;
    const char* benchText = std::getenv("GSR_VB_BENCH");
    if (cost || benchText) {
        // what a renderer compiles, with the renderer's flags
        const D3D_SHADER_MACRO rawOnly[] = {{"VP_RAW_ONLY", "1"}, {nullptr, nullptr}};
        const D3D_SHADER_MACRO rawPrevious[] = {{"VP_RAW_ONLY", "1"}, {"VP_PREVIOUS", "1"}, {nullptr, nullptr}};
        const ComPtr<ID3DBlob> plain =
            compile(library, "the raw entry point alone (packet)", vertexBatchHlsl(arithmetic), "VSBatchRaw", rawOnly,
                    D3DCOMPILE_OPTIMIZATION_LEVEL3);
        const ComPtr<ID3DBlob> twin = compile(library, "the raw entry point with the previous position", text,
                                              "VSBatchRaw", rawPrevious, D3DCOMPILE_OPTIMIZATION_LEVEL3);
        if (!plain || !twin)
            return bad + 1;
        if (cost) {
            const D3D_SHADER_MACRO draw[] = {{"HW_BLEND", "0"}, {nullptr, nullptr}};
            if (!compile(library, "the draw's entry point (the batch path, then the renderer's VSMain)",
                         std::string(kHlslMain) + vertexBatchHlsl(arithmetic), "VSBatch", draw,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3))
                return bad + 1;
        }
        if (benchText) {
            Pipeline plainPipeline, twinPipeline;
            if (!plainPipeline.create(gpu, plain.Get(), false, false) ||
                !twinPipeline.create(gpu, twin.Get(), true, false))
                return bad + 1;
            const int repeats = std::max(3, std::atoi(benchText));
            bench(gpu, library, plainPipeline, "packet", repeats);
            bench(gpu, library, twinPipeline, "packet and previous position", repeats);
        }
    }
    return bad;
}
} // namespace

int main() {
    Gpu gpu;
    const bool warp = std::getenv("GSR_VP_WARP") != nullptr;
    ComPtr<IDXGIAdapter> chosen;
    if (const char* index = std::getenv("GSR_VP_ADAPTER")) {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapters((UINT)std::atoi(index), &chosen))) {
            std::printf("SKIP: no adapter %s\n", index);
            return 77;
        }
    }
    const D3D_DRIVER_TYPE type = chosen ? D3D_DRIVER_TYPE_UNKNOWN
                                 : warp ? D3D_DRIVER_TYPE_WARP
                                        : D3D_DRIVER_TYPE_HARDWARE;
    if (FAILED(D3D11CreateDevice(chosen.Get(), type, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &gpu.dev, nullptr,
                                 &gpu.ctx))) {
        std::printf("SKIP: no Direct3D 11 device\n");
        return 77;
    }
    char adapterName[160] = "?";
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC desc{};
        if (SUCCEEDED(gpu.dev.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetDesc(&desc)))
            std::snprintf(adapterName, sizeof(adapterName), "%ls", desc.Description);
    }
    D3D11_FEATURE_DATA_DOUBLES doubles{};
    const bool doubleSupport =
        SUCCEEDED(gpu.dev->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
        doubles.DoublePrecisionFloatShaderOps;
    std::printf("Direct3D 11 on %s, double support in shaders: %s\n", adapterName, doubleSupport ? "yes" : "no");

    Cases cases = vbcases::standardCases();
    vbcases::describe(cases);
    if (const char* stepText = std::getenv("GSR_VB_STEP")) {
        // every n-th vertex (the pass table stays whole)
        const size_t step = (size_t)std::max(1, std::atoi(stepText));
        size_t kept = 0;
        for (size_t i = 0; i < cases.vertices.size(); i += step, ++kept) {
            cases.vertices[kept] = cases.vertices[i];
            cases.recordOf[kept] = cases.recordOf[i];
            cases.previousRecordOf[kept] = cases.previousRecordOf[i];
        }
        cases.vertices.resize(kept);
        cases.recordOf.resize(kept);
        cases.previousRecordOf.resize(kept);
        std::printf("GSR_VB_STEP=%zu: %zu vertices are run\n", step, kept);
    }
    const vbcases::Reference ref = vbcases::reference(cases);

    ComPtr<ID3D11Buffer> table;
    ComPtr<ID3D11ShaderResourceView> tableView;
    if (!gpu.passTable(cases, table, tableView)) {
        std::printf("[FAIL] the pass table\n");
        return 1;
    }
    // The library choice a renderer makes per adapter (vp::chooseArithmetic); here every available one is run.
    const vp::ArithmeticRequest only = vp::parseArithmeticRequest(std::getenv("GSR_VP_ARITHMETIC"));
    uint64_t bad = 0;
    for (const vp::ArithmeticRequest request : {vp::ArithmeticRequest::Doubles, vp::ArithmeticRequest::Limbs}) {
        if (only != vp::ArithmeticRequest::Auto && only != request)
            continue;
        const vp::Arithmetic arithmetic = vp::chooseArithmetic(vp::ShaderBackend::Direct3D11, doubleSupport, request);
        if (arithmetic == vp::Arithmetic::None) {
            std::printf("doubles: not available on this adapter (a renderer evaluates on the CPU or takes limbs)\n");
            continue;
        }
        bad += runLibrary(gpu, arithmetic, cases, ref, tableView.Get());
    }
    std::printf("batch path shaders against the CPU evaluation: %llu differ in total\n", (unsigned long long)bad);
    return bad ? 1 : 0;
}
